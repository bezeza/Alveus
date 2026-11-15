#!/usr/bin/env bash
set -euo pipefail

PACKAGE_ID="alveus-focus-deck"
PACKAGE_TYPE="KWin/WindowSwitcher"

if ! command -v kpackagetool6 >/dev/null 2>&1; then
    printf 'Error: kpackagetool6 was not found.\n' >&2
    exit 1
fi

kpackagetool6 --type "$PACKAGE_TYPE" --remove "$PACKAGE_ID"
printf 'Alveus Focus Deck was removed.\n'
printf 'If it was selected, choose another layout in Task Switcher settings.\n'
