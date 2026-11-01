#!/usr/bin/env bash
# Alveus — remove. Your files stay where they are (in the Desktop folder).
# Видалення Alveus. Файли залишаються на місці (у теці «Стільниця»).
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"

# 1. make sure no desktop still uses Alveus
./recover.sh

# 2. remove installed files
if [[ -f build/install_manifest.txt ]]; then
    xargs -d '\n' sudo rm -fv < build/install_manifest.txt
    # leftover empty folders of the package and the QML module
    pkg="$(grep -m1 -o '^.*/plasmoids/ua\.bezeza\.alveus/' build/install_manifest.txt || true)"
    [[ -n "$pkg" ]] && sudo rm -rf "$pkg"
    mod="$(grep -m1 -o '^.*/ua/bezeza/alveus/' build/install_manifest.txt || true)"
    [[ -n "$mod" ]] && sudo rm -rf "$mod" && sudo rmdir --ignore-fail-on-non-empty "${mod%alveus/}" "${mod%bezeza/alveus/}" 2>/dev/null || true
else
    echo "build/install_manifest.txt not found — run this from the folder you installed from."
    echo "Не знайдено build/install_manifest.txt — запустіть з теки, з якої встановлювали."
    exit 1
fi

echo
echo "Alveus removed. Fence layout is kept in ~/.local/share/alveus/ (delete it if you like)."
echo "Alveus видалено. Розкладка парканів лишилася в ~/.local/share/alveus/ (можна видалити)."
