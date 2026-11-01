#!/usr/bin/env bash
# Alveus — build and install (KDE Plasma 6)
# Встановлення Alveus: ставить залежності, збирає і встановлює.
# On Fedora you can instead build a proper package: ./packaging/build-rpm.sh
# На Fedora можна натомість зібрати справжній пакет: ./packaging/build-rpm.sh
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"

say() { printf '\n\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\n\033[1;33m!!\033[0m %s\n' "$*"; }

# ---- Plasma 6? ----------------------------------------------------------
if command -v plasmashell >/dev/null 2>&1; then
    ver="$(plasmashell --version 2>/dev/null | grep -oE '[0-9]+(\.[0-9]+)*' | head -1 || true)"
    if [[ -n "$ver" && "${ver%%.*}" -lt 6 ]]; then
        warn "Plasma $ver found. Alveus needs Plasma 6. / Знайдено Plasma $ver, а потрібна Plasma 6."
        exit 1
    fi
fi

# ---- dependencies ---------------------------------------------------------
. /etc/os-release
ids=" ${ID:-} ${ID_LIKE:-} "
say "Installing build dependencies / Встановлюю залежності для збірки"
if [[ "$ids" == *" arch "* ]]; then
    sudo pacman -S --needed --noconfirm base-devel cmake qt6-base qt6-declarative gettext
    sudo pacman -S --needed --noconfirm ffmpegthumbnailer poppler || true   # thumbnails (optional)
elif [[ "$ids" == *" fedora "* ]]; then
    sudo dnf install -y cmake gcc-c++ qt6-qtbase-devel qt6-qtdeclarative-devel gettext
    sudo dnf install -y ffmpegthumbnailer poppler-utils || true   # thumbnails (optional)
elif [[ "$ids" == *" suse "* || "$ids" == *" opensuse "* || "${ID:-}" == opensuse* ]]; then
    sudo zypper install -y cmake gcc-c++ qt6-base-devel qt6-declarative-devel gettext-tools
    sudo zypper install -y ffmpegthumbnailer poppler-tools || true   # thumbnails (optional)
elif [[ "$ids" == *" debian "* || "$ids" == *" ubuntu "* ]]; then
    sudo apt-get install -y build-essential cmake qt6-base-dev qt6-declarative-dev gettext
    sudo apt-get install -y ffmpegthumbnailer poppler-utils || true   # thumbnails (optional)
else
    warn "Unknown distribution. Install yourself: cmake, a C++20 compiler (GCC 11+), Qt 6 base + declarative development packages, gettext."
    warn "Невідомий дистрибутив. Встановіть самі: cmake, компілятор C++20 (GCC 11+), пакети розробки Qt 6 (base, declarative), gettext."
fi

# ---- the old 0.1 widget (same id) in the home folder would hide this one ----
old="${XDG_DATA_HOME:-$HOME/.local/share}/plasma/plasmoids/ua.bezeza.alveus"
if [[ -d "$old" ]]; then
    say "Removing the old Alveus 0.1 widget / Прибираю старий віджет Alveus 0.1"
    kpackagetool6 -t Plasma/Applet -r ua.bezeza.alveus >/dev/null 2>&1 || rm -rf "$old"
fi

# ---- build & install ------------------------------------------------------
say "Building / Збираю"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j"$(nproc)"

say "Installing (needs your password) / Встановлюю (потрібен пароль)"
sudo cmake --install build

# ---- restart the desktop shell so it sees the new module -------------------
say "Restarting Plasma desktop / Перезапускаю робочий стіл Plasma"
if systemctl --user is-active --quiet plasma-plasmashell.service 2>/dev/null; then
    systemctl --user restart plasma-plasmashell.service
else
    (kquitapp6 plasmashell >/dev/null 2>&1 || true) && sleep 1 && (kstart plasmashell >/dev/null 2>&1 &)
fi

cat <<'EOF'

Done! Turn it on:
  Right-click the desktop → "Configure Desktop and Wallpaper…" → Layout: Alveus → Apply.
  Do it on every monitor where you want fences.

Готово! Увімкніть:
  Правий клік на столі → «Налаштувати стільницю й шпалери…» → Компонування: Alveus → Застосувати.
  Зробіть це на кожному моніторі, де хочете паркани.

If anything goes wrong / Якщо щось піде не так:  ./recover.sh
EOF
