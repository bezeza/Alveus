#!/usr/bin/env bash
# Build an RPM of Alveus on Fedora from this folder, without COPR.
# Збирає RPM-пакет Alveus на Fedora з цієї теки, без COPR.
#
#   ./packaging/build-rpm.sh          # then: sudo dnf install ~/rpmbuild/RPMS/*/alveus-*.rpm
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/.."

spec=packaging/alveus.spec
version="$(sed -n 's/^Version:[[:space:]]*//p' "$spec")"

echo "==> Tools and build dependencies (needs your password) / Інструменти й залежності"
sudo dnf install -y rpm-build rpmdevtools dnf-plugins-core
sudo dnf builddep -y "$spec"

rpmdev-setuptree
src="$HOME/rpmbuild/SOURCES/alveus-plasma-$version.tar.gz"
echo "==> Packing sources / Пакую вихідний код: $src"
tar --exclude=./build --exclude=./.git --transform "s,^\.,alveus-plasma-$version," -czf "$src" .

echo "==> Building / Збираю"
rpmbuild -ba "$spec"

echo
echo "Done. Install with / Встановити:"
ls "$HOME"/rpmbuild/RPMS/*/alveus-"$version"-*.rpm | sed 's/^/  sudo dnf install /'
