# Bitácora — VirtualPad

> Registro histórico de sesiones de desarrollo.
> Cada entrada documenta qué se hizo, qué se aprendió y qué decisiones se tomaron.

---

## V1Pro3 — ~2026/03/03 — Base: lectura WinMM → virtualización ViGEm

- Primer prototipo funcional: WinMM → `GamepadState` → ViGEmBus.
- `IInputSource` (strategy pattern), `EightBitDoInputSource`, `ViGEmOutputAdapter`.
- Loop principal en consola. Hardware objetivo: 8BitDo Pro 2/Pro 3 D-mode.

---

## V2Pro3 — ~2026/03/04 — Configuración dinámica por VID/PID

- `ConfigLoader` + `ControllerConfig`: mappings desde JSON indexados por VID/PID.
- Soporte `dinput` / `xinput`. Librería `nlohmann/json` añadida.

---

## V3Pro3 — ~2026/03/10 — Bot visual: LightningBot (FFX Thunder Plains)

- `LightningBot`: detecta flash de pantalla en thread dedicado → pulsa botón virtual.
- `tools/TriggerCount/`: herramienta de calibración de timings. No forma parte del producto.

---

## V4Pro3 — ~2026/03/15 — Sistema de macros (consola pura, pre-GUI)

- `MacroParser` + DSL compacto: secuencias, combos, holds, repeats, analógicos.
- `LuluMacro`: rotación stick derecho ~4 RPM para FFX (7 vueltas conseguidas).
- `tools/lulu_macro_tests.csv`: datos de calibración del timing.

---

## V5Pro3 — ~2026/03/15 — Refactor: modularización en directorios

- Fuentes reorganizados en `input/`, `output/`, `config/`, `bots/`, `macros/`.

---

## V6Pro3 — ~2026/03/15 — GUI con ImGui + threading (PadEngine / AppWindow)

- Dear ImGui (Win32 + D3D11). `PadEngine` en hilo de fondo (8ms tick). `configs/` → `data/`.

---

## V7Pro3 — ~2026/03/15 — PadScanner: enumeración visual de mandos WinMM

- `PadScanner`: enumera puertos WinMM, lee valores raw. Tab Scanner en ImGui.

---

## V8 — ~2026/03/17 — Consolidación + data/ + virtualpad.json + preparación dual-API

- `data/virtualpad.json`: VID/PID del mando virtual. Preparación interna para HID.

---

## V9 — ~2026/03/18 — Soporte HID: HIDInputSource + HIDScanner + refactor PadEngine

**Qué se hizo:**
- `HIDScanner` (`input/`): enumera dispositivos HID filtrando por usage page 0x01 (Joystick/Gamepad). Devuelve VID, PID, usage, device path y product name.
- `HIDInputSource` (`input/`): lectura de mandos via HID API (ReadFile overlapped).
  - HID preparsed data y ValCaps para normalización automática de ejes.
  - Usages mapeados: hid_x, hid_y, hid_z, hid_rx, hid_ry, hid_rz, hid_brake (0xC4), hid_accel (0xC5).
  - D-pad HAT switch parsing.
  - Fix del Report ID mismatch del Pro 2 D-mode BT: si `HidP` falla con `INCOMPATIBLE_REPORT_ID`, swap temporal de `buf[0]` y retry.
- **Refactor PadEngine**: nuevo `struct DeviceCandidate` que unifica WinMM y HID. `selectDevice(int index)` reemplaza `selectDevice(UINT port)`. `getCandidates()` reemplaza la lista WinMM pura.
- Tab Scanner muestra dos secciones: dispositivos WinMM y dispositivos HID.
- **Pro 2 D-mode**: funciona vía HID con gatillos analógicos reales (hid_brake→triggerR, hid_accel→triggerL).
