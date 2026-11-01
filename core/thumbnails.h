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
    explicit Thumbnailer(QObject *parent = nullptr);
    ~Thumbnailer() override;

    // file:// URL of an up-to-date thumbnail, or "" (then one may be on its way)
    QString lookup(const QString &path, const QString &mime);

    static QString cacheFile(const QString &path, const QString &flavor);
    static QString cacheRoot();
    bool canGenerate(const QString &mime) const;

Q_SIGNALS:
    void ready(const QString &path);

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
    QHash<QString, QString> m_failed; // path -> size/mtime stamp; retry when the source changes
    QSet<QTemporaryDir *> m_temporaryDirs;
    int m_running = 0;
    bool m_hasFfmpegThumbnailer = false;
    bool m_hasFfmpeg = false;
    bool m_hasPdftoppm = false;
};
