/*
 * This file was generated by qdbusxml2cpp version 0.7
 * Command line was: qdbusxml2cpp -N -p workerdbus worker/org.kubuntu.qaptworker.xml
 *
 * qdbusxml2cpp is Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 *
 * This is an auto-generated file.
 * Do not edit! All changes made to it will be lost.
 */

#ifndef WORKERDBUS_H_1276129391
#define WORKERDBUS_H_1276129391

#include <QtCore/QObject>
#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QMap>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariant>
#include <QtDBus/QtDBus>

/*
 * Proxy class for interface org.kubuntu.qaptworker
 */
class OrgKubuntuQaptworkerInterface: public QDBusAbstractInterface
{
    Q_OBJECT
public:
    static inline const char *staticInterfaceName()
    { return "org.kubuntu.qaptworker"; }

public:
    OrgKubuntuQaptworkerInterface(const QString &service, const QString &path, const QDBusConnection &connection, QObject *parent = 0);

    ~OrgKubuntuQaptworkerInterface();

public Q_SLOTS: // METHODS
    inline QDBusPendingReply<> answerWorkerQuestion(const QVariantMap &response)
    {
        QList<QVariant> argumentList;
        argumentList << qVariantFromValue(response);
        return asyncCallWithArgumentList(QLatin1String("answerWorkerQuestion"), argumentList);
    }

    inline QDBusPendingReply<> cancelDownload()
    {
        QList<QVariant> argumentList;
        return asyncCallWithArgumentList(QLatin1String("cancelDownload"), argumentList);
    }

    inline QDBusPendingReply<> commitChanges(const QVariantMap &in0)
    {
        QList<QVariant> argumentList;
        argumentList << qVariantFromValue(in0);
        return asyncCallWithArgumentList(QLatin1String("commitChanges"), argumentList);
    }

    inline QDBusPendingReply<> updateCache()
    {
        QList<QVariant> argumentList;
        return asyncCallWithArgumentList(QLatin1String("updateCache"), argumentList);
    }

Q_SIGNALS: // SIGNALS
    void commitProgress(const QString &status, int percentage);
    void downloadMessage(int flag, const QString &message);
    void downloadProgress(int percentage, int speed, int ETA);
    void errorOccurred(int code, const QVariantMap &details);
    void questionOccurred(int questionCode, const QVariantMap &details);
    void workerEvent(int code);
    void workerFinished(bool result);
    void workerStarted();
};

#endif
