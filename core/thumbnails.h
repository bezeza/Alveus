/*
 * Alveus — thumbnails for videos and PDFs
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Uses the shared freedesktop thumbnail cache (~/.cache/thumbnails), the same
 * one Dolphin fills, so most thumbnails already exist. Missing ones are made
 * with ffmpegthumbnailer / ffmpeg (video) or pdftoppm (PDF) when installed.
 */

#pragma once

#include <QHash>
#include <QObject>
#include <QQueue>
#include <QSet>

class QTemporaryDir;

class Thumbnailer : public QObject
{
    Q_OBJECT
public:
    explicit Thumbnailer(QObject *parent = nullptr, int processTimeoutMs = 30000);
    ~Thumbnailer() override;

    // file:// URL of an up-to-date thumbnail, or "" (then one may be on its way)
    QString lookup(const QString &path, const QString &mime);

    static QString cacheFile(const QString &path, const QString &flavor);
    static QString cacheRoot();
    bool canGenerate(const QString &mime) const;

Q_SIGNALS:
    void ready(const QString &path);
    void message(const QString &kind, const QString &detail);

private:
    struct Job {
        QString path;
        QString mime;
        qint64 size = 0;
        qint64 modifiedMs = 0;
    };
    void pump();
    void finish(const Job &job, const QString &tmp, bool ok);

    QQueue<Job> m_queue;
    QSet<QString> m_pending;
    QHash<QString, QString> m_failed; // path -> size/mtime stamp
    QHash<QString, qint64> m_retryAt; // path -> earliest retry time after helper failure
    QSet<QTemporaryDir *> m_temporaryDirs;
    int m_running = 0;
    int m_processTimeoutMs = 30000;
    bool m_hasFfmpegThumbnailer = false;
    bool m_hasFfmpeg = false;
    bool m_hasPdftoppm = false;
};
