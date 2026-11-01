/*
 * Alveus — Windows file operation adapter
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Qt-based first implementation. Copies reject symlinks/reparse points rather
 * than dereferencing them. Native reparse-point preservation is a later milestone.
 */
#include "windowsfileoperations.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QUuid>

namespace Alveus::Platform {
namespace {
bool pathExists(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() || info.isSymLink();
}
bool cancelled(const FileOps::Control *control) { return control && control->cancel.load(); }
bool supportedTree(const QString &path)
{
    const QFileInfo info(path);
    if ((!info.exists() && !info.isSymLink()) || info.isSymLink()) return false;
    if (!info.isDir()) return info.isFile();
    const QFileInfoList entries = QDir(path).entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) if (!supportedTree(entry.filePath())) return false;
    return true;
}
FileOps::Result copyNode(const QString &from, const QString &to, FileOps::Control *control)
{
    if (cancelled(control)) return FileOps::Result::Cancelled;
    const QFileInfo info(from);
    if (info.isSymLink()) return FileOps::Result::Failed;
    if (info.isDir()) {
        if (!QDir().mkpath(to)) return FileOps::Result::Failed;
        const QFileInfoList entries = QDir(from).entryInfoList(
            QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : entries) {
            const auto result = copyNode(entry.filePath(), QDir(to).filePath(entry.fileName()), control);
            if (result != FileOps::Result::Done) return result;
        }
        return FileOps::Result::Done;
    }
    if (!info.isFile()) return FileOps::Result::Failed;

    QFile input(from), output(to);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return FileOps::Result::Failed;
    while (!input.atEnd()) {
        if (cancelled(control)) { output.close(); return FileOps::Result::Cancelled; }
        const QByteArray buffer = input.read(1024 * 1024);
        if (buffer.isEmpty() && input.error() != QFileDevice::NoError) {
            output.close(); return FileOps::Result::Failed;
        }
        qint64 offset = 0;
        while (offset < buffer.size()) {
            const qint64 written = output.write(buffer.constData() + offset, buffer.size() - offset);
            if (written <= 0) { output.close(); return FileOps::Result::Failed; }
            offset += written;
        }
        if (control) control->bytesCurrent += buffer.size();
    }
    if (input.error() != QFileDevice::NoError || !output.flush()) {
        output.close(); return FileOps::Result::Failed;
    }
    output.setFileTime(info.lastModified(), QFileDevice::FileModificationTime);
    output.setPermissions(info.permissions());
    output.close();
    return output.error() == QFileDevice::NoError ? FileOps::Result::Done : FileOps::Result::Failed;
}
} // namespace

qint64 WindowsFileOperations::treeSize(const QString &path) const
{
    const QFileInfo root(path);
    if (!root.exists() || root.isSymLink()) return 0;
    if (root.isFile()) return root.size();
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); const QFileInfo info = it.fileInfo(); if (!info.isSymLink()) total += info.size(); }
    return total;
}

bool WindowsFileOperations::sameFilesystem(const QString &path, const QString &directory) const
{
    const QFileInfo source(path);
    const QString sourceDir = source.isDir() && !source.isSymLink() ? source.absoluteFilePath() : source.absolutePath();
    const QStorageInfo a(sourceDir), b(directory);
    return a.isValid() && b.isValid() && !a.device().isEmpty() && a.device() == b.device();
}

bool WindowsFileOperations::removeTree(const QString &path) const
{
    const QFileInfo info(path);
    if (info.isDir() && !info.isSymLink()) return QDir(path).removeRecursively();
    return QFile::remove(path);
}

FileOps::Result WindowsFileOperations::copyTree(const QString &from, const QString &to,
                                                 FileOps::Control *control) const
{
    if (!supportedTree(from) || pathExists(to)) return FileOps::Result::Failed;
    if (cancelled(control)) return FileOps::Result::Cancelled;
    const QString parent = QFileInfo(to).absolutePath();
    if (!QDir().mkpath(parent)) return FileOps::Result::Failed;
    QString temporary;
    do {
        temporary = QDir(parent).filePath(QStringLiteral(".") + QFileInfo(to).fileName()
            + QStringLiteral(".alveus-copy-") + QUuid::createUuid().toString(QUuid::Id128).left(12));
    } while (pathExists(temporary));
    const auto result = copyNode(from, temporary, control);
    if (result != FileOps::Result::Done) { removeTree(temporary); return result; }
    const QFileInfo tmp(temporary);
    const bool renamed = tmp.isDir() && !tmp.isSymLink()
        ? QDir(parent).rename(tmp.fileName(), QFileInfo(to).fileName()) : QFile::rename(temporary, to);
    if (!renamed) { removeTree(temporary); return FileOps::Result::Failed; }
    return FileOps::Result::Done;
}

FileOps::Result WindowsFileOperations::movePath(const QString &from, const QString &to,
                                                 FileOps::Control *control) const
{
    const QFileInfo source(from);
    if (!pathExists(from) || pathExists(to) || QDir::cleanPath(from) == QDir::cleanPath(to))
        return FileOps::Result::Failed;
    if (!QDir().mkpath(QFileInfo(to).absolutePath())) return FileOps::Result::Failed;
    const bool renamed = source.isDir() && !source.isSymLink()
        ? QDir().rename(from, to) : QFile::rename(from, to);
    if (renamed) return FileOps::Result::Done;
    const auto copied = copyTree(from, to, control);
    if (copied != FileOps::Result::Done) return copied;
    return removeTree(from) ? FileOps::Result::Done : FileOps::Result::SourceLeft;
}
} // namespace Alveus::Platform
