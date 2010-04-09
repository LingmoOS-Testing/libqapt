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

#ifndef WORKERACQUIRE_H
#define WORKERACQUIRE_H

#include <QtCore/QEventLoop>

#include <apt-pkg/acquire.h>

#include "globals.h"

class WorkerAcquire : public QObject, public pkgAcquireStatus
{
    Q_OBJECT
    Q_ENUMS(FetchType)
public:
    WorkerAcquire();

    virtual ~WorkerAcquire();

    virtual void Start();
    virtual void IMSHit(pkgAcquire::ItemDesc &Itm);
    virtual void Fetch(pkgAcquire::ItemDesc &Itm);
    virtual void Done(pkgAcquire::ItemDesc &Itm);
    virtual void Fail(pkgAcquire::ItemDesc &Itm);
    virtual void Stop();
    virtual bool MediaChange(string Media, string Drive);

    bool Pulse(pkgAcquire *Owner);

public Q_SLOTS:
    void requestCancel();

private:
    QEventLoop *m_mediaBlock;
    bool m_canceled;

signals:
    void mediaChangeRequest(const QString &media, const QString &drive);
    void downloadProgress(int percentage);
    void downloadSubProgress(int percentage);
    // TODO: Set fetch type in downloadMessage, (Get, Hit, Ign, etc like in apt-get)
    void downloadMessage(int flag, const QString &message);
};

#endif
