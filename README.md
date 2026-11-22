# tetra

A sleek, minimal, lightweight desktop environment for Linux: a wlroots-based
Wayland compositor with a Dear ImGui shell and core apps, unified in look,
controls and configuration. See [docs/design.md](docs/design.md).

Status: phase 1 — compositor with server-side decorations, layer-shell and
Xwayland; ImGui toolkit; `tetra-shell` with a bottom bar and clock; one JSON
configuration with live reload ([docs/config.md](docs/config.md)).

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
scripts/run-headless.sh -w 6 -- tests/headless/reload-and-restart.sh
# -> build/screenshots/headless.png (+ .log)
```

**Nested** in an existing Wayland session (opens as a window):

```bash
build/debug/src/compositor/tetra -s foot
```

**On real hardware**: switch to a free TTY (Ctrl+Alt+F3), log in, then run
`build/debug/src/compositor/tetra -s foot`. Your user needs to be in the
`video`, `render` and `input` groups.

To pick tetra in GDM's session menu, add a session entry (needs root):

```bash
printf '[Desktop Entry]\nName=tetra (dev build)\nExec=%s\nType=Application\n' \
  "$PWD/build/debug/src/compositor/tetra" | sudo tee /usr/share/wayland-sessions/tetra-dev.desktop
```

## Using it

| Input | Action |
|---|---|
| Drag the title bar | move (dragging a maximized window restores it) |
| Double-click the title bar | maximize / restore |
| Drag a window edge or corner | resize |
| Super+Up / Super+Down | maximize / restore or minimize |
| Alt+Tab | cycle windows (also brings back minimized ones) |
| Alt+F4 | close window |
| Super+Return | open a terminal (foot) |
| Super+Shift+Escape | quit tetra |
| Ctrl+Alt+F1…F12 | switch VT |

These built-in bindings become configurable with the task bar (phase 2).

## Layout

```
src/core/         configuration, theme palette, file watching (shared)
src/compositor/   the compositor (binary: tetra)
src/toolkit/      ImGui-on-Wayland toolkit for the shell and apps
src/shell/        tetra-shell: bar (launcher, tray, ... later)
scripts/          deps, vendored build, headless runner
tests/headless/   scripted headless scenarios
patches/          local patches to vendored dependencies
third_party/      pinned submodules (wlroots, wayland, imgui, ...)
docs/             design notes, configuration reference
```

## License

MIT — see [LICENSE](LICENSE).
