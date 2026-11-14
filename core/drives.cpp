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
#include <QProcess>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <functional>

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
                                    QStringLiteral("iso9660")};
    return s;
}

qint64 num(const QJsonValue &v)
{
    // lsblk prints numbers, or strings in older versions
    return v.isString() ? v.toString().toLongLong() : qint64(v.toDouble());
}

void walk(const QJsonArray &devices, const std::function<void(const QJsonObject &)> &fn)
{
    for (const QJsonValue &v : devices) {
        const QJsonObject o = v.toObject();
        fn(o);
        walk(o.value(QStringLiteral("children")).toArray(), fn);
    }
}

QStringList mountpointsOf(const QJsonObject &o)
{
    QStringList out;
    for (const QJsonValue &m : o.value(QStringLiteral("mountpoints")).toArray()) {
        if (m.isString() && !m.toString().isEmpty()) {
            out << m.toString();
        }
    }
    const QJsonValue single = o.value(QStringLiteral("mountpoint")); // older lsblk
    if (single.isString() && !single.toString().isEmpty()) {
        out << single.toString();
    }
    return out;
}
} // namespace

bool isUserMount(const QString &mp, const QString &fsType, const QString &device)
{
    if (pseudoFs().contains(fsType) || fsType.startsWith(QLatin1String("cgroup"))) {
        return false;
    }
    if (mp == QLatin1String("/")) {
        return true; // shown as the system disk
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
    return device.startsWith(QLatin1String("/dev/")) || networkFs().contains(fsType) || device.contains(QLatin1Char(':'))
        || device.startsWith(QLatin1String("//"));
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
        // btrfs subvolumes etc.: one entry per device, the shortest path wins
        if (dev.startsWith(QLatin1String("/dev/")) && seenDevices.contains(dev)) {
            continue;
        }
        seenDevices.insert(dev);
        Drive d;
        d.device = dev;
        d.label = v.name();
        d.mountPoint = mp;
        d.fsType = fs;
        d.total = v.bytesTotal();
        d.free = v.bytesAvailable();
        d.readOnly = v.isReadOnly();
        d.network = networkFs().contains(fs) || dev.startsWith(QLatin1String("//")) || (dev.contains(QLatin1Char(':')) && !dev.startsWith(QLatin1Char('/')));
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
    const QJsonArray devices = QJsonDocument::fromJson(json).object().value(QStringLiteral("blockdevices")).toArray();
    walk(devices, [&out](const QJsonObject &o) {
        const QString fs = o.value(QStringLiteral("fstype")).toString();
        const QString type = o.value(QStringLiteral("type")).toString();
        if (!mountableFs().contains(fs) || !mountpointsOf(o).isEmpty()) {
            return;
        }
        if (type != QLatin1String("part") && type != QLatin1String("disk") && type != QLatin1String("crypt")) {
            return;
        }
        const qint64 size = num(o.value(QStringLiteral("size")));
        // skip small helper partitions (EFI, Windows recovery, boot)
        if (size > 0 && size < 2LL * 1024 * 1024 * 1024) {
            return;
        }
        Drive d;
        d.device = o.value(QStringLiteral("path")).toString();
        if (d.device.isEmpty()) {
            d.device = QStringLiteral("/dev/") + o.value(QStringLiteral("name")).toString();
        }
        d.label = o.value(QStringLiteral("label")).toString();
        d.fsType = fs;
        d.total = size;
        d.removable = o.value(QStringLiteral("rm")).toBool() || o.value(QStringLiteral("hotplug")).toBool();
        out << d;
    });
    return out;
}

QHash<QString, bool> removableFromLsblk(const QByteArray &json)
{
    QHash<QString, bool> out;
    const QJsonArray devices = QJsonDocument::fromJson(json).object().value(QStringLiteral("blockdevices")).toArray();
    walk(devices, [&out](const QJsonObject &o) {
        const QString path = o.value(QStringLiteral("path")).toString();
        if (!path.isEmpty()) {
            out.insert(path, o.value(QStringLiteral("rm")).toBool() || o.value(QStringLiteral("hotplug")).toBool());
        }
    });
    return out;
}

QVariantMap toMap(const Drive &d)
{
    QVariantMap m;
    m.insert(QStringLiteral("device"), d.device);
    m.insert(QStringLiteral("label"), d.label);
    m.insert(QStringLiteral("mountPoint"), d.mountPoint);
    m.insert(QStringLiteral("fsType"), d.fsType);
    m.insert(QStringLiteral("total"), double(d.total));
    m.insert(QStringLiteral("free"), double(d.free));
    m.insert(QStringLiteral("readOnly"), d.readOnly);
    m.insert(QStringLiteral("removable"), d.removable);
    m.insert(QStringLiteral("network"), d.network);
    m.insert(QStringLiteral("system"), d.system);
    m.insert(QStringLiteral("mounted"), !d.mountPoint.isEmpty());
    // name to show: the volume label, else the folder it is mounted on
    QString name = d.label;
    if (name.isEmpty() && !d.mountPoint.isEmpty() && d.mountPoint != QLatin1String("/")) {
        name = QFileInfo(d.mountPoint).fileName();
    }
    if (name.isEmpty() && d.mountPoint.isEmpty()) {
        name = QFileInfo(d.device).fileName();
    }
    m.insert(QStringLiteral("name"), name);
    QString icon = QStringLiteral("drive-harddisk");
    if (d.system) {
        icon = QStringLiteral("drive-harddisk-root");
    } else if (d.network) {
        icon = QStringLiteral("folder-network");
    } else if (d.removable) {
        icon = QStringLiteral("drive-removable-media-usb");
    }
    m.insert(QStringLiteral("icon"), icon);
    return m;
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
        refresh();  // then the rest from lsblk
    }
    return m_list;
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

    m_lsblk = new QProcess(this);
    QProcess *process = m_lsblk;
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        if (m_lsblk != process) {
            process->deleteLater();
            return;
        }
        if (Alveus::processTimedOut(process)) {
            m_timer.setInterval(qMin(m_timer.interval() * 2, 60000));
            Q_EMIT message(QStringLiteral("drive-list-timeout"),
                           QStringLiteral("lsblk exceeded %1 ms; keeping the last known drive list").arg(m_lsblkTimeoutMs));
        } else if (status == QProcess::NormalExit && code == 0) {
            const QByteArray output = process->readAllStandardOutput();
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
        } else {
            m_timer.setInterval(qMin(m_timer.interval() * 2, 60000));
            QString detail = QString::fromUtf8(process->readAllStandardError()).trimmed();
            if (detail.isEmpty()) {
                detail = QStringLiteral("lsblk exited with code %1").arg(code);
            }
            Q_EMIT message(QStringLiteral("drive-list-failed"), detail + QStringLiteral("; keeping the last known drive list"));
        }
        m_lsblk->deleteLater();
        m_lsblk = nullptr;
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
    process->start(QStringLiteral("lsblk"), {QStringLiteral("-J"), QStringLiteral("-b"), QStringLiteral("-o"),
                                              QStringLiteral("NAME,PATH,LABEL,FSTYPE,MOUNTPOINTS,SIZE,RM,HOTPLUG,TYPE")});
}

void DriveWatcher::rebuild()
{
    const QVariantList list = buildList();
    if (list != m_list) {
        m_list = list;
        Q_EMIT changed();
    }
}

QVariantList DriveWatcher::buildList() const
{
    QList<Alveus::Drive> mounted = Alveus::fromMounts(QStorageInfo::mountedVolumes());
    const QHash<QString, bool> removable = Alveus::removableFromLsblk(m_lsblkJson);
    QVariantList list;
    for (Alveus::Drive &d : mounted) {
        d.removable = removable.value(d.device, d.mountPoint.startsWith(QLatin1String("/run/media/")));
        list << Alveus::toMap(d);
    }
    for (const Alveus::Drive &d : Alveus::unmountedFromLsblk(m_lsblkJson)) {
        list << Alveus::toMap(d);
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
