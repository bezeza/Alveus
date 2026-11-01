/*
 * Alveus — platform backend factory
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "platform/interfaces/fileoperations.h"
#include <memory>

namespace Alveus::Platform {
std::unique_ptr<IFileOperations> createFileOperations();
} // namespace Alveus::Platform
