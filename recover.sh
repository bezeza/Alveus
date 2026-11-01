#!/usr/bin/env bash
# Alveus — emergency switch back to the standard KDE desktop.
# Аварійне повернення до стандартного робочого столу KDE.
#
# Your files and Alveus fences are not touched: switching back to Alveus later
# brings the fences back. / Файли й паркани не змінюються: якщо потім знову
# увімкнути Alveus, паркани повернуться.
set -euo pipefail

cfg="${XDG_CONFIG_HOME:-$HOME/.config}/plasma-org.kde.plasma.desktop-appletsrc"
if [[ ! -f "$cfg" ]]; then
    echo "Plasma config not found: $cfg"
    exit 1
fi
if ! grep -q '^plugin=ua.bezeza.alveus$' "$cfg"; then
    echo "Alveus is not used as a desktop layout right now. Nothing to do."
    echo "Alveus зараз не використовується як робочий стіл. Нічого робити не треба."
    exit 0
fi

backup="$cfg.before-recover-$(date +%Y%m%d-%H%M%S)"

# Stop the shell first so it doesn't write its own copy over ours
if systemctl --user is-active --quiet plasma-plasmashell.service 2>/dev/null; then
    systemctl --user stop plasma-plasmashell.service
    restart() { systemctl --user start plasma-plasmashell.service; }
else
    kquitapp6 plasmashell >/dev/null 2>&1 || true
    sleep 1
    restart() { (kstart plasmashell >/dev/null 2>&1 &); }
fi

cp "$cfg" "$backup"
sed -i 's/^plugin=ua\.bezeza\.alveus$/plugin=org.kde.desktopcontainment/' "$cfg"
restart

echo "Back to the standard desktop. Backup of the old settings: $backup"
echo "Повернуто стандартний робочий стіл. Копія старих налаштувань: $backup"
