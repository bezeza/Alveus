/*
 * Alveus — KDE Connect, Spectacle, terminal, screen sharing
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "integrations.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

Integrations::Integrations(QObject *parent)
    : QObject(parent)
{
    m_shareTimer.setInterval(4000);
    connect(&m_shareTimer, &QTimer::timeout, this, &Integrations::pollScreenShare);
}

// ---- KDE Connect ------------------------------------------------------------

QVariantList Integrations::parsePhones(const QString &out)
{
    // `kdeconnect-cli --list-available --id-name-only` prints "<id> <name>"
    QVariantList list;
    for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString t = line.trimmed();
        const int sp = t.indexOf(QLatin1Char(' '));
        if (sp <= 0) {
            continue;
        }
        QVariantMap m;
        m.insert(QStringLiteral("id"), t.left(sp));
        m.insert(QStringLiteral("name"), t.mid(sp + 1).trimmed());
        list << m;
    }
    return list;
}

void Integrations::refreshPhones()
{
    if (QStandardPaths::findExecutable(QStringLiteral("kdeconnect-cli")).isEmpty()) {
        Q_EMIT phonesChanged({});
        return;
    }
    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this, [this, proc](int code, QProcess::ExitStatus status) {
        Q_EMIT phonesChanged(status == QProcess::NormalExit && code == 0
                                 ? parsePhones(QString::fromUtf8(proc->readAllStandardOutput()))
                                 : QVariantList{});
        proc->deleteLater();
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            Q_EMIT phonesChanged({});
            proc->deleteLater();
        }
    });
    proc->start(QStringLiteral("kdeconnect-cli"), {QStringLiteral("--list-available"), QStringLiteral("--id-name-only")});
}

void Integrations::sendToPhone(const QString &deviceId, const QStringList &paths)
{
    for (const QString &p : paths) {
        QProcess::startDetached(QStringLiteral("kdeconnect-cli"),
                                {QStringLiteral("-d"), deviceId, QStringLiteral("--share"), QUrl::fromLocalFile(p).toString()});
    }
}

QString Integrations::phoneFolder(const QString &configHome)
{
    // ~/.config/kdeconnect/<device>/kdeconnect_share/config : [General] incoming_path
    QDirIterator it(configHome + QStringLiteral("/kdeconnect"), {QStringLiteral("config")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        if (!file.contains(QLatin1String("kdeconnect_share"))) {
            continue;
        }
        QSettings s(file, QSettings::IniFormat);
        // QSettings maps the [General] section to top-level keys
        QString path = s.value(QStringLiteral("incoming_path")).toString();
        if (path.isEmpty()) {
            path = s.value(QStringLiteral("General/incoming_path")).toString();
        }
        if (path.startsWith(QLatin1String("file:"))) {
            path = QUrl(path).toLocalFile();
        }
        path.replace(QStringLiteral("$HOME"), QDir::homePath());
        if (path.startsWith(QLatin1Char('~'))) {
            path.replace(0, 1, QDir::homePath());
        }
        if (!path.isEmpty()) {
            return QDir::cleanPath(path);
        }
    }
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
}

// ---- Spectacle ----------------------------------------------------------------

QString Integrations::screenshotFolder(const QString &configHome)
{
    QSettings s(configHome + QStringLiteral("/spectaclerc"), QSettings::IniFormat);
    QString loc = s.value(QStringLiteral("ImageSave/imageSaveLocation")).toString();
    if (loc.startsWith(QLatin1String("file:"))) {
        loc = QUrl(loc).toLocalFile();
    }
    if (loc.isEmpty()) {
        loc = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + QStringLiteral("/Screenshots");
    }
    return QDir::cleanPath(loc);
}

// ---- terminal -------------------------------------------------------------------

void Integrations::openTerminal(const QString &folder)
{
    if (!QStandardPaths::findExecutable(QStringLiteral("konsole")).isEmpty()) {
        QProcess::startDetached(QStringLiteral("konsole"), {QStringLiteral("--workdir"), folder});
        return;
    }
    for (const QString &t : {QStringLiteral("xdg-terminal-exec"), QStringLiteral("kgx"), QStringLiteral("gnome-terminal"),
                             QStringLiteral("x-terminal-emulator"), QStringLiteral("xterm")}) {
        if (!QStandardPaths::findExecutable(t).isEmpty()) {
            QProcess::startDetached(t, {}, folder);
            return;
        }
    }
}

// ---- screen sharing ---------------------------------------------------------------

bool Integrations::screencastIn(const QByteArray &json)
{
    // A screen being shared shows up in PipeWire as a running video source
    // that is not a camera (cameras carry device.api / a v4l2/libcamera tag).
    const QJsonArray objects = QJsonDocument::fromJson(json).array();
    for (const QJsonValue &v : objects) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("type")).toString() != QLatin1String("PipeWire:Interface:Node")) {
            continue;
        }
        const QJsonObject info = o.value(QStringLiteral("info")).toObject();
        const QJsonObject props = info.value(QStringLiteral("props")).toObject();
        if (props.value(QStringLiteral("media.class")).toString() != QLatin1String("Video/Source")) {
            continue;
        }
        if (info.value(QStringLiteral("state")).toString() != QLatin1String("running")) {
            continue;
        }
        if (props.contains(QStringLiteral("device.api")) || props.contains(QStringLiteral("api.v4l2.path"))
            || props.contains(QStringLiteral("api.libcamera.location"))) {
            continue;
        }
        return true;
    }
    return false;
}

void Integrations::setWatchScreenShare(bool on)
{
    if (on && !QStandardPaths::findExecutable(QStringLiteral("pw-dump")).isEmpty()) {
        m_shareTimer.start();
        pollScreenShare();
    } else {
        m_shareTimer.stop();
        if (m_shared) {
            m_shared = false;
            Q_EMIT screenSharedChanged(false);
        }
    }
}

void Integrations::pollScreenShare()
{
    if (m_shareProc) {
        return; // previous check still running
    }
    m_shareProc = new QProcess(this);
    QProcess *process = m_shareProc;
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        if (m_shareProc != process) {
            process->deleteLater();
            return;
        }
        const bool shared = status == QProcess::NormalExit && code == 0
            ? screencastIn(process->readAllStandardOutput()) : false;
        process->deleteLater();
        m_shareProc = nullptr;
        if (shared != m_shared) {
            m_shared = shared;
            Q_EMIT screenSharedChanged(shared);
        }
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_shareProc != process) {
            return;
        }
        process->deleteLater();
        m_shareProc = nullptr;
        if (m_shared) {
            m_shared = false;
            Q_EMIT screenSharedChanged(false);
        }
    });
    process->start(QStringLiteral("pw-dump"), {});
}
