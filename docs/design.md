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
  - **Super+Shift+F*n*** (or middle click): new instance.
  - Hold Super+F*n* or click: window list popup for that slot. *(not yet —
    needs popup surfaces in the toolkit; clicking cycles for now)*
- A mixed stack shows its most recently used app's icon with a window-count badge.
- Pins are desktop file IDs (`taskbar.pinned` in the config). Apps are
  matched to them by app_id, then StartupWMClass, so X11 apps (WM_CLASS
  `XTerm` → `debian-xterm`) group correctly.
- **Super tap** (press and release with nothing else) opens the launcher.

### Implementation (phase 2)

- The **compositor owns the slot model** (`src/compositor/taskbar.cpp`); key
  bindings and clicks end up in the same `Taskbar::activate/launch`.
- **`tetra-shell-unstable-v1`** (`protocols/`) publishes slot state to the
  shell (`slot` events + `done`) and carries `activate_slot` / `launch_slot`
  requests and the `show_launcher` event. A global filter offers it only to
  the shell process the compositor started (and offers `xwayland_shell_v1`
  only to Xwayland).
- The shell resolves icons through the desktop database and the icon theme
  (`ui::IconCache`: Adwaita → hicolor → pixmaps; PNG via cairo, SVG via
  librsvg; symbolic icons tinted with the text colour).
- Launcher: a full-output transparent overlay layer surface with exclusive
  keyboard focus; the panel sits above the bar. Search matches name, generic
  name, ID and keywords. Click outside or Escape closes it.
- Apps are started with `spawn_detached()` (double fork, new session), so
  they survive a shell restart.

### Tray (right side)

Clock/calendar, battery (UPower), network (NetworkManager), volume (PipeWire),
Bluetooth (BlueZ), notifications, plus **StatusNotifierItem** icons for
third-party apps (Steam, Discord, …).

Phase 2 reads network and battery state from sysfs (`/sys/class/net`,
`/sys/class/power_supply`) every 5 s. Volume arrived in phase 3 (left to
right: app icons, network, volume, battery). The UPower/NetworkManager
D-Bus services and Bluetooth follow.

App icons (StatusNotifierItem), also phase 3:
- **Watcher and host:** tetra-shell is `org.kde.StatusNotifierWatcher` and
  its only host. Items register by bus name or, libappindicator-style, by
  object path on their own connection. An item is dropped when its bus
  name goes away (NameOwnerChanged).
- **Async reads:** properties are read with GetAll asynchronously and
  re-read on any New* signal, so a hung app never blocks the bar.
- **Icons:** IconName (the app's IconThemePath first), else IconPixmap
  (ARGB → texture), else a generic icon. NeedsAttention uses the attention
  icon; Passive items are hidden.
- **Clicks:** left click calls Activate, or opens the menu for
  ItemIsMenu items and for apps without Activate. Right click opens the
  menu, or calls ContextMenu when there is no menu. Middle click calls
  SecondaryActivate; the wheel calls Scroll (±120 per notch).
- **Menus:** com.canonical.dbusmenu, read asynchronously (AboutToShow,
  then GetLayout, refetched on LayoutUpdated). They are drawn with ImGui
  menu items above the icon, with submenus, check marks, radio items,
  separators and disabled entries. A click sends the "clicked" event.
- **Not yet:** tooltips (the bar surface is only as tall as the bar), an
  overflow for many icons, the legacy XEmbed tray, and running beside
  another watcher (if one owns the name, app icons are off).

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

- One file, `~/.config/tetra/config.json`: JSON with comments, validated on
  load into typed structs (`src/core/config.hpp`, nlohmann/json). Reference:
  [config.md](config.md).
- Live reload (inotify on the directory, so atomic saves work): the compositor,
  shell and apps each watch the file. A file with errors is rejected and the
  previous configuration stays.
- Colours are never configured one by one: `theme` (dark/light, accent, font)
  is turned into a `theme::Palette` in one place (`src/core/theme.cpp`), used
  by both the compositor's decorations and the ImGui style.
- The Settings app is an editor for this file — never a second source of truth.
  `config::Document` (`src/core/config_edit.*`) edits the text in place: setting
  a key replaces only that value's text, or adds the member matching the
  surrounding indentation (inline objects stay inline). Comments, order and
  formatting elsewhere stay byte for byte. `config::edit_file` refuses to touch
  a file that does not load, never writes a result the loader would reject,
  and saves atomically (temporary file + rename). `merge_element` edits one
  object of an array by a key (`outputs` by `name`). Unit tests:
  `tests/unit/config_edit_test.cpp`.
- Displays are no exception. The compositor publishes heads, modes and the
  layout through wlr-output-management (Settings and wlr-randr read it), but
  refuses requests to apply or test a configuration: changes go into
  `outputs`. Settings' Displays page writes the entry for one connector
  (starting from the `"*"` entry if it had none) and asks to keep the
  change; after 15 s, or on Revert, it puts back the file as it was, unless
  something else changed it meanwhile.

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

### Local patches

Patches in `patches/<dep>/` are applied by `build-deps.sh` to an export of the
submodule in `.deps/src/`; the submodule checkout itself stays pristine.

- `wlroots/0001-skip-dmabuf-import-check-on-vmwgfx`: wlroots validates every
  client dmabuf by importing it into the DRM device and closing the handle;
  vmwgfx (VMware/VirtualBox) rejects the close, so *every* GPU client —
  Xwayland, Mesa EGL apps, our own toolkit — would be refused. The check is
  skipped on that driver only. Real GPUs are unaffected.
- `wlroots/0002-gbm-linear-only-buffers-are-linear`: on drivers without
  modifier support (vmwgfx), GBM accepts a LINEAR-only allocation but reports
  the modifier as INVALID; the cursor plane only lists LINEAR, so wlroots
  rejected every cursor buffer and drew the cursor in software (a frame
  behind the mouse: "floaty"). A buffer allocated from a LINEAR-only list is
  now recorded as LINEAR. In VirtualBox the hardware cursor is drawn by the
  host, like GNOME's.

## Compositor structure (phase 1)

- `View` (`XdgView`, `XwaylandView`) — one managed window. Content box in
  layout coordinates = the client's window geometry; decorations hang around it.
- `Decoration` — server-side title bar rendered with cairo + pango into a
  `wlr_buffer` (`CairoBuffer`), plus border strips and an invisible resize
  margin, all plain scene nodes. Hit-testing returns title / buttons / edges.
  Clients that speak xdg-decoration always get server-side decorations;
  clients without it (GTK4/libadwaita) draw their own.
- `LayerSurface` + `Output::arrange_layers()` — layer-shell placement and the
  usable area (what is left after exclusive zones such as the bar).
  Maximized windows fill the usable area, fullscreen windows the output.
  When the usable area shrinks (resolution, scale, a bar), normal windows
  are moved, and shrunk if needed, to stay inside it. When `outputs` moves a
  display in the layout, its windows move with it; windows are fitted only
  once every display is in place.
- **Window memory** (`src/core/window_memory.*`, `src/compositor/placement.cpp`):
  main windows (no parent, not modal) with an app_id open where they were.
  An app's open windows hold numbered slots (lowest free first), so two
  foot windows come back as two places. A slot keeps a place per display
  (content box relative to the display, size, maximized) and, per set of
  connected displays, the display it was on, so a window returns to the
  external monitor when it is plugged back in and to its laptop place when
  it is not. Displays are known by make, model and serial when the monitor
  reports a serial, else by connector. A place is recorded when a move or
  resize ends, on (un)maximize, when the client resizes itself and when the
  window closes; `~/.local/state/tetra/windows.json` is written a second
  after the last change and on exit. xdg-shell windows get the remembered
  size in their first configure; X11 windows are resized as they map, and
  keep a position they chose themselves. Places are fitted to the usable
  area. When the set of displays changes, open windows move to where they
  were with that set. The compositor rereads the file if someone else
  changes or deletes it.
- **Snap zones** (`src/core/zones.*`, `ZoneOverlay`, input.cpp): a layout
  is a list of zones as fractions of a display's usable area; built-ins plus
  `windows.zone_layouts`, chosen per display (`outputs[].zones`, else
  `windows.zones`). During an interactive move with Shift held (checked on
  motion and on modifier changes), the zones of the display under the
  pointer are drawn (cairo, one buffer, in a scene layer between windows and
  the top layer; it takes no input) and the zone under the pointer, plus any
  within 16 px, is the target; releasing the button there fits the frame
  into it. A snapped window keeps its previous size, which comes back when
  it is dragged out (as with maximized windows); resizing by hand forgets it.
  Super+Left/Right step through the zones in layout order, then to the
  display beside.
- Scene layers, bottom to top: background, bottom, windows, top, fullscreen,
  unmanaged (X11 menus), overlay.
- Every object that can be hit carries a `SceneDescriptor` in `node.data`;
  `Server::hit_test()` walks up from the node under the cursor.
- The compositor starts the shell (`"shell"` in the config) and restarts it
  when it exits, backing off and giving up after 5 quick exits.
- **Output frames are capped at the refresh rate.** vmwgfx (VirtualBox)
  reports page flips as complete immediately instead of at vblank, so
  wlroots requested the next frame at once: up to ~1800 frames/s, each
  sending frame callbacks, which made Firefox (software WebRender) repaint
  its page hundreds of times a second while hovered and starved input. A
  frame event sooner than ¾ of the refresh period is postponed to a timer.
  With real vblank it never triggers. `TETRA_STATS=1` logs frames, drawn,
  postponed, frame times, hardware/software cursor and pointer rates per second.
- **Pointer motion is coalesced**: the cursor image moves on every input
  event, but clients get at most one `wl_pointer.motion` per refresh of the
  output under the cursor (4–16.7 ms), always with the latest position.
  Buttons and scrolling flush pending motion first. Devices report at up to
  1000 Hz (VirtualBox's USB tablet follows the host mouse), and apps such as
  Firefox hit-test and repaint per event — that made hovering a web page
  saturate the VM's CPUs. Window move/resize grabs are not throttled.

## Toolkit structure (phase 1)

- `ui::App` — Wayland connection, globals, seat (pointer, keyboard with xkb and
  client-side key repeat), outputs, timers, fd watchers, one EGL context
  (GLES 3), configuration with live reload.
- `ui::Window` — wl_surface + EGL surface + its own ImGui context. Renders
  only when input arrives, on resize or `request_redraw()`, and at most once
  per `wl_surface.frame` callback, so idle and hidden windows cost nothing.
  `LayerWindow` (bars, panels) and `ToplevelWindow` (apps, asks for
  server-side decorations) set the role.
- Fonts: fontconfig resolves `theme.font` to a file; ImGui renders it with
  FreeType. Cursor shapes go through `cursor-shape-v1`.
- Rules learned the hard way (phase 2 bug fixes):
  - Creating or destroying a window must not change the current EGL surface
    (`EglGuard`): windows are created from other windows' draw callbacks
    (the bar opening the launcher). Otherwise the drawing window's frame is
    never committed, its frame callback never fires and it freezes for good.
  - ImGui trickles queued input (move, press, release take a frame each), so
    a window keeps rendering until `InputEventsQueue` is empty; a fixed
    "two frames per event" loses quick clicks.
  - Hit tests for "click outside closes" use plain rectangles:
    `IsWindowHovered()` is false while an item in the window is active.
  - Never destroy a window from its own draw callback; defer it to a timer.

## Shell services (phase 3)

- **D-Bus**: sdbus-c++ 2.x, driven by the toolkit's own event loop
  (`ui::DBusConnection` registers a poll source with `App::add_source`), so
  handlers run on the UI thread between frames; no extra threads.
- In a real session (not headless or nested) the compositor runs
  `dbus-update-activation-environment --systemd` so D-Bus/systemd-activated
  services (portals, notification actions) find the display.
  `scripts/run-headless.sh` starts a private bus (used as both session and
  system bus) so tests never touch the services of a real session.
- **Notifications**: tetra-shell owns `org.freedesktop.Notifications`
  (spec 1.2: replaces_id, actions, urgency, expire timeouts, app icon,
  image-path and image-data, desktop-entry). Popups stack at the bottom-right
  above the bar (newest at the bottom, 4 at most) in one layer surface sized
  to its content. Default timeout 6 s; critical ones stay until dismissed;
  hovering pauses expiry. Clicking a card runs its "default" action or
  dismisses it. Not yet: a notification centre/history, do-not-disturb,
  sounds, body markup.
- **Media keys and OSD**: the compositor intercepts XF86AudioRaiseVolume /
  LowerVolume / Mute / MicMute and XF86MonBrightnessUp / Down (before any
  client sees them) and forwards them as `tetra_shell_v1.media_key`
  (protocol version 2). The shell applies them and shows a small OSD at the
  bottom centre (icon, level bar, percentage; 1.5 s; greyed out when muted).
  Steps are 5 %.
- **Volume** (`src/audio`, shared by the shell and Settings): libpulse
  (threaded mainloop), which works against both PulseAudio and PipeWire's
  pipewire-pulse. Each change event re-reads the server info and every sink
  and source (monitors left out) into one snapshot: the outputs and
  microphones, the defaults, their volume and mute. It reconnects every 5 s
  if the server goes away. State is handed to the UI thread through an
  eventfd. The tray speaker opens a popup (device, mute button, slider) and
  the mouse wheel over it steps the volume. Settings > Sound picks the
  default output and input (`set_default_sink/source`; the server moves
  playing streams and remembers the choice) and sets their volume and mute.
  Under `TETRA_AUDIO=fake` (set by `run-headless.sh`), in-memory devices
  replace the server; `run-headless.sh -a` instead runs a private PipeWire
  (pipewire, WirePlumber with every hardware monitor disabled,
  pipewire-pulse; config and state in the runtime dir) with two virtual
  outputs and a virtual microphone, each kept busy with a silent stream
  because idle virtual devices ignore volume changes.
- **Brightness**: reads `/sys/class/backlight/*` and writes through logind's
  `Session.SetBrightness` on the system bus, so no udev rule or setuid
  helper is needed. With no backlight (VMs, desktops) the keys do nothing.
- **Mice and touchpads**: every pointer device gets a `Pointer`, which
  sets libinput's speed, acceleration profile, natural scrolling,
  left-handed buttons and, for touchpads (devices that can tap), tapping,
  disable-while-typing, scroll and click methods from config `mouse` and
  `touchpad`, on plug-in and on reload. Settings a device lacks are skipped;
  with no `touchpad.click` the device's own default is restored. Virtual
  pointers and nested/headless backends have nothing to set.
- **Pointer refocus**: when a surface maps, unmaps or is minimized under a
  stationary cursor, the compositor re-runs pointer focus on idle, so the
  surface now under the cursor gets enter/leave without waiting for the
  next mouse move. It skips this while a button is held.
- **Lock screen** (`ext-session-lock-v1`): the compositor's side is
  `SessionLock`. While locked, an opaque cover sits above every layer,
  so only lock surfaces are visible or reachable. Lock surfaces get all
  keyboard input; a click picks between outputs. Only VT switching and the
  media keys bypass it. "locked" is sent once every output has drawn the
  cover. If the lock client dies without unlocking, the session stays
  covered ("abandoned") and accepts a new lock client. The compositor asks
  the shell to lock (`tetra_shell_v1.lock`, protocol version 3); a
  restarted shell is asked as soon as it binds. A second lock client while
  locked gets `finished`.
  The shell draws the clock, date, user name and a password field on each
  output. It checks the password with PAM on a worker thread (service
  `tetra`, shipped as `data/pam.d/tetra`; without it Debian's `other`
  stack applies). The password buffer is wiped after use.
  Super+L locks. If the shell exits while locked, the session stays locked.
- **Idle** (`ext-idle-notify-v1` + `idle-inhibit-unstable-v1`): input
  resets the compositor's idle timers, and any idle inhibitor (video
  players) suspends them. Simplification: inhibitors count even when their
  surface is hidden. The shell locks after `idle.lock_after`. It blanks the
  screens after `idle.screen_off_after` (60 s at most on the lock screen)
  through `wlr-output-power-management`. The compositor disables the output
  but keeps it in the layout, and restores the same mode on wake.
- **polkit agent**: tetra-shell exports
  `org.freedesktop.PolicyKit1.AuthenticationAgent` on the system bus and
  registers it for its logind session. polkitd may then call
  BeginAuthentication (for pkexec, mounting, package installs). For each
  try, the shell runs the setuid `polkit-agent-helper-1`, sends the cookie
  on stdin (never on the command line) and relays the helper's PAM prompts
  (`PAM_PROMPT_ECHO_OFF` …) to a dialog. On success the helper reports to
  polkitd itself; on failure a new try starts.
  - The dialog dims the screen and shows the app's message, which user
    (admin groups such as sudo expand to their members, the current user
    first), the prompt, PAM messages and the action id.
  - Requests queue and show one at a time. Escape or Cancel returns
    `Error.Cancelled`, and CancelAuthentication closes the dialog.
  - `TETRA_POLKIT_HELPER` replaces the helper. Tests use this, and it grants
    nothing, since polkitd only trusts the result from the real helper,
    which runs as root.
- **Screenshots**: the compositor turns Print (output under the pointer),
  Shift+Print or Super+Shift+S (region) and Alt+Print (focused window, frame
  included) into `tetra_shell_v1.screenshot` (protocol version 4). The event
  carries the shell's wl_output, a box in output-logical coordinates, and a
  serial registered with the seat for the shell's client. The shell
  captures the whole output with wlr-screencopy (shm, without the cursor)
  and crops it. For a region, it first shows that frozen capture full screen,
  dimmed, and the user drags a rectangle (Esc cancels). The result is saved
  as PNG (cairo) to `$XDG_PICTURES_DIR` (else user-dirs.dirs, else
  ~/Pictures) + `/Screenshots/Screenshot <date> <time>.png`. It is offered
  as `image/png` on the clipboard (wl_data_device with the event's serial;
  a thread writes it to each reader). A notification with a thumbnail
  follows; clicking it opens the image (xdg-open).
- **Clipboard tools**: the compositor offers wlr- and ext-data-control, so
  clipboard managers and wl-copy/wl-paste work without keyboard focus.
- **logind**: Session.Lock (`loginctl lock-session`) locks. With
  `idle.lock_before_sleep`, the shell holds a delay inhibitor for sleep: on
  PrepareForSleep it locks, then releases the inhibitor once the compositor
  confirms the lock, so the machine never wakes up unlocked.

## Development workflow

- VirtualBox VM, Debian 13, VMSVGA + 3D (Mesa SVGA3D, GLES 3.0).
- **Headless loop:** `scripts/run-headless.sh` runs tetra on wlroots' headless
  backend with a private runtime dir and an empty config, launches clients, and
  screenshots with `grim` — no display needed, works over SSH, and is the basis
  for screenshot regression tests in CI. Scenarios live in `tests/headless/`;
  pointer input is injected with `wlrctl` (the pointer starts at 100,100),
  key chords with our `tetra-sendkeys` (`super+F1`, `super` …).
  `scripts/run-tests.sh` runs all scenarios (ASan by default) and flags
  crashes and sanitizer reports; the screenshots still need looking at.
  A scenario can ask for a longer wait with a `# wait: SECS` header line.
- **Lock screen tests:** the headless session runs under pam_wrapper
  (LD_PRELOAD) with a `tetra` service backed by pam_matrix. Only
  `$TETRA_TEST_PASSWORD` is accepted, so tests never touch real accounts
  or passwords. ASan needs `PAM_WRAPPER_DISABLE_DEEPBIND=1`. Scenarios
  change settings such as idle timeouts by rewriting `$TETRA_CONFIG`
  (live reload). polkit requests come from `tests/tools/polkit-request`
  (it calls the shell's agent like polkitd would); the helper is
  `tests/tools/fake-polkit-helper`, which accepts the same test password.
  Screenshots go to a private `$XDG_PICTURES_DIR`, window memory to a
  private `$XDG_STATE_HOME`. `tetra-pointer` gives
  absolute moves, clicks, drags (wlrctl cannot drag) and wheel notches;
  `tetra-sendkeys --hold` keeps keys (Shift) down meanwhile. wlroots releases a
  virtual pointer's buttons when it goes away, so `hold` keeps running.
  `tetra-tray-item` is a StatusNotifierItem with a dbusmenu (or
  libappindicator-style with `--appindicator`). It reports what it receives
  as notifications.
- The seat always advertises a keyboard, so clients hold a `wl_keyboard`
  before any (virtual or hot-plugged) keyboard appears.
- **Real session:** from a spare TTY, or later through a `wayland-sessions`
  entry in GDM. GDM stays until tetra has its own greetd greeter.
- Presets: `debug` (GCC, -Werror), `clang`, `asan` (ASan + UBSan), `release`.

## Roadmap

0. **Infrastructure** — repo, vendored deps, build, compositor skeleton,
   headless screenshot loop. *(done)*
1. **Compositor + toolkit MVP** — layer-shell, server-side decorations,
   JSON config, Xwayland; libtoolkit with event-driven ImGui; a bar with a clock.
   *(done; not yet tried on a real seat)*
2. **Task bar** — private protocol, F-key slots, launcher, basic tray.
   *(done; window-list popup, tooltips and pinning from the UI pending)*
3. **Shell services** — notifications, OSD, lock/idle, polkit agent, screenshots, SNI tray.
   *(done; not yet tried on a real seat)*
4. **Settings app** — `tetra-settings` (Super+I, or the launcher), an editor
   for config.json. *(done: appearance, displays, snap zones and window
   memory, sound, taskbar pins, keyboard, mouse & touchpad, lock & power,
   apps & startup, about; not yet tried on a real seat)*
5. **Apps and portal** — terminal, system monitor, portal backend.
6. **Final pieces** — greeter, animations, polish.

## Open questions

- Overflow beyond 12 apps: is the single overflow stack enough in practice?
- Virtual input (`wlr-virtual-pointer`, `virtual-keyboard`) is open to every
  client so tests can drive the UI. Restrict it (config flag or trusted
  clients only) before tetra is used for real.
- Fractional scaling: the compositor offers `wp-fractional-scale-v1`, but the
  toolkit only handles integer `preferred_buffer_scale` so far.
- KDE `server-decoration` (some GTK3/Qt5 apps) is not implemented; those
  apps draw their own decorations.
- Toolkit popups (xdg_popup from layer surfaces): needed for slot tooltips,
  the per-slot window list and right-click menus (pin/unpin).
- Pinning from the bar itself (right-click a slot) could use the same
  comment-preserving editor as Settings.
- The desktop database is read at startup (compositor) and when the
  launcher opens (shell); watching `applications/` dirs would pick up new
  installs immediately.
- XPM icons (e.g. xterm's `mini.xterm`) are not loaded; such apps fall back
  to a matching theme icon or the generic one.

## Decided

- Config library: nlohmann/json (Debian-packaged, comments supported).
- Decorations: cairo + pango into scene buffers, not ImGui in the compositor —
  no GL state shared with wlroots' renderer, same palette and font as the shell.
