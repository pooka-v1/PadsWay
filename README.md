# VirtualPad

Reads physical gamepads (WinMM, HID, XInput) and forwards them as a virtual Xbox 360 controller via ViGEm.
Supports macros, bots, and JSON-based configuration — no code changes needed.

[Leer en español](README.es.md)

---

## Requirements

### To run

| Dependency | Purpose |
|---|---|
| Windows 10/11 | Required APIs: WinMM, HID, DirectX 11 |
| [ViGEmBus driver](https://github.com/nefarius/ViGEmBus/releases) | Creates the virtual Xbox 360 controller |
| [HidHide driver](https://github.com/nefarius/HidHide/releases) | Hides the physical gamepad from games to prevent double input |

> **ViGEmBus** and **HidHide** are by the same author (Nefarius) and install like any Windows driver.
> VirtualPad controls them automatically — no need to touch their interfaces manually.

### To build

- Visual Studio 2022 (C++17 + Windows SDK)
- All other dependencies are included in the repository (`imgui/`, `nlohmann/`)

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

The `"mode"` field determines which API is used to read the device:

| mode | API | When to use |
|---|---|---|
| `"dinput"` | WinMM (`joyGetPosEx`) | Controller that appears in `joy.cpl` and in WinMM |
| `"hid"` | HID raw (`HidP_*`) | Controller that appears in `joy.cpl` but **not** in WinMM |
| `"xinput"` | WinMM compat layer | XInput controller (goes through WinMM in compatibility mode) |

> **Tip:** open the Scanner tab with the controller connected. If it appears under **WinMM**, use `"dinput"`. If it only appears under **HID-only**, use `"hid"`.

---

## Mode `"dinput"` — WinMM

Axes use the field names from `JOYINFOEX`:

| Axis name | WinMM field | Typical use |
|---|---|---|
| `"dwXpos"` | `dwXpos` | Left stick X |
| `"dwYpos"` | `dwYpos` | Left stick Y |
| `"dwZpos"` | `dwZpos` | Right stick X (D-mode) or combined triggers (X-mode) |
| `"dwRpos"` | `dwRpos` | Right stick Y |
| `"dwUpos"` | `dwUpos` | Right stick X (X-mode on some controllers) |
| `"dwVpos"` | `dwVpos` | Auxiliary axis |

The D-pad in DInput is usually a POV hat:
```json
"dpad": "pov"
```

### Example — 8BitDo Pro 3 (D-mode, Bluetooth)
```json
{
  "vid": "2DC8", "pid": "6009",
  "source_name": "8BitDo Pro 3 (D-mode)",
  "mode": "dinput",
  "buttons": {
    "1":  { "physical": "b",      "virtual": "b"      },
    "2":  { "physical": "a",      "virtual": "a"      },
    "3":  { "physical": "rp"                          },
    "4":  { "physical": "y",      "virtual": "y"      },
    "5":  { "physical": "x",      "virtual": "x"      },
    "6":  { "physical": "lp"                          },
    "7":  { "physical": "l1",     "virtual": "l1"     },
    "8":  { "physical": "r1",     "virtual": "r1"     },
    "9":  { "physical": "l2",     "type": "trigger",  "target": "l2" },
    "10": { "physical": "r2",     "type": "trigger",  "target": "r2" },
    "11": { "physical": "select", "virtual": "select" },
    "12": { "physical": "start",  "virtual": "start"  },
    "13": { "physical": "home",   "virtual": "home"   },
    "14": { "physical": "l3",     "virtual": "l3"     },
    "15": { "physical": "r3",     "virtual": "r3"     },
    "17": { "physical": "l4"                          },
    "18": { "physical": "r4"                          }
  },
  "axes": {
    "dwXpos": { "target": "left_x",  "invert": false },
    "dwYpos": { "target": "left_y",  "invert": true  },
    "dwZpos": { "target": "right_x", "invert": false },
    "dwRpos": { "target": "right_y", "invert": true  }
  },
  "dpad": "pov"
}
```

---

## Mode `"xinput"` — WinMM compat layer

XInput controllers (Pro 3/Pro 2 in X-mode, F310 in X-mode) don't expose separate axes for
L2 and R2 — they share a single Z axis: positive → L2, negative → R2.
This is a WinMM limitation; both triggers cannot be pressed simultaneously.

```json
"dwZpos": { "target": "trigger_combined", "invert": false }
```

The D-pad in X-mode also goes as a POV hat:
```json
"dpad": "pov"
```

### Example — 8BitDo Pro 3 (X-mode, Bluetooth)
```json
{
  "vid": "2DC8", "pid": "310B",
  "source_name": "8BitDo Pro 3 (X-mode)",
  "mode": "xinput",
  "buttons": {
    "1": "a", "2": "b", "3": "x", "4": "y",
    "5": "l1", "6": "r1",
    "7": "select", "8": "start", "9": "l3", "10": "r3"
  },
  "axes": {
    "dwXpos": { "target": "left_x",  "invert": false },
    "dwYpos": { "target": "left_y",  "invert": true  },
    "dwZpos": { "target": "trigger_combined", "invert": false },
    "dwRpos": { "target": "right_y", "invert": true  },
    "dwUpos": { "target": "right_x", "invert": false }
  },
  "dpad": "pov"
}
```

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

> VirtualPad automatically detects the real HID page from the device descriptor —
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
Use the **Scanner tab** in VirtualPad to identify which number lights up when pressing each physical button.

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
| `"home"` | Guide (not injectable on virtual Xbox 360) |
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
but VirtualPad's convention is `+1.0 = up`. Adjust based on what you see in the scanner.

---

## How to discover the mapping for a new controller

1. Connect the controller
2. Open the **Scanner tab** in VirtualPad
3. If it appears under **WinMM**: press each button and note which number lights up → use `mode: "dinput"` or `"xinput"`
4. If it appears under **HID-only**: the console log at startup shows all `ValCaps` (Usage ID and range) → use `mode: "hid"` and map the usages you see
5. Move each stick and observe which axis changes and in which direction
6. For triggers: look for `Usage=0xC4` / `Usage=0xC5` (Simulation Controls) or `Usage=0x33`/`0x34` (Rx/Ry in Generic Desktop) in the log
7. Add the entry to `controllers.json` and restart VirtualPad

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

| WinMM button | Physical | Virtual Xbox | Use |
|---|---|---|---|
| 3 | Rp (right paddle) | — | macro / bot in game profile |
| 6 | Lp (left paddle) | — | macro / bot in game profile |
| 17 | L4 | — | macro / keyboard in game profile |
| 18 | R4 | — | macro / keyboard in game profile |

### Game profile format

```json
{
  "profile_name": "Final Fantasy X",
  "overrides": [
    {
      "vid": "2DC8",
      "pid": "6009",
      "_note": "Pro 3 D-mode: Rp=BanishingBlade, Lp=LightningBot, R3=LuluOverdrive",
      "buttons": {
        "3":  { "type": "macro", "name": "BanishingBlade" },
        "6":  { "type": "bot",   "name": "LightningBot"   },
        "15": { "type": "macro", "name": "LuluOverdrive"  }
      }
    }
  ]
}
```

- Only declare the buttons that change from the base. Everything else is inherited unchanged.
- You can add one `"overrides"` entry per controller you want to customize (different VID/PID).
- If a profile reassigns a standard button (like 15 = R3), that button stops working as a virtual button while the profile is active. This is an intentional decision.
- JSON doesn't support native comments. Fields prefixed with `_` (`_note`, `_button_map`, `_hid_prototype`) are ignored by the parser and serve as annotations.

### Adding a new profile

1. Create `data/GameName.json` with the structure above.
2. Declare only the buttons you need to change; leave the rest in the base.
3. Select the profile from VirtualPad's UI (pending implementation) or from `virtualpad.json`.

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
| `data/FinalFantasyX.json` | Game profile for FFX (overrides on top of base) |
| `data/MonsterHunterStories2.json` | Game profile for MHS2 (overrides on top of base) |
| `data/macros.json` | Reusable macro library |
| `data/virtualpad.json` | VID/PID of the virtual controller, locale, log level, and stick thresholds |
| `data/strings/strings_en.json` | UI strings — English |
| `data/strings/strings_es.json` | UI strings — Spanish |

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

| WinMM button | Physical | Virtual Xbox |
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

| WinMM button | Physical | Virtual Xbox |
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

> L2 and R2 are analog but share the Z axis (trigger_combined). They cannot both be pressed simultaneously.

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

| WinMM button | Physical | Virtual Xbox |
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

> L2/R2 share the Z axis (trigger_combined).

---

### Logitech F310 — D-mode (VID:046D PID:C216)

| WinMM button | Physical | Virtual Xbox |
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

| WinMM button | Physical | Virtual Xbox |
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

### 8BitDo Zero 2 — Bluetooth (VID:2DC8 PID:6006)

⚠️ **Known limitation:** the Zero 2 shares VID and PID with the Pro 2 in Bluetooth mode.
VirtualPad cannot distinguish between them — it will load the Pro 2 config for both.

When running with the Zero 2, the D-pad is reported as 4 analog axes instead of a hat switch,
so directional input does not work correctly with the Pro 2 profile.

**Workaround:** pending investigation. Connect the controller and use the Scanner tab to inspect
how the Zero 2 actually reports its axes and D-pad, then create a dedicated profile or add a
distinguishing mechanism.
