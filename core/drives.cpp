/*
 * Alveus — drives
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "drives.h"
#include "processutils.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <functional>
#include <memory>

namespace Alveus
{

namespace
{
const QSet<QString> &pseudoFs()
{
    static const QSet<QString> s = {
        QStringLiteral("tmpfs"), QStringLiteral("devtmpfs"), QStringLiteral("proc"), QStringLiteral("sysfs"),
        QStringLiteral("cgroup"), QStringLiteral("cgroup2"), QStringLiteral("overlay"), QStringLiteral("squashfs"),
        QStringLiteral("efivarfs"), QStringLiteral("autofs"), QStringLiteral("ramfs"), QStringLiteral("nsfs"),
        QStringLiteral("tracefs"), QStringLiteral("debugfs"), QStringLiteral("securityfs"), QStringLiteral("pstore"),
        QStringLiteral("bpf"), QStringLiteral("configfs"), QStringLiteral("mqueue"), QStringLiteral("hugetlbfs"),
        QStringLiteral("binfmt_misc"), QStringLiteral("fusectl"), QStringLiteral("devpts"), QStringLiteral("swap"),
        QStringLiteral("fuse.portal"), QStringLiteral("fuse.gvfsd-fuse"), QStringLiteral("fuse.kio-fuse"),
        QStringLiteral("fuse.rclone"), QStringLiteral("selinuxfs"), QStringLiteral("zram"),
    };
    return s;
}

const QSet<QString> &networkFs()
{
    static const QSet<QString> s = {QStringLiteral("nfs"), QStringLiteral("nfs4"), QStringLiteral("cifs"),
                                    QStringLiteral("smb3"), QStringLiteral("smbfs"), QStringLiteral("fuse.sshfs")};
    return s;
}

const QSet<QString> &mountableFs()
{
    static const QSet<QString> s = {QStringLiteral("ntfs"), QStringLiteral("ntfs3"), QStringLiteral("vfat"),
                                    QStringLiteral("exfat"), QStringLiteral("ext4"), QStringLiteral("ext3"),
                                    QStringLiteral("ext2"), QStringLiteral("btrfs"), QStringLiteral("xfs"),
                                    QStringLiteral("f2fs"), QStringLiteral("hfsplus"), QStringLiteral("udf"),
                                    QStringLiteral("iso9660"), QStringLiteral("swap")};
    return s;
}

qint64 num(const QJsonValue &v)
{
    if (v.isString()) {
        bool ok = false;
        const qint64 value = v.toString().toLongLong(&ok);
        return ok ? value : 0;
    }
    return v.isDouble() ? qint64(v.toDouble()) : 0;
}

bool boolValue(const QJsonValue &v)
{
    if (v.isBool()) {
        return v.toBool();
    }
    if (v.isDouble()) {
        return v.toDouble() != 0;
    }
    if (v.isString()) {
        const QString value = v.toString().trimmed();
        return value == QLatin1String("1") || value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
    }
    return false;
}

QString devicePathOf(const QJsonObject &o)
{
    QString path = o.value(QStringLiteral("path")).toString().trimmed();
    if (path.isEmpty()) {
        const QString name = o.value(QStringLiteral("name")).toString().trimmed();
        if (!name.isEmpty()) {
            path = name.startsWith(QLatin1Char('/')) ? name : QStringLiteral("/dev/") + name;
        }
    }
    return path;
}

QStringList mountpointsOf(const QJsonObject &o)
{
    QStringList out;
    QSet<QString> seen;
    const auto append = [&out, &seen](const QString &path) {
        const QString clean = path.trimmed();
        if (!clean.isEmpty() && clean.compare(QLatin1String("[SWAP]"), Qt::CaseInsensitive) != 0 && !seen.contains(clean)) {
            seen.insert(clean);
            out << clean;
        }
    };

    const QJsonValue values = o.value(QStringLiteral("mountpoints"));
    if (values.isArray()) {
        for (const QJsonValue &value : values.toArray()) {
            if (value.isString()) {
                append(value.toString());
            }
        }
    }

    // Compatibility with older util-linux output and synthetic fixtures.
    const QJsonValue single = o.value(QStringLiteral("mountpoint"));
    if (single.isString()) {
        append(single.toString());
    }
    return out;
}

QString preferredMountPoint(const QStringList &mountPoints)
{
    if (mountPoints.contains(QStringLiteral("/"))) {
        return QStringLiteral("/");
    }
    return mountPoints.isEmpty() ? QString() : mountPoints.first();
}

QString stableIdFor(const QString &type, const QString &device, const QString &wwn,
                   const QString &partUuid, const QString &uuid, const QString &parentIdentity)
{
    if (type == QLatin1String("disk") || type == QLatin1String("rom")) {
        if (!wwn.trimmed().isEmpty()) {
            return QStringLiteral("wwn:") + wwn.trimmed().toLower();
        }
        return QStringLiteral("device:") + device;
    }
    if (!partUuid.trimmed().isEmpty()) {
        return QStringLiteral("partuuid:") + partUuid.trimmed().toLower()
            + (parentIdentity.isEmpty() ? QString() : QStringLiteral("@") + parentIdentity);
    }
    if (!uuid.trimmed().isEmpty()) {
        return QStringLiteral("uuid:") + uuid.trimmed().toLower()
            + (parentIdentity.isEmpty() ? QString() : QStringLiteral("@") + parentIdentity);
    }
    return QStringLiteral("device:") + device;
}

void walk(const QJsonArray &devices, const std::function<void(const QJsonObject &)> &fn)
{
    for (const QJsonValue &v : devices) {
        if (!v.isObject()) {
            continue;
        }
        const QJsonObject o = v.toObject();
        fn(o);
        walk(o.value(QStringLiteral("children")).toArray(), fn);
    }
}

Drive parseDriveNode(const QJsonObject &o, const Drive *parent)
{
    Drive d;
    d.device = devicePathOf(o);
    d.type = o.value(QStringLiteral("type")).toString().trimmed().toLower();
    d.label = o.value(QStringLiteral("label")).toString();
    d.fsType = o.value(QStringLiteral("fstype")).toString();
    d.model = o.value(QStringLiteral("model")).toString().trimmed();
    d.vendor = o.value(QStringLiteral("vendor")).toString().trimmed();
    d.transport = o.value(QStringLiteral("tran")).toString().trimmed().toLower();
    d.uuid = o.value(QStringLiteral("uuid")).toString().trimmed();
    d.partUuid = o.value(QStringLiteral("partuuid")).toString().trimmed();
    d.partType = o.value(QStringLiteral("parttype")).toString().trimmed();
    d.partLabel = o.value(QStringLiteral("partlabel")).toString().trimmed();
    d.total = num(o.value(QStringLiteral("size")));
    d.readOnly = boolValue(o.value(QStringLiteral("ro")));
    const QJsonValue rota = o.value(QStringLiteral("rota"));
    if (!rota.isUndefined() && !rota.isNull()) {
        d.rotationalKnown = true;
        d.rotational = boolValue(rota);
    } else if (parent && parent->rotationalKnown) {
        d.rotationalKnown = true;
        d.rotational = parent->rotational;
    }
    d.hotplug = boolValue(o.value(QStringLiteral("hotplug"))) || (parent && parent->hotplug);
    d.removable = boolValue(o.value(QStringLiteral("rm"))) || d.hotplug || (parent && parent->removable);
    if (d.transport.isEmpty() && parent) {
        d.transport = parent->transport;
    }
    d.usb = d.transport == QLatin1String("usb") || (parent && parent->usb);
    d.physical = d.type == QLatin1String("disk") || d.type == QLatin1String("rom");
    d.mountPoints = mountpointsOf(o);
    d.mountPoint = preferredMountPoint(d.mountPoints);
    d.system = d.mountPoints.contains(QStringLiteral("/"));
    if (parent) {
        d.parentId = parent->id;
        d.parentDevice = parent->device;
    }
    const QString parentIdentity = !d.parentId.isEmpty() ? d.parentId : d.parentDevice;
    d.id = stableIdFor(d.type, d.device, o.value(QStringLiteral("wwn")).toString(),
                       d.partUuid, d.uuid, parentIdentity);

    const QJsonArray children = o.value(QStringLiteral("children")).toArray();
    d.children.reserve(children.size());
    for (const QJsonValue &value : children) {
        if (!value.isObject()) {
            continue;
        }
        Drive child = parseDriveNode(value.toObject(), &d);
        d.system = d.system || child.system;
        d.children << child;
    }
    return d;
}

} // namespace

bool isNetworkMount(const QString &fsType, const QString &device)
{
    return networkFs().contains(fsType) || device.startsWith(QLatin1String("//"))
        || (device.contains(QLatin1Char(':')) && !device.startsWith(QLatin1Char('/')));
}

bool isUserMount(const QString &mp, const QString &fsType, const QString &device)
{
    if (pseudoFs().contains(fsType) || fsType.startsWith(QLatin1String("cgroup"))) {
        return false;
    }
    if (mp == QLatin1String("/")) {
        return true; // shown as the system disk
    }
    if (mp == QLatin1String("/boot/efi")) {
        return true; // the visibility engine decides whether the EFI partition is shown
    }
    static const QStringList hidden = {QStringLiteral("/proc"), QStringLiteral("/sys"), QStringLiteral("/dev"),
                                       QStringLiteral("/boot"), QStringLiteral("/snap"), QStringLiteral("/var"),
                                       QStringLiteral("/usr"), QStringLiteral("/tmp"), QStringLiteral("/opt"),
                                       QStringLiteral("/efi"), QStringLiteral("/sysroot"), QStringLiteral("/root"),
                                       QStringLiteral("/srv"), QStringLiteral("/etc"), QStringLiteral("/recovery")};
    for (const QString &h : hidden) {
        if (mp == h || mp.startsWith(h + QLatin1Char('/'))) {
            return false;
        }
    }
    // /run only for disks the desktop mounted (/run/media/<user>/...)
    if (mp.startsWith(QLatin1String("/run/")) && !mp.startsWith(QLatin1String("/run/media/"))) {
        return false;
    }
    // the home folder on the same disk as the system is not a separate drive
    if (mp == QLatin1String("/home") || mp == QDir::homePath()) {
        return false;
    }
    return device.startsWith(QLatin1String("/dev/")) || isNetworkMount(fsType, device);
}

QList<Drive> fromMounts(const QList<QStorageInfo> &volumes)
{
    QList<Drive> out;
    QSet<QString> seenDevices;
    for (const QStorageInfo &v : volumes) {
        if (!v.isValid()) {
            continue;
        }
        const QString mp = v.rootPath();
        const QString fs = QString::fromLatin1(v.fileSystemType());
        const QString dev = QString::fromLocal8Bit(v.device());
        if (!isUserMount(mp, fs, dev)) {
            continue;
        }
        const bool network = isNetworkMount(fs, dev);
        // btrfs subvolumes etc.: one entry per block device, the shortest path wins.
        // FUSE network mounts can all report /dev/fuse, so keep those mount points separate.
        if (!network && dev.startsWith(QLatin1String("/dev/")) && seenDevices.contains(dev)) {
            continue;
        }
        if (!network) {
            seenDevices.insert(dev);
        }
        Drive d;
        d.device = dev;
        d.id = (network ? QStringLiteral("network:") : QStringLiteral("device:")) + dev;
        if (network && dev.startsWith(QLatin1String("/dev/"))) {
            d.id += QLatin1Char('@');
            d.id += mp;
        }
        d.type = network ? QStringLiteral("network") : QStringLiteral("volume");
        d.label = v.name();
        d.mountPoint = mp;
        d.mountPoints << mp;
        d.fsType = fs;
        d.total = v.bytesTotal();
        d.free = v.bytesAvailable();
        d.readOnly = v.isReadOnly();
        d.network = network;
        d.system = (mp == QLatin1String("/"));
        out << d;
    }
    std::sort(out.begin(), out.end(), [](const Drive &a, const Drive &b) {
        if (a.system != b.system) {
            return a.system; // the system disk first
        }
        return a.mountPoint < b.mountPoint;
    });
    return out;
}

QList<Drive> unmountedFromLsblk(const QByteArray &json)
{
    QList<Drive> out;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return out;
    }

    const QJsonArray devices = document.object().value(QStringLiteral("blockdevices")).toArray();
    std::function<void(const QJsonArray &, const Drive *)> visit;
    visit = [&out, &visit](const QJsonArray &nodes, const Drive *parent) {
        for (const QJsonValue &value : nodes) {
            if (!value.isObject()) {
                continue;
            }
            const QJsonObject o = value.toObject();
            Drive d;
            d.device = devicePathOf(o);
            d.label = o.value(QStringLiteral("label")).toString();
            d.fsType = o.value(QStringLiteral("fstype")).toString().trimmed().toLower();
            d.type = o.value(QStringLiteral("type")).toString().trimmed().toLower();
            d.total = num(o.value(QStringLiteral("size")));
            d.hotplug = boolValue(o.value(QStringLiteral("hotplug"))) || (parent && parent->hotplug);
            d.removable = boolValue(o.value(QStringLiteral("rm"))) || d.hotplug || (parent && parent->removable);
            d.transport = o.value(QStringLiteral("tran")).toString().trimmed().toLower();
            if (d.transport.isEmpty() && parent) {
                d.transport = parent->transport;
            }
            d.usb = d.transport == QLatin1String("usb") || (parent && parent->usb);
            d.model = o.value(QStringLiteral("model")).toString().trimmed();
            d.vendor = o.value(QStringLiteral("vendor")).toString().trimmed();
            d.uuid = o.value(QStringLiteral("uuid")).toString().trimmed();
            d.partUuid = o.value(QStringLiteral("partuuid")).toString().trimmed();
            d.partType = o.value(QStringLiteral("parttype")).toString().trimmed();
            d.partLabel = o.value(QStringLiteral("partlabel")).toString().trimmed();
            d.readOnly = boolValue(o.value(QStringLiteral("ro")));
            const QJsonValue rota = o.value(QStringLiteral("rota"));
            if (!rota.isUndefined() && !rota.isNull()) {
                d.rotationalKnown = true;
                d.rotational = boolValue(rota);
            } else if (parent && parent->rotationalKnown) {
                d.rotationalKnown = true;
                d.rotational = parent->rotational;
            }
            if (parent) {
                d.parentId = parent->id;
                d.parentDevice = parent->device;
            } else {
                const QString parentName = o.value(QStringLiteral("pkname")).toString().trimmed();
                d.parentDevice = parentName.isEmpty() ? QString()
                    : (parentName.startsWith(QLatin1Char('/')) ? parentName : QStringLiteral("/dev/") + parentName);
            }
            const QString parentIdentity = parent ? parent->id : d.parentDevice;
            d.id = stableIdFor(d.type, d.device, o.value(QStringLiteral("wwn")).toString(),
                               d.partUuid, d.uuid, parentIdentity);

            const bool supportedType = d.type == QLatin1String("part") || d.type == QLatin1String("disk")
                || d.type == QLatin1String("crypt") || d.type == QLatin1String("lvm")
                || d.type == QLatin1String("raid") || d.type.startsWith(QLatin1String("raid"))
                || d.type == QLatin1String("loop") || d.type == QLatin1String("rom");
            if (supportedType && mountableFs().contains(d.fsType) && mountpointsOf(o).isEmpty()) {
                out << d;
            }

            visit(o.value(QStringLiteral("children")).toArray(), &d);
        }
    };
    visit(devices, nullptr);
    return out;
}

QHash<QString, bool> removableFromLsblk(const QByteArray &json)
{
    QHash<QString, bool> out;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return out;
    }
    const QJsonArray devices = document.object().value(QStringLiteral("blockdevices")).toArray();
    walk(devices, [&out](const QJsonObject &o) {
        const QString path = devicePathOf(o);
        if (!path.isEmpty()) {
            out.insert(path, boolValue(o.value(QStringLiteral("rm"))) || boolValue(o.value(QStringLiteral("hotplug"))));
        }
    });
    return out;
}

QList<Drive> driveHierarchyFromLsblk(const QByteArray &json)
{
    QList<Drive> out;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return out;
    }
    const QJsonValue blockDevices = document.object().value(QStringLiteral("blockdevices"));
    if (!blockDevices.isArray()) {
        return out;
    }

    const QJsonArray devices = blockDevices.toArray();
    out.reserve(devices.size());
    for (const QJsonValue &value : devices) {
        if (value.isObject()) {
            out << parseDriveNode(value.toObject(), nullptr);
        }
    }
    return out;
}


QString servicePartitionKind(const Drive &drive)
{
    QString guid = drive.partType.trimmed().toLower();
    guid.remove(QLatin1Char('{'));
    guid.remove(QLatin1Char('}'));

    if (guid == QLatin1String("c12a7328-f81f-11d2-ba4b-00a0c93ec93b")) {
        return QStringLiteral("efi");
    }
    if (guid == QLatin1String("e3c9e316-0b5c-4db8-817d-f92df00215ae")) {
        return QStringLiteral("microsoftReserved");
    }
    if (guid == QLatin1String("de94bba4-06d1-4d40-a16a-bfd50179d6ac")) {
        return QStringLiteral("recovery");
    }
    if (guid == QLatin1String("0657fd6d-a4ab-43c4-84e5-0933c84b4f4f")
        || drive.fsType.compare(QLatin1String("swap"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("swap");
    }
    if (guid == QLatin1String("21686148-6449-6e6f-744e-656564454649")) {
        return QStringLiteral("biosBoot");
    }
    return QString();
}

QVariantMap toMap(const Drive &d)
{
    QVariantMap m;
    m.insert(QStringLiteral("id"), d.id);
    m.insert(QStringLiteral("device"), d.device);
    m.insert(QStringLiteral("parentId"), d.parentId);
    m.insert(QStringLiteral("parentDevice"), d.parentDevice);
    m.insert(QStringLiteral("label"), d.label);
    m.insert(QStringLiteral("mountPoint"), d.mountPoint);
    QStringList mountPoints = d.mountPoints;
    if (mountPoints.isEmpty() && !d.mountPoint.isEmpty()) {
        mountPoints << d.mountPoint;
    }
    m.insert(QStringLiteral("mountPoints"), mountPoints);
    m.insert(QStringLiteral("fsType"), d.fsType);
    m.insert(QStringLiteral("type"), d.type);
    m.insert(QStringLiteral("model"), d.model);
    m.insert(QStringLiteral("vendor"), d.vendor);
    m.insert(QStringLiteral("transport"), d.transport);
    m.insert(QStringLiteral("uuid"), d.uuid);
    m.insert(QStringLiteral("partUuid"), d.partUuid);
    m.insert(QStringLiteral("partType"), d.partType);
    m.insert(QStringLiteral("partLabel"), d.partLabel);
    m.insert(QStringLiteral("serviceKind"), servicePartitionKind(d));
    m.insert(QStringLiteral("total"), double(d.total));
    m.insert(QStringLiteral("free"), double(d.free));
    m.insert(QStringLiteral("readOnly"), d.readOnly);
    m.insert(QStringLiteral("removable"), d.removable);
    m.insert(QStringLiteral("hotplug"), d.hotplug);
    m.insert(QStringLiteral("usb"), d.usb);
    m.insert(QStringLiteral("physical"), d.physical);
    m.insert(QStringLiteral("rotational"), d.rotational);
    m.insert(QStringLiteral("rotationalKnown"), d.rotationalKnown);
    m.insert(QStringLiteral("network"), d.network);
    m.insert(QStringLiteral("system"), d.system);
    m.insert(QStringLiteral("mounted"), !mountPoints.isEmpty());

    QString name = d.label;
    if (name.isEmpty() && d.physical && !d.model.isEmpty()) {
        name = (d.vendor + QLatin1Char(' ') + d.model).trimmed();
    }
    if (name.isEmpty() && !d.mountPoint.isEmpty() && d.mountPoint != QLatin1String("/")) {
        name = QFileInfo(d.mountPoint).fileName();
    }
    if (name.isEmpty()) {
        name = QFileInfo(d.device).fileName();
    }
    m.insert(QStringLiteral("name"), name);

    QString icon = QStringLiteral("drive-harddisk");
    if (d.network) {
        icon = QStringLiteral("folder-network");
    } else if (d.system) {
        icon = QStringLiteral("drive-harddisk-root");
    } else if (d.usb || d.removable) {
        icon = QStringLiteral("drive-removable-media-usb");
    } else if (d.type == QLatin1String("rom")) {
        icon = QStringLiteral("drive-optical");
    }
    m.insert(QStringLiteral("icon"), icon);

    QVariantList children;
    children.reserve(d.children.size());
    for (const Drive &child : d.children) {
        children << toMap(child);
    }
    m.insert(QStringLiteral("children"), children);
    m.insert(QStringLiteral("childCount"), children.size());
    return m;
}


QVariantList filterDriveList(const QVariantList &drives, const QVariantMap &options, bool showHiddenTemporarily)
{
    if (showHiddenTemporarily) {
        return drives;
    }

    const QVariantMap groups = options.value(QStringLiteral("groups")).toMap();
    const QVariantMap exceptions = options.value(QStringLiteral("visibilityExceptions")).toMap();
    const QString partitionMode = options.value(QStringLiteral("partitionMode"), QStringLiteral("all")).toString();
    const bool showUnmounted = options.value(QStringLiteral("showUnmounted"), true).toBool();
    const qint64 minimumBytes = qMax<qint64>(0, options.value(QStringLiteral("minimumPartitionSizeMiB"), 0).toLongLong()) * 1024 * 1024;

    const auto groupEnabled = [&groups](const QString &group, bool fallback) {
        return groups.contains(group) ? groups.value(group).toBool() : fallback;
    };
    const auto groupForDrive = [](const QVariantMap &drive) {
        const QString type = drive.value(QStringLiteral("type")).toString().toLower();
        if (drive.value(QStringLiteral("network")).toBool()) {
            return QStringLiteral("network");
        }
        if (drive.value(QStringLiteral("usb")).toBool()) {
            return QStringLiteral("usb");
        }
        if (type == QLatin1String("rom")) {
            return QStringLiteral("optical");
        }
        if (drive.value(QStringLiteral("removable")).toBool() || drive.value(QStringLiteral("hotplug")).toBool()) {
            return QStringLiteral("external");
        }
        if (type == QLatin1String("loop") || type == QLatin1String("lvm") || type == QLatin1String("crypt")
            || type == QLatin1String("raid") || type.startsWith(QLatin1String("raid"))
            || type == QLatin1String("md") || type == QLatin1String("mpath") || type == QLatin1String("zram")) {
            return QStringLiteral("virtual");
        }
        return QStringLiteral("internal");
    };

    QVariantList out;
    out.reserve(drives.size());
    for (const QVariant &value : drives) {
        const QVariantMap drive = value.toMap();
        const QString id = drive.value(QStringLiteral("id")).toString();
        const QString exception = exceptions.value(id).toString();
        if (exception == QLatin1String("hide")) {
            continue;
        }

        const QString group = groupForDrive(drive);
        const bool defaultEnabled = group != QLatin1String("virtual");
        if (!groupEnabled(group, defaultEnabled)) {
            continue;
        }
        // "Always show" bypasses automatic filters, but not a disabled group.
        if (exception == QLatin1String("show")) {
            out << value;
            continue;
        }

        const bool mounted = drive.value(QStringLiteral("mounted")).toBool();
        if (!showUnmounted && !mounted) {
            continue;
        }

        const QString type = drive.value(QStringLiteral("type")).toString().toLower();
        if (type == QLatin1String("part")) {
            if (partitionMode == QLatin1String("mounted") && !mounted) {
                continue;
            }
            if (partitionMode == QLatin1String("selected")) {
                continue;
            }
            if (minimumBytes > 0) {
                const qint64 size = drive.value(QStringLiteral("total")).toLongLong();
                if (size > 0 && size < minimumBytes) {
                    continue;
                }
            }
        }

        const QString serviceKind = drive.value(QStringLiteral("serviceKind")).toString();
        const QString filterKey = serviceKind == QLatin1String("efi") ? QStringLiteral("hideEfi")
            : serviceKind == QLatin1String("microsoftReserved") ? QStringLiteral("hideMicrosoftReserved")
            : serviceKind == QLatin1String("recovery") ? QStringLiteral("hideRecovery")
            : serviceKind == QLatin1String("swap") ? QStringLiteral("hideSwap")
            : serviceKind == QLatin1String("biosBoot") ? QStringLiteral("hideBiosBoot") : QString();
        if (!filterKey.isEmpty() && options.value(filterKey, true).toBool()) {
            continue;
        }

        out << value;
    }
    return out;
}

QVariantList filterDriveHierarchy(const QVariantList &devices, const QVariantMap &options, bool showHiddenTemporarily)
{
    if (showHiddenTemporarily) {
        return devices;
    }

    const auto hasMountedDescendant = [](const auto &self, const QVariantList &children) -> bool {
        for (const QVariant &childValue : children) {
            const QVariantMap child = childValue.toMap();
            if (child.value(QStringLiteral("mounted")).toBool()
                || self(self, child.value(QStringLiteral("children")).toList())) {
                return true;
            }
        }
        return false;
    };

    QVariantList out;
    out.reserve(devices.size());
    for (const QVariant &value : devices) {
        const QVariantMap original = value.toMap();
        const QVariantList children = filterDriveHierarchy(original.value(QStringLiteral("children")).toList(), options);
        QVariantMap candidate = original;

        // A physical disk is not itself mounted, but it must remain visible
        // when a visible descendant volume is mounted.
        if (!candidate.value(QStringLiteral("mounted")).toBool() && hasMountedDescendant(hasMountedDescendant, children)) {
            candidate.insert(QStringLiteral("mounted"), true);
        }
        if (filterDriveList({candidate}, options).isEmpty()) {
            continue;
        }

        QVariantMap filtered = original;
        filtered.insert(QStringLiteral("children"), children);
        filtered.insert(QStringLiteral("childCount"), children.size());
        out << filtered;
    }
    return out;
}

} // namespace Alveus

// ---------------------------------------------------------------------------

DriveWatcher::DriveWatcher(QObject *parent, int lsblkTimeoutMs, int operationTimeoutMs)
    : QObject(parent)
    , m_lsblkTimeoutMs(lsblkTimeoutMs > 0 ? lsblkTimeoutMs : 10000)
    , m_operationTimeoutMs(operationTimeoutMs > 0 ? operationTimeoutMs : 30000)
{
    // mounts and free space change without telling anyone: look now and then
    m_timer.setInterval(15000);
    connect(&m_timer, &QTimer::timeout, this, &DriveWatcher::refresh);
}

DriveWatcher::~DriveWatcher()
{
    if (m_lsblk) {
        m_lsblk->disconnect(this);
        m_lsblk->kill();
        m_lsblk->waitForFinished(1000);
    }
}

QVariantList DriveWatcher::drives()
{
    if (!m_timer.isActive()) {
        m_timer.start();
        // Mounted ones right away, but quietly: this runs while QML reads the
        // property, and emitting changed() here would be a binding loop.
        m_list = buildList();
        m_deviceList = buildDeviceList();
        refresh(); // then the rest from lsblk
    }
    return m_list;
}

QVariantList DriveWatcher::devices()
{
    if (!m_timer.isActive()) {
        m_timer.start();
        m_list = buildList();
        m_deviceList = buildDeviceList();
        refresh();
    }
    return m_deviceList;
}

void DriveWatcher::refresh()
{
    if (m_lsblk) {
        return;
    }
    if (QStandardPaths::findExecutable(QStringLiteral("lsblk")).isEmpty()) {
        Q_EMIT message(QStringLiteral("drive-list-unavailable"), QStringLiteral("lsblk was not found; keeping the last known drive list"));
        rebuild();
        return;
    }
    startLsblk(m_extendedColumnsSupported);
}

void DriveWatcher::startLsblk(bool extendedColumns)
{
    if (m_lsblk) {
        return;
    }

    m_lsblk = new QProcess(this);
    QProcess *process = m_lsblk;
    connect(process, &QProcess::finished, this, [this, process, extendedColumns](int code, QProcess::ExitStatus status) {
        if (m_lsblk != process) {
            process->deleteLater();
            return;
        }

        const bool timedOut = Alveus::processTimedOut(process);
        const QByteArray output = process->readAllStandardOutput();
        const QString errorOutput = QString::fromUtf8(process->readAllStandardError()).trimmed();
        process->deleteLater();
        m_lsblk = nullptr;

        if (timedOut) {
            m_timer.setInterval(qMin(m_timer.interval() * 2, 60000));
            Q_EMIT message(QStringLiteral("drive-list-timeout"),
                           QStringLiteral("lsblk exceeded %1 ms; keeping the last known drive list").arg(m_lsblkTimeoutMs));
        } else if (status == QProcess::NormalExit && code == 0) {
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(output, &parseError);
            if (parseError.error == QJsonParseError::NoError && document.isObject()
                && document.object().value(QStringLiteral("blockdevices")).isArray()) {
                m_lsblkJson = output;
                m_timer.setInterval(15000);
            } else {
                m_timer.setInterval(qMin(m_timer.interval() * 2, 60000));
                Q_EMIT message(QStringLiteral("drive-list-invalid-output"),
                               QStringLiteral("lsblk returned invalid JSON; keeping the last known drive list"));
            }
        } else if (extendedColumns) {
            // Older util-linux versions may not provide every optional column.
            // Retry once with the known baseline rather than losing drive discovery.
            m_extendedColumnsSupported = false;
            startLsblk(false);
            return;
        } else {
            m_timer.setInterval(qMin(m_timer.interval() * 2, 60000));
            QString detail = errorOutput;
            if (detail.isEmpty()) {
                detail = QStringLiteral("lsblk exited with code %1").arg(code);
            }
            Q_EMIT message(QStringLiteral("drive-list-failed"), detail + QStringLiteral("; keeping the last known drive list"));
        }
        rebuild();
    });

    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_lsblk != process) {
            return;
        }
        const QString detail = process->errorString();
        m_lsblk = nullptr;
        m_timer.setInterval(qMin(m_timer.interval() * 2, 60000));
        process->deleteLater();
        Q_EMIT message(QStringLiteral("drive-list-failed"), detail + QStringLiteral("; keeping the last known drive list"));
        rebuild();
    });

    Alveus::killAfterTimeout(process, m_lsblkTimeoutMs);
    const QString columns = extendedColumns
        ? QStringLiteral("NAME,PATH,PKNAME,LABEL,FSTYPE,MOUNTPOINTS,SIZE,RM,HOTPLUG,RO,ROTA,TYPE,TRAN,MODEL,VENDOR,WWN,UUID,PARTUUID,PARTTYPE,PARTLABEL")
        : QStringLiteral("NAME,PATH,LABEL,FSTYPE,MOUNTPOINTS,SIZE,RM,HOTPLUG,TYPE");
    process->start(QStringLiteral("lsblk"), {QStringLiteral("-J"), QStringLiteral("-b"), QStringLiteral("-o"), columns});
}

void DriveWatcher::rebuild()
{
    const QVariantList list = buildList();
    const QVariantList deviceList = buildDeviceList();
    if (list != m_list || deviceList != m_deviceList) {
        m_list = list;
        m_deviceList = deviceList;
        Q_EMIT changed();
    }
}

QVariantList DriveWatcher::buildList() const
{
    QList<Alveus::Drive> mounted = Alveus::fromMounts(QStorageInfo::mountedVolumes());
    const QList<Alveus::Drive> hierarchy = Alveus::driveHierarchyFromLsblk(m_lsblkJson);
    const QHash<QString, bool> removable = Alveus::removableFromLsblk(m_lsblkJson);

    std::function<const Alveus::Drive *(const QList<Alveus::Drive> &, const QString &)> findDevice;
    findDevice = [&findDevice](const QList<Alveus::Drive> &items, const QString &path) -> const Alveus::Drive * {
        for (const Alveus::Drive &item : items) {
            if (item.device == path) {
                return &item;
            }
            if (const Alveus::Drive *child = findDevice(item.children, path)) {
                return child;
            }
        }
        return nullptr;
    };

    QVariantList list;
    for (Alveus::Drive &d : mounted) {
        if (const Alveus::Drive *metadata = findDevice(hierarchy, d.device)) {
            d.id = metadata->id;
            d.parentId = metadata->parentId;
            d.parentDevice = metadata->parentDevice;
            d.label = d.label.isEmpty() ? metadata->label : d.label;
            d.type = metadata->type;
            d.model = metadata->model;
            d.vendor = metadata->vendor;
            d.transport = metadata->transport;
            d.uuid = metadata->uuid;
            d.partUuid = metadata->partUuid;
            d.partType = metadata->partType;
            d.partLabel = metadata->partLabel;
            d.removable = metadata->removable;
            d.hotplug = metadata->hotplug;
            d.usb = metadata->usb;
            d.physical = metadata->physical;
            d.network = d.network || metadata->network;
        } else {
            d.removable = removable.value(d.device, d.mountPoint.startsWith(QLatin1String("/run/media/")));
        }
        list << Alveus::toMap(d);
    }
    for (const Alveus::Drive &d : Alveus::unmountedFromLsblk(m_lsblkJson)) {
        list << Alveus::toMap(d);
    }
    return list;
}

QVariantList DriveWatcher::buildDeviceList() const
{
    QList<Alveus::Drive> devices = Alveus::driveHierarchyFromLsblk(m_lsblkJson);

    std::function<Alveus::Drive *(QList<Alveus::Drive> &, const QString &)> findDevice;
    findDevice = [&findDevice](QList<Alveus::Drive> &items, const QString &path) -> Alveus::Drive * {
        for (Alveus::Drive &item : items) {
            if (item.device == path) {
                return &item;
            }
            if (Alveus::Drive *child = findDevice(item.children, path)) {
                return child;
            }
        }
        return nullptr;
    };

    for (const QStorageInfo &volume : QStorageInfo::mountedVolumes()) {
        if (!volume.isValid()) {
            continue;
        }
        const QString mountPoint = volume.rootPath();
        const QString fsType = QString::fromLatin1(volume.fileSystemType());
        const QString device = QString::fromLocal8Bit(volume.device());
        if (mountPoint.isEmpty() || !Alveus::isUserMount(mountPoint, fsType, device)) {
            continue;
        }

        const bool network = Alveus::isNetworkMount(fsType, device);
        const bool fuseNetwork = network && device.startsWith(QLatin1String("/dev/"));
        Alveus::Drive *target = fuseNetwork ? nullptr : findDevice(devices, device);
        if (!target) {
            Alveus::Drive fallback;
            fallback.device = device;
            fallback.label = volume.name();
            fallback.fsType = fsType;
            fallback.type = network ? QStringLiteral("network") : QStringLiteral("volume");
            fallback.network = network;
            fallback.id = (network ? QStringLiteral("network:") : QStringLiteral("device:")) + device;
            if (fuseNetwork) {
                fallback.id += QLatin1Char('@');
                fallback.id += mountPoint;
            }
            devices << fallback;
            target = &devices.last();
        }

        if (target->label.isEmpty()) {
            target->label = volume.name();
        }
        if (target->fsType.isEmpty()) {
            target->fsType = fsType;
        }
        if (volume.bytesTotal() > 0) {
            target->total = volume.bytesTotal();
        }
        target->free = volume.bytesAvailable();
        target->readOnly = volume.isReadOnly();
        target->network = target->network || network;
        if (!target->mountPoints.contains(mountPoint)) {
            target->mountPoints << mountPoint;
        }
        if (!target->mountPoints.isEmpty()) {
            target->mountPoint = target->mountPoints.contains(QStringLiteral("/"))
                ? QStringLiteral("/") : target->mountPoints.first();
        }
        target->system = target->system || mountPoint == QLatin1String("/");
    }

    std::function<void(Alveus::Drive &)> propagateSystem = [&propagateSystem](Alveus::Drive &drive) {
        for (Alveus::Drive &child : drive.children) {
            propagateSystem(child);
            drive.system = drive.system || child.system;
        }
    };
    for (Alveus::Drive &device : devices) {
        propagateSystem(device);
    }

    std::stable_sort(devices.begin(), devices.end(), [](const Alveus::Drive &a, const Alveus::Drive &b) {
        const auto group = [](const Alveus::Drive &d) {
            if (d.system) return 0;
            if (d.usb) return 2;
            if (d.removable) return 3;
            if (d.network) return 4;
            return 1;
        };
        const int aGroup = group(a);
        const int bGroup = group(b);
        if (aGroup != bGroup) {
            return aGroup < bGroup;
        }
        return a.device < b.device;
    });

    QVariantList list;
    list.reserve(devices.size());
    for (const Alveus::Drive &device : devices) {
        list << Alveus::toMap(device);
    }
    return list;
}

void DriveWatcher::runUdisksctl(const QString &operation, const QString &device)
{
    auto *process = new QProcess(this);
    auto completed = std::make_shared<bool>(false);
    auto complete = [this, process, completed, operation](bool success, const QString &kind, const QString &detail) {
        if (*completed) {
            return;
        }
        *completed = true;
        if (!success) {
            Q_EMIT message(kind, detail);
        }
        process->deleteLater();
        refresh();
    };
    connect(process, &QProcess::finished, this, [this, process, complete, operation](int code, QProcess::ExitStatus status) {
        if (Alveus::processTimedOut(process)) {
            complete(false, operation + QStringLiteral("-timeout"),
                     QStringLiteral("udisksctl %1 exceeded %2 ms").arg(operation).arg(m_operationTimeoutMs));
        } else if (status == QProcess::NormalExit && code == 0) {
            complete(true, QString(), QString());
        } else {
            QString detail = QString::fromUtf8(process->readAllStandardError()).trimmed();
            if (detail.isEmpty()) {
                detail = QStringLiteral("udisksctl %1 exited with code %2").arg(operation).arg(code);
            }
            complete(false, operation + QStringLiteral("-failed"), detail);
        }
    });
    connect(process, &QProcess::errorOccurred, this, [process, complete, operation](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            complete(false, operation + QStringLiteral("-failed"), process->errorString());
        }
    });
    Alveus::killAfterTimeout(process, m_operationTimeoutMs);
    process->start(QStringLiteral("udisksctl"), {operation, QStringLiteral("-b"), device});
}

void DriveWatcher::mount(const QString &device)
{
    runUdisksctl(QStringLiteral("mount"), device);
}

void DriveWatcher::unmount(const QString &device)
{
    runUdisksctl(QStringLiteral("unmount"), device);
}
