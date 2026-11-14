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
    echo "Plasma config not found: $cfg" >&2
    exit 1
fi
if ! grep -q '^plugin=ua.bezeza.alveus$' "$cfg"; then
    echo "Alveus is not used as a desktop layout right now. Nothing to do."
    echo "Alveus зараз не використовується як робочий стіл. Нічого робити не треба."
    exit 0
fi

backup=""
tmp=""
plasma_stopped=0
restart_mode=""

restart_plasma() {
    case "$restart_mode" in
        systemctl)
            systemctl --user start plasma-plasmashell.service
            ;;
        kstart)
            kstart plasmashell >/dev/null 2>&1
            ;;
        *)
            return 1
            ;;
    esac
    plasma_stopped=0
}

cleanup() {
    local status=$?
    trap - EXIT INT TERM HUP

    if (( plasma_stopped )); then
        if ! restart_plasma; then
            echo "Warning: could not restart Plasma automatically. Start plasmashell manually." >&2
            if (( status == 0 )); then
                status=1
            fi
        fi
    fi

    if [[ -n "$tmp" && -e "$tmp" ]]; then
        if ! rm -f -- "$tmp"; then
            echo "Warning: could not remove temporary config: $tmp" >&2
            if (( status == 0 )); then
                status=1
            fi
        fi
    fi

    exit "$status"
}

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

# Stop the shell first so it cannot write its own copy over ours. Set the
# restart guard before attempting to stop it, so even a partial stop failure
# gets a corresponding restart attempt from the EXIT trap.
if systemctl --user is-active --quiet plasma-plasmashell.service 2>/dev/null; then
    restart_mode=systemctl
    plasma_stopped=1
    systemctl --user stop plasma-plasmashell.service
else
    restart_mode=kstart
    plasma_stopped=1
    kquitapp6 plasmashell >/dev/null 2>&1 || true
    sleep 1
fi

backup="$cfg.before-recover-$(date +%Y%m%d-%H%M%S)-$$"
cp -p -- "$cfg" "$backup"

# Keep the temporary file beside the live config: rename is then atomic on
# the same filesystem. Never redirect AWK output into the live config.
tmp="$(mktemp "${cfg}.tmp.XXXXXX")"

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
' "$backup" "$backup" > "$tmp"

# Refuse to replace the live config if the transformation did not produce a
# usable result or any Alveus containment reference remains.
if [[ ! -s "$tmp" ]]; then
    echo "Recovery produced an empty Plasma config; original file was kept." >&2
    exit 1
fi
if grep -q '^plugin=ua.bezeza.alveus$' "$tmp"; then
    echo "Recovery output still contains an Alveus plugin reference; original file was kept." >&2
    exit 1
fi

chmod --reference="$cfg" "$tmp"
mv -f -- "$tmp" "$cfg"
tmp=""

restart_plasma

echo "Back to the standard desktop. Backup of the old settings: $backup"
echo "Повернуто стандартний робочий стіл. Копія старих налаштувань: $backup"
