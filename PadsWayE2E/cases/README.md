# E2E assignment cases

`assignments.json` drives the data-driven E2E suite (`tests/e2e_assignments.cpp`). Each row assigns
one `target` to one `source` through the Mapeador's data path, presses the source on the fake
DualShock 4, checks the result, undoes the assignment and checks the source is back to its shipped
output. The expected result is derived from the target — only macros spell theirs out.

Run it: `x64\Release\PadsWayE2E.exe "[mapeador]"`. One row only: `-c "a -> r2"` (the row's label).
A malformed file (unknown name, missing field) fails the run with the row number and the reason.

## Short names

The same vocabulary as `controllers.json`, for both ends (physical source and virtual target):

| Kind            | Names                                                               |
|-----------------|---------------------------------------------------------------------|
| Face buttons    | `a` `b` `x` `y` (DS4 positions: Cross, Circle, Square, Triangle)    |
| Shoulders       | `l1` `r1`                                                           |
| Center          | `select` (Share) `start` (Options)                                  |
| Stick clicks    | `l3` `r3`                                                           |
| Dpad            | `dpad_up` `dpad_down` `dpad_left` `dpad_right`                      |
| Triggers        | `l2` `r2` (as target: fully pressed)                                |
| Stick half-axes | `left_x_pos` `left_x_neg` `left_y_pos` `left_y_neg`, same for `right_` (`_pos` = right/up) |

**Sources**: buttons (`a` `b` `x` `y` `l1` `r1` `select` `start` `l3` `r3`), dpad directions
(`dpad_up`..., saved under `dpad_remap`), stick half-axes (`left_x_pos`..., saved under
`axis_actions`; pressed = pushed all the way) and triggers (`l2` `r2`, saved under
`trigger_actions`; pressed = fully). A row whose target is its own source is rejected (it would
prove nothing).

**`home` (PS) is excluded** as source and target: a Home on the virtual Xbox pad makes Windows
inject its own Guide key (VK `0x07`, for the Game Bar), and it maps like any other button, so
leaving it out loses nothing. The loader rejects it.

## Targets

| Target                                                        | Checked                                        |
|---------------------------------------------------------------|------------------------------------------------|
| `"b"`, `"dpad_up"`, `"right_x_neg"`...                        | exactly that on the virtual pad, nothing else  |
| `"l2"` / `"r2"`                                               | that trigger fully pressed, nothing else       |
| `{ "type": "keyboard", "keys": ["shift", "k"] }`              | keys down in order, up in reverse; pad neutral |
| `{ "type": "mouse_click", "button": "middle" }`               | button down / up; pad neutral                  |
| `{ "type": "macro", "execution": "X + Y=300" }` + `"expect"`  | see below                                      |
| `{ "type": "bot", "name": "TestBot" }`                        | see below                                      |
| `{ "type": "mouse_move", "axis": "x" }` (half-axis source)    | see below                                      |

Target objects use the same encoding as a button entry in `controllers.json`. Macros are inline
(the DSL in `execution`, as the Mapeador's macro creator stores it) and must be Once mode: the
source is held, the macro must show `expect.plays` for `minMs`..`maxMs` and then end by itself.

```json
"expect": { "plays": ["x", "y"], "minMs": 200, "maxMs": 600 }
```

Bots: only `TestBot` (the loader rejects any other name). It's the suite's own bot
(`TestBotDLL/`, built with `PadsWayE2E`, never deployed to the app): while ON it loops A alone
200 ms, B alone 200 ms. The check presses the source (BotToggle ON), releases it and expects A, B,
A on the virtual pad; presses again (OFF) and expects the pad to stay neutral.

Mouse movement (`mouse_move`, `axis` `"x"`/`"y"`, optional `speed`, default 15): only from a stick
half-axis, and like the Mapeador it assigns the **whole axis** (the source half and its opposite).
Holding each half must move the cursor its way and nothing else — `_pos` = right (x) / up (y) —
with a neutral pad, and stop on release. Only the direction is checked (pointer ballistics).
Afterwards both halves must be back to their shipped output. In a chain it claims both halves.

Keyboard key names are the Mapeador's capture names: `a`-`z`, `0`-`9`, `shift`, `ctrl`, `alt`,
`space`, `f1`-`f12`. Mouse buttons: `left` `right` `middle` `x1` `x2`.

## Chains

`"chains"` holds rows of **several assignments active at once**, each an array of the same
`{ "source", "target" }` objects as `"cases"` (at least 2, no source twice):

```json
[ { "source": "a", "target": "b" },
  { "source": "b", "target": "y" } ]
```

All steps are assigned and saved together, then each source is pressed alone and must give exactly
its own target: `a` gives `b`, never `y` (no chaining through another assignment's physical
source), and `b` gives `y`. Afterwards everything is undone and each source must give itself
again. Worth one chain per source kind (button, dpad, half-axis, trigger), plus crossings between
kinds (`a -> left_x_pos` with `left_x_pos -> x`), and swaps (`x -> y` with `y -> x`).
Run only these: `"[chain]"`; one: `-c "a -> b, b -> y"`.

## Adding a name

- **Short name**: `setPressed()` in `src/E2ECases.cpp` (and the table above). A new *source* also
  needs a `SourceKind` in `parseAssignment()` — plus its own assign path in `E2EMapping` if it's
  stored somewhere new.
- **Keyboard key**: `keyVk()` in `src/E2ECases.cpp`. It's written out independently of the engine's
  own key table on purpose: a test that borrowed the engine's mapping could never catch a wrong entry.
- **Target type**: `parseTarget()` in `src/E2ECases.cpp` + a check in `tests/e2e_assignments.cpp`.
