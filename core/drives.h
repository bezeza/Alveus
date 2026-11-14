/*
 * Alveus — drives: physical devices, partitions, mounted volumes and shares.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QStorageInfo>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

class QProcess;

namespace Alveus
{

struct Drive {
    QString id;           // Stable best-effort identity (WWN, PARTUUID, UUID or device path)
    QString device;       // /dev/nvme0n1, /dev/nvme0n1p1, server:/share
    QString parentId;
    QString parentDevice;
    QString label;
    QString mountPoint;   // Preferred mount point; empty when none is mounted
    QStringList mountPoints;
    QString fsType;
    QString type;         // disk, rom, part, crypt, lvm, loop, network, volume, ...
    QString model;
    QString vendor;
    QString transport;    // nvme, sata, usb, sas, ...
    QString uuid;
    QString partUuid;
    QString partType;
    QString partLabel;
    qint64 total = 0;
    qint64 free = 0;
    bool readOnly = false;
    bool removable = false;
    bool hotplug = false;
    bool usb = false;      // True only when transport metadata identifies USB
    bool physical = false;
    bool network = false;
    bool system = false;   // This device or one of its child volumes contains /
    QList<Drive> children;
};

// Is this mount something a person would call "a disk"?
bool isUserMount(const QString &mountPoint, const QString &fsType, const QString &device);
bool isNetworkMount(const QString &fsType, const QString &device);
QList<Drive> fromMounts(const QList<QStorageInfo> &volumes);
// Flat compatibility helpers used by the current drive-fence UI.
QList<Drive> unmountedFromLsblk(const QByteArray &json);
QHash<QString, bool> removableFromLsblk(const QByteArray &json);
// New physical-device -> volume hierarchy. Unlike the legacy flat helper,
// this parser does not discard small partitions or apply display filters.
QList<Drive> driveHierarchyFromLsblk(const QByteArray &json);
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
    QVariantList devices();
    void refresh();
    void mount(const QString &device);
    void unmount(const QString &device);

Q_SIGNALS:
    void changed();
    void message(const QString &kind, const QString &detail);

private:
    void startLsblk(bool extendedColumns);
    void rebuild();
    QVariantList buildList() const;
    QVariantList buildDeviceList() const;
    void runUdisksctl(const QString &operation, const QString &device);

    QTimer m_timer;
    QProcess *m_lsblk = nullptr;
    QByteArray m_lsblkJson;
    QVariantList m_list;
    QVariantList m_deviceList;
    int m_lsblkTimeoutMs = 10000;
    int m_operationTimeoutMs = 30000;
    bool m_extendedColumnsSupported = true;
};
