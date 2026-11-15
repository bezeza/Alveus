#!/usr/bin/env bash
set -euo pipefail

PACKAGE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_ID="alveus-focus-deck"
PACKAGE_TYPE="KWin/WindowSwitcher"

if ! command -v kpackagetool6 >/dev/null 2>&1; then
    printf 'Error: kpackagetool6 was not found. Install the KDE KPackage tools first.\n' >&2
    exit 1
fi

if kpackagetool6 --type "$PACKAGE_TYPE" --list | grep -Fxq "$PACKAGE_ID"; then
    kpackagetool6 --type "$PACKAGE_TYPE" --upgrade "$PACKAGE_DIR"
else
    kpackagetool6 --type "$PACKAGE_TYPE" --install "$PACKAGE_DIR"
fi

printf '\nAlveus Focus Deck is installed.\n'
printf 'Select it in System Settings → Window Management → Task Switcher.\n'
printf 'KWin settings and shortcuts were not changed.\n'
