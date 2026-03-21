# Bitácora — VirtualPad

> Registro histórico de sesiones de desarrollo.
> Cada entrada documenta qué se hizo, qué se aprendió y qué decisiones se tomaron.

---

## V1–V9 (resumen)

- **V1** — WinMM → ViGEm. Loop consola.
- **V2** — Config JSON por VID/PID. nlohmann/json.
- **V3** — LightningBot (FFX). tools/TriggerCount (calibración).
- **V4** — DSL de macros. LuluMacro. tools/lulu_macro_tests.csv.
- **V5** — Refactor modular (input/, output/, config/, bots/, macros/).
- **V6** — Dear ImGui + PadEngine threaded. configs/ → data/.
- **V7** — PadScanner visual (Tab Scanner).
- **V8** — virtualpad.json. Prep dual-API.
- **V9** — HIDInputSource + HIDScanner. ValCaps. hid_brake/hid_accel. DeviceCandidate.

---

## V10 — ~2026/03/19 — spdlog + HidHide (Fase B)

- spdlog (header-only): consola + fichero rotativo. Nivel configurable en virtualpad.json.
- HidHideClient: whitelist VirtualPad.exe, blacklist mando físico mientras está activo.
- Cadena completa verificada: Pro 3 D-mode BT → VirtualPad → ViGEm → Steam ✓.

---

## Sesión 2026/03/19 — V11: fixes HIDInputSource

### Decisiones de arquitectura
- **WinMM queda como legacy** para D-mode (no se elimina).
- **XInput descartado**: no aporta nada que HID no dé.
- **X-mode vía HID = prototipo**: solo expone 10 botones estándar Xbox. Se mantiene WinMM para X-mode.
- **D-mode** es el camino para botones extra (Home, L4, R4, Lp, Rp).

### Bug: `normalizeHIDAxis` — ejes unsigned `[0, -1]`
- **Síntoma**: sticks y triggers devolvían 0.0f siempre en modo HID.
- **Causa**: descriptor HID reporta `logMax = -1` (LONG) para ejes unsigned → `range = -1` → early return 0.
- **Fix**: añadido `bitSize` a `ValueRange`. Si `logMax < logMin` (unsigned), usa `uMax = (1 << bitSize) - 1`.
- **Afecta**: todos los mandos HID con descriptor unsigned (Pro 2 D-mode, F310).

### Bug: Hat switch encoding `[1,8]` vs `[0,8]`
- **Síntoma**: diagonal NW no funcionaba en Pro 3 X-mode.
- **Causa**: Pro 3 X-mode usa hat `[1,8]` donde 8=NW y center=0 (fuera de rango).
- **Fix**: detección por rango — si `hatValue < logMin || hatValue > logMax` → neutral.
- **Backward compatible**: Pro 2 D-mode usa `[0,8]` — sigue funcionando.

### Estado al cerrar
- Pro 2 D-mode: HID con normalización de ejes correcta ✓
- F310 D-mode: ídem ✓
