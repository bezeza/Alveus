/*
 * Alveus — bounded external helper process utilities
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QProcess>
#include <QTimer>
#include <QVariant>

namespace Alveus
{

inline bool processTimedOut(const QProcess *process)
{
    return process && process->property("alveusTimedOut").toBool();
}

// The QProcess is the timer context, so destroying the process also cancels
// the deadline callback. Killing is asynchronous; callers finish and clean up
// from QProcess::finished, guarded against FailedToStart/double completion.
inline void killAfterTimeout(QProcess *process, int timeoutMs)
{
    if (!process || timeoutMs <= 0) {
        return;
    }

    QTimer::singleShot(timeoutMs, process, [process]() {
        if (process->state() == QProcess::NotRunning) {
            return;
        }
        process->setProperty("alveusTimedOut", true);
        process->kill();
    });
}

} // namespace Alveus
