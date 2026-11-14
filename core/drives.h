/*
 * Alveus — drives: mounted disks (NTFS, USB sticks, network shares) with
 * free space, plus partitions that exist but are not mounted yet.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QStorageInfo>
#include <QString>
#include <QTimer>
#include <QVariantList>

class QProcess;

namespace Alveus
{

struct Drive {
    QString device;      // /dev/sdb1, server:/share
    QString label;       // volume name, may be empty
    QString mountPoint;  // "" = not mounted
    QString fsType;
    qint64 total = 0;
    qint64 free = 0;
    bool readOnly = false;
    bool removable = false;
    bool network = false;
    bool system = false; // the disk Linux runs from
};

// Is this mount something a person would call "a disk"?
bool isUserMount(const QString &mountPoint, const QString &fsType, const QString &device);
QList<Drive> fromMounts(const QList<QStorageInfo> &volumes);
// Partitions from `lsblk -J -b -o ...` that could be mounted
QList<Drive> unmountedFromLsblk(const QByteArray &json);
// Removable flags for mounted devices, by device path
QHash<QString, bool> removableFromLsblk(const QByteArray &json);
QVariantMap toMap(const Drive &d);

} // namespace Alveus

class DriveWatcher : public QObject
{
    Q_OBJECT
public:
    explicit DriveWatcher(QObject *parent = nullptr, int lsblkTimeoutMs = 10000, int operationTimeoutMs = 30000);
    ~DriveWatcher() override;

    // Starts watching the first time someone asks (a drives fence exists)
    QVariantList drives();
    void refresh();
    void mount(const QString &device);
    void unmount(const QString &device);

Q_SIGNALS:
    void changed();
    void message(const QString &kind, const QString &detail);

private:
    void rebuild();
    QVariantList buildList() const;
    void runUdisksctl(const QString &operation, const QString &device);

    QTimer m_timer;
    QProcess *m_lsblk = nullptr;
    QByteArray m_lsblkJson;
    QVariantList m_list;
    int m_lsblkTimeoutMs = 10000;
    int m_operationTimeoutMs = 30000;
};
