# Bitácora — VirtualPad

> Registro histórico de sesiones de desarrollo.
> Cada entrada documenta qué se hizo, qué se aprendió y qué decisiones se tomaron.

---

## V1–V12 (resumen)

- **V1** — WinMM → ViGEm. Loop consola.
- **V2** — Config JSON por VID/PID.
- **V3** — LightningBot (FFX). tools/TriggerCount.
- **V4** — DSL de macros. LuluMacro. tools/lulu_macro_tests.csv.
- **V5** — Refactor modular.
- **V6** — Dear ImGui + PadEngine threaded. data/.
- **V7** — PadScanner visual.
- **V8** — virtualpad.json.
- **V9** — HIDInputSource + HIDScanner. DeviceCandidate.
- **V10** — spdlog + HidHide. Cadena completa verificada ✓.
- **V11** — Fix normalizeHIDAxis + hat switch.
- **V12** — DS4, F310, botones extra, perfiles de juego hot-swap.

---

## Sesión 2026/03/21 — V13: Fase C — salida teclado/ratón

### Reorganización de fases
- Fases A y B cerradas definitivamente.
- Plan: C (teclado/ratón), D (UI visual), E (drift + hot-plug), F (Steam Controller).

### Implementación

#### Acciones de botón — teclado y ratón
- `ButtonActionType::Keyboard` — combo de teclado con `SendInput` al pulsar/soltar. Edge-triggered.
- `ButtonActionType::MouseClick` — click de ratón (left/right/middle). Edge-triggered.
- Press: teclas en orden → down. Release: en orden inverso → up. Correcto para modificadores.
- Helpers estáticos: `keyNameToVK`, `sendKeyCombo`, `sendMouseButton`.

#### Movimiento de ratón desde stick analógico
- Targets de eje nuevos: `mouse_x` / `mouse_y` — campos en `GamepadState`, populados por ambos InputSource.
- PadEngine: acumulador sub-píxel (`mouseAccumX/Y`) → `SendInput(MOUSEEVENTF_MOVE)` cada tick.
- `speed` como parámetro de `AxisMapping` (default 15 px/tick a deflexión máxima).
- `setMouseSpeed`/`getMouseSpeed` expuestos en PadEngine.

#### Fixes
- **Zona muerta ratón**: `kMouseDeadZone = 0.12f` — stick Pro 2 derivaba levemente en reposo.
- **Inversión eje Y**: `mouse_y` lleva `invert` contrario al `right_y` del mismo eje físico.

### Verificado
- Pro 2 D-mode: stick derecho mueve cursor ✓, botón 13 (Home) → Alt+Tab ✓.

### Pendientes dentro de Fase C
- Axis overrides en perfiles de juego.
- Sintaxis teclado: cambiar array `["alt","tab"]` por cadena `"alt+tab"`.
- `freeze_output`: flag en acción keyboard → output neutral hasta primer input del mando.
