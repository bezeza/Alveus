#!/usr/bin/env bash
# Build an RPM of Alveus on Fedora from this folder, without COPR.
# Збирає RPM-пакет Alveus на Fedora з цієї теки, без COPR.
#
#   ./packaging/build-rpm.sh              # build only
#   ./packaging/build-rpm.sh --install    # build and install through DNF
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/.."

usage() {
    cat <<'EOF'
Usage: ./packaging/build-rpm.sh [--install]

Build an Alveus RPM on Fedora.
  --install  Install the newly built RPM with DNF after the build.
  --help     Show this help.

Without --install, the script only builds the RPM and prints the DNF command.
EOF
}

if (( $# > 1 )); then
    usage >&2
    printf 'Error: only one option is supported.\n' >&2
    exit 2
fi

install_after_build=0
case "${1:-}" in
    "")
        ;;
    --install)
        install_after_build=1
        ;;
    --help|-h)
        usage
        exit 0
        ;;
    *)
        usage >&2
        printf 'Error: unknown option: %s\n' "$1" >&2
        exit 2
        ;;
esac

spec=packaging/alveus.spec
version="$(sed -n 's/^Version:[[:space:]]*//p' "$spec")"
if [[ -z "$version" ]]; then
    printf 'Error: could not read Version from %s\n' "$spec" >&2
    exit 1
fi

echo "==> Tools and build dependencies (needs your password) / Інструменти й залежності"
sudo dnf install -y rpm-build rpmdevtools dnf-plugins-core
sudo dnf builddep -y "$spec"

rpmdev-setuptree
src="$HOME/rpmbuild/SOURCES/alveus-plasma-$version.tar.gz"
echo "==> Packing sources / Пакую вихідний код: $src"
tar --exclude=./build --exclude=./.git --transform "s,^\.,alveus-plasma-$version," -czf "$src" .

echo "==> Building / Збираю"
rpmbuild -ba "$spec"

rpm_root="$HOME/rpmbuild/RPMS"
if [[ ! -d "$rpm_root" ]]; then
    printf 'Error: RPM output directory was not created: %s\n' "$rpm_root" >&2
    exit 1
fi

mapfile -t built_rpms < <(
    find "$rpm_root" -type f -name "alveus-$version-*.rpm" -printf '%T@ %p\n' |
        sort -nr |
        cut -d' ' -f2-
)
if (( ${#built_rpms[@]} == 0 )); then
    printf 'Error: no alveus RPM was produced under %s\n' "$rpm_root" >&2
    exit 1
fi

rpm_file="${built_rpms[0]}"
printf '\nBuilt RPM / Зібраний RPM: %s\n' "$rpm_file"

if (( install_after_build )); then
    echo "==> Installing with DNF (DNF will resolve runtime dependencies)"
    sudo dnf install "$rpm_file"
    echo
    echo "Alveus is installed. To activate it, choose Layout: Alveus in Configure Desktop and Wallpaper."
else
    echo
    echo "Install with / Встановити:"
    printf '  sudo dnf install %q\n' "$rpm_file"
    echo "Or run this script again with --install."
fi
