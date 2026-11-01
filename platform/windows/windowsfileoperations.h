/*
 * Alveus — Windows file operation adapter
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "platform/interfaces/fileoperations.h"

namespace Alveus::Platform {
class WindowsFileOperations final : public IFileOperations {
public:
    qint64 treeSize(const QString &path) const override;
    bool sameFilesystem(const QString &path, const QString &directory) const override;
    FileOps::Result copyTree(const QString &from, const QString &to,
                             FileOps::Control *control = nullptr) const override;
    FileOps::Result movePath(const QString &from, const QString &to,
                             FileOps::Control *control = nullptr) const override;
    bool removeTree(const QString &path) const override;
};
} // namespace Alveus::Platform
