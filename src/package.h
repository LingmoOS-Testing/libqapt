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

#ifndef PACKAGE_H
#define PACKAGE_H

#include <QtCore/QObject>

#include <apt-pkg/pkgcache.h>
#include <apt-pkg/pkgrecords.h>
#include <apt-pkg/depcache.h>

/**
 * The QApt namespace is the main namespace for LibQApt. All classes in this
 * library fall under this namespace.
 */
namespace QApt {

/**
 * GroupPrivate is a class containing all private members of the Group class
 */
class PackagePrivate;

/**
 * The Package class is an object for referencing a software package in the Apt
 * package database. You will be getting most of your information about your
 * packages from this class.
 * @par
 * As long as you provide pointers to your pkgDepCache, pkgRecords and
 * PkgIterator you could use this class apart from the Backend class and use it
 * as a package container in your own application
 *
 * @author Jonathan Thomas
 */
class Package : public QObject
{
    Q_OBJECT
    Q_ENUMS(PackageState)
    Q_ENUMS(UpdateImportance)
public:
   /**
    * Default constructor
    */
    Package(QObject* parent, pkgDepCache *depcache,
            pkgRecords *records, pkgCache::PkgIterator &packageIter);

   /**
    * Default destructor
    */
    virtual ~Package();

    /**
    * Defines the Package::List type, which is a QList of Packages
    */
    typedef QList<Package*> List;

   /**
    * Pointer to the Apt dependency cache passed to us by the constructor
    */
    pkgDepCache *m_depCache;

   /**
    * Pointer to the Apt package records passed to us by the constructor
    */
    pkgRecords *m_records;

   /**
    * Pointer to the Apt package iterator passed to us by the constructor
    */
    pkgCache::PkgIterator *m_packageIter;


    /**
     * Member function that returns the name of the package
     *
     * \return The name of the package as a @c QString
     */
    QString name() const;

    /**
     * Member function that returns the version of the package, regardless of
     * whether it is installed or not
     *
     * \return The version of the package as a @c QString
     */
    QString version() const;

    /**
     * Member function that returns the section of the package
     *
     * \return The section of the package as a @c QString
     */
    QString section() const;

    /**
     * Member function that returns the source package corresponding
     * to the package
     *
     * \return The source package of the package as a @c QString
     */
    QString sourcePackage() const;

    /**
     * Member function that returns the short description (or summary in
     * libapt-pkg terms) of the package
     *
     * \return The short description of the package as a @c QString
     */
    QString shortDescription() const;

    /**
     * Member function that returns the maintainer of the package
     *
     * \return The maintainer of the package as a @c QString
     */
    QString maintainer() const;

    /**
     * Member function that returns the installed version of the package
     * If this package is not installed, this function will return a null
     * QString
     *
     * \return The installed version of the package as a @c QString
     */
    QString installedVersion() const;

    /**
     * Member function that returns the newest available version of the
     * package if it is not installed. If this package is installed, this
     * function will return a null QString
     *
     * \return The available version of the package as a @c QString
     */
    QString availableVersion() const;

    /**
     * Member function that returns the priority of the package
     *
     * \return The priority of the package as a @c QString
     */
    QString priority() const;

    /**
     * Member function that returns the file list of the package
     *
     * \return The file list of the package as a @c QStringList
     */
    QStringList installedFilesList() const;

    /**
     * Member function that returns the long description of the package. Note
     * that this also includes the summary/short description. Currently the
     * function does not strip away human-nonreadable characters such as the
     * periods in package descriptions that denote a double line break.
     *
     * \return The long description of the package as a @c QString
     */
    QString longDescription() const;

    /**
     * Member function that returns the amount of hard drive space that this
     * package will take up once installed.
     * This is human-unreadable, so KDE applications may wish to run this
     * through the KGlobal::locale()->formatByteSize() function to get a
     * localized, human-readable number.
     *
     * \return The installed size of the package as a @c qint32
     */
    qint32 installedSize() const;

    /**
     * Member function that returns the download size of the package archive
     * in bytes.
     * This is human-unreadable, so KDE applications may wish to run this
     * through the KGlobal::locale()->formatByteSize() function to get a
     * localized, human-readable number.
     *
     * \return The installed size of the package as a @c qint32
     */
    qint32 downloadSize() const;

    /**
     * Member function that returns the state of a package, using the
     * @b PackageState enum to define states.
     *
     * \return The PackageState flags of the package as an @c int
     */
    int state();

    /**
     * Checks whether or not the Package object is installed
     *
     * @return @c true if installed
     * @return @c false if not installed
     */
    bool isInstalled();

    /**
     * Member function that returns a list of the names of all the packages
     * that depend on this package. (Reverse dependencies)
     * I would like to see if I could figure out how to construct a Package
     * from inside a Package and have this function return a Package::List
     *
     * \return A list of packages that depend on this package as a @c QStringList
     */
    QStringList requiredByList();


    // TODO: Implement, take an int/PackageState flag
    //void setState();

    // "//" == TODO
    enum PackageState {
        ToKeep = 1 << 0,
        ToInstall = 1 << 1,
        NewInstall = 1 << 2,
        ToReInstall = 1 << 3,//
        ToUpgrade = 1 << 4,
        ToDowngrade = 1 << 5,
        ToRemove = 1 << 6,
        Held = 1 << 7,
        Installed = 1 << 8,
        Upgradeable = 1 << 9,
        NowBroken = 1 << 10,
        InstallBroken = 1 << 11,
        Orphaned = 1 << 12,//
        Pinned = 1 << 13,//
        New = 1 << 14, //
        ResidualConfig = 1 << 15,
        NotInstallable = 1 << 16,
        ToPurge = 1 << 17,//
        IsImportant = 1 << 18,
        OverrideVersion = 1 << 19,//
        IsAuto = 1 << 20,
        IsGarbage = 1 << 21,
        NowPolicyBroken = 1 << 22,
        InstallPolicyBroken = 1 << 23
    };
    Q_DECLARE_FLAGS(PackageStates, PackageState)

    enum UpdateImportance {
        UnknownImportance = 1,//
        NormalImportance = 2,//
        CriticalImportance = 3,//
        SecurityImportance = 4//
    };
    Q_DECLARE_FLAGS(UpdateImportances, UpdateImportance)

private:
    /**
     * Pointer to the GroupPrivate class that contains all of Group's private
     * members
     */
    PackagePrivate * const d;
};

}

#endif
