#!/usr/bin/env bash
# Build the tetra .deb (debian/) from a clean copy of the sources (tracked
# files, submodules and new files not ignored by git, as they are in the
# work tree) under build/deb, so the work tree gets no build products.
#
# Usage: scripts/build-deb.sh [--release] [--install-deps]
#   --release       keep debian/changelog's version; otherwise the package is
#                   a snapshot, <version>+git<UTC date and time>.<commit>, so
#                   each build upgrades the previous one
#   --install-deps  install missing build dependencies (sudo apt build-dep)
#
# Then: sudo apt install ./build/deb/tetra_<version>_<arch>.deb
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$root/build/deb"
release=0
install_deps=0
for arg in "$@"; do
    case "$arg" in
        --release) release=1 ;;
        --install-deps) install_deps=1 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

version="$(dpkg-parsechangelog -l "$root/debian/changelog" -S Version)"
if ((!release)); then
    version+="+git$(date -u +%Y%m%d%H%M).$(git -C "$root" rev-parse --short HEAD)"
fi
if [[ -n "$(git -C "$root" status --porcelain --ignore-submodules=none)" ]]; then
    echo "note: building uncommitted changes too" >&2
fi

# The copy: tracked files (with the submodules' own) plus new, unignored ones.
src="$out/src/tetra-$version"
rm -rf "$out/src" && mkdir -p "$src"
{
    git -C "$root" ls-files -z --recurse-submodules
    git -C "$root" ls-files -z --others --exclude-standard
} | tar -C "$root" --null --files-from=- --ignore-failed-read -cf - | tar -x -C "$src"
sed -i "1s/([^)]*)/($version)/" "$src/debian/changelog"

cd "$src"
if ! dpkg-checkbuilddeps 2>/dev/null; then
    if ((install_deps)); then
        sudo apt build-dep -y "$src"
    else
        dpkg-checkbuilddeps || true
        echo "Install them with: $0 --install-deps" >&2
        exit 1
    fi
fi

export DEB_BUILD_OPTIONS="parallel=$(nproc) ${DEB_BUILD_OPTIONS:-}"
dpkg-buildpackage -b -us -uc

# Results next to the copy; keep only this build's.
rm -f "$out"/tetra*_*.{deb,ddeb,buildinfo,changes}
mv "$out/src"/tetra*_"$version"_*.{deb,ddeb,buildinfo,changes} "$out/" 2>/dev/null || true
cd "$out"
rm -rf "$out/src"

changes=("$out"/tetra_"$version"_*.changes)
if command -v lintian >/dev/null; then
    echo "==> lintian"
    lintian "${changes[0]}" || true
fi
echo "==> built:"
ls -1 "$out"/*.deb
echo "Install with: sudo apt install $(ls "$out"/tetra_"$version"_*.deb)"
