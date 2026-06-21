# PadsWay — Installation Guide (Quick Start)

A short guide to get PadsWay running from scratch: install the drivers, unzip the app, and
pair your controller. For the technical details (`controllers.json` format, manual editing,
per-controller button maps, etc.) see the [full README](README.md).

[Leer en español](QUICK_START.es.md)

---

## In 4 steps

1. Install **ViGEmBus** (required) and **HidHide** (recommended).
2. Unzip PadsWay into a folder you can write to.
3. Run `PadsWay.exe` and pair your controller with the wizard.
4. Open your game: a virtual Xbox 360 controller appears.

---

## 1. Requirements

| Requirement | Detail |
|---|---|
| OS | Windows 10 64-bit (or later) |
| GPU | Any — the app uses DirectX 11 with fallback to DX10 and software rendering (WARP) |
| Hardware | Modest. Runs on low-end machines (tested on a 1.44 GHz Intel Atom with 4 GB RAM) |
| ViGEmBus | **Required** — creates the virtual Xbox 360 controller |
| HidHide | **Recommended** — hides the physical controller from games to avoid double input |

> ViGEmBus and HidHide are by the same author (Nefarius) and install like any Windows driver.
> PadsWay controls them automatically — no need to open their interfaces by hand.

---

## 2. Install the drivers

Only needed once.

1. **ViGEmBus** — download the installer from the official releases:
   https://github.com/nefarius/ViGEmBus/releases
   Run it and follow the steps (it requires administrator rights).

2. **HidHide** — download the installer from:
   https://github.com/nefarius/HidHide/releases
   Run it the same way. Reboot if the installer asks you to.

> **Versions validated with PadsWay:** `ViGEmBus_1.22.0_x64_x86_arm64` and
> `HidHide_1.5.230_x64`. Install exactly these versions; don't update the drivers on your own
> unless you know what you're doing.

---

## 3. Download and unzip PadsWay

1. Download the `PadsWay-vX.Y-win64.zip` package.
2. Unzip it into a folder **you can write to** — for example `Documents`, the `Desktop`, or
   your own folder such as `C:\PadsWay`.
   - **Do not** leave it inside `C:\Program Files`: Windows (UAC) blocks saving the
     configuration there.
3. PadsWay is **portable**: it installs nothing and touches no registry keys. To uninstall,
   just delete the folder.

The unzipped folder contains:

```
PadsWay.exe
data/      (strings, layouts, state map)
images/    (controller and macro icons)
```

---

## 4. First launch

1. Run `PadsWay.exe`.
2. The interface starts **in English** by default. To switch to Spanish, edit (or create) the
   file `data/virtualpad.json` with:

   ```json
   { "locale": "es" }
   ```

   Save and reopen the app.
3. Being a clean build, it starts with **no controllers configured**. That's expected — you
   set yours up in the next step.

---

## 5. Set up and test your controller

Four quick steps take you from "plugged in" to "working in games":

**1. Check detection — Scanner tab.** Connect your controller, open the **Scanner** tab and
select your device. Press a few buttons: the scanner should react. This confirms PadsWay reads
your controller over HID.

**2. Get a layout — Layout tab.** Open the **Layout** tab. If a layout for your controller
already exists, use it. If not, create one with **[New]** (it asks for an ID). It does not have
to be an exact replica — you can reuse another controller's images (e.g. the DualShock artwork)
to build, say, an Xbox-style pad.

**3. Pair the controller.** With your layout selected, click **Pair controller** and complete
the wizard's 5 stages:
   1. **Select controller** — pick your device (the ViGEm virtual controller is filtered out).
   2. **Name** — give it a friendly name.
   3. **Buttons** — press each button as the wizard asks.
   4. **Axes and triggers** — move each stick / press each trigger fully in the arrow's
      direction (left stick X → **right**; Y → **down**; triggers → full press).
   5. **Review** — check and save. This creates/updates `data/controllers.json`.

   > **DualShock 4:** pair it over **USB** for now (Bluetooth has a known calibration issue).

**4. Test it — Pads tab.** Open the **Pads** tab and check the controller responds. If it does,
it's ready to use in your games.

> If the **Pads** tab doesn't show the controller after saving, **restart the app** (known
> issue, under investigation).

---

## 6. Use it in a game

1. With the controller paired, PadsWay emits a **virtual Xbox 360 controller**.
2. Open your game or Steam: it should detect an "Xbox 360 Controller".
3. HidHide hides the physical controller so the game doesn't receive double input.

---

## 7. (Optional) Macros and game profiles

- You can create **macros** and **per-game profiles** from the UI itself.
- The full macro syntax is in [MACROS.md](MACROS.md).
- Ready-made per-game profiles and macros are available in the
  [PadsWay-Presets](https://github.com/pooka-v1/PadsWay-Presets) repository.

---

## Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| The game detects no controller | ViGEmBus isn't installed, or you didn't reboot after installing it |
| The game receives double input | HidHide is missing, or isn't hiding the physical controller |
| The configuration isn't saved | The folder is under `Program Files`; move it to a writable location |
| Left stick X won't calibrate in the wizard | Known issue (under investigation); retry the step |
| The controller doesn't appear after pairing | Restart the app (the Pads tab doesn't auto-refresh, under investigation) |
| Graphics issues on launch | The software-rendering fallback should cover it; update your GPU drivers |

---

> Full technical docs → [README.md](README.md) · Macro syntax → [MACROS.md](MACROS.md)
