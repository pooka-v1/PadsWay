# PadsWay — Macro System

Macros can be defined in two ways:

**Option A — inline in `data/controllers.json`** (the macro lives alongside the button):
```json
"15": { "type": "macro", "name": "MacroName", "execution": "A, B, X" }
```

**Option B — in the library `data/macros.json`** (reusable from multiple buttons/controllers):
```json
[
  { "name": "MacroName", "execution": "A, B, X" }
]
```
```json
"15": { "type": "macro", "name": "MacroName" }
```

If the button has an inline `"execution"`, it is used directly. Otherwise, the engine looks up the macro by `"name"` in `macros.json`.

[Leer en español](MACROS.es.md)

---

## Visual Macro Editor

Open from the **Pads tab → Macros button**. The left panel lists all macros in `data/macros.json` with a visual preview of the selected macro's steps. Use **New**, **Edit**, or **Copy** to open the editor modal.

### Building steps with the token picker

- **Click an icon** → creates a new Press step at the end of the sequence (no selection)
- **Click a step** in the sequence → selects it (blue highlight)
- **Click an icon while a Press step is selected** → adds that token to the step (simultaneous combo)
- **Click the same icon again on a selected step** → removes that token
- **Removing the last token** from a step → deletes the step

The sequence renders combos as `[→]+[A]`, matching the DSL `CR + A`.

### Token picker sections

| Section | Contents |
|---|---|
| **Buttons** | Face buttons, bumpers, triggers, stick clicks, Start / Select |
| **D-pad** | 8 directions · Spin CCW/CW · HCF ← · HCF → · QCF ← · QCF → |
| **Analog L / R** | 8 directions per stick · Spin CCW/CW · HCF ← · HCF → · QCF ← · QCF → |
| **Macros** | Insert macro from library (MacroRef) |

### Step types

| Type | How to create |
|---|---|
| **Press** | Click any button/direction icon |
| **Wait** | Set ms value → "Add Wait" |
| **Group** | Select a range of steps → set repeat mode → "Create repeat" |
| **MacroRef** | "Insert macro..." → pick from library |

**Hold ms** — with a Press step selected, sets how long the button is held. Default 0 uses `DEFAULT_PRESS_MS` (80ms).

### Repeat controls

With one or more steps selected, choose a mode and click **Create repeat**:

| Mode | DSL | Description |
|---|---|---|
| Loop (ms) | `*5000` | Repeat for N milliseconds |
| N times in (ms) | `*1000/10` | Exactly N repetitions in N ms |
| While held | `*UP` | Repeat while the physical button is held |
| Toggle | `*DO` | First press starts, second press stops |

Selecting a **single step** applies the repeat spec directly: `A*UP`.
Selecting a **range** wraps the steps in a Group: `(A, B, C)*5000`.

### Spin presets

The spin buttons (CCW / CW) insert 8 directional Press steps in circular order at **30ms each**. The d-pad spin starts at Right (CW) or Left (CCW); the analog spins do the same for L and R stick independently. Useful for rotation macros (see Lulu Overdrive example).

### Motion presets

Predefined directional sequences for fighting game inputs (**40ms per step**):

| Button | Sequence | Tokens | Use |
|---|---|---|---|
| HCF ← | Half circle left | CR → CDR → CD → CDL → CL | Half-circle back |
| HCF → | Half circle right | CL → CDL → CD → CDR → CR | Half-circle forward |
| QCF ← | Quarter circle left | CD → CDL → CL | Quarter-circle back |
| QCF → | Quarter circle right | CD → CDR → CR | Quarter-circle forward |

Available for d-pad and analog (L and R stick). To build a complete special move: insert the motion preset, then click the attack button to add it as a separate Press step. Adjust hold times as needed.

### Insert macro (MacroRef)

**Insert macro...** opens a dropdown of all library macros. Selecting one inserts its full DSL **inline** at the end of the sequence (expanded at insert time — later edits to the source macro do not affect the referencing one).

### DSL field

The raw DSL string is always visible and editable below the steps. Editing it manually switches to **complex mode**: the visual step list is hidden and icon clicks append tokens to the DSL string directly. Complex mode allows arbitrarily advanced macros while still using the picker for quick insertion.

### Saving

Give the macro a name and click **Save**. The macro is written to `data/macros.json` and immediately hot-reloaded by the engine — no restart required.

---

## DSL Reference

---

## Tokens — Buttons

| Token | Button                    |
|-------|---------------------------|
| `A`   | A                         |
| `B`   | B                         |
| `X`   | X                         |
| `Y`   | Y                         |
| `L1`  | LB (left bumper)          |
| `R1`  | RB (right bumper)         |
| `L2`  | LT (left trigger)         |
| `R2`  | RT (right trigger)        |
| `L3`  | LS (left stick click)     |
| `R3`  | RS (right stick click)    |
| `CU`  | D-pad up                  |
| `CD`  | D-pad down                |
| `CL`  | D-pad left                |
| `CR`  | D-pad right               |
| `CUR` | Diagonal up-right         |
| `CUL` | Diagonal up-left          |
| `CDR` | Diagonal down-right       |
| `CDL` | Diagonal down-left        |
| `ST`  | Start                     |
| `SE`  | Select / Back             |
| `HO`  | Home (ignored — not injectable on virtual Xbox 360) |

---

## Tokens — Analog

Format: `LAX`, `LAY`, `RAX`, `RAY` followed by a float value between `-1.0` and `1.0`.

| Token   | Axis                         |
|---------|------------------------------|
| `LAX`   | Left stick, X axis           |
| `LAY`   | Left stick, Y axis           |
| `RAX`   | Right stick, X axis          |
| `RAY`   | Right stick, Y axis          |

**Value convention:**
- X: `-1.0` = full left · `0` = center · `+1.0` = full right
- Y: `-1.0` = full down · `0` = center · `+1.0` = full up

**Diagonals (unit circle):** use `±0.71` (= cos/sin of 45°), not `±0.5`.
`0.5+0.5` gives the right angle but the stick only reaches 70% of travel.

---

## Operators

| Syntax         | Meaning                                                     |
|----------------|-------------------------------------------------------------|
| `A, B, C`      | Sequence: press A, then B, then C (200ms between each)      |
| `A + Y`        | Combo: A and Y at the same time                             |
| `B=1000`       | Hold: keep B pressed for 1000ms                             |
| `500`          | Wait: pause of 500ms (bare number in the sequence)          |
| `A*5000`       | Repeat A for 5000ms (default interval: 200ms)               |
| `A*1000/10`    | Repeat A 10 times in 1000ms                                 |
| `A*UP`         | Repeat A while the physical button is held                  |
| `A*DO`         | Toggle: starts on press, stops on next press                |
| `(A, B)*5000`  | Repeat the sequence A,B in a loop for 5000ms                |
| `(A, B)*UP`    | Repeat the sequence while the button is held                |
| `(A, B)*DO`    | Toggle the sequence                                         |
| `(A, B)*1000/5`| Repeat the sequence 5 times in 1000ms                       |

**Combinable:** `A, 200, (B + X)*3000` — press A, wait 200ms, then loop B+X for 3s.

---

## Default timings

| Constant           | Value  | Description                                        |
|--------------------|--------|----------------------------------------------------|
| `DEFAULT_STEP_MS`  | 200ms  | Slot for each item in a sequence (provisional)     |
| `DEFAULT_PRESS_MS` | 80ms   | Press duration within the slot                     |

> The 200ms value is provisional. Measure the user's actual average press time
> and adjust `DEFAULT_STEP_MS` in `MacroParser.h`.

To use a duration other than the default: `A=150` (hold+slot = 150ms).

### Hold vs slot — the duty cycle (avoiding flicker on repeats)

Every step has two timings: the **hold** (how long the button is actually pressed) and the
**slot** (how long until the next step starts). By default they differ — `hold = 80ms` inside a
`slot = 200ms` — so the button is *released* for the remaining ~120ms of the slot. That gap is
harmless in a one-shot sequence, but inside a repeat (`*UP`, `*N`, loops) it surfaces as a
**flicker**: the virtual button blinks off between cycles instead of staying held.

Use `=` to make the hold equal the slot (`A=200`). The button is then pressed for the whole
cycle — a continuous hold with no gap. This is the fix for "keep a direction/button held while a
repeat runs" cases, e.g. a run-while-held macro:

```
(LAX1+LAY0+A=200)*UP        // right stick + A held continuously while the button is held
```

> Rule of thumb: a **gap** between presses (default `hold < slot`) is what you want for *mashing*;
> **no gap** (`hold = slot` via `=`) is what you want for a *sustained* hold. Note that in the
> Component System a half-axis range already owns its target (first match wins), so "direction +
> button" held together is expressed precisely with a macro like the one above.

### D-pad steps suppress physical input

When a macro step includes any d-pad token (`CU`, `CD`, `CL`, `CR`, `CDR`, …), the macro **takes full ownership of the d-pad** for that step. Physical d-pad input from the player is suppressed.

This is intentional: a directional macro needs to control movement precisely. If the player is pressing right and the macro sends down, the game must receive pure down — not the diagonal that a simple OR would produce.

Macros with **no d-pad tokens** are unaffected — physical d-pad passes through normally.

### Minimum step duration for games

Most games run at 60 fps and read input once per frame (~16ms). **If a step is shorter than ~16ms, the game may not register it.**

- Safe minimum per direction step: **~40ms** (≈ 2–3 frames; accounts for polling jitter)
- Safe minimum for the button press: **~80ms** (≥ 5 frames)

For fighting game motions, use `=` to reduce both hold and slot:

```
CD=80, CDL=50, CL + Y=100
```

> Without `=`, each step defaults to 200ms — correct for menus and cutscenes,
> but too slow for special moves. `A=80` sets both the hold duration and the
> slot (time before the next step starts) to 80ms.

---

## Tested examples

### Auron Overdrive (FFX)
Button sequence for the overdrive. Adjust buttons and timings to match the game's actual sequence.
```json
          "name": "BanishingBlade",
          "execution": "CU, L1, CD, R1, CR, CL, Y"
```

### Lulu Overdrive (FFX)
Rotates the right analog stick through 8 points on the unit circle, ~4 rotations/second.
Each position is held for 30ms. Runs for 10 seconds and stops automatically.
```json
		  "name": "LuluOverdrive",
          "execution": "A, 1500,(RAX0+RAY1=17, RAX0.383+RAY0.924=17, RAX0.71+RAY0.71=17, RAX0.924+RAY0.383=17, RAX1+RAY0=17, RAX0.924+RAY-0.383=17, RAX0.71+RAY-0.71=17, RAX0.383+RAY-0.924=17, RAX0+RAY-1=17, RAX-0.383+RAY-0.924=17, RAX-0.71+RAY-0.71=17, RAX-0.924+RAY-0.383=17, RAX-1+RAY0=17, RAX-0.924+RAY0.383=17, RAX-0.71+RAY0.71=17, RAX-0.383+RAY0.924=17)*7000",
```

> Result: 7 rotations achieved (manual maximum ~6). ✓

### Soul Calibur grab
Two buttons that are difficult to press simultaneously on a PlayStation controller.
```json
"execution": "A + Y"
```

### Hadouken (Street Fighter / fighting games)
Facing right. Quarter circle forward + punch/kick.
D-pad motion: down → down-right → right + button.

Each direction must be held long enough for the game to register it (≥ 2 frames at 60 fps ≈ 30ms). Use `=` to reduce both hold and slot per step.

```
CD=80, CDR=50, CR + X=100
```

Facing left (mirror):
```
CD=80, CDL=50, CL + X=100
```

`CDR` / `CDL` are single diagonal tokens (down-right / down-left) — they press both directions simultaneously and are not the same as `CD + CR` (which would be a combo, not a sequence).

The default 200ms per step works for Konami codes and menus but is too slow for fighting game specials.

> **Visual editor shortcut:** use the **QCF →** motion preset button in the d-pad section (inserts CD=40, CDR=40, CR=40 automatically), then click the attack button icon to add it as a separate step. Adjust hold times with the Hold ms field.

### Konami Code
```json
"execution": "(CU, CU, CD, CD, CL, CR, CL, CR, B, A, ST)*1"
```
> With `*1` it executes once (1ms total time, the sequence completes in full).
> Alternative without repeat: `"CU, CU, CD, CD, CL, CR, CL, CR, B, A, ST"`

### Button mash (Chun-Li style)
Press A repeatedly while the physical button is held.
```json
"execution": "A*UP"
```

### Mash with exact speed
10 presses of A in 1 second.
```json
"execution": "A*1000/10"
```
