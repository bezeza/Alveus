/*
 * Alveus — thumbnails for videos and PDFs
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "thumbnails.h"
#include "processutils.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <memory>
#include <QUrl>

#include <memory>
#include <utility>

namespace
{
constexpr int kMaxParallel = 2;
constexpr int kSize = 256; // "large" flavour
constexpr int kFailureRetryDelayMs = 30000;

QString uriOf(const QString &path)
{
    return QString::fromLatin1(QUrl::fromLocalFile(path).toEncoded());
}

QString fileStamp(const QFileInfo &info)
{
    return QString::number(info.size()) + QLatin1Char(':')
        + QString::number(info.lastModified().toMSecsSinceEpoch());
}
} // namespace

Thumbnailer::Thumbnailer(QObject *parent, int processTimeoutMs)
    : QObject(parent)
    , m_processTimeoutMs(processTimeoutMs > 0 ? processTimeoutMs : 30000)
{
    m_hasFfmpegThumbnailer = !QStandardPaths::findExecutable(QStringLiteral("ffmpegthumbnailer")).isEmpty();
    m_hasFfmpeg = !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty();
    m_hasPdftoppm = !QStandardPaths::findExecutable(QStringLiteral("pdftoppm")).isEmpty();
}

Thumbnailer::~Thumbnailer()
{
    // don't leave half-made thumbnails running when the desktop goes away
    const auto procs = findChildren<QProcess *>();
    for (QProcess *p : procs) {
        p->disconnect(this);
        p->kill();
        p->waitForFinished(1000);
    }
    for (QTemporaryDir *dir : std::as_const(m_temporaryDirs)) {
        delete dir;
    }
    m_temporaryDirs.clear();
}

QString Thumbnailer::cacheRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/thumbnails");
}

QString Thumbnailer::cacheFile(const QString &path, const QString &flavor)
{
    const QByteArray md5 = QCryptographicHash::hash(uriOf(path).toUtf8(), QCryptographicHash::Md5).toHex();
    return cacheRoot() + QLatin1Char('/') + flavor + QLatin1Char('/') + QString::fromLatin1(md5) + QStringLiteral(".png");
}

bool Thumbnailer::canGenerate(const QString &mime) const
{
    if (mime.startsWith(QLatin1String("video/"))) {
        return m_hasFfmpegThumbnailer || m_hasFfmpeg;
    }
    if (mime == QLatin1String("application/pdf")) {
        return m_hasPdftoppm;
    }
    return false;
}

QString Thumbnailer::lookup(const QString &path, const QString &mime)
{
    const QFileInfo info(path);
    if (!info.isFile()) {
        return QString();
    }
    const qint64 mtime = info.lastModified().toSecsSinceEpoch();
    const qint64 mtimeMs = info.lastModified().toMSecsSinceEpoch();

    for (const QString &flavor : {QStringLiteral("large"), QStringLiteral("x-large"), QStringLiteral("normal")}) {
        const QString file = cacheFile(path, flavor);
        if (!QFile::exists(file)) {
            continue;
        }
        QImage img;
        if (!img.load(file)) {
            continue;
        }
        const QString cachedMs = img.text(QStringLiteral("Alveus::MTimeMs"));
        const QString cachedSize = img.text(QStringLiteral("Thumb::Size"));
        const bool mtimeMatches = cachedMs.isEmpty()
            ? img.text(QStringLiteral("Thumb::MTime")).toLongLong() == mtime
            : cachedMs.toLongLong() == mtimeMs;
        const bool sizeMatches = cachedSize.isEmpty() || cachedSize.toLongLong() == info.size();
        if (mtimeMatches && sizeMatches) {
            return QUrl::fromLocalFile(file).toString();
        }
    }

    const QString stamp = fileStamp(info);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool sameFailure = m_failed.value(path) == stamp;
    const bool retryAllowed = !sameFailure || m_retryAt.value(path, 0) <= nowMs;
    if (canGenerate(mime) && !m_pending.contains(path) && retryAllowed) {
        m_failed.remove(path);
        m_retryAt.remove(path);
        m_pending.insert(path);
        m_queue.enqueue({path, mime, info.size(), mtimeMs});
        pump();
    }
    return QString();
}

void Thumbnailer::pump()
{
    while (m_running < kMaxParallel && !m_queue.isEmpty()) {
        const Job job = m_queue.dequeue();
        auto *tmpDir = new QTemporaryDir();
        if (!tmpDir->isValid()) {
            finish(job, QString(), false);
            delete tmpDir;
            continue;
        }
        const QString out = tmpDir->path() + QStringLiteral("/thumb.png");
        m_temporaryDirs.insert(tmpDir);

        QString program;
        QStringList args;
        if (job.mime.startsWith(QLatin1String("video/"))) {
            if (m_hasFfmpegThumbnailer) {
                program = QStringLiteral("ffmpegthumbnailer");
                args = {QStringLiteral("-i"), job.path, QStringLiteral("-o"), out, QStringLiteral("-s"),
                        QString::number(kSize), QStringLiteral("-c"), QStringLiteral("png")};
            } else {
                program = QStringLiteral("ffmpeg");
                args = {QStringLiteral("-y"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                        QStringLiteral("-ss"), QStringLiteral("1"), QStringLiteral("-i"), job.path,
                        QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-vf"),
                        QStringLiteral("scale=%1:-2").arg(kSize), out};
            }
        } else {
            program = QStringLiteral("pdftoppm");
            args = {QStringLiteral("-png"), QStringLiteral("-singlefile"), QStringLiteral("-f"), QStringLiteral("1"),
                    QStringLiteral("-scale-to"), QString::number(kSize), job.path,
                    tmpDir->path() + QStringLiteral("/thumb")};
        }

        auto *proc = new QProcess(this);
        ++m_running;
        auto completed = std::make_shared<bool>(false);
        auto complete = [this, proc, tmpDir, job, out, completed](bool ok) {
            if (*completed) {
                return;
            }
            *completed = true;
            finish(job, out, ok);
            proc->deleteLater();
            m_temporaryDirs.remove(tmpDir);
            delete tmpDir;
            --m_running;
            pump();
        };
        connect(proc, &QProcess::finished, this, [this, proc, job, complete](int code, QProcess::ExitStatus status) {
            const bool timedOut = Alveus::processTimedOut(proc);
            const bool ok = !timedOut && status == QProcess::NormalExit && code == 0;
            if (timedOut) {
                Q_EMIT message(QStringLiteral("thumbnail-timeout"),
                               QStringLiteral("%1 exceeded %2 ms")
                                   .arg(QFileInfo(job.path).fileName()).arg(m_processTimeoutMs));
            } else if (!ok) {
                QString detail = QString::fromUtf8(proc->readAllStandardError()).trimmed();
                if (detail.isEmpty()) {
                    detail = QStringLiteral("%1 exited with code %2")
                                 .arg(QFileInfo(job.path).fileName()).arg(code);
                }
                Q_EMIT message(QStringLiteral("thumbnail-helper-failed"), detail);
            }
            complete(ok);
        });
        connect(proc, &QProcess::errorOccurred, this, [this, proc, job, complete](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                Q_EMIT message(QStringLiteral("thumbnail-helper-failed"),
                               QStringLiteral("%1: %2").arg(QFileInfo(job.path).fileName(), proc->errorString()));
                complete(false);
            }
        });
        Alveus::killAfterTimeout(proc, m_processTimeoutMs);
        proc->start(program, args);
    }
}

void Thumbnailer::finish(const Job &job, const QString &tmp, bool ok)
{
    m_pending.remove(job.path);
    const QFileInfo info(job.path);
    if (!info.exists() || !info.isFile()) {
        m_failed.remove(job.path);
        m_retryAt.remove(job.path);
        return;
    }

    const qint64 currentMtimeMs = info.lastModified().toMSecsSinceEpoch();
    if (info.size() != job.size || currentMtimeMs != job.modifiedMs) {
        // The file changed while ffmpeg/poppler was reading it. Queue a fresh
        // generation instead of caching an image for stale contents.
        m_failed.remove(job.path);
        m_retryAt.remove(job.path);
        m_pending.insert(job.path);
        m_queue.enqueue({job.path, job.mime, info.size(), currentMtimeMs});
        return;
    }

    QImage img;
    if (!ok || !img.load(tmp)) {
        m_failed.insert(job.path, fileStamp(info));
        m_retryAt.insert(job.path, QDateTime::currentMSecsSinceEpoch() + kFailureRetryDelayMs);
        return;
    }
    m_failed.remove(job.path);
    m_retryAt.remove(job.path);
    if (img.width() > kSize || img.height() > kSize) {
        img = img.scaled(kSize, kSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    img.setText(QStringLiteral("Thumb::URI"), uriOf(job.path));
    img.setText(QStringLiteral("Thumb::MTime"), QString::number(info.lastModified().toSecsSinceEpoch()));
    img.setText(QStringLiteral("Thumb::Size"), QString::number(info.size()));
    img.setText(QStringLiteral("Alveus::MTimeMs"), QString::number(currentMtimeMs));
    img.setText(QStringLiteral("Software"), QStringLiteral("Alveus"));

    const QString file = cacheFile(job.path, QStringLiteral("large"));
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile::setPermissions(cacheRoot(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    // QSaveFile replaces the cache entry atomically; readers never see a
    // missing or partially-written thumbnail if the process is interrupted.
    QSaveFile cache(file);
    if (cache.open(QIODevice::WriteOnly) && img.save(&cache, "PNG") && cache.commit()) {
        m_failed.remove(job.path);
        Q_EMIT ready(job.path);
    } else {
        cache.cancelWriting();
        m_failed.insert(job.path, fileStamp(info));
    }
}
