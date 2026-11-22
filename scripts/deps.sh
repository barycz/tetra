#!/usr/bin/env bash
# Install the Debian 13 (trixie) packages needed to build tetra and its
# vendored dependencies (see scripts/build-deps.sh). Safe to re-run.
set -euo pipefail

packages=(
    # toolchain
    build-essential cmake ninja-build meson pkg-config git
    clang clangd clang-format clang-tidy

    # vendored libs: wayland, xkbcommon, pixman, libdrm
    libffi-dev libexpat1-dev libxml2-dev bison xkb-data

    # wlroots
    libudev-dev libinput-dev libseat-dev libdisplay-info-dev libliftoff-dev
    hwdata libgbm-dev libegl-dev libgles-dev liblcms2-dev

    # Xwayland support
    xwayland libxcb1-dev libxcb-composite0-dev libxcb-ewmh-dev
    libxcb-icccm4-dev libxcb-render0-dev libxcb-res0-dev libxcb-xfixes0-dev
    libxcb-errors-dev

    # dev/test tools: screenshots, a reference terminal, EGL info
    grim foot mesa-utils
)

sudo apt-get update -qq
sudo apt-get install -y --no-install-recommends "${packages[@]}"
