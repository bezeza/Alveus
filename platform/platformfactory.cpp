/*
 * Alveus — platform backend factory
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "platformfactory.h"
#if defined(Q_OS_WIN)
#include "windows/windowsfileoperations.h"
#elif defined(Q_OS_LINUX)
#include "linux/linuxfileoperations.h"
#else
#error "Alveus currently supports Linux and Windows platform backends only."
#endif

namespace Alveus::Platform {
std::unique_ptr<IFileOperations> createFileOperations()
{
#if defined(Q_OS_WIN)
    return std::make_unique<WindowsFileOperations>();
#elif defined(Q_OS_LINUX)
    return std::make_unique<LinuxFileOperations>();
#endif
}
} // namespace Alveus::Platform
