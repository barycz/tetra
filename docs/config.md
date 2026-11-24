# Configuration

tetra reads one file: `~/.config/tetra/config.json` (or
`$XDG_CONFIG_HOME/tetra/config.json`). The compositor, the shell and every
tetra app read the same file and **reload it automatically** when it is saved.
A file with errors is rejected and the previous configuration stays active;
the error is logged by each component.

- JSON with `//` and `/* */` comments.
- Every key is optional; missing keys use the defaults below.
- Unknown keys are reported in the compositor log but are not errors.
- `tetra -c FILE` uses another file (exported to the shell and apps as
  `TETRA_CONFIG`).

## Example

```jsonc
{
  "keyboard": {
    "layout": "us,cz",            // xkb layouts
    "variant": ",qwerty",
    "options": "grp:alt_shift_toggle",
    "repeat_rate": 30,            // keys per second
    "repeat_delay": 400           // milliseconds
  },

  "mouse": {
    "speed": 0,                   // -1 (slowest) to 1 (fastest)
    "acceleration": true,         // false: flat, however fast you move
    "natural_scroll": false,
    "left_handed": false          // swaps buttons on mice and touchpads
  },

  "touchpad": {
    "speed": 0,
    "tap_to_click": true,
    "natural_scroll": true,
    "disable_while_typing": true,
    "scroll": "two_finger",       // "two_finger", "edge" or "none"
    "click": "fingers"            // right click: "areas" (bottom right) or "fingers" (two)
  },

  "outputs": [
    { "name": "DP-1", "mode": "2560x1440@144", "scale": 1.25, "position": [0, 0] },
    { "name": "HDMI-A-1", "enabled": false, "zones": "grid-4" },
    { "name": "*", "scale": 1 }   // fallback for outputs not listed
  ],

  "theme": {
    "dark": true,
    "accent": "#4c8dff",
    "font": "Inter",              // fontconfig family name
    "font_size": 14               // pixels at scale 1
  },

  "desktop": { "background": "#1b1d22" },

  "windows": {
    "titlebar_height": 32,
    "border_width": 1,
    "remember_positions": true,   // open windows where they were last
    "zones": "columns-3",         // snap zones for displays without their own
    "zone_gap": 8,
    "zone_layouts": {             // your own: [x, y, width, height] in percent
      "coding": [[0, 0, 60, 100], [60, 0, 40, 50], [60, 50, 40, 50]]
    }
  },

  "taskbar": {
    // Desktop file IDs (or app_ids) for slots F1, F2, ... — at most 11.
    "pinned": ["foot", "firefox-esr", "org.gnome.Nautilus"]
  },

  "idle": {
    "lock_after": 300,            // seconds without input; 0 = never
    "screen_off_after": 600,      // 0 = never (on the lock screen: 60 s at most)
    "lock_before_sleep": true
  },

  "terminal": "foot",             // Super+Return and Terminal=true apps
  "shell": "tetra-shell",         // "" to run without a shell
  "autostart": ["nm-applet", "foot --server"]
}
```

Settings (Super+I) edits this file in place: it changes only the values you
change and keeps your comments and layout. It leaves a file with errors
alone until you fix it. Its Displays page writes one `outputs` entry per
display (by connector name) and goes back to the previous file if you do
not keep a change within 15 seconds.

Tools that change displays through wlr-output-management (wlr-randr,
kanshi) can list them but not change them: `outputs` is the one place.

Windows open where they were last, on each display, and two windows of the
same app keep two places. This is remembered in
`~/.local/state/tetra/windows.json` (not a configuration file; delete it to
forget every place). `"windows": { "remember_positions": false }` turns it
off.

## Snap zones

Hold **Shift** while dragging a window: the zones of the display under the
pointer appear, and the window fills the zone you drop it on. Near the edge
between zones it fills both. Dragging a snapped window out of its zone gives
it back its own size. **Super+Left / Super+Right** move the focused window
to the previous / next zone, and on to the display beside at the ends.

Built-in layouts: `none`, `columns-2`, `columns-3`, `priority` (wide middle),
`main-left`, `main-right`, `rows-2`, `grid-4`, `grid-6`. Add your own under
`windows.zone_layouts`; Settings > Snap zones picks a layout per display.

## Reference

| Key | Type | Default | Live reload |
|---|---|---|---|
| `keyboard.layout` / `model` / `variant` / `options` | string | `"us"` / `""` | yes |
| `keyboard.repeat_rate` | integer, keys/s | `25` | yes |
| `keyboard.repeat_delay` | integer, ms | `600` | yes |
| `mouse.speed` | number, -1–1 | `0` | yes |
| `mouse.acceleration` | bool | `true` | yes |
| `mouse.natural_scroll` | bool | `false` | yes |
| `mouse.left_handed` | bool (mice and touchpads) | `false` | yes |
| `touchpad.speed` | number, -1–1 | `0` | yes |
| `touchpad.tap_to_click` | bool | `true` | yes |
| `touchpad.natural_scroll` | bool | `true` | yes |
| `touchpad.disable_while_typing` | bool | `true` | yes |
| `touchpad.scroll` | `"two_finger"`, `"edge"`, `"none"` | `"two_finger"` | yes |
| `touchpad.click` | `"areas"`, `"fingers"` | the touchpad's default | yes |
| `outputs[].name` | connector name or `"*"` | required | yes |
| `outputs[].enabled` | bool | `true` | yes |
| `outputs[].mode` | `"WxH"` or `"WxH@Hz"` | preferred mode | yes |
| `outputs[].scale` | number, 0.5–4 | `1` | yes |
| `outputs[].position` | `[x, y]` | automatic, left to right | yes |
| `outputs[].zones` | layout name | `windows.zones` | yes |
| `theme.dark` | bool | `true` | yes |
| `theme.accent` | colour | `"#4c8dff"` | yes |
| `theme.font` | string | `"Inter"` | yes |
| `theme.font_size` | number, 6–48 | `14` | yes |
| `desktop.background` | colour | `"#1b1d22"` | yes |
| `windows.titlebar_height` | integer, 16–96 | `32` | yes |
| `windows.border_width` | integer, 0–16 | `1` | yes |
| `windows.remember_positions` | bool | `true` | yes |
| `windows.zones` | layout name | `"columns-2"` | yes |
| `windows.zone_gap` | integer, px, 0–64 | `8` | yes |
| `windows.zone_layouts` | object: name → list of `[x, y, w, h]` percent, 1–16 zones | `{}` | yes |
| `taskbar.pinned` | array of desktop IDs, at most 11 | `[]` | yes (open windows stay put) |
| `idle.lock_after` | integer, seconds, 0–86400 (0: never) | `300` | yes |
| `idle.screen_off_after` | integer, seconds, 0–86400 (0: never) | `600` | yes |
| `idle.lock_before_sleep` | bool | `true` | yes |
| `terminal` | command | `"foot"` | yes |
| `shell` | command | `"tetra-shell"` | restarts the shell |
| `autostart` | array of commands | `[]` | at startup only |

Colours are `"#rgb"`, `"#rrggbb"` or `"#rrggbbaa"`.

Mouse and touchpad settings apply to devices tetra opens through libinput
(a real session). A device that lacks a setting (a mouse without
acceleration profiles, say) keeps its own behaviour for that setting.
