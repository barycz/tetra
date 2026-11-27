#!/usr/bin/env bash
# Install the Debian 13 (trixie) packages needed to build tetra and its
# vendored dependencies (see scripts/build-deps.sh). Safe to re-run.
set -euo pipefail

packages=(
    # toolchain
    build-essential cmake ninja-build meson pkg-config git
    clang clangd clang-format clang-tidy

    # wlroots' libraries: the system's where new enough, the rest is built
    # by build-deps.sh (needs libffi, expat, libxml2 and bison for that)
    libwayland-dev wayland-protocols libdrm-dev libxkbcommon-dev libpixman-1-dev
    libffi-dev libexpat1-dev libxml2-dev bison xkb-data

    # wlroots
    libudev-dev libinput-dev libseat-dev libdisplay-info-dev libliftoff-dev
    hwdata libgbm-dev libegl-dev libgles-dev liblcms2-dev

    # Xwayland support
    xwayland libxcb1-dev libxcb-composite0-dev libxcb-ewmh-dev
    libxcb-icccm4-dev libxcb-render0-dev libxcb-res0-dev libxcb-xfixes0-dev
    libxcb-errors-dev

    # compositor: decorations (title text), config
    libcairo2-dev libpango1.0-dev nlohmann-json3-dev

    # toolkit: fonts for ImGui, icons (PNG via cairo, SVG via librsvg)
    libfreetype-dev libfontconfig-dev fonts-inter librsvg2-dev
    adwaita-icon-theme hicolor-icon-theme

    # shell services: D-Bus (notifications, tray, polkit, brightness),
    # PAM (lock screen), PulseAudio API (volume, served by pipewire-pulse)
    libsdbus-c++-dev libpam0g-dev libnotify-bin libpulse-dev

    # apps: terminal emulation; the portal frontend tetra-portal serves
    libvterm-dev xdg-desktop-portal shared-mime-info libpipewire-0.3-dev

    # dev/test tools: screenshots, reference terminals, X11 test apps,
    # input injection, EGL info, fake PAM for lock screen tests, display
    # management client, a private sound server for audio tests
    # (run-headless.sh -a) and its command-line client
    grim foot xterm x11-apps wlrctl mesa-utils libpam-wrapper wl-clipboard
    wlr-randr pipewire pipewire-pulse wireplumber pulseaudio-utils
    # portal tests: a scripted portal client (Python GIO), a GTK 4 app
    python3-gi gir1.2-glib-2.0 zenity gstreamer1.0-pipewire gstreamer1.0-tools
    # packages (scripts/build-deb.sh)
    debhelper lintian
)

sudo apt-get update -qq
sudo apt-get install -y --no-install-recommends "${packages[@]}"
