/***************************************************************************
 *   Copyright © 2010 Jonathan Thomas <echidnaman@kubuntu.org>             *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU General Public License as        *
 *   published by the Free Software Foundation; either version 2 of        *
 *   the License or (at your option) version 3 or any later version        *
 *   accepted by the membership of KDE e.V. (or its successor approved     *
 *   by the membership of KDE e.V.), which shall act as a proxy            *
 *   defined in Section 14 of version 3 of the license.                    *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#include "worker.h"

#include "qaptworkeradaptor.h"

// QApt includes
#include "../cache.h"
#include "../globals.h"
#include "../package.h"

// Apt includes
#include <apt-pkg/algorithms.h>
#include <apt-pkg/acquire-item.h>
#include <apt-pkg/aptconfiguration.h>
#include <apt-pkg/cachefile.h>
#include <apt-pkg/configuration.h>
#include <apt-pkg/error.h>
#include <apt-pkg/fileutl.h>
#include <apt-pkg/init.h>
#include <apt-pkg/pkgrecords.h>
#include <apt-pkg/pkgsystem.h>
#include <apt-pkg/policy.h>

#include <sys/statvfs.h>
#include <sys/statfs.h>
#include <sys/wait.h>
#include <sys/fcntl.h>

// Qt includes
#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>

#define RAMFS_MAGIC     0x858458f6

#include "../debfile.h"
#include "qaptauthorization.h"
#include "workeracquire.h"
#include "workerinstallprogress.h"

QAptWorker::QAptWorker(int &argc, char **argv)
        : QCoreApplication(argc, argv)
        , m_cache(0)
        , m_records(0)
        , m_questionBlock(0)
        , m_systemLocked(false)
        , m_initialized(false)
{
    new QaptworkerAdaptor(this);

    if (!QDBusConnection::systemBus().registerService(QLatin1String("org.kubuntu.qaptworker"))) {
        // Another worker is already here, quit
        QTimer::singleShot(0, QCoreApplication::instance(), SLOT(quit()));
        return;
    }

    if (!QDBusConnection::systemBus().registerObject(QLatin1String("/"), this)) {
        QTimer::singleShot(0, QCoreApplication::instance(), SLOT(quit()));
        return;
    }

    m_timeout = new QTimer(this);
    connect(m_timeout, SIGNAL(timeout()), this, SLOT(quit()));
    m_timeout->setSingleShot(true);
    m_timeout->start(30000);
}

QAptWorker::~QAptWorker()
{
    delete m_cache;
    delete m_records;
}

int QAptWorker::dbusSenderUid() const
{
    return connection().interface()->serviceUid( message().service()).value();
}

void QAptWorker::setLocale(const QString &locale) const
{
    std::setlocale(LC_ALL, locale.toAscii());
}

void QAptWorker::setProxy(const QString &proxy) const
{
    setenv("http_proxy", proxy.toAscii(), 1);
}

bool QAptWorker::lockSystem()
{
    if (m_systemLocked) {
        return true;
    }

    m_systemLocked = _system->Lock();

    return m_systemLocked;
}

bool QAptWorker::unlockSystem()
{
    if (!m_systemLocked) {
        return true;
    }

    bool systemUnlocked = _system->UnLock();
    m_systemLocked = !systemUnlocked;

    return systemUnlocked;
}

bool QAptWorker::initializeApt()
{
    // Discard errors from past calls to initApt
    _error->Discard();
    if (!m_initialized) {
        if (!pkgInitConfig(*_config) || !pkgInitSystem(*_config, _system)) {
            throwInitError();
            return false;
        }

        if (!m_cache) {
            m_cache = new pkgCacheFile();
        }

        m_initialized = true;
    }

    reloadCache();

    return true;
}

void QAptWorker::reloadCache()
{
    if (!m_cache->ReadOnlyOpen(nullptr)) {
        qDebug() << "Cache didn't open";
        throwInitError();
        return;
    }

    delete m_records;
    m_records = new pkgRecords(*m_cache);
}

void QAptWorker::throwInitError()
{
    QVariantMap details;
    string message;
    bool isError = _error->PopMessage(message);
    if (isError) {
        details[QLatin1String("FromWorker")] = true;
        details[QLatin1String("ErrorText")] = QString::fromStdString(message);
    }

    errorOccurred(QApt::InitError, details);
}

void QAptWorker::initializeStatusWatcher()
{
    m_acquireStatus = new WorkerAcquire(this);
    connect(m_acquireStatus, SIGNAL(downloadProgress(int,int,int)),
            this, SIGNAL(downloadProgress(int,int,int)));
    connect(m_acquireStatus, SIGNAL(packageDownloadProgress(QString,int,QString,double,int)),
            this, SIGNAL(packageDownloadProgress(QString,int,QString,double,int)));
    connect(m_acquireStatus, SIGNAL(fetchError(int,QVariantMap)),
            this, SIGNAL(errorOccurred(int,QVariantMap)));
    connect(m_acquireStatus, SIGNAL(fetchWarning(int,QVariantMap)),
            this, SIGNAL(warningOccurred(int,QVariantMap)));
    connect(m_acquireStatus, SIGNAL(workerQuestion(int,QVariantMap)),
            this, SIGNAL(questionOccurred(int,QVariantMap)));
}

void QAptWorker::updateCache()
{
    if (!QApt::Auth::authorize(QLatin1String("org.kubuntu.qaptworker.updateCache"), message().service())) {
        emit errorOccurred(QApt::AuthError, QVariantMap());
        return;
    }

    m_timeout->stop();

    emit workerStarted();
    emit workerEvent(QApt::CacheUpdateStarted);

    if (!initializeApt()) {
        emit workerFinished(false);
        return;
    }
    // Lock the list directory
    FileFd Lock;
    if (!_config->FindB("Debug::NoLocking", false)) {
        Lock.Fd(GetLock(_config->FindDir("Dir::State::Lists") + "lock"));
        if (_error->PendingError()) {
            emit errorOccurred(QApt::LockError, QVariantMap());
            emit workerFinished(false);
            return;
        }
    }

    initializeStatusWatcher();
    pkgAcquire fetcher;
    fetcher.Setup(m_acquireStatus);
    // Populate it with the source selection and get all Indexes
    // (GetAll=true)
    if (!m_cache->GetSourceList()->GetIndexes(&fetcher,true)) {
        emit workerFinished(false);
        return;
    }

    // do the work
    if (_config->FindB("APT::Get::Download",true)) {
        bool result = ListUpdate(*m_acquireStatus, *m_cache->GetSourceList());
        emit workerFinished(result);
    } else {
        emit errorOccurred(QApt::DownloadDisallowedError, QVariantMap());
        emit workerFinished(false);
    }

    emit workerEvent(QApt::CacheUpdateFinished);
    m_timeout->start();
}

void QAptWorker::cancelDownload()
{
    if (m_acquireStatus) {
        m_acquireStatus->requestCancel();
    }
}

void QAptWorker::commitChanges(QMap<QString, QVariant> instructionsList)
{
    if (!initializeApt()) {
        emit workerFinished(false);
        return;
    }

    m_timeout->stop();

    pkgDepCache::ActionGroup *actionGroup = new pkgDepCache::ActionGroup(*m_cache);
    // Parse out the argument list and mark packages for operations
    QVariantMap::const_iterator mapIter = instructionsList.constBegin();

    while (mapIter != instructionsList.constEnd()) {
        int operation = mapIter.value().toInt();

        // Find package in cache
        pkgCache::PkgIterator iter;
        QString packageString = mapIter.key();
        QString version;

        // Check if a version is specified
        if (packageString.contains(QLatin1Char(','))) {
            QStringList split = packageString.split(QLatin1Char(','));
            iter = (*m_cache)->FindPkg(split.at(0).toStdString());
            version = split.at(1);
        } else {
            iter = (*m_cache)->FindPkg(packageString.toStdString());
        }

        // Somehow the package searched for doesn't exist.
        if (iter == 0) {
            QVariantMap args;
            args[QLatin1String("NotFoundString")] = packageString;
            emit errorOccurred(QApt::NotFoundError, args);
            emit workerFinished(false);
            m_timeout->start();
            delete actionGroup;
            return;
        }

        pkgDepCache::StateCache & State = (*m_cache)[iter];
        pkgProblemResolver Fix(*m_cache);

        // Then mark according to the instruction
        switch (operation) {
            case QApt::Package::Held:
                (*m_cache)->MarkKeep(iter, false);
                (*m_cache)->SetReInstall(iter, false);
                break;
            case QApt::Package::ToUpgrade: {
                bool fromUser = !(State.Flags & pkgCache::Flag::Auto);
                (*m_cache)->MarkInstall(iter, true, 0, fromUser);
                if (!State.Install() || (*m_cache)->BrokenCount() > 0) {
                    pkgProblemResolver Fix(*m_cache);
                    Fix.Protect(iter);
                    Fix.ResolveByKeep();
                }
                break;
            }
            case QApt::Package::ToInstall:
                (*m_cache)->MarkInstall(iter, true);
                if (!State.Install() || (*m_cache)->BrokenCount() > 0) {
                    pkgProblemResolver Fix(*m_cache);
                    Fix.Clear(iter);
                    Fix.Protect(iter);
                    Fix.Resolve(true);
                }
                break;
            case QApt::Package::ToReInstall:
                (*m_cache)->SetReInstall(iter, true);
                break;
            case QApt::Package::ToDowngrade: {
                pkgVersionMatch Match(version.toStdString(), pkgVersionMatch::Version);
                pkgCache::VerIterator Ver = Match.Find(iter);

                (*m_cache)->SetCandidateVersion(Ver);

                (*m_cache)->MarkInstall(iter, true);
                if (!State.Install() || (*m_cache)->BrokenCount() > 0) {
                    pkgProblemResolver Fix(*m_cache);
                    Fix.Clear(iter);
                    Fix.Protect(iter);
                    Fix.Resolve(true);
                }
                break;
            }
            case QApt::Package::ToPurge:
                (*m_cache)->SetReInstall(iter, false);
                (*m_cache)->MarkDelete(iter, true);
                break;
            case QApt::Package::ToRemove:
                (*m_cache)->SetReInstall(iter, false);
                (*m_cache)->MarkDelete(iter, false);
                break;
            default:
                // Unsupported operation. Should emit something here?
                break;
        }
        mapIter++;
    }

    if (_error->PendingError() && ((*m_cache)->BrokenCount() == 0))
        _error->Discard(); // We had dep errors, but fixed them

    delete actionGroup;

    emit workerStarted();

    if (_error->PendingError()) {
        string message;
        QVariantMap details;

        _error->PopMessage(message);
        details[QLatin1String("FromWorker")] = true;
        details[QLatin1String("ErrorText")] = QString::fromStdString(message);

        emit errorOccurred(QApt::InitError, details);
        emit workerFinished(false);
        m_timeout->start();
        return;
    }

    // Lock the archive directory
    FileFd Lock;
    if (_config->FindB("Debug::NoLocking",false) == false)
    {
        Lock.Fd(GetLock(_config->FindDir("Dir::Cache::Archives") + "lock"));
        if (_error->PendingError() || !lockSystem()) {
            emit errorOccurred(QApt::LockError, QVariantMap());
            emit workerFinished(false);
            m_timeout->start();
            return;
        }
    }

    initializeStatusWatcher();
    pkgAcquire fetcher;
    fetcher.Setup(m_acquireStatus);

    pkgPackageManager *packageManager;
    packageManager = _system->CreatePM(*m_cache);

    if (!packageManager->GetArchives(&fetcher, m_cache->GetSourceList(), m_records) ||
        _error->PendingError()) {
        emit errorOccurred(QApt::FetchError, QVariantMap());
        emit workerFinished(false);
        m_timeout->start();
        return;
    }

    // Display statistics
    double FetchBytes = fetcher.FetchNeeded();
    double FetchPBytes = fetcher.PartialPresent();
    double DebBytes = fetcher.TotalNeeded();
    if (DebBytes != (*m_cache)->DebSize())
    {
        emit warningOccurred(QApt::SizeMismatchWarning, QVariantMap());
    }

    // Check for enough free space
    struct statvfs Buf;
    string OutputDir = _config->FindDir("Dir::Cache::Archives");
    if (statvfs(OutputDir.c_str(),&Buf) != 0) {
        QVariantMap args;
        args[QLatin1String("DirectoryString")] = QLatin1String(OutputDir.c_str());
        emit errorOccurred(QApt::DiskSpaceError, args);
        emit workerFinished(false);
        m_timeout->start();
        return;
    }
    if (unsigned(Buf.f_bfree) < (FetchBytes - FetchPBytes)/Buf.f_bsize)
    {
        struct statfs Stat;
        if (statfs(OutputDir.c_str(), &Stat) != 0 ||
            unsigned(Stat.f_type)            != RAMFS_MAGIC)
        {
            QVariantMap args;
            args[QLatin1String("DirectoryString")] = QLatin1String(OutputDir.c_str());
            emit errorOccurred(QApt::DiskSpaceError, args);
            emit workerFinished(false);
            m_timeout->start();
            return;
        }
    }

    QStringList untrustedPackages;
    for (pkgAcquire::ItemIterator I = fetcher.ItemsBegin(); I < fetcher.ItemsEnd(); ++I)
    {
        if (!(*I)->IsTrusted())
        {
            untrustedPackages << QString::fromStdString((*I)->ShortDesc());
        }
    }

    if (!untrustedPackages.isEmpty()) {
        QVariantMap args;
        args[QLatin1String("UntrustedItems")] = untrustedPackages;

        if (_config->FindB("APT::Get::AllowUnauthenticated", true) == true) {
            // Ask if the user really wants to install untrusted packages, if
            // allowed in the APT config.
            m_questionBlock = new QEventLoop;
            connect(this, SIGNAL(answerReady(QVariantMap)),
                    this, SLOT(setAnswer(QVariantMap)));

            emit questionOccurred(QApt::InstallUntrusted, args);
            m_questionBlock->exec();

            // m_questionBlock will block function until we get a response in m_installUntrusted
            bool m_installUntrusted = m_questionResponse[QLatin1String("InstallUntrusted")].toBool();
            if(!m_installUntrusted) {
                m_questionResponse = QVariantMap(); //Reset for next question
                emit workerFinished(false);
                m_timeout->start();
                return;
            } else {
                m_questionResponse = QVariantMap(); //Reset for next question
            }
        } else {
            // If disallowed in APT config, return a fatal error
            emit errorOccurred(QApt::UntrustedError, args);
            emit workerFinished(false);
            m_timeout->start();
            return;
        }
    }

    if (!QApt::Auth::authorize(QLatin1String("org.kubuntu.qaptworker.commitChanges"), message().service())) {
        emit errorOccurred(QApt::AuthError, QVariantMap());
        emit workerFinished(false);
        m_timeout->start();
        return;
    }

    emit workerEvent(QApt::PackageDownloadStarted);

    if (fetcher.Run() != pkgAcquire::Continue) {
        // Our fetcher will report warnings for itself, but if it fails entirely
        // we have to send the error and finished signals
        if (!m_acquireStatus->wasCancelled()) {
            emit errorOccurred(QApt::FetchError, QVariantMap());
        }

        emit workerFinished(false);
        m_timeout->start();
        return;
    }

    bool failed = false;
    for (pkgAcquire::ItemIterator i = fetcher.ItemsBegin(); i != fetcher.ItemsEnd(); ++i) {
        if((*i)->Status == pkgAcquire::Item::StatDone && (*i)->Complete)
            continue;
        if((*i)->Status == pkgAcquire::Item::StatIdle)
            continue;

        failed = true;
        break;
    }

    if (failed && !packageManager->FixMissing()) {
        emit errorOccurred(QApt::FetchError, QVariantMap());
        emit workerFinished(false);
        m_timeout->start();
        return;
    }

    emit workerEvent(QApt::PackageDownloadFinished);

    emit workerEvent(QApt::CommitChangesStarted);

    WorkerInstallProgress *installProgress = new WorkerInstallProgress(this);
    connect(installProgress, SIGNAL(commitError(int,QVariantMap)),
            this, SIGNAL(errorOccurred(int,QVariantMap)));
    connect(installProgress, SIGNAL(commitProgress(QString,int)),
            this, SIGNAL(commitProgress(QString,int)));
    connect(installProgress, SIGNAL(workerQuestion(int,QVariantMap)),
            this, SIGNAL(questionOccurred(int,QVariantMap)));

    setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);

    unlockSystem();
    pkgPackageManager::OrderResult res = installProgress->start(packageManager);
    bool success = (res == pkgPackageManager::Completed);

    emit workerEvent(QApt::CommitChangesFinished);
    emit workerFinished(success);

    delete installProgress;

    m_timeout->start();
}

void QAptWorker::downloadArchives(const QStringList &packageStrings, const QString &dest)
{
    if (!initializeApt()) {
        emit workerFinished(false);
        return;
    }

    m_timeout->stop();

    emit workerStarted();

    initializeStatusWatcher();
    pkgAcquire fetcher;
    fetcher.Setup(m_acquireStatus);

    pkgIndexFile *index;

    foreach (const QString &packageString, packageStrings) {
        pkgCache::PkgIterator iter = (*m_cache)->FindPkg(packageString.toStdString());

        if (!iter) {
            // Package not found
            continue;
        }

        pkgCache::VerIterator ver = (*m_cache)->GetCandidateVer(iter);

        if (!ver || !ver.Downloadable() || !ver.Arch()) {
            // Virtual package or not downloadable or broken
            continue;
        }

        // Obtain package info
        pkgCache::VerFileIterator vf = ver.FileList();
        pkgRecords::Parser &rec = m_records->Lookup(ver.FileList());

        // Try to cross match against the source list
        if (!m_cache->GetSourceList()->FindIndex(vf.File(), index)) {
            continue;
        }

        string fileName = rec.FileName();
        string MD5sum = rec.MD5Hash();

        if (fileName.empty()) {
            //TODO: Error
            return;
        }

        new pkgAcqFile(&fetcher,
                       index->ArchiveURI(fileName),
                       MD5sum,
                       ver->Size,
                       index->ArchiveInfo(ver),
                       ver.ParentPkg().Name(),
                       dest.toStdString(), "");
    }

    emit workerEvent(QApt::PackageDownloadStarted);

    if (fetcher.Run() != pkgAcquire::Continue) {
        // Our fetcher will report errors for itself, but we have to send the
        // finished signal
        emit workerFinished(false);
        return;
    }

    m_timeout->start();

    emit workerEvent(QApt::PackageDownloadFinished);
    emit workerFinished(true);
}

void QAptWorker::installDebFile(const QString &fileName)
{
    if (!initializeApt()) {
        emit workerFinished(false);
        return;
    }
    m_timeout->stop();

    emit workerStarted();

    QApt::DebFile deb(fileName);

    QString debArch = deb.architecture();

    QStringList archList;
    archList.append(QLatin1String("all"));
    std::vector<std::string> archs = APT::Configuration::getArchitectures(false);

    for (std::string &arch : archs)
    {
         archList.append(QString::fromStdString(arch));
    }

    if (!archList.contains(debArch)) {
        QVariantMap details;
        details["RequestedArch"] = debArch;

        emit errorOccurred(QApt::WrongArchError, details);
        emit workerFinished(false);
        m_timeout->start();
        return;
    }

    if (!QApt::Auth::authorize(QLatin1String("org.kubuntu.qaptworker.commitChanges"), message().service())) {
        emit errorOccurred(QApt::AuthError, QVariantMap());
        emit workerFinished(false);
        m_timeout->start();
        return;
    }

    m_dpkgProcess = new QProcess(this);
    QString program = QLatin1String("dpkg") %
            QLatin1String(" -i ") % '"' % fileName % '"';
    setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
    setenv("DEBIAN_FRONTEND", "passthrough", 1);
    setenv("DEBCONF_PIPE", "/tmp/qapt-sock", 1);
    m_dpkgProcess->start(program);
    connect(m_dpkgProcess, SIGNAL(started()), this, SLOT(dpkgStarted()));
    connect(m_dpkgProcess, SIGNAL(readyRead()), this, SLOT(updateDpkgProgress()));
    connect(m_dpkgProcess, SIGNAL(finished(int,QProcess::ExitStatus)),
            this, SLOT(dpkgFinished(int,QProcess::ExitStatus)));
}

void QAptWorker::dpkgStarted()
{
    emit workerEvent(QApt::DebInstallStarted);
}

void QAptWorker::updateDpkgProgress()
{
    QString str = m_dpkgProcess->readLine();

    emit debInstallMessage(str);
}

void QAptWorker::dpkgFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitCode);

    if (!exitStatus) {
        emit workerEvent(QApt::DebInstallFinished);
    }

    m_timeout->start();

    emit workerFinished(!exitStatus);
    delete m_dpkgProcess;
    m_dpkgProcess = 0;
}

void QAptWorker::answerWorkerQuestion(const QVariantMap &response)
{
    emit answerReady(response);
}

void QAptWorker::setAnswer(const QVariantMap &answer)
{
    disconnect(this, SIGNAL(answerReady(QVariantMap)),
               this, SLOT(setAnswer(QVariantMap)));
    m_questionResponse = answer;
    m_questionBlock->quit();
}

void QAptWorker::updateXapianIndex()
{
    m_timeout->stop();
    emit workerEvent(QApt::XapianUpdateStarted);


    QDBusMessage m = QDBusMessage::createMethodCall(QLatin1String("org.debian.AptXapianIndex"),
                                                    QLatin1String("/"),
                                                    QLatin1String("org.debian.AptXapianIndex"),
                                                    QLatin1String("update_async"));
    QVariantList dbusArgs;

    dbusArgs << /*force*/ true << /*update_only*/ true;
    m.setArguments(dbusArgs);
    QDBusConnection::systemBus().send(m);

    QDBusConnection::systemBus().connect(QLatin1String("org.debian.AptXapianIndex"), QLatin1String("/"), QLatin1String("org.debian.AptXapianIndex"),
                                         QLatin1String("UpdateProgress"), this, SIGNAL(xapianUpdateProgress(int)));
    QDBusConnection::systemBus().connect(QLatin1String("org.debian.AptXapianIndex"), QLatin1String("/"), QLatin1String("org.debian.AptXapianIndex"),
                                         QLatin1String("UpdateFinished"), this, SLOT(xapianUpdateFinished(bool)));
}

void QAptWorker::xapianUpdateFinished(bool result)
{
    Q_UNUSED(result);
    QDBusConnection::systemBus().disconnect(QLatin1String("org.debian.AptXapianIndex"), QLatin1String("/"), QLatin1String("org.debian.AptXapianIndex"),
                                            QLatin1String("UpdateProgress"), this, SIGNAL(xapianUpdateProgress(int)));
    QDBusConnection::systemBus().disconnect(QLatin1String("org.debian.AptXapianIndex"), QLatin1String("/"), QLatin1String("org.debian.AptXapianIndex"),
                                            QLatin1String("UpdateFinished"), this, SLOT(xapianUpdateFinished(bool)));

    emit workerEvent(QApt::XapianUpdateFinished);
    m_timeout->start();
}

bool QAptWorker::writeFileToDisk(const QString &contents, const QString &path)
{
    if (!QApt::Auth::authorize(QLatin1String("org.kubuntu.qaptworker.writeFileToDisk"), message().service())) {
        emit errorOccurred(QApt::AuthError, QVariantMap());
        return false;
    }

    QFile file(path);

    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(contents.toStdString().c_str());

        return true;
    }

    return false;
}

bool QAptWorker::copyArchiveToCache(const QString &archivePath)
{
    if (!QApt::Auth::authorize(QLatin1String("org.kubuntu.qaptworker.writeFileToDisk"), message().service())) {
        emit errorOccurred(QApt::AuthError, QVariantMap());
        return false;
    }

    initializeApt();
    QString cachePath = QString::fromStdString(_config->FindDir("Dir::Cache::Archives"));
    // Filename
    cachePath += archivePath.right(archivePath.size() - archivePath.lastIndexOf('/'));

    if (QFile::exists(cachePath)) {
        // Already copied
        return true;
    }

    return QFile::copy(archivePath, cachePath);
}
