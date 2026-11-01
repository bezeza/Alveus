/*
 * Alveus — desktop fences for KDE Plasma 6
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "fileops.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Alveus::FileOps
{

namespace
{

bool pathExists(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() || info.isSymLink();
}

bool cancelled(const Control *control)
{
    return control && control->cancel.load();
}

// Reject special files before starting cp. A FIFO opened with QFile can block
// indefinitely, while sockets and device nodes are not ordinary desktop files.
bool validateSupportedTree(const QString &path)
{
    const QByteArray encoded = QFile::encodeName(path);
    struct stat st {};
    if (::lstat(encoded.constData(), &st) != 0) {
        return false;
    }
    if (S_ISLNK(st.st_mode) || S_ISREG(st.st_mode)) {
        return true;
    }
    if (!S_ISDIR(st.st_mode)) {
        return false;
    }

    const QDir directory(path);
    if (!directory.isReadable() || !QFileInfo(path).isExecutable()) {
        return false;
    }
    const QFileInfoList entries = directory.entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        if (!validateSupportedTree(entry.filePath())) {
            return false;
        }
    }
    return true;
}

// Data-only fallback for filesystems that cannot preserve Unix metadata.
// Symlinks are never dereferenced or skipped: if the destination cannot create
// the same link, fail the whole copy so a move never deletes the only link.
Result plainCopy(const QString &from, const QString &to, Control *control)
{
    if (cancelled(control)) {
        return Result::Cancelled;
    }

    const QByteArray encodedFrom = QFile::encodeName(from);
    const QByteArray encodedTo = QFile::encodeName(to);
    struct stat st {};
    if (::lstat(encodedFrom.constData(), &st) != 0) {
        return Result::Failed;
    }

    if (S_ISLNK(st.st_mode)) {
        QByteArray target(256, '\0');
        for (;;) {
            const ssize_t length = ::readlink(encodedFrom.constData(), target.data(), size_t(target.size()));
            if (length < 0) {
                return Result::Failed;
            }
            if (length < target.size()) {
                target.resize(int(length));
                break;
            }
            if (target.size() >= (1 << 20)) {
                return Result::Failed;
            }
            target.resize(target.size() * 2);
        }
        // symlink() fails on filesystems that do not support links (e.g. FAT).
        return ::symlink(target.constData(), encodedTo.constData()) == 0 ? Result::Done : Result::Failed;
    }

    if (S_ISDIR(st.st_mode)) {
        if (!QDir().mkdir(to)) {
            return Result::Failed;
        }
        const QFileInfoList entries = QDir(from).entryInfoList(
            QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : entries) {
            const Result r = plainCopy(entry.filePath(), to + QLatin1Char('/') + entry.fileName(), control);
            if (r != Result::Done) {
                return r;
            }
        }
        return Result::Done;
    }

    // Only regular files are copied as data. O_NOFOLLOW + fstat closes the
    // race where a regular file is replaced with a FIFO after lstat().
    if (!S_ISREG(st.st_mode)) {
        return Result::Failed;
    }
    const int inputFd = ::open(encodedFrom.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (inputFd < 0) {
        return Result::Failed;
    }
    struct stat opened {};
    if (::fstat(inputFd, &opened) != 0 || !S_ISREG(opened.st_mode)) {
        ::close(inputFd);
        return Result::Failed;
    }
    const int outputFd = ::open(encodedTo.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0666);
    if (outputFd < 0) {
        ::close(inputFd);
        return Result::Failed;
    }

    QFile in;
    QFile out;
    if (!in.open(inputFd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(inputFd);
        ::close(outputFd);
        return Result::Failed;
    }
    if (!out.open(outputFd, QIODevice::WriteOnly, QFileDevice::AutoCloseHandle)) {
        ::close(outputFd);
        return Result::Failed;
    }

    QByteArray buffer;
    while (!(buffer = in.read(1 << 20)).isEmpty()) {
        if (cancelled(control)) {
            return Result::Cancelled;
        }
        qint64 offset = 0;
        while (offset < buffer.size()) {
            const qint64 written = out.write(buffer.constData() + offset, buffer.size() - offset);
            if (written <= 0) {
                return Result::Failed;
            }
            offset += written;
        }
        if (control) {
            control->bytesCurrent += buffer.size();
        }
    }
    if (in.error() != QFileDevice::NoError || !out.flush()) {
        return Result::Failed;
    }
    out.setFileTime(QFileInfo(from).lastModified(), QFileDevice::FileModificationTime); // best effort
    out.close();
    return out.error() == QFileDevice::NoError ? Result::Done : Result::Failed;
}

// cp -a into `temporary`, watching for cancel and reporting progress by
// measuring what has arrived so far.
Result runCp(const QString &cp, const QString &from, const QString &temporary, Control *control)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    // Do not preserve ownership: an ordinary desktop user cannot chown.
    process.start(cp, {QStringLiteral("-a"), QStringLiteral("--no-preserve=ownership"), QStringLiteral("--"), from, temporary});
    if (!process.waitForStarted(5000)) {
        return Result::Failed;
    }
    QElapsedTimer sinceMeasure;
    sinceMeasure.start();
    while (!process.waitForFinished(100)) {
        if (process.state() == QProcess::NotRunning) {
            break;
        }
        if (cancelled(control)) {
            process.kill();
            process.waitForFinished(3000);
            return Result::Cancelled;
        }
        if (control && sinceMeasure.elapsed() > 700) {
            control->bytesCurrent = treeSize(temporary);
            sinceMeasure.restart();
        }
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 ? Result::Done : Result::Failed;
}

} // namespace

qint64 treeSize(const QString &path)
{
    const QFileInfo info(path);
    if (info.isSymLink() || !info.exists()) {
        return 0;
    }
    if (!info.isDir()) {
        return info.size();
    }
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo f = it.fileInfo();
        if (!f.isSymLink()) {
            total += f.size();
        }
    }
    return total;
}

bool sameFilesystem(const QString &path, const QString &dir)
{
    struct stat a {};
    struct stat b {};
    // lstat: a symlink is moved as the link itself
    if (::lstat(QFile::encodeName(path).constData(), &a) != 0 || ::stat(QFile::encodeName(dir).constData(), &b) != 0) {
        return false;
    }
    return a.st_dev == b.st_dev;
}

bool removeTree(const QString &path)
{
    const QFileInfo info(path);
    if (info.isDir() && !info.isSymLink()) {
        return QDir(path).removeRecursively();
    }
    return QFile::remove(path);
}

Result copyTree(const QString &from, const QString &to, Control *control)
{
    if (!pathExists(from) || pathExists(to) || !validateSupportedTree(from)) {
        return Result::Failed;
    }
    if (cancelled(control)) {
        return Result::Cancelled;
    }
    const QString parent = QFileInfo(to).absolutePath();
    if (!QDir().mkpath(parent)) {
        return Result::Failed;
    }

    QString temporary;
    do {
        temporary = parent + QStringLiteral("/.") + QFileInfo(to).fileName() + QStringLiteral(".alveus-copy-")
            + QUuid::createUuid().toString(QUuid::Id128).left(8);
    } while (pathExists(temporary));

    Result result = Result::Failed;
    const QString cp = QStandardPaths::findExecutable(QStringLiteral("cp"));
    if (!cp.isEmpty()) {
        result = runCp(cp, from, temporary, control);
    }
    if (result == Result::Failed) {
        // FAT and similar: retry data-only for regular files; an unsupported symlink
        // causes plainCopy to fail, preserving the source during a move.
        removeTree(temporary);
        if (control) {
            control->bytesCurrent = 0;
        }
        result = plainCopy(from, temporary, control);
    }
    if (result != Result::Done) {
        removeTree(temporary);
        return result;
    }

    const QFileInfo tempInfo(temporary);
    const bool renamed = tempInfo.isDir() && !tempInfo.isSymLink()
        ? QDir(parent).rename(tempInfo.fileName(), QFileInfo(to).fileName())
        : QFile::rename(temporary, to);
    if (!renamed) {
        removeTree(temporary);
        return Result::Failed;
    }
    return Result::Done;
}

Result movePath(const QString &from, const QString &to, Control *control)
{
    const QFileInfo source(from);
    if (!pathExists(from) || pathExists(to) || QDir::cleanPath(from) == QDir::cleanPath(to)
        || !validateSupportedTree(from)) {
        return Result::Failed;
    }
    if (!QDir().mkpath(QFileInfo(to).absolutePath())) {
        return Result::Failed;
    }
    const bool renamed = source.isDir() && !source.isSymLink() ? QDir().rename(from, to) : QFile::rename(from, to);
    if (renamed) {
        return Result::Done;
    }
    // Another filesystem: copy completely first, remove the original only
    // after the copy is in place. A cancel or failure leaves the source as is.
    const Result copied = copyTree(from, to, control);
    if (copied != Result::Done) {
        return copied;
    }
    return removeTree(from) ? Result::Done : Result::SourceLeft;
}

} // namespace Alveus::FileOps
