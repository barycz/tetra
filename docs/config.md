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

  "outputs": [
    { "name": "DP-1", "mode": "2560x1440@144", "scale": 1.25, "position": [0, 0] },
    { "name": "HDMI-A-1", "enabled": false },
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
    "border_width": 1
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
alone until you fix it.

## Reference

| Key | Type | Default | Live reload |
|---|---|---|---|
| `keyboard.layout` / `model` / `variant` / `options` | string | `"us"` / `""` | yes |
| `keyboard.repeat_rate` | integer, keys/s | `25` | yes |
| `keyboard.repeat_delay` | integer, ms | `600` | yes |
| `outputs[].name` | connector name or `"*"` | required | yes |
| `outputs[].enabled` | bool | `true` | yes |
| `outputs[].mode` | `"WxH"` or `"WxH@Hz"` | preferred mode | yes |
| `outputs[].scale` | number, 0.5–4 | `1` | yes |
| `outputs[].position` | `[x, y]` | automatic, left to right | yes |
| `theme.dark` | bool | `true` | yes |
| `theme.accent` | colour | `"#4c8dff"` | yes |
| `theme.font` | string | `"Inter"` | yes |
| `theme.font_size` | number, 6–48 | `14` | yes |
| `desktop.background` | colour | `"#1b1d22"` | yes |
| `windows.titlebar_height` | integer, 16–96 | `32` | yes |
| `windows.border_width` | integer, 0–16 | `1` | yes |
| `taskbar.pinned` | array of desktop IDs, at most 11 | `[]` | yes (open windows stay put) |
| `idle.lock_after` | integer, seconds, 0–86400 (0: never) | `300` | yes |
| `idle.screen_off_after` | integer, seconds, 0–86400 (0: never) | `600` | yes |
| `idle.lock_before_sleep` | bool | `true` | yes |
| `terminal` | command | `"foot"` | yes |
| `shell` | command | `"tetra-shell"` | restarts the shell |
| `autostart` | array of commands | `[]` | at startup only |

Colours are `"#rgb"`, `"#rrggbb"` or `"#rrggbbaa"`.
