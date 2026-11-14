/*
 * Alveus — KDE Connect, Spectacle, terminal, screen sharing
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "integrations.h"
#include "processutils.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

#include <memory>

Integrations::Integrations(QObject *parent, int processTimeoutMs)
    : QObject(parent)
    , m_processTimeoutMs(processTimeoutMs > 0 ? processTimeoutMs : 5000)
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
    if (m_phoneProc) {
        return;
    }
    if (QStandardPaths::findExecutable(QStringLiteral("kdeconnect-cli")).isEmpty()) {
        Q_EMIT message(QStringLiteral("kdeconnect-unavailable"), QStringLiteral("kdeconnect-cli was not found; keeping the last known device list"));
        return;
    }

    auto *proc = new QProcess(this);
    m_phoneProc = proc;
    auto completed = std::make_shared<bool>(false);
    auto complete = [this, proc, completed](const QVariantList &phones, const QString &kind, const QString &detail) {
        if (*completed) {
            return;
        }
        *completed = true;
        if (m_phoneProc == proc) {
            m_phoneProc = nullptr;
        }
        if (!kind.isEmpty()) {
            Q_EMIT message(kind, detail + QStringLiteral("; keeping the last known device list"));
        } else {
            Q_EMIT phonesChanged(phones);
        }
        proc->deleteLater();
    };
    connect(proc, &QProcess::finished, this, [proc, complete, this](int code, QProcess::ExitStatus status) {
        if (Alveus::processTimedOut(proc)) {
            complete({}, QStringLiteral("kdeconnect-timeout"),
                     QStringLiteral("kdeconnect-cli exceeded %1 ms").arg(m_processTimeoutMs));
        } else if (status == QProcess::NormalExit && code == 0) {
            complete(parsePhones(QString::fromUtf8(proc->readAllStandardOutput())), QString(), QString());
        } else {
            QString detail = QString::fromUtf8(proc->readAllStandardError()).trimmed();
            if (detail.isEmpty()) {
                detail = QStringLiteral("kdeconnect-cli exited with code %1").arg(code);
            }
            complete({}, QStringLiteral("kdeconnect-failed"), detail);
        }
    });
    connect(proc, &QProcess::errorOccurred, this, [proc, complete](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            complete({}, QStringLiteral("kdeconnect-failed"), proc->errorString());
        }
    });
    Alveus::killAfterTimeout(proc, m_processTimeoutMs);
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

void Integrations::setScreenShared(bool shared)
{
    if (m_shared == shared) {
        return;
    }
    m_shared = shared;
    Q_EMIT screenSharedChanged(shared);
}

void Integrations::setWatchScreenShare(bool on)
{
    m_watchScreenShare = on;
    if (!on) {
        m_shareTimer.stop();
        if (m_shareProc) {
            m_shareProc->setProperty("alveusCancelled", true);
            if (m_shareProc->state() != QProcess::NotRunning) {
                m_shareProc->kill();
            }
        }
        setScreenShared(false);
        return;
    }

    m_shareTimer.setInterval(4000);
    m_shareTimer.start();
    pollScreenShare();
}

void Integrations::pollScreenShare()
{
    if (!m_watchScreenShare || m_shareProc) {
        return;
    }
    if (QStandardPaths::findExecutable(QStringLiteral("pw-dump")).isEmpty()) {
        setScreenShared(true); // fail closed until a successful probe is available
        if (!m_pwDumpUnavailableReported) {
            Q_EMIT message(QStringLiteral("screen-share-unavailable"),
                           QStringLiteral("pw-dump was not found; automatic privacy is failing closed"));
            m_pwDumpUnavailableReported = true;
        }
        m_shareTimer.setInterval(qMin(m_shareTimer.interval() * 2, 60000));
        return;
    }
    m_pwDumpUnavailableReported = false;

    auto *process = new QProcess(this);
    m_shareProc = process;
    auto completed = std::make_shared<bool>(false);
    auto complete = [this, process, completed](bool success, bool shared,
                                               const QString &kind, const QString &detail) {
        if (*completed) {
            return;
        }
        *completed = true;
        const bool cancelled = process->property("alveusCancelled").toBool();
        if (m_shareProc == process) {
            m_shareProc = nullptr;
        }
        if (!cancelled && m_watchScreenShare) {
            if (success) {
                m_shareTimer.setInterval(4000);
                setScreenShared(shared);
            } else {
                // Never turn a failed/unknown probe into a false negative.
                setScreenShared(true);
                m_shareTimer.setInterval(qMin(m_shareTimer.interval() * 2, 60000));
                Q_EMIT message(kind, detail);
            }
        }
        process->deleteLater();
    };

    connect(process, &QProcess::finished, this, [this, process, complete](int code, QProcess::ExitStatus status) {
        if (Alveus::processTimedOut(process)) {
            complete(false, false, QStringLiteral("screen-share-timeout"),
                     QStringLiteral("pw-dump exceeded %1 ms").arg(m_processTimeoutMs));
            return;
        }
        if (status != QProcess::NormalExit || code != 0) {
            QString detail = QString::fromUtf8(process->readAllStandardError()).trimmed();
            if (detail.isEmpty()) {
                detail = QStringLiteral("pw-dump exited with code %1").arg(code);
            }
            complete(false, false, QStringLiteral("screen-share-check-failed"), detail);
            return;
        }

        const QByteArray output = process->readAllStandardOutput();
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(output, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
            complete(false, false, QStringLiteral("screen-share-check-failed"),
                     QStringLiteral("pw-dump returned invalid JSON"));
            return;
        }
        complete(true, screencastIn(output), QString(), QString());
    });
    connect(process, &QProcess::errorOccurred, this, [process, complete](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            complete(false, false, QStringLiteral("screen-share-check-failed"), process->errorString());
        }
    });
    Alveus::killAfterTimeout(process, m_processTimeoutMs);
    process->start(QStringLiteral("pw-dump"), {});
}
