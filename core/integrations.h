/*
 * Alveus — talking to the rest of the desktop:
 * KDE Connect, Spectacle, terminals, and noticing screen sharing.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>

class QProcess;

class Integrations : public QObject
{
    Q_OBJECT
public:
    explicit Integrations(QObject *parent = nullptr);

    // KDE Connect
    void refreshPhones();
    void sendToPhone(const QString &deviceId, const QStringList &paths);
    static QVariantList parsePhones(const QString &cliOutput);
    static QString phoneFolder(const QString &configHome);

    // Spectacle
    static QString screenshotFolder(const QString &configHome);

    // Terminal in a folder
    static void openTerminal(const QString &folder);

    // Screen sharing (PipeWire screencast streams), checked while enabled
    void setWatchScreenShare(bool on);
    static bool screencastIn(const QByteArray &pwDumpJson);

Q_SIGNALS:
    void phonesChanged(const QVariantList &phones);
    void screenSharedChanged(bool shared);

private:
    void pollScreenShare();

    QTimer m_shareTimer;
    QProcess *m_shareProc = nullptr;
    bool m_shared = false;
};
