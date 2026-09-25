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

**Sources**: buttons (`a` `b` `x` `y` `l1` `r1` `select` `start` `l3` `r3`) and dpad directions
(`dpad_up` `dpad_down` `dpad_left` `dpad_right`, saved under `dpad_remap`) for now. Trigger and
half-axis sources store their assignment elsewhere in `controllers.json` and come with their own
phase. A row whose target is its own source is rejected (the Mapeador saves it as no assignment).

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

Keyboard key names are the Mapeador's capture names: `a`-`z`, `0`-`9`, `shift`, `ctrl`, `alt`,
`space`, `f1`-`f12`. Mouse buttons: `left` `right` `middle` `x1` `x2`.

## Adding a name

- **Short name**: `setPressed()` in `src/E2ECases.cpp` (and the table above). A new *source* also
  needs `isAssignableSource()` — or its own assign path in `E2EMapping` if it isn't a button entry.
- **Keyboard key**: `keyVk()` in `src/E2ECases.cpp`. It's written out independently of the engine's
  own key table on purpose: a test that borrowed the engine's mapping could never catch a wrong entry.
- **Target type**: `parseTarget()` in `src/E2ECases.cpp` + a check in `tests/e2e_assignments.cpp`.
