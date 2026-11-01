/*
 * Alveus — platform-neutral file operation contract
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "core/fileops.h"

namespace Alveus::Platform {
class IFileOperations {
public:
    virtual ~IFileOperations() = default;
    virtual qint64 treeSize(const QString &path) const = 0;
    virtual bool sameFilesystem(const QString &path, const QString &directory) const = 0;
    virtual FileOps::Result copyTree(const QString &from, const QString &to,
                                     FileOps::Control *control = nullptr) const = 0;
    virtual FileOps::Result movePath(const QString &from, const QString &to,
                                     FileOps::Control *control = nullptr) const = 0;
    virtual bool removeTree(const QString &path) const = 0;
};
} // namespace Alveus::Platform
