#!/usr/bin/env bash
# Build the vendored C dependencies (third_party/*) into .deps/prefix.
#
# Debian 13 ships older wayland/libdrm/pixman/xkbcommon/wayland-protocols than
# wlroots 0.20 needs, so we build pinned versions ourselves. tetra binaries find
# them through RPATH; the system copies are left untouched. Packages
# (TETRA_BUNDLE_DEPS) ship the same libraries in a private directory.
#
# Usage: scripts/build-deps.sh [--clean] [--debug] [--dir DIR]
#   --dir DIR   build in DIR/{src,build,prefix} instead of .deps (packaging)
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
deps="$root/.deps"
buildtype=release
clean=0

while (($#)); do
    case "$1" in
        --clean) clean=1 ;;
        --debug) buildtype=debugoptimized ;;
        --dir) deps="$(realpath -m "${2:?--dir needs a directory}")"; shift ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done
((clean)) && rm -rf "$deps"
prefix="$deps/prefix"
builddir="$deps/build"

if [[ ! -f "$root/third_party/wlroots/meson.build" ]]; then
    git -C "$root" submodule update --init --recursive
fi

export PKG_CONFIG_PATH="$prefix/lib/pkgconfig:$prefix/share/pkgconfig:$prefix/lib/x86_64-linux-gnu/pkgconfig"
export PATH="$prefix/bin:$PATH"
# Every vendored lib resolves its own deps next to itself first, so e.g.
# libwlroots never picks up the older system libdrm, wherever they are
# installed (.deps/prefix/lib, or a package's private directory).
export LDFLAGS="-Wl,-rpath,\$ORIGIN ${LDFLAGS:-}"

# Dependencies with patches in patches/<name>/ are built from a patched
# copy in <dir>/src/<name>; the submodule checkout itself stays pristine.
source_dir() {
    local name="$1"
    local patches=("$root/patches/$name"/*.patch)
    if [[ ! -e "${patches[0]}" ]]; then
        echo "$root/third_party/$name"
        return
    fi
    local dest="$deps/src/$name"
    rm -rf "$dest" && mkdir -p "$dest"
    tar -C "$root/third_party/$name" --exclude=.git -cf - . | tar -x -C "$dest"
    for patch in "${patches[@]}"; do
        patch -d "$dest" -p1 --quiet --forward <"$patch" >&2
    done
    echo "$dest"
}

build() {
    local name="$1"; shift
    echo "==> $name"
    local src
    src="$(source_dir "$name")"
    if [[ "$src" != "$root/third_party/$name" ]]; then
        rm -rf "${builddir:?}/$name" # source tree was re-exported
    fi
    meson setup "$builddir/$name" "$src" \
        --prefix "$prefix" --libdir lib --buildtype "$buildtype" \
        --wrap-mode nofallback --reconfigure "$@" >/dev/null
    meson compile -C "$builddir/$name" >/dev/null
    meson install -C "$builddir/$name" --quiet >/dev/null
}

build wayland -Dtests=false -Ddocumentation=false -Ddtd_validation=false
build wayland-protocols -Dtests=false
# Only the core libdrm.so; GPU-specific helpers come from the system Mesa stack.
build libdrm -Dtests=false -Dman-pages=disabled -Dvalgrind=disabled \
    -Dcairo-tests=disabled -Damdgpu=disabled -Dintel=disabled \
    -Dradeon=disabled -Dnouveau=disabled -Dvmwgfx=disabled \
    -Domap=disabled -Dexynos=disabled -Dfreedreno=disabled \
    -Dtegra=disabled -Dvc4=disabled -Detnaviv=disabled
build pixman -Dtests=disabled -Ddemos=disabled -Dgtk=disabled -Dlibpng=disabled
# Keymaps and compose tables come from the system xkb-data package.
build libxkbcommon -Denable-docs=false -Denable-tools=false \
    -Denable-x11=false -Denable-bash-completion=false \
    -Dxkb-config-root=/usr/share/X11/xkb -Dx-locale-root=/usr/share/X11/locale
build wlroots -Dexamples=false -Drenderers=gles2 -Dbackends=drm,libinput \
    -Dallocators=gbm -Dxwayland=enabled -Dsession=enabled \
    -Dcolor-management=enabled -Dlibliftoff=enabled

echo "deps installed to $prefix"
