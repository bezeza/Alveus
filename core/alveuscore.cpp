/*
 * Alveus — desktop fences for KDE Plasma 6
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "alveuscore.h"
#include "drives.h"
#include "integrations.h"
#include "runner.h"
#include "thumbnails.h"

#include <QClipboard>
#include <QDateTime>
#include <QCollator>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLocale>
#include <QMimeData>
#include <QMimeDatabase>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QTextStream>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#ifndef ALVEUS_VERSION
#define ALVEUS_VERSION "0.03.1 beta"
#endif

namespace
{
constexpr int kStoreVersion = 2;
constexpr int kMaxUndo = 30;
constexpr qint64 kMaxPreviewBytes = 25 * 1024 * 1024;
const QString kCutMime = QStringLiteral("application/x-kde-cutselection");

AlveusCore *s_instance = nullptr;

QMutex s_diagnosticsMutex;
QString s_diagnosticsPath;
QString s_diagnosticsHomePath;
QString s_diagnosticsDesktopPath;
QString s_diagnosticsSettingsPath;
QtMessageHandler s_previousMessageHandler = nullptr;
bool s_diagnosticsHandlerInstalled = false;
thread_local bool s_insideDiagnosticsHandler = false;

constexpr qint64 kDiagnosticsMaxBytes = 1024 * 1024;
constexpr int kDiagnosticsRotations = 3;
constexpr int kDiagnosticsMaxMessageLength = 8192;

QString redactDiagnostic(QString value)
{
    if (!s_diagnosticsDesktopPath.isEmpty()) {
        value.replace(s_diagnosticsDesktopPath, QStringLiteral("<desktop>"));
    }
    if (!s_diagnosticsSettingsPath.isEmpty()) {
        value.replace(s_diagnosticsSettingsPath, QStringLiteral("<settings>"));
    }
    if (!s_diagnosticsHomePath.isEmpty()) {
        value.replace(s_diagnosticsHomePath, QStringLiteral("~"));
    }
    if (value.size() > kDiagnosticsMaxMessageLength) {
        value.truncate(kDiagnosticsMaxMessageLength);
        value += QStringLiteral("…");
    }
    return value;
}

QString diagnosticsLevel(QtMsgType type)
{
    switch (type) {
    case QtWarningMsg: return QStringLiteral("warning");
    case QtCriticalMsg: return QStringLiteral("critical");
    case QtFatalMsg: return QStringLiteral("fatal");
    case QtInfoMsg: return QStringLiteral("info");
    case QtDebugMsg: return QStringLiteral("debug");
    }
    return QStringLiteral("unknown");
}

void rotateDiagnosticsLog()
{
    if (QFileInfo(s_diagnosticsPath).size() < kDiagnosticsMaxBytes) {
        return;
    }
    QFile::remove(s_diagnosticsPath + QStringLiteral(".3"));
    QFile::rename(s_diagnosticsPath + QStringLiteral(".2"), s_diagnosticsPath + QStringLiteral(".3"));
    QFile::rename(s_diagnosticsPath + QStringLiteral(".1"), s_diagnosticsPath + QStringLiteral(".2"));
    QFile::rename(s_diagnosticsPath, s_diagnosticsPath + QStringLiteral(".1"));
}

void appendDiagnosticLocked(const QString &level, const QString &category, const QString &message,
                            const QString &source = QString(), int line = 0)
{
    if (s_diagnosticsPath.isEmpty()
        || !QDir().mkpath(QFileInfo(s_diagnosticsPath).absolutePath())) {
        return;
    }

    rotateDiagnosticsLog();

    QJsonObject entry;
    entry.insert(QStringLiteral("timestamp"),
                 QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    entry.insert(QStringLiteral("level"), redactDiagnostic(level));
    entry.insert(QStringLiteral("category"), redactDiagnostic(category));
    entry.insert(QStringLiteral("message"), redactDiagnostic(message));
    if (!source.isEmpty()) {
        entry.insert(QStringLiteral("source"), QFileInfo(source).fileName());
    }
    if (line > 0) {
        entry.insert(QStringLiteral("line"), line);
    }

    QFile file(s_diagnosticsPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }
    file.write(QJsonDocument(entry).toJson(QJsonDocument::Compact));
    file.write("\n", 1);
    file.flush();
}

void appendDiagnostic(const QString &level, const QString &category, const QString &message)
{
    QMutexLocker locker(&s_diagnosticsMutex);
    appendDiagnosticLocked(level, category, message);
}

void alveusMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    // Logging can itself trigger a Qt warning (for example, a filesystem error).
    // Do not recurse into the logger or try to lock its non-recursive mutex again.
    if (s_insideDiagnosticsHandler) {
        if (type != QtDebugMsg && type != QtInfoMsg) {
            const QByteArray output = message.toLocal8Bit();
            std::fprintf(stderr, "%s\n", output.constData());
            std::fflush(stderr);
        }
        return;
    }

    s_insideDiagnosticsHandler = true;
    if (type != QtDebugMsg && type != QtInfoMsg) {
        const QString category = context.category ? QString::fromUtf8(context.category) : QString();
        const QString source = context.file ? QString::fromUtf8(context.file) : QString();
        {
            QMutexLocker locker(&s_diagnosticsMutex);
            appendDiagnosticLocked(diagnosticsLevel(type), category, message, source, context.line);
        }
    }

    QtMessageHandler previous = nullptr;
    {
        QMutexLocker locker(&s_diagnosticsMutex);
        previous = s_previousMessageHandler;
    }
    if (previous && previous != alveusMessageHandler) {
        previous(type, context, message);
    } else if (type != QtDebugMsg && type != QtInfoMsg) {
        const QByteArray output = message.toLocal8Bit();
        std::fprintf(stderr, "%s\n", output.constData());
        std::fflush(stderr);
    }
    s_insideDiagnosticsHandler = false;
}

void initializeDiagnostics(const QString &desktopPath, const QString &settingsPath)
{
    bool installHandler = false;
    {
        QMutexLocker locker(&s_diagnosticsMutex);
        QString dataPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        if (dataPath.isEmpty()) {
            dataPath = QDir::homePath() + QStringLiteral("/.local/share");
        }
        s_diagnosticsPath = QDir(dataPath).filePath(QStringLiteral("alveus/logs/alveus.log"));
        s_diagnosticsHomePath = QDir::homePath();
        s_diagnosticsDesktopPath = QDir::cleanPath(desktopPath);
        s_diagnosticsSettingsPath = QDir::cleanPath(settingsPath);
        if (!s_diagnosticsHandlerInstalled) {
            s_diagnosticsHandlerInstalled = true;
            installHandler = true;
        }
    }
    if (installHandler) {
        const QtMessageHandler previous = qInstallMessageHandler(alveusMessageHandler);
        QMutexLocker locker(&s_diagnosticsMutex);
        s_previousMessageHandler = previous;
    }
}

QVariantList readRecentDiagnostics(int limit)
{
    QMutexLocker locker(&s_diagnosticsMutex);
    QVariantList entries;
    if (s_diagnosticsPath.isEmpty()) {
        return entries;
    }

    QStringList paths;
    for (int i = kDiagnosticsRotations; i >= 1; --i) {
        paths << s_diagnosticsPath + QLatin1Char('.') + QString::number(i);
    }
    paths << s_diagnosticsPath;

    for (const QString &path : paths) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        while (!file.atEnd()) {
            const QByteArray line = file.readLine().trimmed();
            if (line.isEmpty()) {
                continue;
            }
            const QJsonDocument document = QJsonDocument::fromJson(line);
            if (!document.isObject()) {
                continue;
            }
            QVariantMap entry = document.object().toVariantMap();
            const QDateTime timestamp = QDateTime::fromString(
                entry.value(QStringLiteral("timestamp")).toString(), Qt::ISODateWithMs);
            entry.insert(QStringLiteral("time"), timestamp.isValid()
                ? timestamp.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                : entry.value(QStringLiteral("timestamp")));
            entries << entry;
        }
    }

    const int wanted = limit > 0 ? limit : 100;
    while (entries.size() > wanted) {
        entries.removeFirst();
    }
    std::reverse(entries.begin(), entries.end());
    return entries;
}

QString diagnosticsReport(int limit)
{
    const QVariantList entries = readRecentDiagnostics(limit);
    QString report = QStringLiteral("Alveus diagnostic log\n");
    for (auto it = entries.crbegin(); it != entries.crend(); ++it) {
        const QVariantMap entry = it->toMap();
        report += QStringLiteral("[%1] [%2] %3: %4")
            .arg(entry.value(QStringLiteral("timestamp")).toString(),
                 entry.value(QStringLiteral("level")).toString(),
                 entry.value(QStringLiteral("category")).toString(),
                 entry.value(QStringLiteral("message")).toString());
        const QString source = entry.value(QStringLiteral("source")).toString();
        if (!source.isEmpty()) {
            report += QStringLiteral(" (%1:%2)").arg(source).arg(entry.value(QStringLiteral("line")).toInt());
        }
        report += QLatin1Char('\n');
    }
    return report;
}

void clearDiagnosticFiles()
{
    QMutexLocker locker(&s_diagnosticsMutex);
    if (s_diagnosticsPath.isEmpty()) {
        return;
    }
    QFile::remove(s_diagnosticsPath);
    for (int i = 1; i <= kDiagnosticsRotations; ++i) {
        QFile::remove(s_diagnosticsPath + QLatin1Char('.') + QString::number(i));
    }
}


QString envOr(const char *name, const QString &fallback)
{
    const QString v = qEnvironmentVariable(name);
    return v.isEmpty() ? fallback : v;
}

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
}

QString configHome()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
}

QClipboard *clipboard()
{
    return qobject_cast<QGuiApplication *>(QCoreApplication::instance()) ? QGuiApplication::clipboard() : nullptr;
}

QJsonObject fileOp(const QString &op, const QString &a, const QString &b = QString())
{
    QJsonObject o;
    o.insert(QStringLiteral("op"), op);
    o.insert(QStringLiteral("a"), a);
    if (!b.isEmpty()) {
        o.insert(QStringLiteral("b"), b);
    }
    return o;
}

const QHash<QString, QVariant> &defaultSettings()
{
    static const QHash<QString, QVariant> d = {
        {QStringLiteral("iconSize"), 48},
        {QStringLiteral("autoPrivacy"), false},
        {QStringLiteral("cleanupDays"), 60},
        {QStringLiteral("cleanupReminder"), true},
        {QStringLiteral("singleClick"), false},
        {QStringLiteral("showDonate"), true},
    };
    return d;
}

bool isJsonNumber(const QJsonValue &value);
bool isJsonInteger(const QJsonValue &value);
bool hasExpectedType(const QJsonObject &object, const QString &key, QJsonValue::Type type);

const QJsonObject &defaultDriveOptions()
{
    static const QJsonObject defaults = {
        {QStringLiteral("groups"), QJsonObject{
            {QStringLiteral("internal"), true},
            {QStringLiteral("usb"), true},
            {QStringLiteral("external"), true},
            {QStringLiteral("network"), true},
            {QStringLiteral("virtual"), false},
            {QStringLiteral("optical"), true},
        }},
        {QStringLiteral("hideEfi"), true},
        {QStringLiteral("hideMicrosoftReserved"), true},
        {QStringLiteral("hideRecovery"), true},
        {QStringLiteral("hideSwap"), true},
        {QStringLiteral("hideBiosBoot"), true},
        {QStringLiteral("showUnmounted"), true},
        {QStringLiteral("partitionMode"), QStringLiteral("all")},
        {QStringLiteral("displayMode"), QStringLiteral("medium")},
        {QStringLiteral("minimumPartitionSizeMiB"), 0},
    };
    return defaults;
}

bool validDriveOptions(const QJsonObject &options, bool allowExceptions)
{
    if (options.contains(QStringLiteral("groups"))) {
        const QJsonValue groupsValue = options.value(QStringLiteral("groups"));
        if (!groupsValue.isObject()) {
            return false;
        }
        const QJsonObject groups = groupsValue.toObject();
        const QStringList keys = {QStringLiteral("internal"), QStringLiteral("usb"), QStringLiteral("external"),
                                  QStringLiteral("network"), QStringLiteral("virtual"), QStringLiteral("optical")};
        for (const QString &key : keys) {
            if (!hasExpectedType(groups, key, QJsonValue::Bool)) {
                return false;
            }
        }
    }

    const QStringList boolKeys = {QStringLiteral("hideEfi"), QStringLiteral("hideMicrosoftReserved"),
                                  QStringLiteral("hideRecovery"), QStringLiteral("hideSwap"),
                                  QStringLiteral("hideBiosBoot"), QStringLiteral("showUnmounted")};
    for (const QString &key : boolKeys) {
        if (!hasExpectedType(options, key, QJsonValue::Bool)) {
            return false;
        }
    }

    if (options.contains(QStringLiteral("partitionMode"))) {
        const QJsonValue mode = options.value(QStringLiteral("partitionMode"));
        if (!mode.isString()) {
            return false;
        }
        const QString value = mode.toString();
        if (value != QLatin1String("all") && value != QLatin1String("mounted") && value != QLatin1String("selected")) {
            return false;
        }
    }

    if (options.contains(QStringLiteral("displayMode"))) {
        const QJsonValue mode = options.value(QStringLiteral("displayMode"));
        if (!mode.isString()) {
            return false;
        }
        const QString value = mode.toString();
        if (value != QLatin1String("compact") && value != QLatin1String("medium")
            && value != QLatin1String("large")) {
            return false;
        }
    }

    if (options.contains(QStringLiteral("minimumPartitionSizeMiB"))) {
        const QJsonValue size = options.value(QStringLiteral("minimumPartitionSizeMiB"));
        if (!isJsonInteger(size) || size.toDouble() < 0 || size.toDouble() > 1048576) {
            return false;
        }
    }

    if (options.contains(QStringLiteral("visibilityExceptions"))) {
        if (!allowExceptions || !options.value(QStringLiteral("visibilityExceptions")).isObject()) {
            return false;
        }
        const QJsonObject exceptions = options.value(QStringLiteral("visibilityExceptions")).toObject();
        for (auto it = exceptions.constBegin(); it != exceptions.constEnd(); ++it) {
            if (it.key().trimmed().isEmpty() || !it.value().isString()) {
                return false;
            }
            const QString mode = it.value().toString();
            if (mode != QLatin1String("hide") && mode != QLatin1String("show")) {
                return false;
            }
        }
    }
    return true;
}

bool validSettingsObject(const QJsonObject &settings)
{
    if (!settings.contains(QStringLiteral("drives"))) {
        return true;
    }
    const QJsonValue drives = settings.value(QStringLiteral("drives"));
    return drives.isObject() && validDriveOptions(drives.toObject(), false);
}

QJsonObject mergeDriveOptions(QJsonObject base, const QJsonObject &overrides)
{
    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
        if (it.key() == QLatin1String("groups") && it.value().isObject()) {
            QJsonObject groups = base.value(QStringLiteral("groups")).toObject();
            const QJsonObject groupOverrides = it.value().toObject();
            for (auto group = groupOverrides.constBegin(); group != groupOverrides.constEnd(); ++group) {
                groups.insert(group.key(), group.value());
            }
            base.insert(QStringLiteral("groups"), groups);
        } else {
            base.insert(it.key(), it.value());
        }
    }
    return base;
}

bool setDriveOptionValue(QJsonObject &options, const QString &key, const QJsonValue &value)
{
    if (key.startsWith(QLatin1String("groups."))) {
        const QString group = key.mid(7);
        static const QStringList allowed = {QStringLiteral("internal"), QStringLiteral("usb"), QStringLiteral("external"),
                                            QStringLiteral("network"), QStringLiteral("virtual"), QStringLiteral("optical")};
        if (!allowed.contains(group) || !value.isBool()) {
            return false;
        }
        QJsonObject groups = options.value(QStringLiteral("groups")).toObject();
        groups.insert(group, value);
        options.insert(QStringLiteral("groups"), groups);
        return true;
    }

    static const QStringList boolKeys = {QStringLiteral("hideEfi"), QStringLiteral("hideMicrosoftReserved"),
                                         QStringLiteral("hideRecovery"), QStringLiteral("hideSwap"),
                                         QStringLiteral("hideBiosBoot"), QStringLiteral("showUnmounted")};
    if (boolKeys.contains(key)) {
        if (!value.isBool()) {
            return false;
        }
        options.insert(key, value);
        return true;
    }
    if (key == QLatin1String("partitionMode")) {
        if (!value.isString()) {
            return false;
        }
        const QString mode = value.toString();
        if (mode != QLatin1String("all") && mode != QLatin1String("mounted") && mode != QLatin1String("selected")) {
            return false;
        }
        options.insert(key, value);
        return true;
    }
    if (key == QLatin1String("displayMode")) {
        if (!value.isString()) {
            return false;
        }
        const QString mode = value.toString();
        if (mode != QLatin1String("compact") && mode != QLatin1String("medium")
            && mode != QLatin1String("large")) {
            return false;
        }
        options.insert(key, value);
        return true;
    }
    if (key == QLatin1String("minimumPartitionSizeMiB")) {
        if (!isJsonInteger(value) || value.toDouble() < 0 || value.toDouble() > 1048576) {
            return false;
        }
        options.insert(key, value);
        return true;
    }
    return false;
}

bool removeDriveOptionValue(QJsonObject &options, const QString &key)
{
    if (key.startsWith(QLatin1String("groups."))) {
        const QString group = key.mid(7);
        QJsonObject groups = options.value(QStringLiteral("groups")).toObject();
        if (!groups.contains(group)) {
            return false;
        }
        groups.remove(group);
        if (groups.isEmpty()) {
            options.remove(QStringLiteral("groups"));
        } else {
            options.insert(QStringLiteral("groups"), groups);
        }
        return true;
    }
    if (!options.contains(key)) {
        return false;
    }
    options.remove(key);
    return true;
}

bool isJsonNumber(const QJsonValue &value)
{
    return value.isDouble() && std::isfinite(value.toDouble());
}

bool isJsonInteger(const QJsonValue &value)
{
    return isJsonNumber(value) && std::floor(value.toDouble()) == value.toDouble();
}

bool hasExpectedType(const QJsonObject &object, const QString &key, QJsonValue::Type type)
{
    return !object.contains(key) || object.value(key).type() == type;
}

bool validStateObject(const QJsonObject &root)
{
    // `fences` is the one mandatory state collection in every released layout.
    // Accept older v1 layouts that omitted the newer `pages`/`files` fields.
    if (!root.value(QStringLiteral("fences")).isArray()) {
        return false;
    }

    QSet<QString> fenceIds;
    const QJsonArray fences = root.value(QStringLiteral("fences")).toArray();
    for (const QJsonValue &value : fences) {
        if (!value.isObject()) {
            return false;
        }
        const QJsonObject fence = value.toObject();
        const QJsonValue idValue = fence.value(QStringLiteral("id"));
        if (!idValue.isString() || idValue.toString().trimmed().isEmpty() || fenceIds.contains(idValue.toString())) {
            return false;
        }
        fenceIds.insert(idValue.toString());

        const QStringList stringKeys = {QStringLiteral("title"), QStringLiteral("activity"), QStringLiteral("rules"),
                                        QStringLiteral("portal"), QStringLiteral("group"), QStringLiteral("color"),
                                        QStringLiteral("view")};
        for (const QString &key : stringKeys) {
            if (!hasExpectedType(fence, key, QJsonValue::String)) {
                return false;
            }
        }
        const QStringList boolKeys = {QStringLiteral("collapsed"), QStringLiteral("activeTab"), QStringLiteral("blur")};
        for (const QString &key : boolKeys) {
            if (!hasExpectedType(fence, key, QJsonValue::Bool)) {
                return false;
            }
        }
        const QStringList integerKeys = {QStringLiteral("screen"), QStringLiteral("page"), QStringLiteral("iconSize")};
        for (const QString &key : integerKeys) {
            if (fence.contains(key) && !isJsonInteger(fence.value(key))) {
                return false;
            }
        }
        const QStringList numberKeys = {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("w"),
                                         QStringLiteral("h"), QStringLiteral("opacity"), QStringLiteral("created")};
        for (const QString &key : numberKeys) {
            if (fence.contains(key) && !isJsonNumber(fence.value(key))) {
                return false;
            }
        }
        if (fence.contains(QStringLiteral("orphan"))) {
            const QJsonValue orphanValue = fence.value(QStringLiteral("orphan"));
            if (!orphanValue.isArray()) {
                return false;
            }
            const QJsonArray orphan = orphanValue.toArray();
            if (orphan.size() != 4 || !isJsonNumber(orphan[0]) || !isJsonNumber(orphan[1])
                || !isJsonNumber(orphan[2]) || !isJsonNumber(orphan[3])) {
                return false;
            }
        }
        if (fence.contains(QStringLiteral("extensions"))) {
            if (!fence.value(QStringLiteral("extensions")).isArray()) {
                return false;
            }
            for (const QJsonValue &extension : fence.value(QStringLiteral("extensions")).toArray()) {
                if (!extension.isString()) {
                    return false;
                }
            }
        }
        if (fence.contains(QStringLiteral("driveOptions"))
            && (!fence.value(QStringLiteral("driveOptions")).isObject()
                || !validDriveOptions(fence.value(QStringLiteral("driveOptions")).toObject(), true))) {
            return false;
        }
    }

    if (root.contains(QStringLiteral("files"))) {
        if (!root.value(QStringLiteral("files")).isObject()) {
            return false;
        }
        const QJsonObject files = root.value(QStringLiteral("files")).toObject();
        for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
            if (!it.value().isObject()) {
                return false;
            }
            const QJsonObject placement = it.value().toObject();
            if (!hasExpectedType(placement, QStringLiteral("manual"), QJsonValue::Bool)
                || !hasExpectedType(placement, QStringLiteral("fence"), QJsonValue::String)) {
                return false;
            }
            if (placement.contains(QStringLiteral("pos"))) {
                const QJsonValue posValue = placement.value(QStringLiteral("pos"));
                if (!posValue.isArray()) {
                    return false;
                }
                const QJsonArray pos = posValue.toArray();
                if (pos.size() != 4 || !isJsonInteger(pos[0]) || !isJsonInteger(pos[1])
                    || !isJsonInteger(pos[2]) || !isJsonInteger(pos[3])) {
                    return false;
                }
            }
        }
    }

    if (root.contains(QStringLiteral("pages"))) {
        if (!root.value(QStringLiteral("pages")).isObject()) {
            return false;
        }
        const QJsonObject pages = root.value(QStringLiteral("pages")).toObject();
        for (auto it = pages.constBegin(); it != pages.constEnd(); ++it) {
            if (!isJsonInteger(it.value()) || it.value().toInt() < 1) {
                return false;
            }
        }
    }
    return true;
}

bool validSnapshots(const QJsonValue &value)
{
    if (!value.isArray()) {
        return false;
    }
    for (const QJsonValue &item : value.toArray()) {
        if (!item.isObject()) {
            return false;
        }
        const QJsonObject snapshot = item.toObject();
        if (!snapshot.value(QStringLiteral("id")).isString()
            || snapshot.value(QStringLiteral("id")).toString().trimmed().isEmpty()
            || !snapshot.value(QStringLiteral("name")).isString()
            || (snapshot.contains(QStringLiteral("created")) && !isJsonNumber(snapshot.value(QStringLiteral("created"))))) {
            return false;
        }
        if (!snapshot.value(QStringLiteral("state")).isObject()
            || !validStateObject(snapshot.value(QStringLiteral("state")).toObject())) {
            return false;
        }
    }
    return true;
}
} // namespace

// =============================================================================
// life cycle

AlveusCore::AlveusCore(const QString &desktopPath, const QString &storePath, QObject *parent)
    : QObject(parent)
{
    m_desktopPath = QDir::cleanPath(!desktopPath.isEmpty()
        ? desktopPath
        : envOr("ALVEUS_DESKTOP_DIR", QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)));
    m_storePath = !storePath.isEmpty()
        ? storePath
        : envOr("ALVEUS_STORE",
                QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/alveus/layout.json"));
    QDir().mkpath(m_desktopPath);
    initializeDiagnostics(m_desktopPath, m_storePath);
    connect(this, &AlveusCore::message, this, [this](const QString &kind, const QString &detail) {
        if (kind == QStringLiteral("files-done")) {
            return;
        }
        const QString level = (kind == QStringLiteral("file-source-left")
                               || kind == QStringLiteral("undo-conflict"))
            ? QStringLiteral("warning") : QStringLiteral("error");
        appendDiagnostic(level, QStringLiteral("Alveus"), kind + QStringLiteral(": ") + detail);
        Q_EMIT diagnosticsChanged();
    });
    appendDiagnostic(QStringLiteral("info"), QStringLiteral("lifecycle"),
                     QStringLiteral("Alveus started: ") + version());

    // Throttled, not restarted on every event: a burst of changes gives one
    // rescan 250 ms after the first, and continuous changes at most 4 per second.
    m_scanTimer.setSingleShot(true);
    m_scanTimer.setInterval(250);
    connect(&m_scanTimer, &QTimer::timeout, this, &AlveusCore::rescan);

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &AlveusCore::save);

    // Time rules change with the clock; periodic rescans also catch metadata
    // changes on filesystems where a per-file watch could not be installed.
    m_clockTimer.setInterval(10 * 60 * 1000);
    connect(&m_clockTimer, &QTimer::timeout, this, [this]() {
        invalidateAllFolders(); // portal contents may change without a directory-entry event
        rescan();
    });
    m_clockTimer.start();

    m_watcher.addPath(m_desktopPath);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &path) {
        const QString clean = QDir::cleanPath(path);
        invalidateFolder(clean);
        if (clean == m_desktopPath || QDir::cleanPath(QFileInfo(clean).absolutePath()) == m_desktopPath) {
            scheduleRescan();
        }
    });
    // A file being written (a download, a copy from a phone) sends a change
    // event for every write. Refresh only that file, at most once a second.
    m_fileChangeTimer.setSingleShot(true);
    m_fileChangeTimer.setInterval(1000);
    connect(&m_fileChangeTimer, &QTimer::timeout, this, &AlveusCore::refreshChangedFiles);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        const QString parent = QDir::cleanPath(QFileInfo(path).absolutePath());
        if (parent == m_desktopPath) {
            m_changedFiles.insert(QFileInfo(path).fileName());
            if (!m_fileChangeTimer.isActive()) {
                m_fileChangeTimer.start();
            }
        } else {
            invalidateFolder(parent);
        }
    });

    m_jobTimer.setInterval(200);
    connect(&m_jobTimer, &QTimer::timeout, this, &AlveusCore::updateFileJob);
    m_fileJob.insert(QStringLiteral("active"), false);

    m_thumbs = new Thumbnailer(this);
    connect(m_thumbs, &Thumbnailer::ready, this, &AlveusCore::thumbnailReady);
    connect(m_thumbs, &Thumbnailer::message, this, &AlveusCore::message);

    m_drives = new DriveWatcher(this);
    connect(m_drives, &DriveWatcher::changed, this, &AlveusCore::drivesChanged);
    connect(m_drives, &DriveWatcher::message, this, &AlveusCore::message);

    m_integrations = new Integrations(this);
    connect(m_integrations, &Integrations::message, this, &AlveusCore::message);
    connect(m_integrations, &Integrations::phonesChanged, this, [this](const QVariantList &p) {
        m_phones = p;
        Q_EMIT phonesChanged();
    });
    connect(m_integrations, &Integrations::screenSharedChanged, this, [this](bool shared) {
        m_screenShared = shared;
        Q_EMIT privacyChanged();
    });

    if (QClipboard *cb = clipboard()) {
        connect(cb, &QClipboard::dataChanged, this, &AlveusCore::clipboardChanged);
    }

    load();
    rescan();

    m_integrations->setWatchScreenShare(setting(QStringLiteral("autoPrivacy")).toBool());

    if (qEnvironmentVariableIsEmpty("ALVEUS_NO_DBUS")) {
        m_runner = new RunnerService(this);
        m_runner->registerOnBus();
    }
}

AlveusCore::~AlveusCore()
{
    // plasmashell is quitting: stop the running copy (its temporary copy is
    // removed, originals are never touched) and drop the queue.
    m_jobs.clear();
    if (m_jobThread) {
        m_jobControl->cancel = true;
        m_jobThread->wait();
        delete m_jobThread;
        m_jobThread = nullptr;
    }
    if (m_saveTimer.isActive()) {
        save();
    }
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

AlveusCore *AlveusCore::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)
    if (!s_instance) {
        s_instance = new AlveusCore();
    }
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

QString AlveusCore::version() const
{
    return QStringLiteral(ALVEUS_VERSION);
}

QString AlveusCore::diagnosticsPath() const
{
    QMutexLocker locker(&s_diagnosticsMutex);
    return s_diagnosticsPath;
}

QVariantList AlveusCore::recentDiagnostics(int limit) const
{
    return readRecentDiagnostics(limit);
}

void AlveusCore::openDiagnosticsFolder() const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(diagnosticsPath()).absolutePath()));
}

void AlveusCore::copyDiagnostics() const
{
    if (QClipboard *cb = QGuiApplication::clipboard()) {
        cb->setText(diagnosticsReport(1000));
    }
}

void AlveusCore::clearDiagnostics()
{
    clearDiagnosticFiles();
    Q_EMIT diagnosticsChanged();
}

void AlveusCore::bump()
{
    ++m_revision;
    Q_EMIT changed();
}

void AlveusCore::bumpFences(bool fileMembershipChanged)
{
    if (fileMembershipChanged) {
        refreshFileModelCaches(true);
    }
    Q_EMIT fencesChanged();
    bump();
}

void AlveusCore::bumpFileModels(const QSet<QString> &changedNames)
{
    refreshFileModelCaches(false, changedNames);
    bump();
}

void AlveusCore::bumpAllFileModels()
{
    refreshFileModelCaches(true);
    bump();
}

void AlveusCore::bumpDesktopPositions()
{
    Q_EMIT desktopPositionsChanged();
    bump();
}

void AlveusCore::bumpDesktopTopology()
{
    Q_EMIT desktopTopologyChanged();
    bump();
}

const AlveusCore::FileModelCache &AlveusCore::fileModelCacheFor(const QString &activity) const
{
    const auto existing = m_fileModelCache.constFind(activity);
    if (existing != m_fileModelCache.cend()) {
        return existing.value();
    }

    FileModelCache cache;
    for (const Entry &entry : m_files) {
        const QString fenceId = placementFor(entry, activity);
        cache.byFence[fenceId].append(toMap(entry));
        cache.fenceByName.insert(entry.name, fenceId);
    }
    return m_fileModelCache.insert(activity, std::move(cache)).value();
}

void AlveusCore::refreshFileModelCaches(bool rebuildAll, const QSet<QString> &changedNames)
{
    if (m_fileModelCache.isEmpty()) {
        return;
    }

    QHash<QString, const Entry *> currentEntries;
    if (!rebuildAll) {
        for (const Entry &entry : std::as_const(m_files)) {
            currentEntries.insert(entry.name, &entry);
        }
    }

    for (auto cacheIt = m_fileModelCache.begin(); cacheIt != m_fileModelCache.end(); ++cacheIt) {
        const QString activity = cacheIt.key();
        FileModelCache &cache = cacheIt.value();
        QStringList changedFenceIds;

        if (rebuildAll) {
            FileModelCache updated;
            for (const Entry &entry : std::as_const(m_files)) {
                const QString fenceId = placementFor(entry, activity);
                updated.byFence[fenceId].append(toMap(entry));
                updated.fenceByName.insert(entry.name, fenceId);
            }

            QSet<QString> allFenceIds;
            for (auto it = cache.byFence.cbegin(); it != cache.byFence.cend(); ++it) {
                allFenceIds.insert(it.key());
            }
            for (auto it = updated.byFence.cbegin(); it != updated.byFence.cend(); ++it) {
                allFenceIds.insert(it.key());
            }
            for (const QString &fenceId : std::as_const(allFenceIds)) {
                if (cache.byFence.value(fenceId) != updated.byFence.value(fenceId)) {
                    changedFenceIds << fenceId;
                }
            }
            cache = std::move(updated);
        } else {
            QSet<QString> affectedFenceIds;
            for (const QString &name : changedNames) {
                const auto old = cache.fenceByName.find(name);
                if (old != cache.fenceByName.end()) {
                    affectedFenceIds.insert(old.value());
                    cache.fenceByName.erase(old);
                }

                const auto current = currentEntries.constFind(name);
                if (current != currentEntries.cend()) {
                    const QString newFenceId = placementFor(*current.value(), activity);
                    cache.fenceByName.insert(name, newFenceId);
                    affectedFenceIds.insert(newFenceId);
                }
            }

            for (const QString &fenceId : std::as_const(affectedFenceIds)) {
                QVariantList updated;
                for (const Entry &entry : std::as_const(m_files)) {
                    if (cache.fenceByName.value(entry.name) == fenceId) {
                        updated << toMap(entry);
                    }
                }
                if (cache.byFence.value(fenceId) != updated) {
                    changedFenceIds << fenceId;
                }
                if (updated.isEmpty()) {
                    cache.byFence.remove(fenceId);
                } else {
                    cache.byFence.insert(fenceId, updated);
                }
            }
        }

        if (!changedFenceIds.isEmpty()) {
            changedFenceIds.removeDuplicates();
            changedFenceIds.sort();
            Q_EMIT fileModelsChanged(activity, changedFenceIds);
        }
    }
}

void AlveusCore::invalidateFolder(const QString &path)
{
    const QString clean = QDir::cleanPath(path);
    if (m_folders.remove(clean)) {
        Q_EMIT folderContentsChanged(clean);
    }
}

void AlveusCore::invalidateAllFolders()
{
    const QStringList paths = m_folders.keys();
    m_folders.clear();
    for (const QString &path : paths) {
        Q_EMIT folderContentsChanged(path);
    }
}

// =============================================================================
// persistence

static QJsonObject fenceToJson(const auto &f)
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), f.id);
    o.insert(QStringLiteral("title"), f.title);
    o.insert(QStringLiteral("screen"), f.screen);
    o.insert(QStringLiteral("activity"), f.activity);
    o.insert(QStringLiteral("page"), f.page);
    o.insert(QStringLiteral("x"), f.x);
    o.insert(QStringLiteral("y"), f.y);
    o.insert(QStringLiteral("w"), f.w);
    o.insert(QStringLiteral("h"), f.h);
    if (f.hasOrphanGeometry) {
        o.insert(QStringLiteral("orphan"), QJsonArray{f.ox, f.oy, f.ow, f.oh});
    }
    o.insert(QStringLiteral("collapsed"), f.collapsed);
    o.insert(QStringLiteral("rules"), f.rules);
    o.insert(QStringLiteral("portal"), f.portal);
    o.insert(QStringLiteral("group"), f.group);
    o.insert(QStringLiteral("activeTab"), f.activeTab);
    o.insert(QStringLiteral("color"), f.color);
    o.insert(QStringLiteral("opacity"), f.opacity);
    o.insert(QStringLiteral("blur"), f.blur);
    o.insert(QStringLiteral("view"), f.view);
    o.insert(QStringLiteral("iconSize"), f.iconSize);
    o.insert(QStringLiteral("created"), f.created);
    if (!f.driveOptions.isEmpty()) {
        o.insert(QStringLiteral("driveOptions"), f.driveOptions);
    }
    return o;
}

QJsonObject AlveusCore::stateJson() const
{
    QJsonObject root;
    QJsonArray fences;
    for (const Fence &f : m_fences) {
        fences.append(fenceToJson(f));
    }
    root.insert(QStringLiteral("fences"), fences);

    QJsonObject files;
    for (auto it = m_place.cbegin(); it != m_place.cend(); ++it) {
        if (!it->manual && !it->hasPos) {
            continue;
        }
        QJsonObject o;
        if (it->manual) {
            o.insert(QStringLiteral("fence"), it->fence);
            o.insert(QStringLiteral("manual"), true);
        }
        if (it->hasPos) {
            o.insert(QStringLiteral("pos"), QJsonArray{it->screen, it->page, it->col, it->row});
        }
        files.insert(it.key(), o);
    }
    root.insert(QStringLiteral("files"), files);

    QJsonObject pages;
    for (auto it = m_pages.cbegin(); it != m_pages.cend(); ++it) {
        pages.insert(it.key(), it.value());
    }
    root.insert(QStringLiteral("pages"), pages);
    return root;
}

void AlveusCore::applyState(const QJsonObject &root, bool includeSettings)
{
    m_fences.clear();
    m_place.clear();
    m_pages.clear();

    for (const QJsonValue &v : root.value(QStringLiteral("fences")).toArray()) {
        const QJsonObject o = v.toObject();
        Fence f;
        f.id = o.value(QStringLiteral("id")).toString();
        if (f.id.isEmpty()) {
            continue;
        }
        f.title = o.value(QStringLiteral("title")).toString();
        f.screen = o.value(QStringLiteral("screen")).toInt();
        f.activity = o.value(QStringLiteral("activity")).toString();
        f.page = std::max(0, o.value(QStringLiteral("page")).toInt());
        f.x = o.value(QStringLiteral("x")).toDouble();
        f.y = o.value(QStringLiteral("y")).toDouble();
        f.w = o.value(QStringLiteral("w")).toDouble(0.2);
        f.h = o.value(QStringLiteral("h")).toDouble(0.25);
        const QJsonArray orphan = o.value(QStringLiteral("orphan")).toArray();
        if (orphan.size() == 4) {
            f.hasOrphanGeometry = true;
            f.ox = orphan[0].toDouble();
            f.oy = orphan[1].toDouble();
            f.ow = orphan[2].toDouble();
            f.oh = orphan[3].toDouble();
        }
        f.collapsed = o.value(QStringLiteral("collapsed")).toBool();
        f.rules = o.value(QStringLiteral("rules")).toString();
        if (f.rules.isEmpty() && o.contains(QStringLiteral("extensions"))) { // version 1
            QStringList ext;
            for (const QJsonValue &e : o.value(QStringLiteral("extensions")).toArray()) {
                ext << e.toString();
            }
            f.rules = ext.join(QStringLiteral(", "));
        }
        f.portal = o.value(QStringLiteral("portal")).toString();
        f.group = o.value(QStringLiteral("group")).toString();
        f.activeTab = o.value(QStringLiteral("activeTab")).toBool(true);
        f.color = o.value(QStringLiteral("color")).toString();
        f.opacity = o.value(QStringLiteral("opacity")).toDouble(0.55);
        f.blur = o.value(QStringLiteral("blur")).toBool(true);
        f.view = o.value(QStringLiteral("view")).toString(QStringLiteral("grid"));
        f.iconSize = o.value(QStringLiteral("iconSize")).toInt();
        f.created = qint64(o.value(QStringLiteral("created")).toDouble());
        f.driveOptions = o.value(QStringLiteral("driveOptions")).toObject();
        f.parsed = Alveus::Rules(f.rules);
        fenceNormalizeGeometry(f);
        m_fences << f;
    }

    const QJsonObject files = root.value(QStringLiteral("files")).toObject();
    for (auto it = files.begin(); it != files.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        Placement p;
        p.manual = o.value(QStringLiteral("manual")).toBool();
        p.fence = o.value(QStringLiteral("fence")).toString();
        const QJsonArray pos = o.value(QStringLiteral("pos")).toArray();
        if (pos.size() == 4) {
            p.hasPos = true;
            p.screen = pos[0].toInt();
            p.page = pos[1].toInt();
            p.col = pos[2].toInt();
            p.row = pos[3].toInt();
        }
        if (p.manual || p.hasPos) {
            m_place.insert(it.key(), p);
        }
    }

    const QJsonObject pages = root.value(QStringLiteral("pages")).toObject();
    for (auto it = pages.begin(); it != pages.end(); ++it) {
        m_pages.insert(it.key(), std::max(1, it.value().toInt()));
    }

    if (includeSettings) {
        m_settings = root.value(QStringLiteral("settings")).toObject();
        m_welcomeDismissed = root.value(QStringLiteral("welcomeDismissed")).toBool();
        m_cleanupSnoozedUntil = qint64(root.value(QStringLiteral("cleanupSnoozedUntil")).toDouble());
        m_snapshots.clear();
        for (const QJsonValue &v : root.value(QStringLiteral("snapshots")).toArray()) {
            m_snapshots << v.toObject();
        }
    }
}

void AlveusCore::load()
{
    if (!QFileInfo::exists(m_storePath)) {
        return;
    }

    QFile file(m_storePath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_storeWritable = false;
        const QString detail = file.errorString();
        QTimer::singleShot(0, this, [this, detail]() {
            Q_EMIT message(QStringLiteral("settings-load-failed"), detail);
        });
        return;
    }

    const QByteArray data = file.readAll();
    file.close();

    auto preserveCorruptStore = [this](const QString &reason) {
        QString backup = m_storePath + QStringLiteral(".corrupt-")
            + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmsszzz"));
        int suffix = 1;
        while (QFileInfo::exists(backup)) {
            backup = m_storePath + QStringLiteral(".corrupt-")
                + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmsszzz"))
                + QLatin1Char('-') + QString::number(suffix++);
        }
        m_storeWritable = QFile::copy(m_storePath, backup);
        const QString detail = m_storeWritable
            ? backup : reason;
        const QString kind = m_storeWritable ? QStringLiteral("settings-corrupt")
                                             : QStringLiteral("settings-backup-failed");
        QTimer::singleShot(0, this, [this, kind, detail]() {
            Q_EMIT message(kind, detail);
        });
    };

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        const QString reason = parseError.error != QJsonParseError::NoError
            ? parseError.errorString() : QStringLiteral("not-object");
        preserveCorruptStore(reason);
        return;
    }

    const QJsonObject root = document.object();
    const QJsonValue versionValue = root.value(QStringLiteral("version"));
    if (root.contains(QStringLiteral("version"))
        && (!isJsonNumber(versionValue) || std::floor(versionValue.toDouble()) != versionValue.toDouble())) {
        preserveCorruptStore(QStringLiteral("bad-structure"));
        return;
    }
    const double versionNumber = root.contains(QStringLiteral("version")) ? versionValue.toDouble() : 1.0;
    if (versionNumber > kStoreVersion) {
        // Never overwrite settings written by a newer Alveus version, even if
        // its version number is larger than a 32-bit integer can represent.
        m_storeWritable = false;
        const QString detail = QString::number(versionNumber, 'g', 15);
        QTimer::singleShot(0, this, [this, detail]() {
            Q_EMIT message(QStringLiteral("settings-newer-version"), detail);
        });
        return;
    }
    if (versionNumber < 1) {
        preserveCorruptStore(QStringLiteral("bad-structure"));
        return;
    }
    if (!validStateObject(root)
        || (root.contains(QStringLiteral("settings"))
            && (!root.value(QStringLiteral("settings")).isObject()
                || !validSettingsObject(root.value(QStringLiteral("settings")).toObject())))
        || (root.contains(QStringLiteral("welcomeDismissed")) && !root.value(QStringLiteral("welcomeDismissed")).isBool())
        || (root.contains(QStringLiteral("cleanupSnoozedUntil"))
            && !isJsonNumber(root.value(QStringLiteral("cleanupSnoozedUntil"))))
        || (root.contains(QStringLiteral("snapshots")) && !validSnapshots(root.value(QStringLiteral("snapshots"))))) {
        preserveCorruptStore(QStringLiteral("bad-structure"));
        return;
    }

    applyState(root, true);
}

void AlveusCore::scheduleSave()
{
    m_saveTimer.start();
}

void AlveusCore::saveNow()
{
    m_saveTimer.stop();
    save();
}

void AlveusCore::save()
{
    if (!m_storeWritable) {
        if (!m_saveBlockedReported) { // say it once, not on every change
            m_saveBlockedReported = true;
            Q_EMIT message(QStringLiteral("settings-save-blocked"), m_storePath);
        }
        return;
    }

    QJsonObject root = stateJson();
    root.insert(QStringLiteral("version"), kStoreVersion);
    root.insert(QStringLiteral("welcomeDismissed"), m_welcomeDismissed);
    root.insert(QStringLiteral("cleanupSnoozedUntil"), double(m_cleanupSnoozedUntil));
    root.insert(QStringLiteral("settings"), m_settings);
    QJsonArray snaps;
    for (const QJsonObject &s : m_snapshots) {
        snaps.append(s);
    }
    root.insert(QStringLiteral("snapshots"), snaps);

    if (!QDir().mkpath(QFileInfo(m_storePath).absolutePath())) {
        Q_EMIT message(QStringLiteral("settings-dir-failed"), QFileInfo(m_storePath).absolutePath());
        return;
    }

    QSaveFile file(m_storePath);
    if (!file.open(QIODevice::WriteOnly)) {
        Q_EMIT message(QStringLiteral("settings-save-failed"), file.errorString());
        return;
    }
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size()) {
        const QString error = file.errorString();
        file.cancelWriting();
        Q_EMIT message(QStringLiteral("settings-save-failed"), error);
        return;
    }
    if (!file.commit()) {
        Q_EMIT message(QStringLiteral("settings-save-failed"), file.errorString());
    }
}

// =============================================================================
// undo

void AlveusCore::pushUndo(const QString &text, const QJsonObject &extra)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // Geometry and appearance controls can emit many patches while a user drags
    // a fence or adjusts a slider. Coalesce those patches into one undo step.
    const QString coalesceKey = extra.value(QStringLiteral("coalesceKey")).toString();
    if (!coalesceKey.isEmpty() && !m_undo.isEmpty()) {
        QJsonObject last = m_undo.last();
        const qint64 lastTime = qint64(last.value(QStringLiteral("timestamp")).toDouble());
        if (last.value(QStringLiteral("coalesceKey")).toString() == coalesceKey
            && now >= lastTime && now - lastTime <= 750) {
            last.insert(QStringLiteral("timestamp"), double(now));
            m_undo.last() = last;
            return;
        }
    }

    QJsonObject entry = extra;
    entry.insert(QStringLiteral("text"), text);
    if (!entry.contains(QStringLiteral("state"))) {
        entry.insert(QStringLiteral("state"), stateJson());
    }
    entry.insert(QStringLiteral("timestamp"), double(now));
    entry.insert(QStringLiteral("uid"), newId());
    m_undo << entry;
    while (m_undo.size() > kMaxUndo) {
        m_undo.removeFirst();
    }
    Q_EMIT undoChanged();
}

QString AlveusCore::undoText() const
{
    return m_undo.isEmpty() ? QString() : m_undo.last().value(QStringLiteral("text")).toString();
}

bool AlveusCore::undo()
{
    if (m_undo.isEmpty() || m_undoRunning) {
        return false;
    }
    // Keep the entry until every file operation succeeds. If an operation
    // fails, the user can fix the underlying permission/conflict and retry.
    const QJsonObject entry = m_undo.last();
    const QJsonArray ops = entry.value(QStringLiteral("files")).toArray();
    const auto exists = [](const QString &p) {
        return !p.isEmpty() && (QFileInfo(p).exists() || QFileInfo(p).isSymLink());
    };

    // Work out what still needs doing (steps already done are skipped, so a
    // retry after a failure continues where it stopped) and catch conflicts
    // before anything is touched.
    QList<FileTask> tasks;
    bool slow = false;
    for (int i = ops.size() - 1; i >= 0; --i) {
        const QJsonObject o = ops.at(i).toObject();
        const QString op = o.value(QStringLiteral("op")).toString();
        const QString a = o.value(QStringLiteral("a")).toString();
        const QString b = o.value(QStringLiteral("b")).toString();
        if (op == QLatin1String("created")) { // made by us: send to trash
            if (exists(a)) {
                tasks << FileTask{FileTask::Trash, a, QString()};
            }
            continue;
        }
        if (op != QLatin1String("move") && op != QLatin1String("trashed")) {
            continue;
        }
        // move: was moved from b to a, put it back. trashed: a = in trash, b = original
        if (exists(a) && !exists(b)) {
            tasks << FileTask{op == QLatin1String("move") ? FileTask::Move : FileTask::Restore, a, b};
            QDir().mkpath(QFileInfo(b).absolutePath());
            const bool sameFilesystem = Alveus::FileOps::sameFilesystem(a, QFileInfo(b).absolutePath());
            // movePath validates directory contents before renaming. Keep that
            // recursive walk off the GUI thread, even when both paths share a disk.
            slow = slow || !sameFilesystem || QFileInfo(a).isDir();
        } else if (exists(a) || !exists(b)) {
            Q_EMIT message(QStringLiteral("undo-conflict"), b);
            return false;
        }
    }

    const QString uid = entry.value(QStringLiteral("uid")).toString();
    auto finish = [this, uid, tasks](const TaskResults &results) {
        m_undoRunning = false;
        QStringList sourceLeft;
        QList<int> sourceLeftTaskIndexes;
        int failedIndex = -1;
        for (int i = 0; i < results.size(); ++i) {
            if (results.at(i) == Alveus::FileOps::Result::SourceLeft) {
                sourceLeft << QFileInfo(tasks.at(i).from).fileName();
                sourceLeftTaskIndexes << i;
            } else if (results.at(i) != Alveus::FileOps::Result::Done) {
                failedIndex = i;
                break;
            }
        }

        if (failedIndex >= 0) {
            // A cross-filesystem undo may have restored the destination but
            // failed to remove the old copy. Mark only that step complete so
            // a retry does not mistake the two copies for a user conflict.
            if (!sourceLeftTaskIndexes.isEmpty()) {
                for (int u = m_undo.size() - 1; u >= 0; --u) {
                    if (m_undo.at(u).value(QStringLiteral("uid")).toString() != uid) {
                        continue;
                    }
                    QJsonObject updated = m_undo.at(u);
                    QJsonArray remaining;
                    const QJsonArray originalOps = updated.value(QStringLiteral("files")).toArray();
                    for (const QJsonValue &value : originalOps) {
                        const QJsonObject op = value.toObject();
                        bool completed = false;
                        for (const int taskIndex : sourceLeftTaskIndexes) {
                            const FileTask &task = tasks.at(taskIndex);
                            const QString expectedOp = task.op == FileTask::Restore
                                ? QStringLiteral("trashed") : QStringLiteral("move");
                            if (op.value(QStringLiteral("op")).toString() == expectedOp
                                && op.value(QStringLiteral("a")).toString() == task.from
                                && op.value(QStringLiteral("b")).toString() == task.to) {
                                completed = true;
                                break;
                            }
                        }
                        if (!completed) {
                            remaining.append(value);
                        }
                    }
                    updated.insert(QStringLiteral("files"), remaining);
                    m_undo[u] = updated;
                    break;
                }
            }
            if (!sourceLeft.isEmpty()) {
                Q_EMIT message(QStringLiteral("file-source-left"), sourceLeft.join(QStringLiteral(", ")));
            }
            if (results.at(failedIndex) != Alveus::FileOps::Result::Cancelled) {
                Q_EMIT message(QStringLiteral("undo-failed"), tasks.at(failedIndex).to.isEmpty()
                                   ? tasks.at(failedIndex).from : tasks.at(failedIndex).to);
            }
            invalidateAllFolders();
            Q_EMIT undoChanged();
            rescanNow();
            return;
        }

        for (int i = m_undo.size() - 1; i >= 0; --i) {
            if (m_undo.at(i).value(QStringLiteral("uid")).toString() == uid) {
                const QJsonObject done = m_undo.takeAt(i);
                applyState(done.value(QStringLiteral("state")).toObject(), false);
                bumpFences();
                break;
            }
        }
        if (!sourceLeft.isEmpty()) {
            Q_EMIT message(QStringLiteral("file-source-left"), sourceLeft.join(QStringLiteral(", ")));
        }
        invalidateAllFolders(); // file operations may have changed any open folder portal
        Q_EMIT undoChanged();
        rescanNow();
        scheduleSave();
    };

    if (!slow) {
        // renames on one disk and trashing: instant, do it right away
        TaskResults results;
        for (const FileTask &task : std::as_const(tasks)) {
            results << runTask(task, nullptr);
            if (results.last() != Alveus::FileOps::Result::Done
                && results.last() != Alveus::FileOps::Result::SourceLeft) {
                break;
            }
        }
        finish(results);
        return results.isEmpty() || results.last() == Alveus::FileOps::Result::Done
            || results.last() == Alveus::FileOps::Result::SourceLeft;
    }
    m_undoRunning = true;
    Q_EMIT undoChanged();
    queueFileJob(FileJob{QStringLiteral("undo"), tasks, finish});
    return true;
}

// =============================================================================
// desktop folder

void AlveusCore::scheduleRescan()
{
    if (!m_scanTimer.isActive()) {
        m_scanTimer.start();
    }
}

void AlveusCore::rescanNow()
{
    m_scanTimer.stop();
    rescan();
}

void AlveusCore::rescan()
{
    QDir dir(m_desktopPath);
    if (!dir.exists()) {
        return; // never forget placements because the folder is briefly missing
    }
    if (!m_watcher.directories().contains(m_desktopPath)) {
        m_watcher.addPath(m_desktopPath);
    }

    const QStringList names = dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System);
    QList<Entry> files;
    files.reserve(names.size());
    for (const QString &name : names) {
        files << makeEntry(m_desktopPath, name);
    }
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(files.begin(), files.end(), [&collator](const Entry &a, const Entry &b) {
        if (a.isDir != b.isDir) {
            return a.isDir;
        }
        return collator.compare(a.label, b.label) < 0;
    });

    const QSet<QString> present(names.cbegin(), names.cend());
    bool dropped = false;
    for (auto it = m_place.begin(); it != m_place.end();) {
        // keep the place of files still being copied here
        if (!present.contains(it.key()) && !isReserved(m_desktopPath + QLatin1Char('/') + it.key())) {
            it = m_place.erase(it);
            dropped = true;
        } else {
            ++it;
        }
    }
    m_files = files;
    m_changedFiles.clear(); // a full rescan has read them all

    // Watch individual desktop files as well as the desktop directory. This
    // catches size/mtime changes that do not alter the directory entries.
    QSet<QString> wantedFileWatches;
    for (const Entry &entry : std::as_const(m_files)) {
        if (!QFileInfo(entry.path).isDir()) {
            wantedFileWatches.insert(entry.path);
        }
    }
    const QStringList watchedFiles = m_watcher.files();
    QSet<QString> currentWatches;
    for (const QString &watched : watchedFiles) {
        if (!wantedFileWatches.contains(watched)) {
            m_watcher.removePath(watched);
        } else {
            currentWatches.insert(watched);
        }
    }
    for (const QString &wanted : std::as_const(wantedFileWatches)) {
        if (!currentWatches.contains(wanted)) {
            m_watcher.addPath(wanted); // watcher limits are platform-dependent; periodic rescan is the fallback
        }
    }

    if (dropped) {
        scheduleSave();
    }
    bumpAllFileModels();
}

AlveusCore::Entry AlveusCore::makeEntry(const QString &dirPath, const QString &name) const
{
    static QMimeDatabase mimeDb;

    Entry e;
    e.name = name;
    e.path = dirPath + QLatin1Char('/') + name;
    e.label = name;

    const QFileInfo info(e.path);
    e.isDir = info.isDir();
    e.size = info.size();
    e.modified = info.lastModified();
    e.accessed = info.lastRead();
    e.origin = Alveus::readOrigin(e.path);
    if (e.isDir) {
        e.icon = QStringLiteral("folder");
        e.fallbackIcon = QStringLiteral("folder");
        e.mime = QStringLiteral("inode/directory");
        return e;
    }

    const QMimeType mime = mimeDb.mimeTypeForFile(info);
    e.mime = mime.name();
    e.icon = mime.iconName();
    e.fallbackIcon = mime.genericIconName();
    if (e.mime.startsWith(QLatin1String("image/")) && info.size() <= kMaxPreviewBytes) {
        e.image = QUrl::fromLocalFile(e.path).toString();
    }

    if (name.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive)) {
        QFile f(e.path);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString lang = QLocale::system().name();
            const QString shortLang = lang.section(QLatin1Char('_'), 0, 0);
            QString nameDefault, nameLang, nameShort, icon;
            bool inEntry = false;
            QTextStream in(&f);
            while (!in.atEnd()) {
                const QString line = in.readLine().trimmed();
                if (line.startsWith(QLatin1Char('['))) {
                    inEntry = (line == QLatin1String("[Desktop Entry]"));
                    continue;
                }
                const int eq = line.indexOf(QLatin1Char('='));
                if (!inEntry || eq <= 0) {
                    continue;
                }
                const QString k = line.left(eq).trimmed();
                const QString v = line.mid(eq + 1).trimmed();
                if (k == QLatin1String("Name")) {
                    nameDefault = v;
                } else if (k == QStringLiteral("Name[%1]").arg(lang)) {
                    nameLang = v;
                } else if (k == QStringLiteral("Name[%1]").arg(shortLang)) {
                    nameShort = v;
                } else if (k == QLatin1String("Icon")) {
                    icon = v;
                }
            }
            const QString label = !nameLang.isEmpty() ? nameLang : !nameShort.isEmpty() ? nameShort : nameDefault;
            if (!label.isEmpty()) {
                e.label = label;
            }
            if (!icon.isEmpty()) {
                if (icon.startsWith(QLatin1Char('/'))) {
                    e.image = QUrl::fromLocalFile(icon).toString();
                } else {
                    e.icon = icon;
                }
                e.fallbackIcon = QStringLiteral("application-x-executable");
            }
        }
    }
    return e;
}

QVariantMap AlveusCore::toMap(const Entry &e) const
{
    QVariantMap m;
    m.insert(QStringLiteral("name"), e.name);
    m.insert(QStringLiteral("path"), e.path);
    m.insert(QStringLiteral("label"), e.label);
    m.insert(QStringLiteral("url"), QUrl::fromLocalFile(e.path).toString());
    m.insert(QStringLiteral("icon"), e.icon);
    m.insert(QStringLiteral("fallbackIcon"), e.fallbackIcon);
    m.insert(QStringLiteral("image"), e.image);
    m.insert(QStringLiteral("mime"), e.mime);
    m.insert(QStringLiteral("isDir"), e.isDir);
    m.insert(QStringLiteral("size"), double(e.size));
    m.insert(QStringLiteral("modified"), e.modified);
    // Any file may already have a thumbnail in the cache Dolphin shares with
    // us (documents, text, archives…); videos and PDFs we can also make.
    // Pictures are shown directly.
    m.insert(QStringLiteral("canThumb"), !e.isDir && e.image.isEmpty());
    return m;
}

// =============================================================================
// desktops, screens and pages

QString AlveusCore::key(int screen, const QString &activity) const
{
    return activity + QLatin1Char('|') + QString::number(screen);
}

void AlveusCore::registerDesktop(int screen, const QString &activity)
{
    if (screen < 0) {
        return;
    }
    ++m_desktops[key(screen, activity)];
    bumpDesktopTopology();
}

void AlveusCore::unregisterDesktop(int screen, const QString &activity)
{
    const QString k = key(screen, activity);
    if (!m_desktops.contains(k)) {
        return;
    }
    if (--m_desktops[k] <= 0) {
        m_desktops.remove(k);
    }
    bumpDesktopTopology();
}

QVariantList AlveusCore::screens(const QString &activity) const
{
    QList<int> list;
    const QString prefix = activity + QLatin1Char('|');
    for (auto it = m_desktops.cbegin(); it != m_desktops.cend(); ++it) {
        if (it.key().startsWith(prefix)) {
            list << it.key().mid(prefix.size()).toInt();
        }
    }
    std::sort(list.begin(), list.end());
    QVariantList out;
    for (int s : list) {
        out << s;
    }
    return out;
}

int AlveusCore::primaryScreen(const QString &activity) const
{
    const QVariantList s = screens(activity);
    return s.isEmpty() ? 0 : s.first().toInt();
}

int AlveusCore::hostScreen(int screen, const QString &activity) const
{
    const QVariantList s = screens(activity);
    if (s.isEmpty() || s.contains(screen)) {
        return screen;
    }
    return s.first().toInt();
}

int AlveusCore::pageCount(int screen, const QString &activity) const
{
    int count = m_pages.value(key(screen, activity), 1);
    for (const Fence &f : m_fences) {
        if (fenceVisibleIn(f, activity) && hostScreen(f.screen, activity) == screen) {
            count = std::max(count, f.page + 1);
        }
    }
    for (const Placement &p : m_place) {
        if (p.hasPos && hostScreen(p.screen, activity) == screen) {
            count = std::max(count, p.page + 1);
        }
    }
    return std::min(count, 9);
}

void AlveusCore::setPageCount(int screen, const QString &activity, int count)
{
    m_pages.insert(key(screen, activity), std::clamp(count, 1, 9));
    scheduleSave();
    bump();
}

// =============================================================================
// fences

int AlveusCore::indexOfFence(const QString &id) const
{
    for (int i = 0; i < m_fences.size(); ++i) {
        if (m_fences.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

const AlveusCore::Fence *AlveusCore::findFence(const QString &id) const
{
    const int i = id.isEmpty() ? -1 : indexOfFence(id);
    return i < 0 ? nullptr : &m_fences.at(i);
}

bool AlveusCore::fenceVisibleIn(const Fence &f, const QString &activity) const
{
    return f.activity.isEmpty() || activity.isEmpty() || f.activity == activity;
}

void AlveusCore::fenceNormalizeGeometry(Fence &f)
{
    f.w = std::clamp(f.w, 0.03, 1.0);
    f.h = std::clamp(f.h, 0.03, 1.0);
    f.x = std::clamp(f.x, 0.0, std::max(0.0, 1.0 - f.w));
    f.y = std::clamp(f.y, 0.0, std::max(0.0, 1.0 - f.h));
    f.ow = std::clamp(f.ow, 0.03, 1.0);
    f.oh = std::clamp(f.oh, 0.03, 1.0);
    f.ox = std::clamp(f.ox, 0.0, std::max(0.0, 1.0 - f.ow));
    f.oy = std::clamp(f.oy, 0.0, std::max(0.0, 1.0 - f.oh));
}

QVariantMap AlveusCore::toMap(const Fence &f, const QString &activity) const
{
    const int host = hostScreen(f.screen, activity);
    const bool orphan = host != f.screen;
    const bool useOrphan = orphan && f.hasOrphanGeometry;

    QVariantMap m;
    m.insert(QStringLiteral("id"), f.id);
    m.insert(QStringLiteral("title"), f.title);
    m.insert(QStringLiteral("screen"), f.screen);
    m.insert(QStringLiteral("hostScreen"), host);
    m.insert(QStringLiteral("orphan"), orphan);
    m.insert(QStringLiteral("activity"), f.activity);
    m.insert(QStringLiteral("page"), f.page);
    m.insert(QStringLiteral("x"), useOrphan ? f.ox : f.x);
    m.insert(QStringLiteral("y"), useOrphan ? f.oy : f.y);
    m.insert(QStringLiteral("w"), useOrphan ? f.ow : f.w);
    m.insert(QStringLiteral("h"), useOrphan ? f.oh : f.h);
    m.insert(QStringLiteral("collapsed"), f.collapsed);
    m.insert(QStringLiteral("rules"), f.rules);
    m.insert(QStringLiteral("timeRule"), f.parsed.hasTimeCondition());
    m.insert(QStringLiteral("portal"), f.portal);
    m.insert(QStringLiteral("group"), f.group);
    m.insert(QStringLiteral("activeTab"), f.activeTab);
    m.insert(QStringLiteral("color"), f.color);
    m.insert(QStringLiteral("opacity"), f.opacity);
    m.insert(QStringLiteral("blur"), f.blur);
    m.insert(QStringLiteral("view"), f.view);
    m.insert(QStringLiteral("iconSize"), f.iconSize);
    m.insert(QStringLiteral("driveOptions"), f.driveOptions.toVariantMap());

    QVariantList tabs;
    if (!f.group.isEmpty()) {
        for (const Fence &t : m_fences) {
            if (t.group == f.group) {
                tabs << QVariantMap{{QStringLiteral("id"), t.id},
                                    {QStringLiteral("title"), t.title},
                                    {QStringLiteral("active"), t.activeTab},
                                    {QStringLiteral("portal"), t.portal}};
            }
        }
    }
    m.insert(QStringLiteral("tabs"), tabs);
    return m;
}

QVariantList AlveusCore::fences(int screen, const QString &activity, int page) const
{
    QVariantList out;
    for (const Fence &f : m_fences) {
        if (fenceVisibleIn(f, activity) && f.page == page && hostScreen(f.screen, activity) == screen) {
            out << toMap(f, activity);
        }
    }
    return out;
}

QVariantMap AlveusCore::fence(const QString &id) const
{
    const Fence *f = findFence(id);
    return f ? toMap(*f, f->activity) : QVariantMap();
}

QString AlveusCore::createFence(int screen, const QString &activity, int page, qreal x, qreal y, qreal w, qreal h,
                                const QString &title, const QString &rules)
{
    pushUndo(QStringLiteral("create-fence|") + title);
    Fence f;
    f.id = newId();
    f.title = title;
    f.screen = screen;
    f.activity = activity;
    f.page = std::max(0, page);
    f.x = x;
    f.y = y;
    f.w = w;
    f.h = h;
    f.rules = Alveus::Rules::normalize(rules);
    f.parsed = Alveus::Rules(f.rules);
    f.created = QDateTime::currentMSecsSinceEpoch();
    fenceNormalizeGeometry(f);
    m_fences << f;
    scheduleSave();
    bumpFences(true);
    return f.id;
}

QString AlveusCore::createPortal(int screen, const QString &activity, int page, qreal x, qreal y, qreal w, qreal h,
                                 const QString &title, const QString &path)
{
    const QString id = createFence(screen, activity, page, x, y, w, h, title, QString());
    m_fences[indexOfFence(id)].portal = path.startsWith(QLatin1String("drives:")) ? QStringLiteral("drives:") : QDir::cleanPath(path);
    watchFolder(path);
    scheduleSave();
    bumpFences(true);
    return id;
}

void AlveusCore::updateFence(const QString &id, const QVariantMap &patch)
{
    const int i = indexOfFence(id);
    if (i < 0) {
        return;
    }

    static const QSet<QString> undoablePatchKeys = {
        QStringLiteral("rules"), QStringLiteral("title"), QStringLiteral("portal"), QStringLiteral("screen"),
        QStringLiteral("page"), QStringLiteral("activity"), QStringLiteral("x"), QStringLiteral("y"),
        QStringLiteral("w"), QStringLiteral("h"), QStringLiteral("collapsed"), QStringLiteral("color"),
        QStringLiteral("opacity"), QStringLiteral("blur"), QStringLiteral("view"), QStringLiteral("iconSize"),
        QStringLiteral("driveOptions")
    };
    bool needsUndo = false;
    for (auto it = patch.cbegin(); it != patch.cend(); ++it) {
        if (undoablePatchKeys.contains(it.key())) {
            needsUndo = true;
            break;
        }
    }
    if (needsUndo) {
        pushUndo(QStringLiteral("edit-fence|") + m_fences[i].title,
                 QJsonObject{{QStringLiteral("coalesceKey"), id}});
    }

    // Tabs share their frame: these settings apply to the whole group.
    static const QStringList shared = {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("w"),
                                       QStringLiteral("h"), QStringLiteral("collapsed"), QStringLiteral("color"),
                                       QStringLiteral("opacity"), QStringLiteral("blur"), QStringLiteral("screen"),
                                       QStringLiteral("page"), QStringLiteral("activity")};
    const QString group = m_fences[i].group;
    const bool orphanEdit = patch.value(QStringLiteral("orphan")).toBool();
    const bool fileMembershipChanged = patch.contains(QStringLiteral("rules"))
        || patch.contains(QStringLiteral("portal")) || patch.contains(QStringLiteral("activity"));

    for (Fence &f : m_fences) {
        const bool self = f.id == id;
        const bool sibling = !group.isEmpty() && f.group == group && !self;
        if (!self && !sibling) {
            continue;
        }
        for (auto it = patch.cbegin(); it != patch.cend(); ++it) {
            const QString &k = it.key();
            if (sibling && !shared.contains(k)) {
                continue;
            }
            const QVariant &v = it.value();
            if (k == QLatin1String("title")) {
                f.title = v.toString();
            } else if (k == QLatin1String("rules")) {
                f.rules = Alveus::Rules::normalize(v.toString());
                f.parsed = Alveus::Rules(f.rules);
            } else if (k == QLatin1String("portal")) {
                const QString p = v.toString();
                f.portal = p.isEmpty() ? QString() : p.startsWith(QLatin1String("drives:")) ? QStringLiteral("drives:") : QDir::cleanPath(p);
            } else if (k == QLatin1String("x")) {
                (orphanEdit ? f.ox : f.x) = v.toReal();
            } else if (k == QLatin1String("y")) {
                (orphanEdit ? f.oy : f.y) = v.toReal();
            } else if (k == QLatin1String("w")) {
                (orphanEdit ? f.ow : f.w) = v.toReal();
            } else if (k == QLatin1String("h")) {
                (orphanEdit ? f.oh : f.h) = v.toReal();
            } else if (k == QLatin1String("collapsed")) {
                f.collapsed = v.toBool();
            } else if (k == QLatin1String("color")) {
                f.color = v.toString();
            } else if (k == QLatin1String("opacity")) {
                f.opacity = std::clamp(v.toReal(), 0.0, 1.0);
            } else if (k == QLatin1String("blur")) {
                f.blur = v.toBool();
            } else if (k == QLatin1String("view")) {
                f.view = v.toString();
            } else if (k == QLatin1String("iconSize")) {
                f.iconSize = std::clamp(v.toInt(), 0, 256);
            } else if (k == QLatin1String("driveOptions")) {
                f.driveOptions = QJsonObject::fromVariantMap(v.toMap());
            } else if (k == QLatin1String("screen")) {
                f.screen = v.toInt();
                f.hasOrphanGeometry = false;
            } else if (k == QLatin1String("page")) {
                f.page = std::clamp(v.toInt(), 0, 8);
            } else if (k == QLatin1String("activity")) {
                f.activity = v.toString();
            }
        }
        if (orphanEdit && !f.hasOrphanGeometry
            && (patch.contains(QStringLiteral("x")) || patch.contains(QStringLiteral("w")))) {
            // first move while the monitor is away: start from the current spot
            f.hasOrphanGeometry = true;
            if (!patch.contains(QStringLiteral("w"))) {
                f.ow = f.w;
                f.oh = f.h;
            }
        }
        fenceNormalizeGeometry(f);
    }
    if (patch.contains(QStringLiteral("portal"))) {
        watchFolder(m_fences[i].portal);
    }
    scheduleSave();
    bumpFences(fileMembershipChanged);
}

void AlveusCore::removeFence(const QString &id)
{
    const int i = indexOfFence(id);
    if (i < 0) {
        return;
    }
    pushUndo(QStringLiteral("remove-fence|") + m_fences[i].title);
    const Fence removed = m_fences.takeAt(i);

    // Its files go back to the desktop (or to another fence by the rules)
    for (auto it = m_place.begin(); it != m_place.end(); ++it) {
        if (it->manual && it->fence == id) {
            it->manual = false;
            it->fence.clear();
        }
    }
    // A tab group of one is just a fence again
    if (!removed.group.isEmpty()) {
        QList<int> members;
        for (int j = 0; j < m_fences.size(); ++j) {
            if (m_fences[j].group == removed.group) {
                members << j;
            }
        }
        if (members.size() == 1) {
            m_fences[members.first()].group.clear();
            m_fences[members.first()].activeTab = true;
        } else if (!members.isEmpty() && removed.activeTab) {
            m_fences[members.first()].activeTab = true;
        }
    }
    scheduleSave();
    bumpFences(true);
}

void AlveusCore::mergeFences(const QString &targetId, const QString &sourceId)
{
    const int t = indexOfFence(targetId);
    const int s = indexOfFence(sourceId);
    if (t < 0 || s < 0 || t == s) {
        return;
    }
    pushUndo(QStringLiteral("merge|") + m_fences[s].title);
    if (m_fences[t].group.isEmpty()) {
        m_fences[t].group = newId();
        m_fences[t].activeTab = true;
    }
    const Fence target = m_fences[t];
    // every member of the source's old group comes along
    const QString oldGroup = m_fences[s].group;
    for (Fence &f : m_fences) {
        if (f.id == sourceId || (!oldGroup.isEmpty() && f.group == oldGroup && f.id != targetId)) {
            f.group = target.group;
            f.activeTab = false;
            f.x = target.x;
            f.y = target.y;
            f.w = target.w;
            f.h = target.h;
            f.screen = target.screen;
            f.page = target.page;
            f.activity = target.activity;
            f.collapsed = target.collapsed;
            f.color = target.color;
            f.opacity = target.opacity;
            f.blur = target.blur;
            f.hasOrphanGeometry = false;
        }
    }
    activateTabInternal(sourceId, true);
}

void AlveusCore::detachTab(const QString &id)
{
    const int i = indexOfFence(id);
    if (i < 0 || m_fences[i].group.isEmpty()) {
        return;
    }
    pushUndo(QStringLiteral("detach|") + m_fences[i].title);
    const QString group = m_fences[i].group;
    const bool wasActive = m_fences[i].activeTab;
    Fence &f = m_fences[i];
    f.group.clear();
    f.activeTab = true;
    f.x += 0.03;
    f.y += 0.04;
    fenceNormalizeGeometry(f);

    QList<int> members;
    for (int j = 0; j < m_fences.size(); ++j) {
        if (m_fences[j].group == group) {
            members << j;
        }
    }
    if (members.size() == 1) {
        m_fences[members.first()].group.clear();
        m_fences[members.first()].activeTab = true;
    } else if (!members.isEmpty() && wasActive) {
        m_fences[members.first()].activeTab = true;
    }
    scheduleSave();
    bumpFences();
}

void AlveusCore::activateTab(const QString &id)
{
    activateTabInternal(id, false);
}

void AlveusCore::activateTabInternal(const QString &id, bool fileMembershipChanged)
{
    const int i = indexOfFence(id);
    if (i < 0) {
        return;
    }
    const QString group = m_fences[i].group;
    for (Fence &f : m_fences) {
        if (!group.isEmpty() && f.group == group) {
            f.activeTab = (f.id == id);
        }
    }
    if (m_fences[i].group.isEmpty()) {
        m_fences[i].activeTab = true;
    }
    scheduleSave();
    bumpFences(fileMembershipChanged);
}

QString AlveusCore::presetRules(const QString &preset) const
{
    static const QHash<QString, QString> presets = {
        {QStringLiteral("documents"), QStringLiteral("pdf, doc, docx, odt, rtf, txt, md, xls, xlsx, ods, csv, ppt, pptx, odp, djvu, epub")},
        {QStringLiteral("images"), QStringLiteral("jpg, jpeg, png, gif, bmp, webp, svg, tif, tiff, heic, avif, dcm")},
        {QStringLiteral("media"), QStringLiteral("mp4, mkv, avi, mov, webm, mp3, ogg, flac, wav, m4a, opus")},
        {QStringLiteral("archives"), QStringLiteral("zip, rar, 7z, tar, gz, xz, bz2, zst, iso")},
        {QStringLiteral("apps"), QStringLiteral("desktop, appimage, sh, deb, rpm, flatpakref")},
        {QStringLiteral("folders"), QStringLiteral("folder")},
        {QStringLiteral("today"), QStringLiteral("newer:1")},
        {QStringLiteral("week"), QStringLiteral("newer:7")},
        {QStringLiteral("old"), QStringLiteral("older:60")},
        {QStringLiteral("big"), QStringLiteral("larger:100M")},
        {QStringLiteral("screenshots"), QStringLiteral("name:Screenshot_*, \"name:Знімок екрана*\", name:Spectacle*")},
        {QStringLiteral("telegram"), QStringLiteral("from:telegram, from:t.me")},
    };
    return presets.value(preset);
}

QStringList AlveusCore::ruleErrors(const QString &rules) const
{
    return Alveus::Rules(rules).errors();
}

void AlveusCore::dismissWelcome()
{
    m_welcomeDismissed = true;
    scheduleSave();
    bump();
}

void AlveusCore::requestFence(const QString &id)
{
    setHidden(false);
    Q_EMIT fenceRequested(id);
}

// =============================================================================
// which fence shows a file

QString AlveusCore::ruleFenceFor(const Entry &e, const QString &activity) const
{
    Alveus::FileFacts facts;
    facts.name = e.name;
    facts.isDir = e.isDir;
    facts.size = e.size;
    facts.modified = e.modified;
    facts.origin = e.origin;
    const QDateTime now = QDateTime::currentDateTime();

    // fences with time rules first ("Today", "Old"), then the rest, oldest first
    for (int pass = 0; pass < 2; ++pass) {
        for (const Fence &f : m_fences) {
            if (!f.portal.isEmpty() || !fenceVisibleIn(f, activity)) {
                continue;
            }
            if (f.parsed.hasTimeCondition() != (pass == 0)) {
                continue;
            }
            if (f.parsed.matches(facts, now)) {
                return f.id;
            }
        }
    }
    return QString();
}

QString AlveusCore::placementFor(const Entry &e, const QString &activity) const
{
    const auto it = m_place.constFind(e.name);
    if (it != m_place.cend() && it->manual) {
        if (it->fence.isEmpty()) {
            return QString(); // put on the desktop by hand
        }
        const Fence *f = findFence(it->fence);
        if (f && f->portal.isEmpty() && fenceVisibleIn(*f, activity)) {
            return f->id;
        }
    }
    return ruleFenceFor(e, activity);
}

QString AlveusCore::fenceOfFile(const QString &name, const QString &activity) const
{
    return fileModelCacheFor(activity).fenceByName.value(name);
}

QVariantList AlveusCore::filesInFence(const QString &fenceId, const QString &activity) const
{
    if (const Fence *f = findFence(fenceId); f && !f->portal.isEmpty()) {
        return listFolder(f->portal);
    }
    return fileModelCacheFor(activity).byFence.value(fenceId);
}

QVariantList AlveusCore::looseFiles(int screen, const QString &activity) const
{
    const int primary = primaryScreen(activity);
    const QVariantList loose = fileModelCacheFor(activity).byFence.value(QString());
    QVariantList out;
    out.reserve(loose.size());
    for (const QVariant &entry : loose) {
        QVariantMap m = entry.toMap();
        const QString name = m.value(QStringLiteral("name")).toString();
        const Placement p = m_place.value(name);
        const int shownOn = p.hasPos ? hostScreen(p.screen, activity) : primary;
        if (shownOn != screen) {
            continue;
        }
        // a position saved for an unplugged monitor is not valid here
        const bool posValid = p.hasPos && hostScreen(p.screen, activity) == p.screen;
        m.insert(QStringLiteral("hasPos"), posValid);
        m.insert(QStringLiteral("page"), posValid ? p.page : 0);
        m.insert(QStringLiteral("col"), p.col);
        m.insert(QStringLiteral("row"), p.row);
        out << m;
    }
    return out;
}

void AlveusCore::assignFiles(const QStringList &names, const QString &fenceId)
{
    if (!fenceId.isEmpty() && !findFence(fenceId)) {
        return;
    }
    pushUndo(QStringLiteral("assign|") + QString::number(names.size()));
    for (const QString &name : names) {
        Placement &p = m_place[name];
        p.manual = true;
        p.fence = fenceId;
        if (!fenceId.isEmpty()) {
            p.hasPos = false;
        }
    }
    QSet<QString> changedNames;
    for (const QString &name : names) {
        changedNames.insert(name);
    }
    scheduleSave();
    bumpFileModels(changedNames);
}

void AlveusCore::setFilePositions(const QVariantList &items)
{
    if (items.isEmpty()) {
        return;
    }
    pushUndo(QStringLiteral("move-icons|") + QString::number(items.size()));
    for (const QVariant &v : items) {
        const QVariantMap m = v.toMap();
        Placement &p = m_place[m.value(QStringLiteral("name")).toString()];
        p.hasPos = true;
        p.screen = m.value(QStringLiteral("screen")).toInt();
        p.page = m.value(QStringLiteral("page")).toInt();
        p.col = std::max(0, m.value(QStringLiteral("col")).toInt());
        p.row = std::max(0, m.value(QStringLiteral("row")).toInt());
    }
    scheduleSave();
    bumpDesktopPositions();
}

void AlveusCore::clearPositions(int screen, int page)
{
    pushUndo(QStringLiteral("arrange|"));
    for (auto it = m_place.begin(); it != m_place.end();) {
        if (it->hasPos && it->screen == screen && it->page == page) {
            it->hasPos = false;
            if (!it->manual) {
                it = m_place.erase(it);
                continue;
            }
        }
        ++it;
    }
    scheduleSave();
    bumpDesktopPositions();
}

bool AlveusCore::isDesktopUrl(const QUrl &url) const
{
    if (!url.isLocalFile()) {
        return false;
    }
    const QFileInfo info(url.toLocalFile());
    return QDir(info.absolutePath()).canonicalPath() == QDir(m_desktopPath).canonicalPath();
}

QString AlveusCore::fileNameFromUrl(const QUrl &url) const
{
    return QFileInfo(url.toLocalFile()).fileName();
}

QStringList AlveusCore::toUrls(const QStringList &paths) const
{
    QStringList out;
    for (const QString &p : paths) {
        out << QString::fromUtf8(QUrl::fromLocalFile(p).toEncoded());
    }
    return out;
}

// =============================================================================
// folders (portals)

void AlveusCore::watchFolder(const QString &path)
{
    if (path.isEmpty() || !QFileInfo(path).isDir()) {
        return;
    }
    const QString clean = QDir::cleanPath(path);
    if (!m_watcher.directories().contains(clean)) {
        m_watcher.addPath(clean);
    }
}

QVariantList AlveusCore::listFolder(const QString &path) const
{
    const QString clean = QDir::cleanPath(path);
    auto self = const_cast<AlveusCore *>(this);
    if (!m_folders.contains(clean)) {
        QDir dir(clean);
        QList<Entry> list;
        if (dir.exists()) {
            for (const QString &name : dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System)) {
                list << makeEntry(clean, name);
            }
            QCollator collator;
            collator.setNumericMode(true);
            collator.setCaseSensitivity(Qt::CaseInsensitive);
            std::sort(list.begin(), list.end(), [&collator](const Entry &a, const Entry &b) {
                if (a.isDir != b.isDir) {
                    return a.isDir;
                }
                return collator.compare(a.label, b.label) < 0;
            });
            self->watchFolder(clean);
        }
        self->m_folders.insert(clean, list);
    }
    QVariantList out;
    for (const Entry &e : m_folders.value(clean)) {
        out << toMap(e);
    }
    return out;
}

QString AlveusCore::parentFolder(const QString &path) const
{
    QDir d(path);
    return d.cdUp() ? d.absolutePath() : path;
}

QString AlveusCore::folderName(const QString &path) const
{
    const QString clean = QDir::cleanPath(path);
    if (clean == QDir::homePath()) {
        return QStringLiteral("~");
    }
    return QFileInfo(clean).fileName();
}

bool AlveusCore::folderExists(const QString &path) const
{
    return QFileInfo(path).isDir();
}

// =============================================================================
// file operations

QString AlveusCore::uniqueName(const QString &dirPath, const QString &name) const
{
    QDir dir(dirPath);
    const auto pathExists = [this](const QString &path) {
        const QFileInfo info(path);
        return info.exists() || info.isSymLink() || isReserved(path);
    };
    if (!pathExists(dir.filePath(name))) {
        return name;
    }
    const QFileInfo info(name);
    const QString base = info.completeBaseName().isEmpty() ? name : info.completeBaseName();
    const QString suffix = info.suffix().isEmpty() || info.completeBaseName().isEmpty() ? QString()
                                                                                        : QLatin1Char('.') + info.suffix();
    for (int n = 1; n < 10000; ++n) {
        const QString candidate = QStringLiteral("%1 (%2)%3").arg(base).arg(n).arg(suffix);
        if (!pathExists(dir.filePath(candidate))) {
            return candidate;
        }
    }
    return newId() + suffix;
}


QStringList AlveusCore::importInto(const QList<QUrl> &urls, bool copy, const QString &dirPath, QJsonArray *ops,
                                   QList<FileTask> *slow)
{
    QStringList created;
    QStringList failed;
    const QString target = QDir(dirPath).canonicalPath();
    for (const QUrl &url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QString src = QDir::cleanPath(url.toLocalFile());
        const QFileInfo info(src);
        if (!info.exists() && !info.isSymLink()) {
            continue;
        }
        if (QDir(info.absolutePath()).canonicalPath() == target) {
            created << src; // already there
            continue;
        }
        // never drop a folder into itself
        if (info.isDir() && (target + QLatin1Char('/')).startsWith(info.canonicalFilePath() + QLatin1Char('/'))) {
            continue;
        }
        const QString dst = dirPath + QLatin1Char('/') + uniqueName(dirPath, info.fileName());
        if (!copy && Alveus::FileOps::sameFilesystem(src, dirPath)) {
            if (info.isDir()) {
                // The rename is instant, but movePath recursively validates the
                // directory tree. Run that validation and move in the worker.
                slow->append(FileTask{FileTask::Move, src, dst});
                m_reserved.insert(QDir::cleanPath(dst));
                created << dst;
            } else if (Alveus::FileOps::movePath(src, dst) == Alveus::FileOps::Result::Done) {
                created << dst;
                ops->append(fileOp(QStringLiteral("move"), dst, src));
            } else {
                failed << info.fileName();
            }
        } else {
            // copies and moves to another disk run in the background; the
            // destination name is reserved so nothing else takes it meanwhile
            slow->append(FileTask{copy ? FileTask::Copy : FileTask::Move, src, dst});
            m_reserved.insert(QDir::cleanPath(dst));
            created << dst;
        }
    }
    reportFailures(copy ? QStringLiteral("file-copy-failed") : QStringLiteral("file-move-failed"), failed);
    return created;
}

void AlveusCore::reportFailures(const QString &kind, const QStringList &names)
{
    if (names.isEmpty()) {
        return;
    }
    // one message for the whole drop, not one per file
    QStringList shown = names.mid(0, 3);
    if (names.size() > 3) {
        shown << QStringLiteral("… (+%1)").arg(names.size() - 3);
    }
    Q_EMIT message(kind, shown.join(QStringLiteral(", ")));
}

Alveus::FileOps::Result AlveusCore::runTask(const FileTask &task, Alveus::FileOps::Control *control)
{
    using Alveus::FileOps::Result;
    switch (task.op) {
    case FileTask::Copy:
        return Alveus::FileOps::copyTree(task.from, task.to, control);
    case FileTask::Move:
        return Alveus::FileOps::movePath(task.from, task.to, control);
    case FileTask::Trash:
        return QFile::moveToTrash(task.from) ? Result::Done : Result::Failed;
    case FileTask::Restore: {
        const Result r = Alveus::FileOps::movePath(task.from, task.to, control);
        // Keep .trashinfo while the item is still physically in the trash.
        // Removing it on SourceLeft would orphan the remaining copy.
        if (r == Result::Done && !QFileInfo::exists(task.from) && !QFileInfo(task.from).isSymLink()) {
            const QFileInfo inTrash(task.from);
            const QString infoPath = QDir::cleanPath(inTrash.absolutePath() + QStringLiteral("/../info/")
                                                     + inTrash.fileName() + QStringLiteral(".trashinfo"));
            QFile::remove(infoPath);
        }
        return r;
    }
    }
    return Result::Failed;
}

// A copy/move job from a drop, paste or tidy-up. `doneOps` are the instant
// renames already made for the same action: they share one undo step.
void AlveusCore::runFileTasks(const QString &kind, const QList<FileTask> &tasks, const QJsonObject &before,
                              const QJsonArray &doneOps)
{
    using Alveus::FileOps::Result;
    auto finish = [this, kind, tasks, before, doneOps](const TaskResults &results) {
        QJsonArray ops = doneOps;
        QStringList failed, sourceLeft;
        int done = 0;
        for (int i = 0; i < tasks.size(); ++i) {
            const FileTask &t = tasks.at(i);
            const Result r = i < results.size() ? results.at(i) : Result::Cancelled;
            m_reserved.remove(QDir::cleanPath(t.to));
            if (r == Result::Done) {
                ++done;
                ops.append(t.op == FileTask::Copy ? fileOp(QStringLiteral("created"), t.to)
                                                  : fileOp(QStringLiteral("move"), t.to, t.from));
            } else if (r == Result::SourceLeft) {
                ++done; // the copy is complete; only the original could not be removed
                ops.append(fileOp(QStringLiteral("created"), t.to));
                sourceLeft << QFileInfo(t.from).fileName();
            } else {
                if (r == Result::Failed) {
                    failed << QFileInfo(t.from).fileName();
                }
                if (QDir::cleanPath(QFileInfo(t.to).absolutePath()) == m_desktopPath) {
                    m_place.remove(QFileInfo(t.to).fileName()); // nothing arrived
                }
            }
        }
        if (!ops.isEmpty()) {
            pushUndo(kind + QLatin1Char('|') + QString::number(ops.size()),
                     QJsonObject{{QStringLiteral("state"), before}, {QStringLiteral("files"), ops}});
        }
        reportFailures(kind == QLatin1String("copy") ? QStringLiteral("file-copy-failed") : QStringLiteral("file-move-failed"),
                       failed);
        reportFailures(QStringLiteral("file-source-left"), sourceLeft);
        // a long job deserves a "done" message (with Undo); a quick one does not
        if (done > 0 && QDateTime::currentMSecsSinceEpoch() - m_jobStarted > 1500) {
            Q_EMIT message(QStringLiteral("files-done"), kind + QLatin1Char('|') + QString::number(done));
        }
        invalidateAllFolders();
        scheduleSave();
        rescanNow();
    };
    queueFileJob(FileJob{kind, tasks, finish});
}

void AlveusCore::queueFileJob(FileJob job)
{
    m_jobs << std::move(job);
    if (!m_jobThread) {
        startNextFileJob();
    } else {
        updateFileJob();
    }
}

void AlveusCore::startNextFileJob()
{
    if (m_jobs.isEmpty()) {
        m_jobTimer.stop();
        m_fileJob = QVariantMap{{QStringLiteral("active"), false}};
        Q_EMIT fileJobChanged();
        return;
    }
    m_runningJob = m_jobs.takeFirst();
    m_jobControl = std::make_shared<Alveus::FileOps::Control>();
    m_jobResults = std::make_shared<TaskResults>();
    m_jobStarted = QDateTime::currentMSecsSinceEpoch();

    const QList<FileTask> tasks = m_runningJob.tasks;
    auto control = m_jobControl;
    auto results = m_jobResults;
    m_jobThread = QThread::create([tasks, control, results]() {
        QList<qint64> sizes;
        for (const FileTask &t : tasks) {
            const bool renameOnly = (t.op == FileTask::Move || t.op == FileTask::Restore)
                && Alveus::FileOps::sameFilesystem(t.from, QFileInfo(t.to).absolutePath());
            // Same-filesystem moves do not copy bytes. In particular, avoid a
            // redundant full tree-size scan before directory validation.
            sizes << (t.op == FileTask::Trash || renameOnly ? 0 : Alveus::FileOps::treeSize(t.from));
            control->bytesTotal += sizes.last();
        }
        for (int i = 0; i < tasks.size(); ++i) {
            control->index = i;
            control->bytesCurrent = 0;
            const auto r = control->cancel ? Alveus::FileOps::Result::Cancelled : runTask(tasks.at(i), control.get());
            *results << r; // read by the GUI thread only after the thread finished
            control->bytesDone += sizes.at(i);
            control->bytesCurrent = 0;
            if (r == Alveus::FileOps::Result::Failed && tasks.at(i).op != FileTask::Copy
                && tasks.at(i).op != FileTask::Move) {
                break; // undo stops at the first failed step
            }
        }
    });
    connect(m_jobThread, &QThread::finished, this, [this]() {
        m_jobThread->deleteLater();
        m_jobThread = nullptr;
        FileJob job = std::move(m_runningJob);
        m_runningJob = FileJob();
        const TaskResults results = *m_jobResults;
        if (job.finish) {
            job.finish(results);
        }
        startNextFileJob();
    });
    m_jobThread->start(QThread::LowPriority);
    m_jobTimer.start();
    updateFileJob();
}

void AlveusCore::updateFileJob()
{
    if (!m_jobThread || !m_jobControl) {
        return;
    }
    const auto &c = *m_jobControl;
    const int index = std::clamp(c.index.load(), 0, std::max(0, int(m_runningJob.tasks.size()) - 1));
    const qint64 total = c.bytesTotal.load();
    const qreal progress = total > 0 ? std::min<qreal>(1.0, qreal(c.bytesDone.load() + c.bytesCurrent.load()) / total) : -1;
    m_fileJob = QVariantMap{
        {QStringLiteral("active"), true},
        {QStringLiteral("kind"), m_runningJob.kind},
        {QStringLiteral("name"), m_runningJob.tasks.isEmpty() ? QString() : QFileInfo(m_runningJob.tasks.at(index).from).fileName()},
        {QStringLiteral("index"), index + 1},
        {QStringLiteral("count"), int(m_runningJob.tasks.size())},
        {QStringLiteral("progress"), progress},
        {QStringLiteral("bytesTotal"), total},
        {QStringLiteral("queued"), int(m_jobs.size())},
        {QStringLiteral("cancelling"), c.cancel.load()},
        {QStringLiteral("elapsed"), QDateTime::currentMSecsSinceEpoch() - m_jobStarted},
    };
    Q_EMIT fileJobChanged();
}

void AlveusCore::cancelFileJobs()
{
    // waiting jobs never started: just release what they reserved
    const QList<FileJob> waiting = std::exchange(m_jobs, {});
    for (const FileJob &job : waiting) {
        if (job.finish) {
            job.finish(TaskResults(job.tasks.size(), Alveus::FileOps::Result::Cancelled));
        }
    }
    if (m_jobControl && m_jobThread) {
        m_jobControl->cancel = true;
    }
    updateFileJob();
}

bool AlveusCore::waitForFileJobs(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (fileJobsBusy() && timer.elapsed() < timeoutMs) {
        QEventLoop loop;
        QTimer::singleShot(10, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return !fileJobsBusy();
}

void AlveusCore::refreshChangedFiles()
{
    // Re-read only the files that changed; a new or vanished file needs the
    // full rescan (sorting, placements).
    const QSet<QString> changed = std::exchange(m_changedFiles, {});
    bool any = false;
    for (const QString &name : changed) {
        auto it = std::find_if(m_files.begin(), m_files.end(), [&name](const Entry &e) { return e.name == name; });
        const QString path = m_desktopPath + QLatin1Char('/') + name;
        if (it == m_files.end() || !QFileInfo::exists(path)) {
            scheduleRescan();
            return;
        }
        *it = makeEntry(m_desktopPath, name);
        if (!m_watcher.files().contains(path)) {
            m_watcher.addPath(path); // the watch is dropped when a file is replaced
        }
        any = true;
    }
    if (any) {
        bumpFileModels(changed);
    }
}

QStringList AlveusCore::importToDesktop(const QList<QUrl> &urls, bool copy, const QString &fenceId)
{
    const QJsonObject before = stateJson();
    QJsonArray ops;
    QList<FileTask> slow;
    const QStringList paths = importInto(urls, copy, m_desktopPath, &ops, &slow);
    if (paths.isEmpty()) {
        return paths;
    }
    if (!slow.isEmpty()) {
        runFileTasks(copy ? QStringLiteral("copy") : QStringLiteral("move"), slow, before, ops);
    } else if (!ops.isEmpty()) {
        pushUndo((copy ? QStringLiteral("copy|") : QStringLiteral("move|")) + QString::number(ops.size()),
                 QJsonObject{{QStringLiteral("state"), before}, {QStringLiteral("files"), ops}});
    }
    for (const QString &p : paths) {
        Placement &pl = m_place[QFileInfo(p).fileName()];
        pl.manual = true;
        pl.fence = fenceId;
        if (!fenceId.isEmpty()) {
            pl.hasPos = false;
        }
    }
    scheduleSave();
    rescanNow();
    return paths;
}

QStringList AlveusCore::importToFolder(const QList<QUrl> &urls, bool copy, const QString &folder)
{
    QJsonArray ops;
    QList<FileTask> slow;
    const QJsonObject before = stateJson();
    const QStringList paths = importInto(urls, copy, QDir::cleanPath(folder), &ops, &slow);
    if (!slow.isEmpty()) {
        runFileTasks(copy ? QStringLiteral("copy") : QStringLiteral("move"), slow, before, ops);
    } else if (!ops.isEmpty()) {
        pushUndo((copy ? QStringLiteral("copy|") : QStringLiteral("move|")) + QString::number(ops.size()),
                 QJsonObject{{QStringLiteral("state"), before}, {QStringLiteral("files"), ops}});
    }
    invalidateFolder(folder);
    rescanNow();
    return paths;
}

void AlveusCore::openPath(const QString &path)
{
    if (!QFileInfo::exists(path)) {
        return;
    }
    // kioclient opens files in their default app and runs .desktop launchers
    // properly (asking first for untrusted ones).
    for (const QString &tool : {QStringLiteral("kioclient6"), QStringLiteral("kioclient")}) {
        if (!QStandardPaths::findExecutable(tool).isEmpty()) {
            QProcess::startDetached(tool, {QStringLiteral("exec"), path});
            return;
        }
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void AlveusCore::showInFolder(const QString &path)
{
    if (!QStandardPaths::findExecutable(QStringLiteral("dolphin")).isEmpty()) {
        QProcess::startDetached(QStringLiteral("dolphin"), {QStringLiteral("--select"), path});
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}

void AlveusCore::showProperties(const QString &path)
{
    for (const QString &tool : {QStringLiteral("kioclient6"), QStringLiteral("kioclient")}) {
        if (!QStandardPaths::findExecutable(tool).isEmpty()) {
            QProcess::startDetached(tool, {QStringLiteral("openProperties"), path});
            return;
        }
    }
    showInFolder(path);
}

void AlveusCore::openTerminal(const QString &folder)
{
    Integrations::openTerminal(folder.isEmpty() ? m_desktopPath : folder);
}

bool AlveusCore::renamePath(const QString &path, const QString &newName)
{
    const QString clean = newName.trimmed();
    const QFileInfo info(path);
    if (clean.isEmpty() || clean == info.fileName() || clean.contains(QLatin1Char('/'))
        || clean == QLatin1String(".") || clean == QLatin1String("..")) {
        return false;
    }
    const QString dst = info.absolutePath() + QLatin1Char('/') + clean;
    if (QFileInfo::exists(dst)) {
        return false;
    }
    const QJsonObject before = stateJson();
    if (!QFile::rename(path, dst)) {
        return false;
    }
    pushUndo(QStringLiteral("rename|") + info.fileName(),
             QJsonObject{{QStringLiteral("state"), before},
                         {QStringLiteral("files"), QJsonArray{fileOp(QStringLiteral("move"), dst, path)}}});
    if (QDir(info.absolutePath()).canonicalPath() == QDir(m_desktopPath).canonicalPath() && m_place.contains(info.fileName())) {
        m_place.insert(clean, m_place.take(info.fileName()));
    }
    invalidateFolder(info.absolutePath());
    scheduleSave();
    rescanNow();
    return true;
}

int AlveusCore::trashPaths(const QStringList &paths)
{
    // Qt falls back to odd places if the home trash doesn't exist yet
    const QString trash = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash");
    QDir().mkpath(trash + QStringLiteral("/files"));
    QDir().mkpath(trash + QStringLiteral("/info"));

    const QJsonObject before = stateJson();
    QJsonArray ops;
    for (const QString &p : paths) {
        QString inTrash;
        if (QFile::moveToTrash(p, &inTrash)) {
            ops.append(fileOp(QStringLiteral("trashed"), inTrash, p));
            invalidateFolder(QFileInfo(p).absolutePath());
        }
    }
    if (!ops.isEmpty()) {
        pushUndo(QStringLiteral("trash|") + QString::number(ops.size()),
                 QJsonObject{{QStringLiteral("state"), before}, {QStringLiteral("files"), ops}});
        scheduleSave();
        rescanNow();
    }
    return ops.size();
}

QString AlveusCore::createFolder(const QString &inFolder, const QString &baseName)
{
    const QString dir = inFolder.isEmpty() ? m_desktopPath : QDir::cleanPath(inFolder);
    const QJsonObject before = stateJson();
    const QString path = dir + QLatin1Char('/') + uniqueName(dir, baseName);
    if (!QDir().mkdir(path)) {
        return QString();
    }
    pushUndo(QStringLiteral("create|") + QFileInfo(path).fileName(),
             QJsonObject{{QStringLiteral("state"), before},
                         {QStringLiteral("files"), QJsonArray{fileOp(QStringLiteral("created"), path)}}});
    invalidateFolder(dir);
    rescanNow();
    return path;
}

QString AlveusCore::createTextFile(const QString &inFolder, const QString &baseName)
{
    const QString dir = inFolder.isEmpty() ? m_desktopPath : QDir::cleanPath(inFolder);
    const QJsonObject before = stateJson();
    const QString path = dir + QLatin1Char('/') + uniqueName(dir, baseName);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        return QString();
    }
    f.close();
    pushUndo(QStringLiteral("create|") + QFileInfo(path).fileName(),
             QJsonObject{{QStringLiteral("state"), before},
                         {QStringLiteral("files"), QJsonArray{fileOp(QStringLiteral("created"), path)}}});
    invalidateFolder(dir);
    rescanNow();
    return path;
}

void AlveusCore::copyToClipboard(const QStringList &paths, bool cut)
{
    QClipboard *cb = clipboard();
    if (!cb || paths.isEmpty()) {
        return;
    }
    auto *data = new QMimeData;
    QList<QUrl> urls;
    for (const QString &p : paths) {
        urls << QUrl::fromLocalFile(p);
    }
    data->setUrls(urls);
    data->setData(kCutMime, cut ? "1" : "0");
    cb->setMimeData(data);
}

bool AlveusCore::canPaste() const
{
    const QClipboard *cb = clipboard();
    const QMimeData *data = cb ? cb->mimeData() : nullptr;
    return data && data->hasUrls();
}

QStringList AlveusCore::paste(const QString &folder, const QString &fenceId)
{
    QClipboard *cb = clipboard();
    const QMimeData *data = cb ? cb->mimeData() : nullptr;
    if (!data || !data->hasUrls()) {
        return {};
    }
    const bool cut = data->data(kCutMime) == "1";
    const QList<QUrl> urls = data->urls();
    const QString dir = folder.isEmpty() ? m_desktopPath : QDir::cleanPath(folder);
    QStringList out;
    if (QDir(dir).canonicalPath() == QDir(m_desktopPath).canonicalPath()) {
        // Pasting a desktop file onto the desktop again makes a copy, like Dolphin.
        QList<QUrl> outside;
        const QJsonObject before = stateJson();
        QList<FileTask> copies;
        for (const QUrl &u : urls) {
            if (isDesktopUrl(u) && !cut) {
                const QString src = u.toLocalFile();
                const QString dst = m_desktopPath + QLatin1Char('/') + uniqueName(m_desktopPath, QFileInfo(src).fileName());
                copies << FileTask{FileTask::Copy, src, dst};
                m_reserved.insert(QDir::cleanPath(dst));
                out << dst;
                Placement &placement = m_place[QFileInfo(dst).fileName()];
                placement.manual = true;
                placement.fence = fenceId;
                if (!fenceId.isEmpty()) {
                    placement.hasPos = false;
                }
            } else {
                outside << u;
            }
        }
        if (!copies.isEmpty()) {
            runFileTasks(QStringLiteral("copy"), copies, before, QJsonArray());
            scheduleSave();
        }
        out << importToDesktop(outside, !cut, fenceId);
        rescanNow();
    } else {
        out = importToFolder(urls, !cut, dir);
    }
    if (cut) {
        cb->clear();
    }
    return out;
}

// =============================================================================
// snapshots

QVariantList AlveusCore::snapshots() const
{
    QVariantList out;
    for (const QJsonObject &s : m_snapshots) {
        out << QVariantMap{{QStringLiteral("id"), s.value(QStringLiteral("id")).toString()},
                           {QStringLiteral("name"), s.value(QStringLiteral("name")).toString()},
                           {QStringLiteral("created"), QDateTime::fromMSecsSinceEpoch(qint64(s.value(QStringLiteral("created")).toDouble()))},
                           {QStringLiteral("fences"), s.value(QStringLiteral("state")).toObject().value(QStringLiteral("fences")).toArray().size()}};
    }
    return out;
}

QString AlveusCore::saveSnapshot(const QString &name)
{
    QJsonObject s;
    const QString id = newId();
    s.insert(QStringLiteral("id"), id);
    s.insert(QStringLiteral("name"), name);
    s.insert(QStringLiteral("created"), double(QDateTime::currentMSecsSinceEpoch()));
    s.insert(QStringLiteral("state"), stateJson());
    m_snapshots << s;
    scheduleSave();
    bump();
    return id;
}

void AlveusCore::restoreSnapshot(const QString &id)
{
    for (const QJsonObject &s : std::as_const(m_snapshots)) {
        if (s.value(QStringLiteral("id")).toString() == id) {
            pushUndo(QStringLiteral("snapshot|") + s.value(QStringLiteral("name")).toString());
            applyState(s.value(QStringLiteral("state")).toObject(), false);
            bumpFences();
            for (const Fence &f : std::as_const(m_fences)) {
                watchFolder(f.portal);
            }
            scheduleSave();
            rescanNow();
            return;
        }
    }
}

void AlveusCore::deleteSnapshot(const QString &id)
{
    for (int i = 0; i < m_snapshots.size(); ++i) {
        if (m_snapshots[i].value(QStringLiteral("id")).toString() == id) {
            m_snapshots.removeAt(i);
            scheduleSave();
            bump();
            return;
        }
    }
}

// =============================================================================
// tidy-up helper

QVariantList AlveusCore::staleFiles(int days) const
{
    const QDateTime now = QDateTime::currentDateTime();
    QVariantList out;
    for (const Entry &e : m_files) {
        const QDateTime lastUse = std::max(e.modified, e.accessed.isValid() ? e.accessed : e.modified);
        const qint64 age = lastUse.daysTo(now);
        if (age >= days) {
            QVariantMap m = toMap(e);
            m.insert(QStringLiteral("days"), age);
            out << m;
        }
    }
    return out;
}

int AlveusCore::staleCount() const
{
    return staleFiles(setting(QStringLiteral("cleanupDays")).toInt()).size();
}

bool AlveusCore::cleanupDue() const
{
    return setting(QStringLiteral("cleanupReminder")).toBool() && QDateTime::currentMSecsSinceEpoch() > m_cleanupSnoozedUntil
        && staleCount() >= 5;
}

void AlveusCore::snoozeCleanup(int days)
{
    m_cleanupSnoozedUntil = QDateTime::currentDateTime().addDays(days).toMSecsSinceEpoch();
    scheduleSave();
    bump();
}

QString AlveusCore::archiveFiles(const QStringList &names, const QString &archiveFolderName)
{
    QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (!qEnvironmentVariableIsEmpty("ALVEUS_ARCHIVE_DIR")) {
        docs = qEnvironmentVariable("ALVEUS_ARCHIVE_DIR");
    }
    const QString target = docs + QLatin1Char('/') + archiveFolderName + QLatin1Char('/')
        + QDate::currentDate().toString(Qt::ISODate);
    if (!QDir().mkpath(target)) {
        return QString();
    }
    const QJsonObject before = stateJson();
    QJsonArray ops;
    QList<FileTask> slow;
    QStringList failed;
    const bool sameDisk = Alveus::FileOps::sameFilesystem(m_desktopPath, target);
    for (const QString &name : names) {
        const QString src = m_desktopPath + QLatin1Char('/') + name;
        if (!QFileInfo::exists(src)) {
            continue;
        }
        const QString dst = target + QLatin1Char('/') + uniqueName(target, name);
        if (!sameDisk || QFileInfo(src).isDir()) {
            // Directory moves validate their full tree, so even same-disk
            // archives must use the background worker.
            slow << FileTask{FileTask::Move, src, dst};
            m_reserved.insert(QDir::cleanPath(dst));
        } else if (Alveus::FileOps::movePath(src, dst) == Alveus::FileOps::Result::Done) {
            ops.append(fileOp(QStringLiteral("move"), dst, src));
        } else {
            failed << name;
        }
    }
    reportFailures(QStringLiteral("file-move-failed"), failed);
    if (!slow.isEmpty()) {
        runFileTasks(QStringLiteral("archive"), slow, before, ops);
    } else if (!ops.isEmpty()) {
        pushUndo(QStringLiteral("archive|") + QString::number(ops.size()),
                 QJsonObject{{QStringLiteral("state"), before}, {QStringLiteral("files"), ops}});
    }
    snoozeCleanup(14);
    rescanNow();
    return target;
}

// =============================================================================
// settings and simple state

QVariant AlveusCore::setting(const QString &key) const
{
    if (m_settings.contains(key)) {
        return m_settings.value(key).toVariant();
    }
    return defaultSettings().value(key);
}

void AlveusCore::setSetting(const QString &key, const QVariant &value)
{
    m_settings.insert(key, QJsonValue::fromVariant(value));
    if (key == QLatin1String("autoPrivacy")) {
        m_integrations->setWatchScreenShare(value.toBool());
        Q_EMIT privacyChanged();
    }
    scheduleSave();
    bump();
}

QVariantMap AlveusCore::driveDefaults() const
{
    return mergeDriveOptions(defaultDriveOptions(), m_settings.value(QStringLiteral("drives")).toObject()).toVariantMap();
}

QVariantMap AlveusCore::driveOptions(const QString &fenceId) const
{
    QJsonObject effective = QJsonObject::fromVariantMap(driveDefaults());
    if (const Fence *f = findFence(fenceId)) {
        effective = mergeDriveOptions(effective, f->driveOptions);
    }
    return effective.toVariantMap();
}

QVariantMap AlveusCore::driveOptionOverrides(const QString &fenceId) const
{
    const Fence *f = findFence(fenceId);
    return f ? f->driveOptions.toVariantMap() : QVariantMap();
}

bool AlveusCore::setDriveDefault(const QString &key, const QVariant &value)
{
    QJsonObject options = m_settings.value(QStringLiteral("drives")).toObject();
    if (!setDriveOptionValue(options, key, QJsonValue::fromVariant(value))
        || !validDriveOptions(options, false)) {
        return false;
    }
    if (options == m_settings.value(QStringLiteral("drives")).toObject()) {
        return true;
    }
    m_settings.insert(QStringLiteral("drives"), options);
    scheduleSave();
    bump();
    return true;
}

bool AlveusCore::resetDriveDefault(const QString &key)
{
    QJsonObject options = m_settings.value(QStringLiteral("drives")).toObject();
    if (!removeDriveOptionValue(options, key)) {
        return false;
    }
    if (options.isEmpty()) {
        m_settings.remove(QStringLiteral("drives"));
    } else {
        m_settings.insert(QStringLiteral("drives"), options);
    }
    scheduleSave();
    bump();
    return true;
}

void AlveusCore::resetDriveDefaults()
{
    if (!m_settings.contains(QStringLiteral("drives"))) {
        return;
    }
    m_settings.remove(QStringLiteral("drives"));
    scheduleSave();
    bump();
}

bool AlveusCore::setDriveOption(const QString &fenceId, const QString &key, const QVariant &value)
{
    const int index = indexOfFence(fenceId);
    if (index < 0 || m_fences.at(index).portal != QLatin1String("drives:")) {
        return false;
    }

    QJsonObject options = m_fences.at(index).driveOptions;
    if (!setDriveOptionValue(options, key, QJsonValue::fromVariant(value))
        || !validDriveOptions(options, true)) {
        return false;
    }
    if (options == m_fences.at(index).driveOptions) {
        return true;
    }
    updateFence(fenceId, {{QStringLiteral("driveOptions"), options.toVariantMap()}});
    return true;
}

bool AlveusCore::resetDriveOption(const QString &fenceId, const QString &key)
{
    const int index = indexOfFence(fenceId);
    if (index < 0 || m_fences.at(index).portal != QLatin1String("drives:")) {
        return false;
    }

    QJsonObject options = m_fences.at(index).driveOptions;
    if (!removeDriveOptionValue(options, key)) {
        return false;
    }
    updateFence(fenceId, {{QStringLiteral("driveOptions"), options.toVariantMap()}});
    return true;
}

void AlveusCore::resetDriveOptions(const QString &fenceId)
{
    const int index = indexOfFence(fenceId);
    if (index < 0 || m_fences.at(index).portal != QLatin1String("drives:")
        || m_fences.at(index).driveOptions.isEmpty()) {
        return;
    }
    updateFence(fenceId, {{QStringLiteral("driveOptions"), QVariantMap()}});
}

QVariantList AlveusCore::driveVisibilityExceptions(const QString &fenceId) const
{
    const Fence *f = findFence(fenceId);
    if (!f) {
        return {};
    }

    const QJsonObject exceptions = f->driveOptions.value(QStringLiteral("visibilityExceptions")).toObject();
    QStringList keys = exceptions.keys();
    keys.sort();

    const QVariantList knownDrives = m_drives->drives();
    QVariantList out;
    out.reserve(keys.size());
    for (const QString &id : std::as_const(keys)) {
        QVariantMap item = {
            {QStringLiteral("id"), id},
            {QStringLiteral("mode"), exceptions.value(id).toString()},
            {QStringLiteral("name"), id},
            {QStringLiteral("device"), QString()},
            {QStringLiteral("connected"), false},
        };
        for (const QVariant &value : knownDrives) {
            const QVariantMap drive = value.toMap();
            if (drive.value(QStringLiteral("id")).toString() == id) {
                item.insert(QStringLiteral("name"), drive.value(QStringLiteral("name")));
                item.insert(QStringLiteral("device"), drive.value(QStringLiteral("device")));
                item.insert(QStringLiteral("connected"), true);
                break;
            }
        }
        if (privacy()) {
            item.insert(QStringLiteral("name"), QStringLiteral("••••"));
            item.insert(QStringLiteral("device"), QString());
        }
        out << item;
    }
    return out;
}

bool AlveusCore::setDriveVisibilityException(const QString &fenceId, const QString &stableId, const QString &mode)
{
    const int index = indexOfFence(fenceId);
    const QString id = stableId.trimmed();
    if (index < 0 || m_fences.at(index).portal != QLatin1String("drives:")
        || id.isEmpty() || id.size() > 1024
        || (mode != QLatin1String("hide") && mode != QLatin1String("show") && !mode.isEmpty())) {
        return false;
    }

    QJsonObject options = m_fences.at(index).driveOptions;
    QJsonObject exceptions = options.value(QStringLiteral("visibilityExceptions")).toObject();
    if (mode.isEmpty()) {
        if (!exceptions.contains(id)) {
            return false;
        }
        exceptions.remove(id);
    } else {
        if (exceptions.value(id).toString() == mode) {
            return true;
        }
        exceptions.insert(id, mode);
    }

    if (exceptions.isEmpty()) {
        options.remove(QStringLiteral("visibilityExceptions"));
    } else {
        options.insert(QStringLiteral("visibilityExceptions"), exceptions);
    }
    updateFence(fenceId, {{QStringLiteral("driveOptions"), options.toVariantMap()}});
    return true;
}

QVariantList AlveusCore::visibleDrives(const QString &fenceId, bool showHiddenTemporarily) const
{
    return Alveus::filterDriveList(m_drives->drives(), driveOptions(fenceId), showHiddenTemporarily);
}

QVariantList AlveusCore::visibleDriveDevices(const QString &fenceId, bool showHiddenTemporarily) const
{
    return Alveus::filterDriveHierarchy(m_drives->devices(), driveOptions(fenceId), showHiddenTemporarily);
}

void AlveusCore::setHidden(bool hidden)
{
    if (m_hidden != hidden) {
        m_hidden = hidden;
        Q_EMIT hiddenChanged();
    }
}

void AlveusCore::setPrivacyManual(bool on)
{
    if (m_privacyManual != on) {
        m_privacyManual = on;
        Q_EMIT privacyChanged();
    }
}

// =============================================================================
// integrations

QString AlveusCore::thumbnail(const QString &path)
{
    static QMimeDatabase db;
    return m_thumbs->lookup(path, db.mimeTypeForFile(path).name());
}

void AlveusCore::refreshPhones()
{
    m_integrations->refreshPhones();
}

void AlveusCore::sendToPhone(const QString &deviceId, const QStringList &paths)
{
    m_integrations->sendToPhone(deviceId, paths);
}

QString AlveusCore::phoneFolder() const
{
    return Integrations::phoneFolder(configHome());
}

QString AlveusCore::screenshotFolder() const
{
    return Integrations::screenshotFolder(configHome());
}

QString AlveusCore::downloadsFolder() const
{
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
}

void AlveusCore::openUrl(const QString &url)
{
    QDesktopServices::openUrl(QUrl(url));
}

QVariantList AlveusCore::drives() const
{
    return m_drives->drives();
}

QVariantList AlveusCore::driveDevices() const
{
    return m_drives->devices();
}


void AlveusCore::refreshDrives()
{
    m_drives->refresh();
}

void AlveusCore::mountDrive(const QString &device)
{
    m_drives->mount(device);
}

void AlveusCore::unmountDrive(const QString &device)
{
    m_drives->unmount(device);
}

// =============================================================================
// KRunner

QVariantList AlveusCore::runnerMatches(const QString &query) const
{
    const QString q = query.trimmed();
    QVariantList out;
    if (q.size() < 2) {
        return out;
    }
    auto relevance = [&q](const QString &text) {
        if (text.compare(q, Qt::CaseInsensitive) == 0) {
            return 1.0;
        }
        return text.startsWith(q, Qt::CaseInsensitive) ? 0.85 : 0.6;
    };
    for (const Fence &f : m_fences) {
        if (f.title.contains(q, Qt::CaseInsensitive)) {
            out << QVariantMap{{QStringLiteral("id"), QStringLiteral("fence:") + f.id},
                               {QStringLiteral("text"), f.title},
                               {QStringLiteral("icon"), QStringLiteral("folder-favorites")},
                               {QStringLiteral("subtext"), QStringLiteral("Alveus")},
                               {QStringLiteral("category"), 70},
                               {QStringLiteral("relevance"), relevance(f.title)}};
        }
    }
    for (const Entry &e : m_files) {
        if (out.size() >= 20) {
            break;
        }
        if (e.label.contains(q, Qt::CaseInsensitive) || e.name.contains(q, Qt::CaseInsensitive)) {
            out << QVariantMap{{QStringLiteral("id"), QStringLiteral("file:") + e.path},
                               {QStringLiteral("text"), e.label},
                               {QStringLiteral("icon"), e.icon},
                               {QStringLiteral("subtext"), e.path},
                               {QStringLiteral("url"), QUrl::fromLocalFile(e.path).toString()},
                               {QStringLiteral("isFile"), true},
                               {QStringLiteral("category"), 50},
                               {QStringLiteral("relevance"), relevance(e.label)}};
        }
    }
    return out;
}

void AlveusCore::runnerRun(const QString &id)
{
    if (id.startsWith(QLatin1String("fence:"))) {
        requestFence(id.mid(6));
    } else if (id.startsWith(QLatin1String("file:"))) {
        openPath(id.mid(5));
    }
}
