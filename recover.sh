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
# Two passes over the backup:
#  1. find Alveus added as a *widget* ([Containments][N][Applets][M] with our plugin)
#  2. write the config back: desktops ([Containments][N]) using Alveus become the
#     standard desktop; Alveus widgets are dropped with all their subsections
#     (turning them into a desktop *inside* a desktop would break Plasma).
awk '
    FNR == NR {
        if ($0 ~ /^\[.*\]$/) s = $0
        if ($0 == "plugin=ua.bezeza.alveus" && s ~ /^\[Containments\]\[[0-9]+\]\[Applets\]\[[0-9]+\]$/) widget[s] = 1
        next
    }
    $0 ~ /^\[.*\]$/ {
        s = $0; skip = 0
        for (w in widget) if (s == w || index(s, w "[") == 1) skip = 1
    }
    skip { next }
    $0 == "plugin=ua.bezeza.alveus" && s ~ /^\[Containments\]\[[0-9]+\]$/ { print "plugin=org.kde.desktopcontainment"; next }
    { print }
' "$backup" "$backup" > "$cfg"
restart

echo "Back to the standard desktop. Backup of the old settings: $backup"
echo "Повернуто стандартний робочий стіл. Копія старих налаштувань: $backup"
