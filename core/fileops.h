/*
 * Alveus — desktop fences for KDE Plasma 6
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * File operations that may take long (copies, moves to another disk). They
 * run on a worker thread so plasmashell — the whole desktop and the panels —
 * keeps responding while gigabytes go to a USB stick or an NTFS disk.
 *
 * Everything here is free of AlveusCore state and safe to call from any thread.
 */

#pragma once

#include <QString>

#include <atomic>

namespace Alveus::FileOps
{

// Shared between the worker and the GUI thread.
struct Control {
    std::atomic<bool> cancel{false};
    std::atomic<qint64> bytesTotal{0};
    std::atomic<qint64> bytesDone{0};   // finished tasks
    std::atomic<qint64> bytesCurrent{0}; // inside the running task
    std::atomic<int> index{0};          // running task
};

enum class Result {
    Done,
    Failed,
    Cancelled,
    SourceLeft, // moved by copying, but the original could not be removed
    Skipped,    // nothing to do (e.g. undo step already done)
};

// Size of a file or of everything inside a folder (symlinks not followed).
qint64 treeSize(const QString &path);

// True when `path` and folder `dir` are on the same filesystem, so a move is
// an instant rename.
bool sameFilesystem(const QString &path, const QString &dir);

// Copy a file or folder. `to` must not exist. Keeps permissions, times,
// extended attributes and symlinks (cp -a) where the target filesystem allows.
// The data-only fallback is limited to regular files and directories: symlinks
// must be recreated exactly or the operation fails. Special files are rejected.
// Copies into a hidden temporary sibling and renames at the end, so a failed or
// cancelled copy never leaves a half copy behind.
Result copyTree(const QString &from, const QString &to, Control *control = nullptr);

// Move: rename when possible, otherwise copyTree + remove the original.
Result movePath(const QString &from, const QString &to, Control *control = nullptr);

// Remove a file, a symlink, or a folder with everything inside.
bool removeTree(const QString &path);

} // namespace Alveus::FileOps
