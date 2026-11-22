# tetra — design

A sleek, minimal, lightweight Linux desktop environment: one Wayland
compositor, one shell, a handful of core apps — all with the same look, the
same controls and one configuration. Users should not need to assemble their
desktop from pieces of other projects.

This document records decisions. Change it when a decision changes.

## Cornerstones

| Area | Decision |
|---|---|
| Display server | Wayland compositor built on **wlroots** (pinned, vendored; currently 0.20.2) |
| UI | **Dear ImGui** for the shell and every tetra app, through our own toolkit library |
| Language | **C++23** (GCC 14 / Clang 19). No C++20 modules until CMake + GCC support matures |
| Configuration | **JSON** (comments allowed), one tree under `~/.config/tetra/` |
| Scripting | None for now |
| Modularity | Not a goal. Unified look, controls and configuration win over pluggability |
| License | MIT (same as wlroots and ImGui) |

## Look and feel

Windows-like rather than macOS-like:

- **No top bar.** A single bottom bar: launcher and task slots centred, tray on the right.
- **Floating, stacking window management:** snapping to halves/quarters,
  Super+arrows, Alt+Tab, minimise to the bar.
- **Server-side decorations.** The compositor draws title bars for every window
  so they match. (GTK4/libadwaita apps will keep drawing their own.)
- Minimal, dark-first theme; a single accent colour.

### Task bar: F-key slots

- **12 slots in 3 groups of 4**, visually separated like a keyboard's F-row,
  each showing a small F-number.
- **Super+F*n*** activates slot *n*. Bare F-keys are an opt-in config option
  (they clash with F2/F5/F11 in apps, and many laptops send media keys).
- **Placement**
  - All windows of an app (same `app_id`) stack in one slot, pinned or not.
  - A new app takes the first free slot.
  - With no free slot, it joins the **rightmost unpinned slot** (the overflow
    stack); other unpinned slots stay single-app and predictable.
  - **At most 11 pins**, so an overflow slot always exists.
  - **Nothing moves while open.** Slots are only (re)assigned when a window
    opens, never by rearranging existing icons.
- **Activation**
  - Empty pinned slot: launch the app.
  - Stack: focus its most recent window; pressing again cycles; on a single
    focused window it minimises.
  - **Super+Shift+F*n***: new instance.
  - Hold Super+F*n* or click: window list popup for that slot.
- A mixed stack shows its most recently used app's icon with a window-count badge.

### Tray (right side)

Clock/calendar, battery (UPower), network (NetworkManager), volume (PipeWire),
Bluetooth (BlueZ), notifications, plus **StatusNotifierItem** icons for
third-party apps (Steam, Discord, …).

## Architecture

```
compositor (tetra)  wlroots, window management, decorations, input, outputs
   │  private Wayland protocol (toplevel list, F-key slots, OSD, shell commands)
shell               layer-shell client: task bar, launcher, tray, notifications, OSD, lock screen
apps/               settings, terminal, system monitor
portal              xdg-desktop-portal backend (file chooser, screenshot, screencast)
libtoolkit          Wayland client platform + ImGui + theme + shared widgets + event loop
libcore             config, D-Bus (sdbus-c++), logging, .desktop / MIME / icon lookup
```

- **The shell is a separate process.** A shell crash must not take windows down;
  the compositor restarts it.
- **One private protocol** between shell and compositor rather than a patchwork
  of `wlr-foreign-toplevel` and friends — we own both ends.
- **libtoolkit** is where the unified look lives. ImGui has no native Wayland
  backend and SDL/GLFW cannot do layer-shell, so we write our own: libwayland-client,
  EGL, ImGui's OpenGL 3 renderer (GLES 3.0 is enough), plus `wp-fractional-scale-v1`,
  `text-input-v3`, `cursor-shape-v1`, `wl_data_device`.
- **Battery:** ImGui redraws every frame by default. libtoolkit renders only
  on input or state changes (a few frames after each, for ImGui to settle),
  continuously only during animations, and paces with `wl_surface.frame`.

### Known ImGui limitations (accepted)

- No complex text shaping or RTL (Arabic, Hebrew, Indic). HarfBuzz later if needed.
- No accessibility tree. AccessKit (C bindings, AT-SPI) is the candidate later.
- Fonts: ImGui ≥ 1.92 dynamic font atlas + FreeType for quality and HiDPI.

### Foreign apps

GTK/Qt apps will not look like tetra. We narrow the gap by exporting accent
colour and dark/light preference (portal `color-scheme`, GTK/Qt settings), and
by implementing the portal **FileChooser**, so GTK, Qt and Firefox open
tetra's file dialog. Xwayland is supported for legacy X11 apps.

## Configuration

- JSON with comments, under `~/.config/tetra/`, one schema, validated on load.
- Live reload (inotify): compositor, shell and apps watch the files they use.
- The Settings app is an editor for these files — never a second source of truth.
- Library: nlohmann/json or glaze (typed structs); to be decided when libcore lands.

## Scope

**In:** compositor, shell (bar, launcher with search, notifications daemon,
volume/brightness OSD, lock screen via `ext-session-lock`, idle/blanking via
`ext-idle-notify`, polkit agent, screenshot tool, clipboard), session (greetd
greeter, XDG autostart, logind power actions), Settings (displays, input,
shortcuts, network, Bluetooth, sound, power, appearance, default apps),
terminal (libvterm), system monitor, portal backend.

**Deferred:** text editor, file manager, image viewer.

**Out:** browser, office suite, media player.

## Dependencies

Debian 13 is the development baseline. Its wayland, libdrm, pixman, xkbcommon
and wayland-protocols are older than wlroots 0.20 needs, so those and wlroots
itself are **git submodules pinned to release tags** under `third_party/`,
built by `scripts/build-deps.sh` into `.deps/prefix`. Binaries use DT_RPATH to
that prefix so the whole process uses one consistent copy of each library.
wlroots' own meson wraps are not used: they track `HEAD`.

| Submodule | Pin |
|---|---|
| wayland | 1.26.0 |
| wayland-protocols | 1.50 |
| libdrm | 2.4.134 (core library only) |
| pixman | 0.46.4 |
| libxkbcommon | 1.13.2 (uses system xkb-data) |
| wlroots | 0.20.2 (gles2 renderer, drm+libinput backends, Xwayland) |
| imgui | v1.93.0 |

### wlroots from C++

wlroots headers use C99 `[static N]` array parameters and fields named
`class`, `namespace`, `delete`. `src/compositor/wlr.hpp` is the only place
wlroots headers are included: it pre-includes their non-wlroots dependencies,
then neutralises those constructs with macros while the wlroots headers are
parsed. The fields become `class_`, `namespace_`, `delete_`.

Signal handling goes through `tetra::Listener` (RAII `wl_listener` with a C++
callback). wlroots asserts that no listeners remain on objects it destroys, so
teardown order in `Server::~Server` is deliberate.

## Development workflow

- VirtualBox VM, Debian 13, VMSVGA + 3D (Mesa SVGA3D, GLES 3.0).
- **Headless loop:** `scripts/run-headless.sh` runs tetra on wlroots' headless
  backend with a private runtime dir, launches clients, and screenshots with
  `grim` — no display needed, works over SSH, and is the basis for screenshot
  regression tests in CI.
- **Real session:** from a spare TTY, or later through a `wayland-sessions`
  entry in GDM. GDM stays until tetra has its own greetd greeter.
- Presets: `debug` (GCC, -Werror), `clang`, `asan` (ASan + UBSan), `release`.

## Roadmap

0. **Infrastructure** — repo, vendored deps, build, compositor skeleton,
   headless screenshot loop. *(done)*
1. **Compositor + toolkit MVP** — layer-shell, server-side decorations,
   JSON config, Xwayland; libtoolkit with event-driven ImGui; a bar with a clock.
2. **Task bar** — private protocol, F-key slots, launcher, basic tray.
3. **Shell services** — notifications, OSD, lock/idle, polkit agent, screenshots, SNI tray.
4. **Settings app.**
5. **Apps and portal** — terminal, system monitor, portal backend.
6. **Final pieces** — greeter, animations, polish.

## Open questions

- Config library: nlohmann/json vs glaze.
- Decoration rendering: ImGui inside the compositor's GL context vs plain scene
  rects + text textures.
- Overflow beyond 12 apps: is the single overflow stack enough in practice?
