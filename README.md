# tetra

A sleek, minimal, lightweight desktop environment for Linux: a wlroots-based
Wayland compositor with a Dear ImGui shell and core apps, unified in look,
controls and configuration. See [docs/design.md](docs/design.md).

Status: phase 2 — compositor with server-side decorations, layer-shell and
Xwayland; ImGui toolkit; `tetra-shell` with the F-key task bar (12 slots in
groups of four), app launcher, network/battery indicators and clock;
desktop notifications (`notify-send` works); volume control in the tray and
a volume/brightness OSD for the media keys; a lock screen (PAM) with idle
locking, screen blanking and locking before sleep; a polkit agent (pkexec and
friends ask for your password in a tetra dialog); screenshots (Print); app
icons in the tray (StatusNotifierItem, with their menus); one JSON configuration with live
reload ([docs/config.md](docs/config.md)). Phase 3 (shell services) done.

## Building (Debian 13)

```bash
git clone --recurse-submodules <repo> tetra && cd tetra
scripts/deps.sh            # apt packages (needs sudo)
scripts/build-deps.sh      # pinned wayland/libdrm/pixman/xkbcommon/wlroots -> .deps/prefix
cmake --preset debug
cmake --build --preset debug
```

Other presets: `clang`, `asan` (ASan + UBSan), `release`.

## Running

`tetra` starts `tetra-shell` itself (and restarts it if it crashes).

**Headless** (no display needed; screenshots via grim):

```bash
scripts/run-headless.sh -s 1920x1080 -- 'foot & xterm'
scripts/run-headless.sh -w 6 -- tests/headless/fkeys.sh
# -> build/screenshots/headless.png (+ .log)
scripts/run-tests.sh        # every scenario under ASan/UBSan
```

`tests/bench/pointer-load.sh` measures compositor and app CPU while the
pointer sweeps over an app (`-w 25`, see the script header);
`tetra-pointer-storm RATE SECONDS CX CY RADIUS` (in `build/<preset>/tests/tools`)
sends absolute pointer motion at a fixed rate, like a VM tablet or a 1000 Hz mouse.

**Nested** in an existing Wayland session (opens as a window):

```bash
build/debug/src/compositor/tetra -s foot
```

**On real hardware**: switch to a free TTY (Ctrl+Alt+F3), log in, then run
`build/debug/src/compositor/tetra -s foot`. Your user needs to be in the
`video`, `render` and `input` groups. To keep a log for debugging:
`build/debug/src/compositor/tetra 2> ~/tetra.log` (`-d` for verbose); frames
that take over 12 ms to render are logged as `slow frame`.

To pick tetra in GDM's session menu, add a session entry (needs root):

```bash
printf '[Desktop Entry]\nName=tetra (dev build)\nExec=%s\nType=Application\n' \
  "$PWD/build/debug/src/compositor/tetra" | sudo tee /usr/share/wayland-sessions/tetra-dev.desktop
```

## Using it

| Input | Action |
|---|---|
| Super (tap), or the logo on the bar | app launcher (type to search, Enter to start) |
| Super+F1…F12, or click a slot | launch the pinned app / focus it / cycle its windows / minimize |
| Super+Shift+F1…F12, or middle-click a slot | new instance of the slot's app |
| Drag the title bar | move (dragging a maximized window restores it) |
| Double-click the title bar | maximize / restore |
| Drag a window edge or corner | resize |
| Super+Up / Super+Down | maximize / restore or minimize |
| Alt+Tab | cycle windows (also brings back minimized ones) |
| Alt+F4 | close window |
| Super+Return | open a terminal (`terminal` in the config, foot by default) |
| Print | screenshot of the screen: saved to Pictures/Screenshots and copied |
| Shift+Print, Super+Shift+S | screenshot of an area (drag; Esc cancels) |
| Alt+Print | screenshot of the focused window |
| Super+L | lock the screen (also after `idle.lock_after`, before sleep, `loginctl lock-session`) |
| Super+Shift+Escape | quit tetra |
| Ctrl+Alt+F1…F12 | switch VT |
| Volume / mute / mic-mute keys | change the volume (OSD); also the wheel over the tray speaker |
| Brightness keys | change the backlight (OSD) |
| Click the tray speaker | volume popup: mute button and slider |

Pin apps to slots with `taskbar.pinned` in `~/.config/tetra/config.json`.

## Layout

```
src/core/         config, theme palette, desktop entries, process spawning (shared)
src/compositor/   the compositor (binary: tetra), incl. the task bar slot model
src/toolkit/      ImGui-on-Wayland toolkit for the shell and apps (incl. icons)
src/shell/        tetra-shell: bar, launcher, tray (incl. app icons), notifications, volume/OSD,
                  lock screen, idle, polkit agent, screenshots
data/             files installed alongside (PAM service for the lock screen)
protocols/        tetra's private Wayland protocol (compositor <-> shell)
scripts/          deps, vendored build, headless runner, test runner
tests/headless/   scripted headless scenarios
tests/tools/      tetra-sendkeys, tetra-pointer, tetra-tray-item, polkit-request, fake-polkit-helper
patches/          local patches to vendored dependencies
third_party/      pinned submodules (wlroots, wayland, imgui, ...)
docs/             design notes, configuration reference
```

## License

MIT — see [LICENSE](LICENSE).
