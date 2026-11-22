# tetra

A sleek, minimal, lightweight desktop environment for Linux: a wlroots-based
Wayland compositor with a Dear ImGui shell and core apps, unified in look,
controls and configuration. See [docs/design.md](docs/design.md).

Status: phase 0 — compositor skeleton (windows, focus, move/resize) and tooling.

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

**Headless** (no display needed; screenshots via grim):

```bash
scripts/run-headless.sh -s 1920x1080 -- 'foot & foot'
# -> build/screenshots/headless.png (+ .log)
```

**Nested** in an existing Wayland session (opens as a window):

```bash
build/debug/src/compositor/tetra -s foot
```

**On real hardware**: switch to a free TTY (Ctrl+Alt+F3), log in, then run
`build/debug/src/compositor/tetra -s foot`. Your user needs to be in the
`video`, `render` and `input` groups.

Development key bindings (temporary):

| Keys | Action |
|---|---|
| Super+Return | open a terminal (foot) |
| Alt+Tab | cycle windows |
| Super+Shift+Escape | quit tetra |
| Ctrl+Alt+F1…F12 | switch VT |

## Layout

```
src/compositor/   the compositor (binary: tetra)
scripts/          deps, vendored build, headless runner
third_party/      pinned submodules (wlroots, wayland, imgui, ...)
docs/             design notes
```

## License

MIT — see [LICENSE](LICENSE).
