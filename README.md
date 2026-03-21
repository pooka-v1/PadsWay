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
    "1":  "b",
    "2":  "a",
    "_3": "Rp (right paddle) — no Xbox equivalent, use in game profile",
    "4":  "y",
    "5":  "x",
    "_6": "Lp (left paddle) — no Xbox equivalent, use in game profile",
    "7":  "l1",
    "8":  "r1",
    "9":  { "type": "trigger", "target": "l2" },
    "10": { "type": "trigger", "target": "r2" },
    "11": "select",
    "12": "start",
    "13": "home",
    "14": "l3",
    "15": "r3",
    "_17": "L4 — no Xbox equivalent, use in game profile",
    "_18": "R4 — no Xbox equivalent, use in game profile"
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

### Button action types

```json
"N": "a"                                      // simple virtual button
"N": { "type": "trigger", "target": "l2" }   // digital trigger (L2 or R2)
"N": { "type": "macro",   "name": "MacroName" }
"N": { "type": "bot",     "name": "LightningBot" }
"N": { "type": "keyboard",    "keys": ["alt", "tab"] }
"N": { "type": "mouse_click", "button": "left" }
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

### Why buttons like Lp, Rp, L4, R4 are not in the base

Some controllers have buttons that don't exist in the Xbox 360 protocol (no virtual equivalent).
In the base config they are left unmapped — they have no effect. They are used exclusively from game profiles, assigned to macros or bots.

Example — Pro 3 D-mode (WinMM buttons):

| WinMM button | Physical | In base | Reason |
|---|---|---|---|
| 3 | Rp (right paddle) | — | no Xbox 360 equivalent |
| 6 | Lp (left paddle) | — | no Xbox 360 equivalent |
| 17 | L4 | — | no Xbox 360 equivalent |
| 18 | R4 | — | no Xbox 360 equivalent |

> Future consideration: if `GamepadState` is extended with custom fields (l4, r4, lp, rp) and `ViGEmOutputAdapter` maps them, these buttons could have their own virtual function. For now, they are only useful as macro/bot triggers.

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

## Data files

| File | Description |
|---|---|
| `data/controllers.json` | Base configuration for physical controllers |
| `data/FinalFantasyX.json` | Game profile for FFX (overrides on top of base) |
| `data/macros.json` | Reusable macro library |
| `data/virtualpad.json` | VID/PID of the virtual controller created by ViGEm + log level |

See [MACROS.md](MACROS.md) for the complete macro syntax.

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

> L2 and R2 are independent analog triggers (dwUpos / dwVpos). Both can be pressed simultaneously.
> Advanced DS4 features (touchpad XY, gyroscope, LEDs, rumble) pending for future phases.
