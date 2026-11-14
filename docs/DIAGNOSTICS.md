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
