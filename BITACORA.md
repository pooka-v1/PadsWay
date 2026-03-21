# Bitácora — VirtualPad

> Registro histórico de sesiones de desarrollo.
> Cada entrada documenta qué se hizo, qué se aprendió y qué decisiones se tomaron.

---

## V1Pro3 — ~2026/03/03 — Base: lectura WinMM → virtualización ViGEm

**Qué se hizo:**
- Primer prototipo funcional: leer un mando físico y reemitirlo como Xbox 360 virtual.
- Implementada la interfaz abstracta `IInputSource` (strategy pattern).
- `EightBitDoInputSource`: lee joystick físico vía WinMM (`joyGetPosEx`), normaliza ejes y botones a `GamepadState`.
- `GamepadState`: struct compartida que representa el estado normalizado del mando (sticks, triggers, botones, dpad).
- `ViGEmOutputAdapter`: crea un pad virtual Xbox 360 via ViGEmBus y envía el estado cada tick.
- Loop principal en consola: scan manual de puertos WinMM → lectura → forwarding → salida.

**Hardware objetivo:** 8BitDo Pro 2/Pro 3 en modo D (DInput).

---

## V2Pro3 — ~2026/03/04 — Configuración dinámica por VID/PID

**Qué se hizo:**
- `ConfigLoader`: carga mappings desde JSON (`configs/controllers.json`) en lugar de estar hardcodeados.
- `ControllerConfig`: estructura de mapeos (ejes, triggers, botones) indexada por VID/PID del mando.
- El scan de WinMM captura `wMid`/`wPid` para identificar qué mando está conectado.
- Soporte para dos modos de dispositivo (`dinput` / `xinput`) en el JSON.
- Introducción de `AxisMapping` y `TriggerMapping` como tipos de configuración.
- Librería `nlohmann/json` añadida para parseo de JSON.

---

## V3Pro3 — ~2026/03/10 — Bot visual: LightningBot (FFX Thunder Plains)

**Qué se hizo:**
- `LightningBot`: bot especializado para esquivar rayos en Final Fantasy X (Thunder Plains).
- Detecta flash lavanda en pantalla (brightness > umbral) monitorizando una región de la pantalla.
- Thread dedicado: detect → wait → press (pulsa un botón del mando virtual al detectar el flash).
- Contador de esquivas (`dodgeCount`).
- La acción bot se dispara desde `ButtonActionType::Bot` en la config: un botón físico activa/desactiva el bot.

**TriggerCount** (`tools/TriggerCount/`): herramienta de calibración desarrollada en paralelo.
- Monitoriza flashes de pantalla (misma lógica que LightningBot) sin pulsar ningún botón.
- Mide duración de la pulsación manual del botón A y la correlaciona con los flashes detectados.
- Usada para calibrar los timings del LightningBot antes de automatizarlos.
- No forma parte del producto final — es un utilitario de desarrollo/investigación.

---

## V4Pro3 — ~2026/03/15 — Sistema de macros (consola pura, pre-GUI)

**Qué se hizo:**
- Motor de macros completo integrado en el loop de consola.
- `MacroParser`: parsea un DSL compacto de texto a pasos compilados. Sintaxis: `"CU, CUR + X"`, `"B=1000"`, `"(A,B,C)*5000"`.
- `Macro` + `CompiledStep`: cada paso tiene `startMs`, `holdMs`, `endMs`. Se ejecutan contra el `GamepadState` cada tick.
- `MacroEffect`: qué botones y sticks afecta cada paso (`hasLeftStick`, `hasRightStick`).
- `MacroRepeatMode`: Once, TimedMs, UntilRelease, Toggle.
- `LuluMacro`: macro especializada, rotación continua del stick derecho (~4 RPM) para el hechizo Lulu en FFX.
- Los macros se asignan a botones del mando físico en la config JSON.
- `tools/lulu_macro_tests.csv`: datos de prueba de la LuluMacro, registro de iteraciones de calibración del timing de la rotación.

**Estado:** aplicación de consola pura, sin GUI. Versión pre-refactor.
