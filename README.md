# PadsWay

Reads physical gamepads (HID) and forwards them as a virtual controller via ViGEm — either an
**Xbox 360** pad (XInput) or a **DualShock 4** (DirectInput), selectable at runtime.
Supports macros, bots, and JSON-based configuration — no code changes needed.

[Leer en español](README.es.md)

---

## Requirements

### To run

| Dependency | Purpose |
|---|---|
| Windows 10/11 | Required APIs: HID, DirectX 11 |
| [ViGEmBus driver](https://github.com/nefarius/ViGEmBus/releases) | Creates the virtual Xbox 360 controller |
| [HidHide driver](https://github.com/nefarius/HidHide/releases) | Hides the physical gamepad from games to prevent double input |

> **ViGEmBus** and **HidHide** are by the same author (Nefarius) and install like any Windows driver.
> PadsWay controls them automatically — no need to touch their interfaces manually.

### To build

- **Visual Studio 2022** (C++17 + Windows SDK).
- All other dependencies are vendored in the repository (`imgui/`, `nlohmann/`, `spdlog/`).
- **Build the `Release | x64` configuration.** `ViGEmClient.lib` is Release-only, so a
  Debug build will not link. To debug, run Release with F5 (it generates a PDB).

The output is `x64\Release\PadsWay.exe`.

### Running from source (development)

By default PadsWay writes its config and logs to `%LOCALAPPDATA%\PadsWay` (see *Data files →
Where these files live*). During development that splits your settings away from the project's
own `data/` folder. To keep everything next to the working directory instead, drop an empty
**`portable.txt`** there — for an F5 launch from Visual Studio that is the project folder
(`PadsWay\`). It is gitignored, so it stays a local dev-only marker and never ships.

### Packaging a release (installer + portable zip)

The distributables are produced by **`installer\package.ps1`**, which encodes the "clean
release" payload in one place (it ships the factory assets and safe defaults, and never your
personal `controllers.json`, profiles or macros):

1. Build the `Release | x64` configuration in Visual Studio.
2. From the repo root, run:
   ```powershell
   powershell -ExecutionPolicy Bypass -File installer\package.ps1
   ```

This writes to `dist\`:

- `PadsWay-v<ver>-win64\` — the portable staging folder,
- `PadsWay-v<ver>-win64.zip` — the portable build,
- `PadsWay-v<ver>-setup.exe` — the Inno Setup installer.

The installer step needs [**Inno Setup 6**](https://jrsoftware.org/isdl.php); if `ISCC.exe`
is not at the default path, pass `-Iscc <path>`. Other options: `-Version 0.19` sets the
release version (kept in sync with the installer), `-SkipZip` / `-SkipInstaller` build only part.

---

## Virtual output type — Xbox 360 or DualShock 4

The virtual controller can present itself to the system as one of two types:

| Type | Protocol | Best for |
|---|---|---|
| **Xbox (XInput)** | XInput | Best compatibility with modern games. The default. |
| **DualShock (DirectInput)** | DirectInput | Old games, emulators, and getting **more than 4 pads** (XInput is capped at 4). |

Pick it from the combo box next to the profile selector in the UI. Switching shows a confirmation
dialog and is applied **live**: the virtual pad is unplugged and re-plugged on the fly, so **close
any running game before switching** or it will lose the controller.

### What the DualShock 4 output does *not* carry

The virtual DS4 reproduces buttons, sticks, d-pad, analog triggers and the PS button. The features
that are exclusive to a real DualShock 4 are **disabled**:

- the **touchpad** — both the surface (finger XY tracking) **and** the touchpad click,
- the **gyroscope** and accelerometer (IMU).

The internal model is always Xbox, so there is no source for that data. Accordingly, the
virtual-pad view for the DS4 shows the touchpad and gyroscope as non-assignable decoration only.
(The DS4 output adapter does wire the touchpad-click bit, but nothing feeds it and it has no use
today — see *Ideas*; it would only matter for an eventual PS4 emulator.)

### Configuration — `data/virtualpad.json`

The chosen type is persisted in `"output_type"`:

```json
{ "output_type": "xbox" }       // or "dualshock"
```

Accepted values (case-insensitive): `"xbox"` / `"xinput"` select the Xbox 360 output (also the safe
default for any unknown value or a missing key); `"dualshock"` / `"ds4"` / `"dinput"` / `"directinput"`
select the DualShock 4 output.

Each type has its own VID/PID so games can tell them apart:

| Key | Default | Identity |
|---|---|---|
| `virtual_x_vid` / `virtual_x_pid` | `5650` / `0001` | Xbox 360 (ViGEm) |
| `virtual_direct_vid` / `virtual_direct_pid` | `054C` / `05C4` | DualShock 4 v1 (Sony) |

> A real Sony VID requires a real Sony PID: `054C:05C4` (DualShock 4 v1) is emulated natively by
> ViGEm and does not clash with a physical DS4 v2 (`054C:09CC`).

---

## Adding a new controller — `data/controllers.json`

Each entry in the `"controllers"` array describes a physical gamepad.
The required fields are the **VID and PID** of the controller (visible in the Scanner tab or in Device Manager).

```json
{
  "vid": "XXXX",
  "pid": "YYYY",
  "source_name": "Descriptive name",
  "mode": "...",
  "buttons": { },
  "axes": { },
  "dpad": "..."
}
```

The `"mode"` field must be `"hid"` — all controllers are read via HID raw (`HidP_*`).

> **Tip:** open the Scanner tab with the controller connected. The console log at startup lists all detected axes (Usage ID + range). Use those to fill in the `"axes"` section.

---

## Mode `"hid"` — HID raw

For controllers that don't appear in WinMM but do appear under **HID-only** in the scanner.
Axes use **HID Usage IDs** instead of WinMM field names:

### Axes — Generic Desktop (HID page 0x01)

| Axis name | Usage ID | Typical use |
|---|---|---|
| `"hid_x"`  | 0x30 | Left stick X |
| `"hid_y"`  | 0x31 | Left stick Y |
| `"hid_z"`  | 0x32 | Right stick X (or trigger on some controllers) |
| `"hid_rx"` | 0x33 | Right stick X / Left trigger (varies by controller) |
| `"hid_ry"` | 0x34 | Right stick Y / Right trigger (varies by controller) |
| `"hid_rz"` | 0x35 | Right stick Y (common in D-mode) |

### Axes — Simulation Controls (HID page 0x02)

Some controllers expose triggers as simulation controls (same standard as racing wheels):

| Axis name | Usage ID | Typical use |
|---|---|---|
| `"hid_brake"` | 0xC4 | L2 trigger (Brake) |
| `"hid_accel"` | 0xC5 | R2 trigger (Accelerator) |

> PadsWay automatically detects the real HID page from the device descriptor —
> no need to worry about whether the controller uses 0x01 or 0x02 internally.

### Available axis targets

| Target | Description |
|---|---|
| `"left_x"` / `"left_y"` | Left stick |
| `"right_x"` / `"right_y"` | Right stick |
| `"trigger_l"` | L2 trigger analog **[0.0 .. 1.0]** |
| `"trigger_r"` | R2 trigger analog **[0.0 .. 1.0]** |
| `"trigger_combined"` | L2/R2 shared on one axis (+ = L2, - = R2) |

### D-pad in HID mode

HID controllers usually have the D-pad as a **hat switch**:
```json
"dpad": "hid_hat"
```

### Example — 8BitDo Pro 2 (D-mode, Bluetooth) with analog triggers
```json
{
  "vid": "2DC8", "pid": "6006",
  "source_name": "8BitDo Pro 2 (D-mode)",
  "mode": "hid",
  "buttons": {
    "1": "b", "2": "a", "3": "x", "4": "y",
    "7": "l1", "8": "r1",
    "11": "select", "12": "start", "13": "home",
    "14": "l3", "15": "r3"
  },
  "axes": {
    "hid_x":     { "target": "left_x",   "invert": false },
    "hid_y":     { "target": "left_y",   "invert": true  },
    "hid_z":     { "target": "right_x",  "invert": false },
    "hid_rz":    { "target": "right_y",  "invert": true  },
    "hid_brake": { "target": "trigger_r", "invert": false },
    "hid_accel": { "target": "trigger_l", "invert": false }
  },
  "dpad": "hid_hat"
}
```

> **Note:** on this specific controller, Brake→R2 and Accel→L2 (inverted relative to the name).
> If the triggers are swapped, exchange `trigger_l` and `trigger_r`.

---

## Button mapping

Indices are **1-based** and correspond to bit N-1 of the device's button mask.
Use the **Scanner tab** in PadsWay to identify which number lights up when pressing each physical button.

### Available virtual buttons

| Value | Button on virtual controller |
|---|---|
| `"a"` | A |
| `"b"` | B |
| `"x"` | X |
| `"y"` | Y |
| `"l1"` | LB (left bumper) |
| `"r1"` | RB (right bumper) |
| `"select"` | Back / Select |
| `"start"` | Start |
| `"home"` | Guide on Xbox (not injectable on virtual Xbox 360) / PS button on DualShock |
| `"l3"` | Left stick click |
| `"r3"` | Right stick click |

### Button format

Each button entry is an object with two independent concepts:

- **`physical`** — the name of the button on the physical controller body (`"a"`, `"l4"`, `"rp"`...).
  Used for visual tracking in the UI. Survives profile overrides — a button always knows what it is physically, regardless of what action is assigned to it.
- **`virtual`** / action — what happens when the button is pressed.

```json
"7":  { "physical": "l1", "virtual": "l1" }           // normal button: physical l1 → virtual l1
"3":  { "physical": "rp" }                             // no Xbox equivalent — visual only
"9":  { "physical": "l2", "type": "trigger", "target": "l2" }
```

The short string format `"1": "b"` is still supported for controllers without extra buttons
and is equivalent to `{ "physical": "b", "virtual": "b" }`.

### Button action types

```json
"N": "b"                                              // short form (physical = virtual = "b")
"N": { "physical": "b", "virtual": "b" }             // explicit
"N": { "physical": "rp" }                             // visual only, no virtual output
"N": { "physical": "l2", "type": "trigger", "target": "l2" }
"N": { "physical": "rp", "type": "macro",   "name": "MacroName" }
"N": { "physical": "lp", "type": "bot",     "name": "LightningBot" }
"N": { "physical": "home", "type": "keyboard",    "keys": ["alt", "tab"] }
"N": { "physical": "l3",   "type": "mouse_click", "button": "left" }
```

#### Action `keyboard` — keyboard shortcuts

Sends a key combination to the system on button press and releases it on button release.
Useful for buttons with no Xbox equivalent (Home, Lp, Rp, L4, R4) or any system shortcut.

Available keys: `alt`, `ctrl`, `shift`, `win`, `tab`, `enter`, `esc`, `space`, `backspace`,
`delete`, `insert`, `home_key`, `end`, `pageup`, `pagedown`, `up`, `down`, `left`, `right`,
`f1`–`f12`, letters `a`–`z`, digits `0`–`9`.

```json
"13": { "type": "keyboard", "keys": ["alt", "tab"] }
```

> **Note**: `"home_key"` refers to the keyboard Home key. `"home"` (without `_key`) is the
> Guide/Home button on the virtual (Xbox) controller.

#### Action `mouse_click` — mouse clicks

```json
"14": { "type": "mouse_click", "button": "left" }
"14": { "type": "mouse_click", "button": "right" }
"14": { "type": "mouse_click", "button": "middle" }
```

---

## Mouse movement from an analog stick

Maps a controller axis to the mouse cursor using the special targets `mouse_x` / `mouse_y`:

```json
"axes": {
  "dwZpos": { "target": "mouse_x", "invert": false, "speed": 15 },
  "dwRpos": { "target": "mouse_y", "invert": false, "speed": 15 }
}
```

The `speed` parameter controls pixels per tick (8ms) at full deflection. With `speed: 15` the
cursor moves ~1875 px/second at full stick — adjust to taste.

> **Y-axis inversion**: the screen has Y positive downward, the controller convention has it
> upward. If the cursor moves in reverse vertically, use `"invert": true` for that axis.
> Usually the opposite of the value you had for `right_y` with the same physical axis.

### Example — Pro 2 D-mode with mouse on right stick

```json
"hid_z":  { "target": "mouse_x", "invert": false, "speed": 15 },
"hid_rz": { "target": "mouse_y", "invert": false, "speed": 15 }
```

These targets can be used in the base config (`controllers.json`) or as axis overrides
in a game profile once axis override support is implemented in profiles.

---

## Axis inversion

Add `"invert": true` to the axis mapping to invert it:
```json
"hid_y": { "target": "left_y", "invert": true }
```

Many controllers report the stick Y axis so that "up" gives the maximum value,
but PadsWay's convention is `+1.0 = up`. Adjust based on what you see in the scanner.

---

## How to discover the mapping for a new controller

1. Connect the controller
2. Open the **Scanner tab** in PadsWay and press each button — note which number lights up
3. The console log at startup shows all `ValCaps` (Usage ID and range) — use these to identify axes
4. Move each stick and observe which axis changes and in which direction
5. For triggers: look for `Usage=0xC4` / `Usage=0xC5` (Simulation Controls) or `Usage=0x33`/`0x34` (Rx/Ry in Generic Desktop) in the log
6. Add the entry to `controllers.json` with `"mode": "hid"` and restart PadsWay

---

## Game profiles

`controllers.json` is the **pure base config**: each physical button maps to its standard Xbox 360 equivalent.
Macros, bots, and special assignments go in a separate JSON file per game.

### Buttons with no Xbox equivalent (Lp, Rp, L4, R4)

Some controllers have buttons that don't exist in the Xbox 360 protocol — they cannot produce virtual output.
They are declared in the base config with only a `physical` field (no `virtual`):

```json
"3":  { "physical": "rp" },
"17": { "physical": "l4" }
```

This means:
- They **light up in the UI** whenever pressed, regardless of what action is assigned.
- They have **no virtual Xbox output** by default.
- In a game profile they can be assigned to macros, bots, keyboard combos, etc.
- Even with a profile active, the UI still reflects that the physical button is pressed.

| HID button | Physical | Virtual Xbox | Use |
|---|---|---|---|
| 3 | Rp (right paddle) | — | macro / bot in game profile |
| 6 | Lp (left paddle) | — | macro / bot in game profile |
| 17 | L4 | — | macro / keyboard in game profile |
| 18 | R4 | — | macro / keyboard in game profile |

### Game profile format

Profiles are **controller-agnostic** — keys use the virtual button name (`physShort`) instead of a hardware-specific index. The same profile works with any connected controller.

```json
{
  "profile_name": "Final Fantasy X",
  "buttons": {
    "r3":   { "type": "macro",       "name": "LuluOverdrive"  },
    "home": { "type": "keyboard",    "keys": ["alt", "tab"]   },
    "l3":   { "type": "mouse_click", "button": "left"         }
  },
  "axes": {
    "right_x": { "target": "mouse_x", "invert": false, "speed": 15 },
    "right_y": { "target": "mouse_y", "invert": false, "speed": 15 }
  }
}
```

- Only declare the buttons/axes that change from the base. Everything else is inherited unchanged.
- If the connected controller has no button matching a given physShort, that override is silently ignored.
- If a profile reassigns a standard button (e.g. `r3`), that button stops working as a virtual button while the profile is active. This is intentional.
- JSON doesn't support native comments. Fields prefixed with `_` (`_note`, `_hid_prototype`) are ignored by the parser and serve as annotations.

### Adding a new profile

1. Create `data/profiles/GameName.json` with the structure above.
2. Declare only the buttons/axes you need to change; leave the rest in the base.
3. Select the profile from PadsWay's UI.

---

## Macro Library

Open from the **Pads tab → Macros button**. Macros are stored in `data/macros.json` and are reusable across any controller or game profile.

### Left panel

Lists all macros in the library. Selecting one shows a visual step preview (chips with button icons) directly in the panel.

| Button | Action |
|---|---|
| **New** | Opens the editor with a blank macro |
| **Edit** | Opens the selected macro in the editor |
| **Copy** | Duplicates the selected macro as a starting point |
| **Delete** | Removes the selected macro from the library |

### Visual editor

The editor modal lets you build macros without writing DSL by hand:

- **Token picker** — face buttons, d-pad directions, analog stick positions, and motion presets (QCF/HCF for d-pad and both analog sticks). Click an icon to create a new Press step; click it again on a selected step to add the token as a simultaneous combo.
- **Step sequence** — each step is displayed as a chip. Click a chip to select it; click another to extend the selection range.
- **Repeat controls** — with one or more steps selected, wrap them in a loop (ms), count-based repeat (`N times in ms`), while-held, or toggle.
- **Hold ms** — with a Press step selected, sets how long the button is held (default 80ms).
- **DSL field** — the raw macro string is always visible and editable. Editing it directly activates complex mode for arbitrarily advanced macros.

Changes are written to `data/macros.json` and hot-reloaded immediately — no restart required.

See [MACROS.md](MACROS.md) for the complete DSL syntax and a full editor reference.

---

## Layout Editor

The **Layout** tab provides a visual editor for `data/pad_layouts.json`.

### Opening a layout

Click any layout name in the left panel to open it in the editor immediately.
If there are unsaved changes, a confirmation dialog appears before switching.

### Canvas interactions

| Action | Result |
|---|---|
| **Single click** on a component | Selects it (highlighted in yellow). Properties appear in the right panel. |
| **Click + drag** | Moves the selected component freely on the canvas. |
| **Arrow keys** (with canvas focused) | Nudge 1 pixel per key press. |

### Three panels

**Left panel** — layout list and element list, each with its own independent scroll:
- `[+ Button]` `[+ Dpad]` `[+ Analog]` `[+ Decoration]` — add a component of that type.
- `[Delete element]` — removes the selected component.
- `[Copy layout]` — duplicates the current layout as a starting point for a new one.
- `[Save]` / `[Discard]` — save to `pad_layouts.json` (creates a `.bak` backup on first save if none exists) or discard all changes.
- `[Pair controller]` — launches the Controller Binding Wizard (see below).

Supported component types: `button`, `stick`, `dpad`, `touchpad`, `gyroscope`, `decoration`, `template`.
The `gyroscope` component is visible in the Pads tab when the connected controller reports IMU data (e.g. DualShock 4 over USB). It requires no calibration — data is read automatically from fixed HID offsets.

**Center panel** — canvas showing the pad layout at scale. Zones FRONT (bottom strip) and TOP (main area) are displayed with their components rendered live.

**Right panel** — properties of the selected component:
- **Position** `cx` / `cy` and **Size** `w` / `h` — numeric fields. Lock aspect ratio with the checkbox next to each pair.
- **Image / Overlay** — combo boxes filtered by subfolder (`templates/`, `buttons/`, `cross/`, `analogics/`, `decorations/`). Selecting an image auto-fills the size from the image dimensions.
- **State bindings** — `state`, `state_x`, `state_y`, `state_click`, `state_up/down/left/right` — combo boxes showing known `GamepadState` field names.
- **Colors** — active/inactive colors for the base image and the overlay.

### Template images

The canvas has two zones stacked vertically:

| Zone | Purpose | Recommended image size |
|---|---|---|
| **FRONT** | Bottom strip — front face of the controller | 480 × 200 px |
| **TOP** | Main area — top/overhead view | 480 × 320 px |

Template images are stretched to fill their zone exactly. Using images at these dimensions avoids any quality loss from scaling.
Image files go in `images/templates/`.

---

### Dirty tracking

A `*` indicator appears when there are unsaved changes. Switching layouts or closing the editor without saving triggers a confirmation dialog.

---

## Controller Binding Wizard

The **Pair controller** button in the Layout Editor launches a step-by-step wizard to map a physical controller to the open layout and generate the corresponding entry in `data/controllers.json`.

### How it works

The wizard guides you through five stages:

1. **Select controller** — lists all detected physical devices (WinMM and HID). The ViGEm virtual controller is automatically filtered out.
2. **Name controller** — enter a friendly name. For WinMM controllers, toggle DInput ↔ XInput mode.
3. **Bind buttons** — for each button/trigger/stick-click in the layout, the wizard shows the active image of that button as a visual reference and prompts you to press the corresponding physical button. A number overlay appears on the layout canvas as each button is bound.
4. **Bind axes and triggers** — for each analog axis (left stick X/Y, right stick X/Y) and trigger (L2, R2), the wizard shows the active image of the component plus directional arrows indicating the expected movement direction.
   - For **left stick X**: push fully to the **right**.
   - For **left stick Y**: push fully **down**.
   - For **right stick X/Y**: same convention.
   - For **triggers**: press fully.
   - **Gyroscope components are skipped** — gyroscope data is read automatically from fixed HID byte offsets and does not require any calibration step.
5. **Review** — shows all bound buttons, axes, and D-pad. Confirm to save or restart from the name step.

### Result

Saving writes or replaces the entry in `data/controllers.json` by VID/PID. The Pads tab reloads the configuration automatically.

### State map (`data/state_map.json`)

The wizard uses `state_map.json` to know which physical button name (`physical`), which axis target (`axis_target`), and which prompt to show for each `GamepadState` field. Components whose `state` field has no entry in the state map are skipped silently.

```json
{
  "state_map": {
    "btnA":    { "physical": "a",   "type": "button" },
    "leftX":   { "type": "axis",    "axis_target": "left_x",
                 "prompt": "Push the left stick to the RIGHT",
                 "invert_if_positive": false },
    "dpadUp":  { "type": "dpad",    "direction": "up" }
  }
}
```

### Known issues (in progress)

| Issue | Status |
|---|---|
| Left analog stick X axis capture may fail (pushing right not detected) | Under investigation |
| Pads tab does not refresh until app restart after wizard saves | Under investigation |

---

## Data files

| File | Description |
|---|---|
| `data/controllers.json` | Base configuration for physical controllers |
| `data/profiles/` | Game profiles — one JSON per game |
| `data/macros.json` | Reusable macro library |
| `data/virtualpad.json` | Virtual output type and per-type VID/PID, locale, log level, and stick thresholds (see *Virtual output type*) |
| `data/strings/strings_en.json` | UI strings — English |
| `data/strings/strings_es.json` | UI strings — Spanish |
| `images/input_tokens/` | PNG icons for the macro creator (24×24) |

### Where these files live

The files you create or edit — `controllers.json`, `profiles/`, `macros.json`,
`pad_layouts.json`, `virtualpad.json` and the `logs/` folder — are stored in a
per-user folder so the app works even when installed into a protected location
such as `Program Files`:

```
%LOCALAPPDATA%\PadsWay\
```

(typically `C:\Users\<you>\AppData\Local\PadsWay\`). On first run the factory
copies shipped next to the executable are seeded there automatically, so you
start with the built-in controller layouts. Read-only assets that ship with the
app (`data/strings/`, `data/state_map.json`, `images/`, `data/bots/`) stay next
to the executable and are never modified.

**Portable mode.** Drop an empty file named `portable.txt` next to `PadsWay.exe`
and the app keeps everything next to the executable instead (the previous
behaviour) — handy for running from a USB stick with no trace left on the
machine. Without it, the per-user location above is used.

See [MACROS.md](MACROS.md) for the complete macro syntax.

---

## Localization

The active UI language is set via `"locale"` in `data/virtualpad.json`:

```json
{ "locale": "en" }
```

| Value | Language |
|---|---|
| `"en"` | English |
| `"es"` | Spanish |

Strings are loaded from `data/strings/strings_{locale}.json` at startup. If a key is missing from the file, the key name itself is shown as a fallback — nothing crashes.

To add a new language: copy `strings_en.json`, rename it `strings_xx.json`, translate the values (never the keys), and set `"locale": "xx"` in `virtualpad.json`.

---

## Button map by controller

Entries in **bold** are buttons with no equivalent in the Xbox 360 protocol.
They produce no virtual output by default — only useful when assigned to macros or bots in a game profile.

### 8BitDo Pro 3 — D-mode (VID:2DC8 PID:6009)

| HID button | Physical | Virtual Xbox |
|---|---|---|
| 1 | B | b |
| 2 | A | a |
| **3** | **Rp (right paddle)** | **— no equivalent** |
| 4 | Y | y |
| 5 | X | x |
| **6** | **Lp (left paddle)** | **— no equivalent** |
| 7 | LB | l1 |
| 8 | RB | r1 |
| 9 | L2 | trigger_l2 (digital) |
| 10 | R2 | trigger_r2 (digital) |
| 11 | Select | select |
| 12 | Start | start |
| 13 | Home | home |
| 14 | L3 | l3 |
| 15 | R3 | r3 |
| **17** | **L4** | **— no equivalent** |
| **18** | **R4** | **— no equivalent** |

---

### 8BitDo Pro 3 — X-mode (VID:2DC8 PID:310B)

In X-mode the firmware only exposes the 10 standard Xbox buttons. Home, Lp, Rp, L4 and R4 are not accessible.

| HID button | Physical | Virtual Xbox |
|---|---|---|
| 1 | A | a |
| 2 | B | b |
| 3 | X | x |
| 4 | Y | y |
| 5 | LB | l1 |
| 6 | RB | r1 |
| 7 | Select | select |
| 8 | Start | start |
| 9 | L3 | l3 |
| 10 | R3 | r3 |

> L2 and R2 share a single axis (trigger_combined) — X-mode firmware limitation. They cannot both be pressed simultaneously.

---

### 8BitDo Pro 2 — D-mode (VID:2DC8 PID:6006)

| HID button | Physical | Virtual Xbox |
|---|---|---|
| 1 | B | b |
| 2 | A | a |
| **3** | **Rp (right paddle)** | **— no equivalent** |
| 4 | Y | y |
| 5 | X | x |
| **6** | **Lp (left paddle)** | **— no equivalent** |
| 7 | LB | l1 |
| 8 | RB | r1 |
| 11 | Select | select |
| 12 | Start | start |
| 13 | Home | home |
| 14 | L3 | l3 |
| 15 | R3 | r3 |

> L2 and R2 are independent analog triggers via HID (hid_brake / hid_accel). Both can be pressed simultaneously.
> Buttons 3 and 6 (Rp/Lp) verified experimentally — match Pro 3 D-mode.
> The Pro 2 does not have L4/R4.

---

### 8BitDo Pro 2 — X-mode (VID:045E PID:02E0)

Same layout as Pro 3 X-mode. 10 standard Xbox buttons, no extras.

| HID button | Physical | Virtual Xbox |
|---|---|---|
| 1 | A | a |
| 2 | B | b |
| 3 | X | x |
| 4 | Y | y |
| 5 | LB | l1 |
| 6 | RB | r1 |
| 7 | Select | select |
| 8 | Start | start |
| 9 | L3 | l3 |
| 10 | R3 | r3 |

> L2/R2 share a single axis (trigger_combined) — X-mode firmware limitation.

---

### Logitech F310 — D-mode (VID:046D PID:C216)

| HID button | Physical | Virtual Xbox |
|---|---|---|
| 1 | X | x |
| 2 | A | a |
| 3 | B | b |
| 4 | Y | y |
| 5 | LB | l1 |
| 6 | RB | r1 |
| 7 | L2 | trigger_l2 (digital) |
| 8 | R2 | trigger_r2 (digital) |
| 9 | Select | select |
| 10 | Start | start |
| 11 | L3 | l3 |
| 12 | R3 | r3 |

> The F310 D-mode has no physical Home button and no extra buttons. Triggers are digital only.

---

### Sony DualShock 4 v2 (VID:054C PID:09CC)

Works the same over USB and Bluetooth (same VID/PID).

| HID button | Physical | Virtual Xbox |
|---|---|---|
| 1 | Square | x |
| 2 | Cross | a |
| 3 | Circle | b |
| 4 | Triangle | y |
| 5 | L1 | l1 |
| 6 | R1 | r1 |
| 9 | Share | select |
| 10 | Options | start |
| 11 | L3 | l3 |
| 12 | R3 | r3 |
| 13 | PS | home |
| **14** | **Touchpad click** | **— no equivalent** |

> L2 and R2 are independent analog triggers via HID (`hid_rx` / `hid_ry`). Both can be pressed simultaneously.
> **USB fully supported**: buttons, analog sticks, analog triggers, touchpad (XY tracking + click, mouse emulation), gyroscope (X/Y/Z axes visible in the Pads tab).
> **Bluetooth**: simplified report only (sticks + face buttons). Full BT support pending.

---

### 8BitDo Zero 2 — Bluetooth D-mode (VID:2DC8 PID:3230)

The D-pad is reported as two binary analog axes (X/Y), not a hat switch.
PadsWay maps them to virtual d-pad directions via `axis_actions`.

| HID button | Physical | Virtual Xbox |
|---|---|---|
| 1 | A | a |
| 2 | B | b |
| 4 | X | x |
| 5 | Y | y |
| 7 | LB | l1 |
| 8 | RB | r1 |
| 11 | Select | select |
| 12 | Start | start |

---

### 8BitDo Zero 2 — Bluetooth X-mode (VID:045E PID:02E0)

Discriminated from other controllers sharing this VID/PID by `product_name: "8BitDo Zero 2 gamepad"`.

Same button layout as D-mode above.

---

### 8BitDo Zero 2 — USB (VID:2DC8 PID:9018)

Both D-mode and X-mode USB share the same VID/PID (`9018`). The button layout differs between modes, so **only one USB profile can be active at a time**.

> **Recommendation:** configure the USB profile while the controller is connected in **Android mode (D-mode)**. Hold the button combination for D-mode before plugging in the USB cable, then run the Controller Binding Wizard. The resulting config will work correctly for D-mode USB. X-mode USB uses the same VID/PID but different button numbering — configuring it would overwrite the D-mode profile.

---

## License

PadsWay is released under the [MIT License](LICENSE) — © 2026 pooka-v1.

It builds on the MIT-licensed work of [Nefarius](https://github.com/nefarius)
([ViGEmBus](https://github.com/nefarius/ViGEmBus), [HidHide](https://github.com/nefarius/HidHide))
and bundles the MIT-licensed libraries [Dear ImGui](https://github.com/ocornut/imgui),
[nlohmann/json](https://github.com/nlohmann/json) and [spdlog](https://github.com/gabime/spdlog).
