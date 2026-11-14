# Alveus diagnostics

## What is recorded

Alveus records:
- Qt warnings, critical and fatal messages, including QML runtime warnings
  delivered through Qt's message system;
- errors emitted by Alveus' existing application message signals;
- the application version at startup.

Routine Qt debug output and successful file operations are not recorded.

## Location and retention

The log is stored under Qt's generic data directory:

`<XDG_DATA_HOME>/alveus/logs/alveus.log`

On a typical Linux desktop:

`~/.local/share/alveus/logs/alveus.log`

The current log is limited to approximately 1 MiB. Up to three rotated files
(`.1`, `.2`, `.3`) are retained.

Open **Alveus Settings → Diagnostics** to inspect recent entries, open the log
folder, or copy a diagnostic report.

## Privacy

The home directory, Alveus desktop directory, and settings-file path are
redacted when known. Third-party messages can still include file names or paths
elsewhere on the system. Review a copied report before sharing it. File contents
are never recorded by this logger.


## External helper process deadlines

Alveus bounds helper processes so a stuck command cannot hold a worker slot or
block future checks indefinitely. Timeout/failure messages are recorded in the
local diagnostics log; no process output is uploaded automatically.

| Helper | Deadline | Failure policy |
|---|---:|---|
| `kdeconnect-cli --list-available` | 5 s | Keep the last known phone list and record a warning; a later refresh can retry |
| `pw-dump` screen-share probe | 5 s | Fail closed: treat the screen as shared and hide fence contents until a successful probe. Poll backoff grows from 4 s to at most 60 s, then resets on success |
| `lsblk` drive discovery | 10 s | Keep the last successful drive list and record a warning. Refresh backoff grows from 15 s to at most 60 s, then resets on success |
| `udisksctl mount/unmount` | 30 s | Kill the helper, report the operation timeout, and refresh discovery. No automatic retry: a timed-out mount operation may already have partially completed |
| Video/PDF thumbnail helpers | 30 s | Kill the helper, release its worker slot and temporary directory, and allow an on-demand retry after a 30 s cooldown for the same file stamp |

A helper timeout is not interpreted as a successful empty result. Screen-share
detection is deliberately privacy-conservative; drive and phone discovery keep
their last successful data and log that it may be stale. User-requested mount
and unmount operations are bounded but not replayed automatically.
