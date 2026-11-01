/*
 * Alveus — Linux file operation adapter
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Keep the existing 0.03.1 POSIX implementation unchanged behind this contract.
 */
#include "linuxfileoperations.h"

namespace Alveus::Platform {
qint64 LinuxFileOperations::treeSize(const QString &path) const { return FileOps::treeSize(path); }
bool LinuxFileOperations::sameFilesystem(const QString &path, const QString &directory) const
{ return FileOps::sameFilesystem(path, directory); }
FileOps::Result LinuxFileOperations::copyTree(const QString &from, const QString &to,
                                               FileOps::Control *control) const
{ return FileOps::copyTree(from, to, control); }
FileOps::Result LinuxFileOperations::movePath(const QString &from, const QString &to,
                                               FileOps::Control *control) const
{ return FileOps::movePath(from, to, control); }
bool LinuxFileOperations::removeTree(const QString &path) const { return FileOps::removeTree(path); }
} // namespace Alveus::Platform
