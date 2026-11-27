#!/usr/bin/env bash
# Build the C dependencies the system lacks (third_party/*) into .deps/prefix.
#
# tetra needs wlroots 0.20. Where the system has it (Debian forky/sid, Arch),
# nothing is built and everything comes from the system. Otherwise wlroots is
# built here, together with each library it needs that the system has in a
# too old version: on Debian 13 wayland, wayland-protocols, libdrm, pixman
# and libxkbcommon (with trixie-backports only libdrm and pixman). tetra
# binaries find the built ones through RPATH; the system copies are left
# untouched. Packages (TETRA_BUNDLE_DEPS) ship them in a private directory.
#
# Usage: scripts/build-deps.sh [--clean] [--debug] [--dir DIR]
#   --dir DIR   build in DIR/{build,prefix} instead of .deps (packaging)
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

# Is the system's (not our prefix's) <module> at least <version>?
system_has() {
    env -u PKG_CONFIG_PATH pkg-config --atleast-version="$2" "$1" 2>/dev/null
}

# The prefix holds exactly this run's builds: a library the system now has
# new enough must not linger there.
rm -rf "$prefix"

if system_has wlroots-0.20 0.20.0; then
    echo "wlroots 0.20 and its dependencies come from the system; nothing to build"
    exit 0
fi

if [[ ! -f "$root/third_party/wlroots/meson.build" ]]; then
    git -C "$root" submodule update --init --recursive
fi

export PKG_CONFIG_PATH="$prefix/lib/pkgconfig:$prefix/share/pkgconfig:$prefix/lib/x86_64-linux-gnu/pkgconfig"
export PATH="$prefix/bin:$PATH"
# Every built lib resolves its own deps next to itself first, so e.g.
# libwlroots never picks up the older system libdrm, wherever they are
# installed (.deps/prefix/lib, or a package's private directory).
export LDFLAGS="-Wl,-rpath,\$ORIGIN ${LDFLAGS:-}"

build() {
    local name="$1"; shift
    echo "==> $name"
    mkdir -p "$builddir"
    local log="$builddir/$name.log"
    # Quiet unless a step fails; then its output says why.
    {
        meson setup "$builddir/$name" "$root/third_party/$name" \
            --prefix "$prefix" --libdir lib --buildtype "$buildtype" \
            --wrap-mode nofallback --reconfigure "$@" &&
            meson compile -C "$builddir/$name" &&
            meson install -C "$builddir/$name" --quiet
    } >"$log" 2>&1 || {
        tail -n 25 "$log" >&2
        echo "error: building $name failed; full output in $log" >&2
        exit 1
    }
}

# Minimum versions: wlroots 0.20's meson.build files (pixman: its renderer's).
if system_has wayland-server 1.24.0 && system_has wayland-scanner 1.24.0; then
    echo "--- wayland: system"
else
    build wayland -Dtests=false -Ddocumentation=false -Ddtd_validation=false
fi
if system_has wayland-protocols 1.47; then
    echo "--- wayland-protocols: system"
else
    build wayland-protocols -Dtests=false
fi
if system_has libdrm 2.4.129; then
    echo "--- libdrm: system"
else
    # Only the core libdrm.so; GPU-specific helpers come from the system Mesa stack.
    build libdrm -Dtests=false -Dman-pages=disabled -Dvalgrind=disabled \
        -Dcairo-tests=disabled -Damdgpu=disabled -Dintel=disabled \
        -Dradeon=disabled -Dnouveau=disabled -Dvmwgfx=disabled \
        -Domap=disabled -Dexynos=disabled -Dfreedreno=disabled \
        -Dtegra=disabled -Dvc4=disabled -Detnaviv=disabled
fi
if system_has pixman-1 0.46.0; then
    echo "--- pixman: system"
else
    build pixman -Dtests=disabled -Ddemos=disabled -Dgtk=disabled -Dlibpng=disabled
fi
if system_has xkbcommon 1.8.0; then
    echo "--- libxkbcommon: system"
else
    # Keymaps and compose tables come from the system xkb-data package.
    build libxkbcommon -Denable-docs=false -Denable-tools=false \
        -Denable-x11=false -Denable-bash-completion=false \
        -Dxkb-config-root=/usr/share/X11/xkb -Dx-locale-root=/usr/share/X11/locale
fi
build wlroots -Dexamples=false -Drenderers=gles2 -Dbackends=drm,libinput \
    -Dallocators=gbm -Dxwayland=enabled -Dsession=enabled \
    -Dcolor-management=enabled -Dlibliftoff=enabled

echo "deps installed to $prefix"
