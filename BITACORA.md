# Bitácora — VirtualPad

> Registro histórico de sesiones de desarrollo.
> Cada entrada documenta qué se hizo, qué se aprendió y qué decisiones se tomaron.

---

## 2026/04/23 — P1→P5: Component System completo en ambas fuentes de input

### Qué se hizo

**P1 — Crear `input/ComponentTypes.h`**
- Todos los enums (`ComponentId`, `ButtonId`, `DpadDir`, `StickSlotId`, `GyroHalf`, `MouseAxis`, `MouseButton`).
- `VirtualTarget` como `std::variant` de 10 tipos (VirtualButton, VirtualDpadDir, VirtualTrigger, VirtualStickSlot, VirtualKeyboard, VirtualMacro, VirtualMouseClick, VirtualMouseMove, VirtualBot, VirtualPassthrough).
- `Range` / `RangedHalfAxis` (lista vacía = passthrough implícito).
- `StickAccumulator` / `GyroAccumulator` con `flush()`.
- Tipos físicos: `PhysicalButton`, `PhysicalDpadDir`, `PhysicalTrigger`, `PhysicalAnalogDir`, `PhysicalTouchpad`, `PhysicalGyro` — cada uno con `process()` declarado.
- `PhysicalComponent` variant. `ModifierMask` (`uint8_t`, hasta 8 modificadores).
- `PhysicalController` con `baseLayer`, `modifierLayers`, `modifierSources`, `process(physical, output)`.
- Añadidos `BtnL4`, `BtnR4`, `BtnLP`, `BtnRP` a `ComponentId` en P2.

**P2 — ConfigLoader: construir PhysicalController desde JSON**
- `parsePhysicalController()`: parsea buttons, dpad + dpad_remap + dpad_actions, trigger_actions, axis_actions, touchpad, imu, axes (passthrough).
- Helpers: `physicalNameToComponentId`, `virtualNameToButtonId`, `slotStringToStickSlotId`, `buttonActionToVT`, `buildRangedHalfAxisFromTrigger/Half`.
- `loadPhysicalControllers()` público. Corre en paralelo con `ControllerConfig`, sin tocarlo.

**P3 — Implementar `process()` por tipo de componente (`ComponentTypes.cpp`)**
- `applyVirtualTarget()`: escribe el VirtualTarget en GamepadState / acumuladores según tipo.
- `PhysicalButton::process()`, `PhysicalDpadDir::process()`, `PhysicalTrigger::process()`, `PhysicalAnalogDir::process()`, `PhysicalTouchpad::process()`, `PhysicalGyro::process()`.
- `PhysicalController::process()`: dos pasadas — Pass 1 construye `ModifierMask`, Pass 2 resuelve capa (modifierLayer → baseLayer) y despacha. Flush de acumuladores al final.
- Diseño clave: `PhysicalController` extrae el valor físico por `ComponentId` y lo pasa como parámetro tipado (`bool` para botones/dpad, `float` para gatillos/analógicos, `const GamepadState&` para touchpad/gyro).

**P4 — HIDInputSource delega en PhysicalController::process()**
- `IInputSource`: añadido `setPhysicalController()` virtual no-op.
- `HIDInputSource`: añadidos `buildPhysicalButtons`, `buildPhysicalAxes`, `applyAxesResidual`.
  `read()` bifurca: si `m_hasPhysicalController` → nuevo path; si no → legacy intacto.
- `ConfigLoader::parsePhysicalController`: añadido bloque que genera `PhysicalAnalogDir`/`PhysicalTrigger`
  passthrough desde la sección `axes` del JSON, para que `process()` pueda manejar sticks y gatillos.
- `PadEngine`: carga `physCtrls` al arrancar junto con `configs`; tras crear el input source busca
  por vid+pid e inyecta con `setPhysicalController()`.
- `VirtualPad.vcxproj`: añadido `ComponentTypes.cpp` al build (faltaba → error de símbolo externo).
- `ComponentTypes.cpp`: añadido `<functional>` (faltaba para `std::function` en `PhysicalGyro`).

**P5 — EightBitDoInputSource misma migración**
- Mismo patrón que P4 pero adaptado a WinMM: `buildPhysical*` toman `const JOYINFOEX&`.
- POV hat → `m_physicalState.dpad*` antes de `process()`.
- `trigger_combined` manejado en `applyAxesResidual` (no va por `PhysicalTrigger`).
- Añadido `m_physicalState` + `getPhysicalState()` override (antes devolvía `{}` siempre).

**fix — hot-reload re-inyecta PhysicalController**
- Al guardar en el mapper, PadEngine recargaba `ControllerConfig` pero `PhysicalController`
  quedaba con la versión de arranque → Pads seguía mostrando la asignación anterior.
- Fix: el bloque `m_configsDirty` ahora también llama `loadPhysicalControllers()` y re-inyecta.

### Qué se aprendió / decidió

- **Bug de ejes "a golpes" resuelto**: el `StickAccumulator` coordina los semiejes del mismo eje
  antes de escribir a `GamepadState`. Elimina el bug de escritura doble que causaba jitter in-game.
  El usuario confirmó que los analógicos funcionan perfectamente con el nuevo sistema.
- **Separación residual / process()**: `applyAxesResidual` solo maneja targets que no tienen
  representación en `VirtualTarget` (mouse_x/y, dpad_x/y, trigger_combined, btn_dir) y las
  axis_actions de tipo Macro/Keyboard/MouseClick (que necesitan `m_activeAxisActions`).
  El resto lo gestiona `process()` vía `PhysicalAnalogDir` con `RangedHalfAxis`.
- **P6 aplazado**: eliminar `ControllerConfig` requiere migrar PadEngine (detección macro/bot),
  `applyAxesResidual` (lee `m_config.axes`) y el mapping editor UI. No urgente.

### Estado al cerrar
- Pro 3 D-mode: botones remapeados, analógicos, dpad, gatillos — verificado ✓
- Pro 2 D-mode: botones verificados ✓
- Hot-reload mapper → Pads: verificado ✓
- Bug analógicos "a golpes": resuelto ✓
- Pendiente próxima sesión: bug ratón no para (H6 T4) / H8 / E4 / F5

---

> **Nota sobre fechas pre-2026/03/19**: El proyecto no usa git, los snapshots no tienen metadatos de fecha.
> Las entradas históricas están ordenadas por versión pero sin fecha exacta.
> El contenido se ha reconstruido leyendo el código de cada snapshot.

---

## Sesiones previas — Reconstrucción histórica (V1 → V10)

### V1Pro3 — ~2026/03/03 — Base: lectura WinMM → virtualización ViGEm

**Qué se hizo:**
- Primer prototipo funcional de la idea central: leer un mando físico y reemitirlo como Xbox 360 virtual.
- Implementada la interfaz abstracta `IInputSource` (strategy pattern).
- `EightBitDoInputSource`: lee joystick físico vía WinMM (`joyGetPosEx`), normaliza ejes y botones a `GamepadState`.
- `GamepadState`: struct compartida que representa el estado normalizado del mando (sticks, triggers, botones, dpad).
- `ViGEmOutputAdapter`: crea un pad virtual Xbox 360 via ViGEmBus y envía el estado cada tick.
- Loop principal en consola: scan manual de puertos WinMM → lectura → forwarding → salida.

**Hardware objetivo:** 8BitDo Pro 2/Pro 3 en modo D (DInput).

---

### V2Pro3 — ~2026/03/04 — Configuración dinámica por VID/PID

**Qué se hizo:**
- `ConfigLoader`: carga mappings desde JSON en lugar de estar hardcodeados.
- `ControllerConfig`: estructura de mapeos (ejes, triggers, botones) indexada por VID/PID del mando.
- El scan de WinMM ya captura `wMid`/`wPid` para identificar qué mando está conectado.
- Soporte para dos modos de dispositivo (`dinput` / `xinput`) en el JSON.
- Introducción de `AxisMapping` y `TriggerMapping` como tipos de configuración.

---

### V3Pro3 — ~2026/03/10 — Bot visual: LightningBot (FFX Thunder Plains)

**Qué se hizo:**
- `LightningBot`: bot especializado para esquivar rayos en Final Fantasy X (Thunder Plains).
- Detecta flash lavanda en pantalla (brightness > umbral) monitorizando una región de la pantalla.
- Thread dedicado: detect → wait → press (pulsa un botón del mando virtual al detectar el flash).
- Contador de esquivas (`dodgeCount`).
- La acción bot se dispara desde `ButtonActionType::Bot` en la config: un botón físico activa/desactiva el bot.

---

### V4Pro3 — ~2026/03/15 — Sistema de macros (consola pura, pre-GUI)

**Qué se hizo:**
- Motor de macros completo integrado en el loop de consola.
- `MacroParser`: parsea un DSL compacto de texto a pasos compilados. Sintaxis: `"CU, CUR + X"`, `"B=1000"`, `"(A,B,C)*5000"`.
- `Macro` + `CompiledStep`: cada paso tiene `startMs`, `holdMs`, `endMs`. Se ejecutan contra el `GamepadState` cada tick.
- `MacroEffect`: qué botones y sticks afecta cada paso (`hasLeftStick`, `hasRightStick`).
- `MacroRepeatMode`: Once, TimedMs, UntilRelease, Toggle.
- `LuluMacro`: macro especializada, rotación continua del stick derecho (~4 RPM) para el hechizo Lulu en FFX.
- Los macros se asignan a botones del mando físico en la config JSON.

**Estado:** aplicación de consola pura, sin GUI. Esta versión es la referenciada en SESSION_CONTEXT como "versión pre-refactor (consola pura, macros)".

---

### V5Pro3 — ~2026/03/15 — Refactor: modularización en directorios

**Qué se hizo:**
- Reorganización de todos los archivos fuente en subdirectorios:
  - `input/` — IInputSource, EightBitDoInputSource, ControllerConfig
  - `output/` — ViGEmOutputAdapter
  - `config/` — ConfigLoader
  - `bots/` — LightningBot
  - `macros/` — Macro, MacroParser, LuluMacro
- En root solo quedan: `VirtualPad.cpp`, `GamepadState.h`, `.vcxproj`.
- Los includes se actualizan a rutas relativas (`"input/EightBitDoInputSource.h"`, etc.).
- No se añade funcionalidad nueva: es un refactor puro de estructura.

---

### V6Pro3 — ~2026/03/15 — GUI con ImGui + threading (PadEngine / AppWindow)

**Qué se hizo:**
- Introducción de **Dear ImGui** (v1.92 WIP) con backend Win32 + Direct3D 11.
- `PadEngine`: el pipeline de lectura/macro/ViGEm se mueve a un hilo de fondo (8ms tick). Expone accessors thread-safe: `isRunning()`, `isConnected()`, `getDevice()`, `getStatus()`.
- `AppWindow`: hilo principal maneja ventana Win32, D3D11 (device, context, swapchain, render target), contexto ImGui y WndProc estático.
- `VirtualPad.cpp` queda en tres líneas: `Log::init()` → `PadEngine engine` → `AppWindow window(engine)` → `window.run()`.
- Primera versión con interfaz gráfica visible. Tabs: Engine / Scanner (aún básico).

---

### V7Pro3 — ~2026/03/15 — PadScanner: enumeración visual de mandos WinMM

**Qué se hizo:**
- `PadScanner`: utilidad estática para enumerar todos los puertos WinMM conectados.
- `DeviceInfo`: struct con `port`, `axes`, `buttons`, `vid`, `pid`, `name`.
- `RawInput`: struct con todos los ejes raw (`xpos`, `ypos`, `zpos`, `rpos`, `upos`, `vpos`, `pov`, `buttons`).
- Métodos: `PadScanner::scan()` y `PadScanner::readRaw(port)`.
- La tab Scanner de ImGui muestra la lista de mandos detectados y sus valores raw en tiempo real.
- Separación clara: PadScanner lee lo que hay en WinMM; PadEngine gestiona el mando activo.

---

### V8 — ~2026/03/17 — Consolidación + directorio data/ + preparación dual-API

**Qué se hizo:**
- Creación del directorio `data/` para los ficheros de configuración en runtime (`controllers.json`, `macros.json`, `virtualpad.json`).
- Los ficheros JSON se separan del directorio fuente.
- Preparación arquitectónica para soportar dos APIs de entrada (WinMM + HID) sin cambios de interfaz pública.
- Estructura de archivos root idéntica a V7. Los cambios son principalmente en organización de datos y preparación interna.

---

### V9 — ~2026/03/18 — Soporte HID: HIDInputSource + HIDScanner + refactor PadEngine

**Qué se hizo:**
- `HIDScanner` (`input/`): enumera dispositivos HID filtrando por usage page 0x01 (Generic Desktop), usage 0x04 (Joystick) y 0x05 (Gamepad). Devuelve `DeviceInfo` con VID, PID, usage page/usage, device path y product name.
- `HIDInputSource` (`input/`): lectura de mandos via HID API (ReadFile overlapped). Maneja:
  - HID preparsed data y ValCaps para normalización automática de ejes.
  - Usages mapeados: hid_x, hid_y, hid_z, hid_rx, hid_ry, hid_rz, hid_brake (0xC4), hid_accel (0xC5).
  - D-pad HAT switch parsing.
  - Fix del Report ID mismatch del Pro 2 D-mode BT: si `HidP` falla con `INCOMPATIBLE_REPORT_ID`, swap temporal de `buf[0]` y retry.
- **Refactor PadEngine**: nuevo `struct DeviceCandidate` que unifica WinMM y HID. `selectDevice(int index)` reemplaza `selectDevice(UINT port)`. `getLastState()` thread-safe. `getCandidates()` reemplaza la lista WinMM pura.
- La tab Scanner de ImGui muestra dos secciones: dispositivos WinMM y dispositivos HID.
- **Pro 2 D-mode**: funciona vía HID con gatillos analógicos reales (hid_brake→triggerR, hid_accel→triggerL).
- **F310 D-mode**: visible en scanner HID.

---

### V10 — ~2026/03/19 — Logging (spdlog) + HidHide integration (Fase B)

**Qué se hizo:**

#### Logging con spdlog
- Librería `spdlog` añadida al proyecto (header-only).
- `Log.h`: wrapper de inicialización con dos sinks: consola coloreada + fichero rotativo (`logs/virtualpad.log`, 1MB × 3 ficheros).
- Nivel de log configurable en `data/virtualpad.json` → `"log_level": "debug"/"info"`.
- `Log::init()` se llama en `main()` antes de crear PadEngine.
- Niveles usados: debug (raw bytes, scan, ValCaps), info (eventos normales), warn (desconexiones, HidHide no instalado), error (fallos driver).

#### HidHideClient (Fase B)
- `HidHideClient` (`output/`): interfaz programática al driver kernel HidHide de Nefarius.
- IOCTLs: device type 32769, funciones 2048–2053, access FILE_READ_DATA. Fuente: `HidHide-master/HidHideCLI/src/FilterDriverProxy.cpp`.
- Control device: `\\.\HidHide`, handle con `GENERIC_READ | FILE_SHARE_READ|WRITE|DELETE`.
- Comportamiento:
  - Al arrancar: añade VirtualPad.exe a la whitelist (idempotente).
  - Al tomar un mando: lo añade al blacklist + activa el filtro (guarda si fuimos nosotros quienes lo activamos).
  - Al soltar/cerrar: lo quita del blacklist + desactiva el filtro solo si nosotros lo habíamos activado.
  - Destructor como red de seguridad para crashes.
  - Si HidHide no instalado: `isAvailable()=false`, todo es no-op.
- **Resultado**: Steam solo ve el mando virtual Xbox 360. El mando físico queda oculto mientras VirtualPad está activo.

#### Estado al cerrar esta fase
- Cadena completa verificada: Pro 3 D-mode BT → VirtualPad → ViGEm → Steam ✓
- Pro 3 X-mode ✓, Pro 2 X-mode ✓, Pro 2 D-mode ✓, F310 D-mode ✓ (en scanner)
- Fase A1–A4 completadas. Fase B completada.

---

## Sesión 2026/03/19 — Fase A4: HID X-mode (prototipo) + fixes HIDInputSource

### Contexto
Continuación tras completar Fase B (HidHide). Estado: snapshots V4–V10 existentes, V10 = Fase B completa.

### Decisiones de arquitectura
- **WinMM queda como legacy** para D-mode. No se elimina.
- **XInput descartado**: no aporta nada que HID no dé, y requiere correlación VID/PID externa.
- **HID para todo**: Pro 3 D/X, Pro 2 D/X, F310 D, DS4 — cualquier mando con algo especial.
- **X-mode vía HID = prototipo**: triggers separados funcionan, pero solo expone 10 botones estándar Xbox (sin Home, L4, R4, Lp, Rp). Firmware del 8BitDo limita X-mode a Xbox 360 estándar. Documentado en controllers.json como `_hid_prototype`. Se mantiene WinMM para X-mode hasta tener botones correlativos completos.
- **D-mode** es el camino para botones extra (Home, L4, R4, Lp, Rp) — los expone todos vía HID.

### Bugs encontrados y corregidos

#### `normalizeHIDAxis` — ejes unsigned `[0, -1]`
- **Síntoma**: sticks y triggers devolvían 0.0f siempre en modo HID.
- **Causa**: el descriptor HID del Pro 3 X-mode reporta `logMax = -1` (LONG) para ejes unsigned, lo que hace `range = logMax - logMin = -1`, y el código tenía `if (range <= 0) return 0.0f`.
- **Fix**: añadido `bitSize` a `ValueRange`. Cuando `logMax < logMin` (unsigned), se usa `uMax = (1 << bitSize) - 1` como denominador.
- **Afecta a**: todos los mandos HID con descriptor unsigned — Pro 2 D-mode y F310 estaban también rotos (sticks siempre 0), aunque coincidía con el valor de centro y no se notaba hasta mover el stick.

#### Hat switch — encoding `[1,8]` vs `[0,8]`
- **Síntoma**: diagonal NW (arriba+izquierda) no funcionaba en Pro 3 X-mode.
- **Causa**: Pro 3 X-mode usa hat `[1,8]` donde **8=NW** y center=0 (fuera de rango). El código anterior trataba `hatValue >= logMax` como center, capturando NW como neutral.
- **Fix**: cambio a detección por rango: si `hatValue < logMin || hatValue > logMax` → neutral. Si dentro del rango → `parseHIDDpad(hatValue - logMin)`.
- **Backward compatible**: Pro 2 D-mode usa `[0,8]` donde 8=center y se maneja correctamente por `parseHIDDpad` internamente.

### Hallazgos técnicos — Pro 3 X-mode HID

**Report format (15 bytes, ReportID=0):**
```
[0]      Report ID = 0x00
[1-2]    Left stick X  (uint16 LE, unsigned, center ~0x8000)
[3-4]    Left stick Y  (uint16 LE, unsigned, center ~0x7FFF)
[5-6]    Right stick X (uint16 LE, unsigned, center ~0x8000)
[7-8]    Right stick Y (uint16 LE, unsigned, center ~0x7FFF)
[9-10]   Trigger Z combinado (uint16 LE, center=0x8000, LT→0, RT→0xFFFF)
[11-12]  Botones (bitfield, HID button usages 1-10)
[13]     Hat switch (0=center, 1=N, 2=NE, 3=E, 4=SE, 5=S, 6=SW, 7=W, 8=NW)
[14]     Padding / no usado
```

**ValCaps (todos ReportID=0, todos unsigned [0,-1] excepto hat):**
- 0x30 (X), 0x31 (Y), 0x33 (Rx), 0x34 (Ry), 0x32 (Z) — range [0,-1], bitSize=16
- 0x39 (Hat) — range [1,8]

**Botones (10 estándar Xbox, correlativos 1-10):**
| HID | Virtual | Físico (8BitDo) |
|-----|---------|-----------------|
| 1 | a | A (pos. sur) |
| 2 | b | B (pos. este) |
| 3 | x | X (pos. oeste) |
| 4 | y | Y (pos. norte) |
| 5 | l1 | LB |
| 6 | r1 | RB |
| 7 | select | Select/Back |
| 8 | start | Start |
| 9 | l3 | L3 |
| 10 | r3 | R3 |

**Botones NO accesibles en X-mode**: Home, L4, R4, Lp, Rp — no están en el descriptor HID. Solo accesibles en D-mode.

**HID Z trigger combinado**: `invert: true` necesario (LT empuja hacia 0 = negativo sin invertir = triggerR incorrecto).

### Estado al cerrar sesión
- Pro 3 X-mode: WinMM (sin cambios funcionales respecto a V10)
- Pro 2 X-mode: WinMM (sin cambios)
- Pro 2 D-mode: HID — ahora con normalización de ejes correcta (fix unsigned)
- F310 D-mode: HID — ídem
- Snapshot V11 ✓

---

## Sesión 2026/03/20 (1) — Sistema de perfiles de juego

- Limpiado `controllers.json`: eliminados todos los macros y bots. Queda como config base pura.
- Documentados los botones sin equivalente Xbox (Rp, Lp, L4, R4) con claves `_` dentro del objeto `buttons` en lugar de un campo `_button_map` separado.
- Verificado mapa completo de botones del Pro 3 D-mode: L4=17, R4=18, Lp=6, Rp=3.
- Creado `data/FinalFantasyX.json` con overrides para Pro 3 y Pro 2 D-mode.
- Implementado sistema de perfiles en `ConfigLoader`: `GameProfile`, `loadGameProfile`, `applyProfile`. Las claves `_` en buttons ahora se ignoran en el parser.
- Implementado selector de perfil en la tab Engine de `AppWindow`: descubre automáticamente los JSONs de perfiles en `data/`.
- Implementado hot-swap de perfil en `PadEngine`: detección de cambio en el loop principal, `effectiveCfg` se recalcula, `input->setConfig()` actualiza el IInputSource sin reabrir el device, macros se re-inicializan.
- Añadido `setConfig()` a `IInputSource` (virtual pura), implementado en `EightBitDoInputSource` y `HIDInputSource`.
- README actualizado: sección de perfiles de juego con formato, ejemplos y explicación de los botones no estándar.
- Snapshot V12 pendiente.
- Verificado 2026/03/20: Pro 2 D-mode responde correctamente al perfil FinalFantasyX.json (botones 3, 6, 15 coinciden con Pro 3).

### Pro 3 D-mode — mapa de botones WinMM (verificado 2026/03/20)

| WinMM | Físico | Virtual Xbox | Notas |
|---|---|---|---|
| 1 | B | b | |
| 2 | A | a | |
| **3** | **Rp (paddle der.)** | **—** | **sin equivalente Xbox** |
| 4 | Y | y | |
| 5 | X | x | |
| **6** | **Lp (paddle izq.)** | **—** | **sin equivalente Xbox** |
| 7 | LB | l1 | |
| 8 | RB | r1 | |
| 9 | L2 | trigger l2 | digital (botón, no analógico) |
| 10 | R2 | trigger r2 | digital (botón, no analógico) |
| 11 | Select | select | |
| 12 | Start | start | |
| 13 | Home | home | |
| 14 | L3 | l3 | |
| 15 | R3 | r3 | |
| **17** | **L4** | **—** | **sin equivalente Xbox** |
| **18** | **R4** | **—** | **sin equivalente Xbox** |

---

## Sesión 2026/03/20 (2) — A4.3 F310 D-mode + A4.2 DS4 v2 + fixes + nuevas fases planificadas

### Fix: traza periódica HIDInputSource
- `m_readCount` solo avanzaba cuando llegaban reportes HID con datos nuevos. Mandos que solo envían en cambio de estado (F310, DS4 en reposo) nunca llegaban a 240 → el dump de debug nunca salía.
- Fix: añadido `++m_readCount` en el path de timeout (20ms sin datos) con su propio dump `(no report)`. Ahora la traza sale cada ~2s independientemente de actividad.

### A4.3 — Logitech F310 D-mode ✓
- Verificado via scanner: gatillos **digitales** (botones 7/8, salto 0→1). El "analógico progresivo" observado inicialmente era del test en X-mode por error.
- Layout de ejes real: dwXpos/dwYpos (left stick), dwZpos/dwRpos (right stick). El F310 D-mode usa Z/Rz para el stick derecho, no Rx/Ry como los 8BitDo.
- Configuración final: mode `dinput` (WinMM), igual que Pro 3 D-mode. Ejes Y invertidos (hid_y, hid_ry → false en HID era incorrecto; en WinMM dwYpos/dwRpos `invert: true`).
- Descartado modo HID para F310: botones extra innecesarios, layout más sucio.

### A4.2 — Sony DualShock 4 v2 ✓ (USB + BT)
- VID:054C PID:09CC. Funciona vía WinMM dinput sin configuración especial.
- BT: mismo VID/PID que USB, Windows lo abstrae igual → config única cubre ambos modos.
- Gatillos analógicos en dwUpos (L2) y dwVpos (R2) — independientes y progresivos.
- 13 botones activos: x/a/b/y, l1/r1, select/start, l3/r3, home (PS), touchpad click (_14).
- Botones 7/8 (digital L2/R2) sin mapear — los ejes analógicos los reemplazan.
- Features DS4 avanzadas pendientes para fases futuras: touchpad posición XY, giroscopio, acelerómetro, control de LEDs, rumble. Requieren HID raw parser con report 0x01 (USB) / 0x11 (BT).

### A4.4 — Botones extra Pro 3/Pro 2 D-mode ✓ (cerrado)
- Home mapeado a botón virtual. Lp/Rp/L4/R4 sin equivalente Xbox → solo disponibles como macro/bot en perfiles de juego. No se puede ir más allá con ViGEm Xbox 360.

### Nuevas fases planificadas
- **A7 — Auto-reconexión/Hot-plug**: PadEngine vigila VID/PID tras desconexión y reconecta solo. Especialmente útil para DS4 (batería corta). Para DS4: auto-detectar USB vs BT (report 0x01 vs 0x11).
- **A6 — Drift calibration**: dead zone por eje con rescalado, configurable por perfil de juego. `|v| <= dz → 0`, `|v| > dz → sign(v)*(|v|-dz)/(1-dz)`. Slider en UI.

### Snapshot V12 ✓
- Cubre: perfiles de juego hot-swap (sesión 1) + A4.2 DS4 + A4.3 F310 + A4.4 botones extra.

### Decisiones de arquitectura
- **WinMM queda como legacy** para D-mode. No se elimina.
- **XInput descartado**: no aporta nada que HID no dé, y requiere correlación VID/PID externa.
- **HID para todo**: Pro 3 D/X, Pro 2 D/X, F310 D, DS4 — cualquier mando con algo especial.
- **X-mode vía HID = prototipo**: triggers separados funcionan, pero solo expone 10 botones estándar Xbox (sin Home, L4, R4, Lp, Rp). Firmware del 8BitDo limita X-mode a Xbox 360 estándar. Documentado en controllers.json como `_hid_prototype`. Se mantiene WinMM para X-mode hasta tener botones correlativos completos.
- **D-mode** es el camino para botones extra (Home, L4, R4, Lp, Rp) — los expone todos vía HID.

### Bugs encontrados y corregidos

#### `normalizeHIDAxis` — ejes unsigned `[0, -1]`
- **Síntoma**: sticks y triggers devolvían 0.0f siempre en modo HID.
- **Causa**: el descriptor HID del Pro 3 X-mode reporta `logMax = -1` (LONG) para ejes unsigned, lo que hace `range = logMax - logMin = -1`, y el código tenía `if (range <= 0) return 0.0f`.
- **Fix**: añadido `bitSize` a `ValueRange`. Cuando `logMax < logMin` (unsigned), se usa `uMax = (1 << bitSize) - 1` como denominador.
- **Afecta a**: todos los mandos HID con descriptor unsigned — Pro 2 D-mode y F310 estaban también rotos (sticks siempre 0), aunque coincidía con el valor de centro y no se notaba hasta mover el stick.

#### Hat switch — encoding `[1,8]` vs `[0,8]`
- **Síntoma**: diagonal NW (arriba+izquierda) no funcionaba en Pro 3 X-mode.
- **Causa**: Pro 3 X-mode usa hat `[1,8]` donde **8=NW** y center=0 (fuera de rango). El código anterior trataba `hatValue >= logMax` como center, capturando NW como neutral.
- **Fix**: cambio a detección por rango: si `hatValue < logMin || hatValue > logMax` → neutral. Si dentro del rango → `parseHIDDpad(hatValue - logMin)`.
- **Backward compatible**: Pro 2 D-mode usa `[0,8]` donde 8=center y se maneja correctamente por `parseHIDDpad` internamente.

### Hallazgos técnicos — Pro 3 X-mode HID

**Report format (15 bytes, ReportID=0):**
```
[0]      Report ID = 0x00
[1-2]    Left stick X  (uint16 LE, unsigned, center ~0x8000)
[3-4]    Left stick Y  (uint16 LE, unsigned, center ~0x7FFF)
[5-6]    Right stick X (uint16 LE, unsigned, center ~0x8000)
[7-8]    Right stick Y (uint16 LE, unsigned, center ~0x7FFF)
[9-10]   Trigger Z combinado (uint16 LE, center=0x8000, LT→0, RT→0xFFFF)
[11-12]  Botones (bitfield, HID button usages 1-10)
[13]     Hat switch (0=center, 1=N, 2=NE, 3=E, 4=SE, 5=S, 6=SW, 7=W, 8=NW)
[14]     Padding / no usado
```

**ValCaps (todos ReportID=0, todos unsigned [0,-1] excepto hat):**
- 0x30 (X), 0x31 (Y), 0x33 (Rx), 0x34 (Ry), 0x32 (Z) — range [0,-1], bitSize=16
- 0x39 (Hat) — range [1,8]

**Botones (10 estándar Xbox, correlativos 1-10):**
| HID | Virtual | Físico (8BitDo) |
|-----|---------|-----------------|
| 1 | a | A (pos. sur) |
| 2 | b | B (pos. este) |
| 3 | x | X (pos. oeste) |
| 4 | y | Y (pos. norte) |
| 5 | l1 | LB |
| 6 | r1 | RB |
| 7 | select | Select/Back |
| 8 | start | Start |
| 9 | l3 | L3 |
| 10 | r3 | R3 |

**Botones NO accesibles en X-mode**: Home, L4, R4, Lp, Rp — no están en el descriptor HID. Solo accesibles en D-mode.

**HID Z trigger combinado**: `invert: true` necesario (LT empuja hacia 0 = negativo sin invertir = triggerR incorrecto).

### Estado al cerrar sesión
- Pro 3 X-mode: WinMM (sin cambios funcionales respecto a V10)
- Pro 2 X-mode: WinMM (sin cambios)
- Pro 2 D-mode: HID — ahora con normalización de ejes correcta (fix unsigned)
- F310 D-mode: HID — ídem
- Snapshot pendiente: V11

---

## Sesión 2026/03/21 — Fase C: salida teclado/ratón ✓

### Reorganización de fases
- Fases A y B cerradas definitivamente.
- Plan renombrado: C (teclado/ratón), D (UI visual), E (drift + hot-plug), F (Steam Controller), Backlog.
- Fase C absorbe la descripción original de "salidas alternativas" (teclado+ratón como output).

### Hardware inventariado para backlog
- Steam Controller: VID:28DE PID:1142 (dongle) / PID:1102 (USB). Solo con Steam cerrado. Lizard mode toggle como acción de botón.
- Wii U Pro Controller (BT, batería agotada), Classic Controller y GameCube (via GBros 8BitDo), Wiimote.
- **Zero 2 colisión VID/PID**: BT D-mode usa VID:2DC8 PID:6006 igual que Pro 2. Si ambos conectados por BT simultáneamente, VirtualPad puede confundirlos. Solución: conectar uno por cable (PID diferente).

### Implementación Fase C

#### Nuevos tipos de acción en botones
- `ButtonActionType::Keyboard` — combo de teclado con `SendInput` al pulsar/soltar. Edge-triggered.
- `ButtonActionType::MouseClick` — click de ratón (left/right/middle). Edge-triggered.
- Ambos skippeados en InputSource (igual que Bot/Macro), dispatch en PadEngine.
- Helpers estáticos: `keyNameToVK`, `sendKeyCombo`, `sendMouseButton`.
- Press: teclas en orden → down. Release: en orden inverso → up. Correcto para modificadores (Alt, Ctrl, Shift).

#### Movimiento de ratón desde stick
- Target de eje nuevo: `mouse_x` / `mouse_y` — nuevos campos en `GamepadState`, poblados por ambos InputSource.
- PadEngine: acumulador sub-píxel (`mouseAccumX/Y`) → `SendInput(MOUSEEVENTF_MOVE)` cada tick.
- `speed` como parámetro de `AxisMapping` (default 15 px/tick a deflexión máxima).
- `setMouseSpeed`/`getMouseSpeed` expuestos en PadEngine — slider en UI pendiente para Fase D.

#### Fixes durante la sesión
- **Zona muerta ratón**: `kMouseDeadZone = 0.12f` — stick Pro 2 derivaba levemente en reposo. Constante hasta Fase E.
- **Inversión eje Y ratón**: `mouse_y` lleva `invert` contrario al `right_y` del mismo eje. Pantalla Y↓ positivo vs mando Y↑ positivo.
- **FinalFantasyX.json**: botón 13 (Home) → `{"type":"keyboard","keys":["alt","tab"]}` en Pro 2 D-mode.

### Verificado en sesión
- Pro 2 D-mode: stick derecho mueve cursor ✓, botón 13 → Alt+Tab ✓

### Pendientes dentro de Fase C
- **Axis overrides en perfiles**: añadir soporte de ejes en `GameProfile::Override` + `ConfigLoader` + `applyProfile`.
- **Sintaxis teclado**: cambiar array `["alt","tab"]` por cadena `"alt+tab"`. Solo afecta `ConfigLoader`.
- **freeze_output**: flag en acción keyboard → output neutral hasta primer input del mando.

### Snapshot V13 ✓
- Cubre: Fase C completa (keyboard, mouse_click, mouse movement, dead zone).

---

## 2026/03/22 — Repo GitHub + limpieza

- Repositorio creado en GitHub (pooka-v1/VirtualPad). Historial reconstruido desde snapshots: 14 commits (v0.1–v0.14), ramas develop + main, gitflow simplificado.
- **BITACORA.md y SESSION_CONTEXT.md** eliminados del tracking git y añadidos al .gitignore. Son documentos internos, no relevantes para usuarios externos. Permanecen en local y en backup.
- **TriggerCount/** eliminado de la raíz del repo. Era una carpeta residual del proceso de construcción del historial — la herramienta real está correctamente incluida en el commit v0.3 bajo `tools/TriggerCount/`.
- README y MACROS traducidos al inglés. Inglés es la portada pública; español en `.es.md`.
- **8BitDo Zero 2 (Bluetooth)**: comparte VID/PID 2DC8:6006 con el Pro 2. VirtualPad carga el perfil del Pro 2 y la cruceta del Zero 2 aparece como 4 ejes analógicos en vez de hat switch. Pendiente de investigar con el scanner para definir solución.

---

## 2026/03/23 — Revisión de estado post-GitHub

### Situación al retomar
- Repo en GitHub funcional, historial reconstruido desde snapshots (v0.1–v0.14).
- Último snapshot funcional: V13 (Fase C: keyboard, mouse_click, mouse movement, dead zone).

### Verificaciones realizadas
- `FinalFantasyX.json`: Pro 2 D-mode tiene botón 13 (Home) → `{"type":"keyboard","keys":["alt","tab"]}` ✓ — sale del juego
- `controllers.json`: Pro 2 D-mode tiene `hid_z → right_x`, `hid_rz → right_y` en base — el ratón se probó en Fase C pero NO está configurado de forma permanente en los JSON (requiere axis overrides en perfiles, pendiente de implementar)
- Dead zone del ratón: `kMouseDeadZone = 0.12f` en código, ampliada respecto al valor original para eliminar drift del Pro 2 en reposo
- Velocidad del ratón: `speed: 15` (px/tick a deflexión máxima), aceptable para uso casual

### Pendientes identificados para Fase C
1. **Axis overrides en perfiles** — necesario para que el perfil FFX reasigne stick derecho a ratón sin cambiar la base. Afecta `GameProfile::Override`, `ConfigLoader`, `applyProfile`.
2. **Pausa del juego en perfil FFX** — añadir acción keyboard a un botón libre. Pro 3 D-mode tiene L4(17)/R4(18) disponibles. Pro 2 no tiene botones extra libres.
3. **Freeze output** al cambiar de ventana (flag en acción keyboard).
4. **Sintaxis teclado** como cadena `"alt+tab"` en lugar de array (cosmético).

### Assets Fase D — primer avance
- Primer borrador del mando: silueta base, cruceta, botones A/B/X/Y y analógico.
- En proceso de refinado — mañana se continúa.

### Estado de la planificación
- Fases A, B, C infraestructura: cerradas ✓
- Pendiente C: axis overrides en perfiles + pausa FFX + freeze output
- Siguiente decisión: ¿terminar pendientes C antes de Fase D (UI) o Fase E (drift/hot-plug)?

### Diseño Fase D — spec acordada
- **Formato assets**: PNG con transparencia. Paint moderno, GIMP, Krita son suficientes.
- **Tamaños**: recomendado 128×128, mínimo 64×64, máximo 256×256. Escalar siempre hacia abajo.
- **Separación shape/símbolo**: la forma del botón y el símbolo (letra, icono) son assets separados. El código superpone y aplica tinte de color en tiempo real (ImGui tint).
- **Dos vistas**: superior (cenital) y frontal (bumpers/triggers/paddles). Steam no muestra la frontal — es nuestra ventaja para mandos con muchos botones.
- **Catálogo**: ~30 assets en total (shapes + símbolos). Detalle completo en SESSION_CONTEXT.
- **Lienzo**: 480×640 px portrait (PS2 rotada 90°). Frontal arriba, superficie abajo.
- **Layout frontal**: L2/R2 encima de L1/R1 (los triggers están más atrás físicamente — al girar el mando hacia ti suben).
- **Total assets**: 37 PNG (23 shapes + 14 símbolos). Checklist completo en SESSION_CONTEXT.
- **Subfases**: D1 vistas solo lectura → D2 vista frontal → D3 más plantillas → D4 editor mappings → D5 editor macros.

### Idea anotada — capas de mapeo por modificador (modifier layers)
Mantener pulsado un botón (ej. Home) activa temporalmente una capa de mapeo diferente: stick pasa a ratón, botones pasan a acciones de teclado/click. Soltar el modificador vuelve al mapeo normal.
Valor: capa extra de botones casi infinita sin botones físicos adicionales.
Complejidad alta: nuevo campo `layers` en perfiles, detección hold en PadEngine, consumo del botón modificador, interacción con macros activas.
No bloquea nada anterior. Implementar tras Fase D y E.

---

## 2026/03/25 — Fase D1: tab Pad operativo

### Assets dibujados por el usuario
19 PNG en `VirtualPad/images/` (todos RGBA con transparencia):
- `TemplatePadSolidPS.png`, `TemplatePadSolidSN.png` — plantillas 480×400
- `Button.png` (50×50) — círculo base para botones de cara
- `Alalogic.png` (70×70) — base analógico
- `GreyCrossClassic.png`, `GreyCrossPS.png`, `GreyCrossTri.png` (80×80) — tres variantes de cruceta
- `L1Button.png`, `R1Button.png` (90×30) — bumpers
- `LR2Button.png` (80×45) — gatillo compartido L2/R2
- `SelectStarButon.png` (40×18) — píldora select/start
- `CharacterA/B/X/Y.png` (50×50) — símbolos de cara
- `CharactersL1/R1.png` (90×30), `CharactersL2/R2.png` (80×45) — etiquetas shoulder
- Originales Krita en `images/founts/` (ignorados en git)

### Infraestructura D1 implementada
- `ui/PadView.h` + `ui/PadView.cpp` — cargador PNG via WIC (sin dependencias extra), render con ImDrawList
- `PadTexture` — wrapper RAII para SRV D3D11
- Tab "Pad" añadido a AppWindow — llama a `m_padView.render(m_engine.getLastState())`
- `m_padView.load()` tras initD3D(), `m_padView.unload()` antes de liberar device en cleanup()

### Decisiones de diseño
- **WIC para carga PNG**: nativo Windows, sin stb_image ni otras dependencias.
- **ImDrawList para render**: posicionamiento absoluto sobre el canvas, sin interferir con layout ImGui. `ImGui::Dummy({W,H})` reserva el espacio.
- **Posiciones en `namespace Layout`** dentro de PadView.cpp: constexpr, fácil de calibrar. Se refactorizará a JSON en D4.
- **Separación shape/símbolo en render**: `faceBtn()` superpone círculo + símbolo con tinte independiente. Inactivo: círculo gris + símbolo negro. Activo: círculo con color del botón + símbolo blanco.
- **Dots de dirección en cruceta**: primitivas ImDrawList (círculo marrón oscuro), sin asset de flecha. Provisional hasta tener `sym_arrow_*.png`.
- **Home**: primitiva círculo hasta tener `btn_home_ps.png`.
- **Deflexión de stick**: cabeza (círculo pequeño) desplazada proporcionalmente al valor del eje. Radio de stick 66px, desplazamiento máximo 12px.

### Calibración de layout para Pro 2 (TemplatePadSolidPS.png, 480×400)
- Cruceta: `GreyCrossTri.png`, cx=124, cy=183
- Botones cara: cluster en (356,185), radio 34px
- Select/Start: centrados en x=240, separación 12px (1/3 del ancho de píldora), cy=185
- Analógicos: cx=180/290, cy=270, size=66px
- L1/R1: cx=124/356, cy=80
- L2/R2: cx=124/356, cy=44
- Home: cx=356, cy=253 (debajo de A, a media distancia entre Y y A)

### .gitignore
- `VirtualPad/images/founts/` añadido — originales Krita, no relevantes para el repo público.

---

## 2026/03/26 — Fase D1/D2: vista frontal + refactor formato botones

### Nuevos assets añadidos
- `TemplatePadSolidPSFront.png`, `TemplatePadSolidSNFront.png` — plantillas vista frontal (canto del mando)
- `TemplatePadSolidSNTop.png` — plantilla vista superior Switch
- `TemplatePadSolidPS.png` → renombrado a `TemplatePadSolidPSTop.png`
- `Button.png` → renombrado a `CircularButton.png` (para distinguir de `SquareButton.png`)
- `SquareButton.png` — rectángulo redondeado para L4/R4 (paddles cortos)
- `L5Button.png`, `R5Button.png` — paddles largos Lp/Rp (formas orgánicas asimétricas)
- `8BitDoHomeBotton.png` — icono home 8BitDo (símbolo, se superpone sobre CircularButton)
- `CharactersL4/R4/L5/R5.png` — etiquetas para paddles
- `PSLogo.png`, `XboxLogo.png` — logos para home de cada plataforma
- Originales Krita en `images/founts/`

### PadView — vista frontal implementada (D2)
- Canvas expandido: 480×560 px (FrontH=160 + TopH=400)
- `TemplatePadSolidPSFront.png` renderizado en la franja superior
- L2/R2, L1/R1 ahora aparecen en la vista frontal (posiciones estimadas, pendiente calibrar)
- L4/R4 (SquareButton) en vista frontal, misma altura que L1/R1, a su lado exterior
- L5/R5 (L5Button/R5Button) en vista frontal, al lado de L2/R2
- Home: CircularButton + icono 8BitDoHomeBotton superpuesto (sustituye primitiva)
- Cruceta cambiada de GreyCrossTri a GreyCrossPS
- Posiciones Layout pendientes de calibración tras primera compilación

### GamepadState — botones extra
- Añadidos: `btnL4`, `btnR4`, `btnLP`, `btnRP`

### Refactor: campo `physical` en ButtonAction
**Problema resuelto**: los botones sin equivalente Xbox (L4, R4, Lp, Rp) tenían acción asignada en perfiles (macro/bot) pero GamepadState nunca se actualizaba → no se iluminaban en PadView.

**Solución**: campo `physical` en `ButtonAction` — identidad visual del botón físico, independiente de su acción asignada. Nunca sobreescrito por perfiles de juego.

**Nuevo formato `controllers.json`**:
```json
"3":  { "physical": "rp" }
"7":  { "physical": "l1", "virtual": "l1" }
"9":  { "physical": "l2", "type": "trigger", "target": "l2" }
```
Backward compat: string corto `"1": "b"` sigue funcionando → equivale a `{physical:"b", virtual:"b"}`.

**`applyProfile`** ahora hace merge en lugar de replace: preserva `physical` del base aunque el perfil sobreescriba la acción (macro/bot/keyboard).

**`EightBitDoInputSource` y `HIDInputSource`**: loop adicional al final del tick — para cada botón con `physical` definido, actualiza el estado visual en GamepadState independientemente de la acción.

**`controllers.json`**: Pro 3 D-mode migrado al nuevo formato completo. Eliminadas las claves `_3`, `_6`, `_17`, `_18` (comentarios antiguos) — la info está ahora en `physical` de cada entrada.

### Visión D largo plazo acordada
- **Panel mando físico R** (en curso): vista superior + vista frontal, botones iluminados en tiempo real.
- **Panel mando virtual V** (futuro): visualización de lo que sale hacia ViGEm — botones, sticks, triggers del virtual. Objetivo: ver "R pulsa Rp → V ejecuta A+B" o "R gira stick → V mueve ratón".
- Los dos paneles en paralelo en la tab Pad. Bot: pendiente de pensar cómo visualizarlo.

### Catálogo de assets — estado actual
**Shapes disponibles**: CircularButton, SquareButton, Alalogic, GreyCrossClassic, GreyCrossPS, GreyCrossTri, L1Button, R1Button, LR2Button, SelectStarButon, L5Button, R5Button, TemplatePadSolidPS(Top/Front), TemplatePadSolidSN(Top/Front)
**Símbolos disponibles**: CharacterA/B/X/Y, CharactersL1/L2/L4/L5/R1/R2/R4/R5, PSLogo, XboxLogo, 8BitDoHomeBotton

**Pendiente de dibujar**:
- Símbolos PS: ×, ○, □, △
- Cruceta clásica ×4 partes separadas (con borde, retoque pendiente)
- Cruceta Xbox Pro (octógono con muescas)
- Cruceta Switch ×4 (4 círculos separados)
- `btn_plus.png`, `btn_minus.png` — shapes propias Switch (+ y −)
- Plantilla Switch top + front
- `btn_touchpad.png` — DS4
- Share + Options DS4, View + Menu Xbox
- Versiones _sm eliminadas del catálogo (el código escala)
- Homes adicionales: PSLogo/XboxLogo se superponen sobre CircularButton → no hacen falta shapes propias

## 2026/03/26 (2) — Calibración D2 + Pro 3 a HID + cruceta 4 brazos

### Pro 3 D-mode migrado a HID
- `mode` cambiado de `"dinput"` a `"hid"` en controllers.json.
- Ejes: `dwXpos/dwYpos/dwZpos/dwRpos` → `hid_x/hid_y/hid_z/hid_rz`; dpad: `pov` → `hid_hat`.
- Gatillos digitales (botones 9/10) eliminados; sustituidos por `hid_brake`/`hid_accel` analógicos.
- `extra_buttons` eliminado del JSON (campo huérfano, sin código que lo leyera).
- Resultado: L2/R2 del Pro 3 D-mode ahora son gatillos analógicos ✓

### Scanner HID — botones extra añadidos
- LP/RP/L4/R4 no aparecían en la cuadrícula del panel HID.
- Causa: la máscara reconstruida en AppWindow no incluía `btnLP/RP/L4/R4`.
- Fix: añadidos a bits 15-18; loop de display extendido de 16 a 19 botones.

### PadView — calibración visual D2
Ajustes aplicados en esta sesión:
- **Color mando**: naranja `kTplTint = {1.0, 0.50, 0.08}` (era turquesa)
- **Opacidad botones**: `kInactive` alpha 1.0 (era 0.82 — transparente sobre la plantilla)
- **Home icono**: siempre naranja (`kTplTint`), no cambia al pulsar; solo el círculo cambia
- **Home posición**: centrado sobre botón A (`HomeCx=FaceCx`), `HomeCy=BtnAy+HomeSize+25`
- **L2/R2**: L2 +2px derecha, R2 +2px izquierda (mejor alineación visual)
- **L4/R4**: 37×29px (era 32×24, +5px en ambas dimensiones)
- **Cruceta, cara, select, start**: bajados 20px (`TopY+129`/`TopY+131`)
- **D-pad**: 89px (era 74, +20%)
- **Analógicos**: subidos `StickSize*0.25` ≈ 16px
- **L5/R5**: 72×56px (doblados), reposicionados -50px X / -28px Y
- **Bug corregido**: `StickSize` se usaba antes de declararse en el namespace Layout → reordenado

### Cruceta 4 brazos independientes
- Reemplazado el asset único `GreyCrossPS.png` por 4 imágenes separadas:
  `CrossUp.png`, `CrossDown.png`, `CrossLeft.png`, `CrossRight.png`
- Cada brazo se posiciona con su extremo de unión en `(DpadCx, DpadCy)`:
  - Up: `cy = DpadCy - h/2 + 2` (ajuste fino +2px)
  - Down: `cy = DpadCy + h/2`
  - Left: `cx = DpadCx - w/2`
  - Right: `cx = DpadCx + w/2`
- Cada brazo se ilumina (`kColWhite`) de forma independiente según su dirección.
- `GreyCrossPS.png` se mantiene cargado como fallback (no se usa en render actual).

### Próximo paso acordado
- **Layout de PadView desde JSON** — D4 adelantado: las constantes del namespace Layout
  pasarán a leerse de un fichero JSON para poder editar posiciones sin recompilar.

## 2026/03/27 — Sistema de layouts JSON (Fase D4 parcial)

### Sistema de layouts implementado y funcional ✓

#### Nuevos ficheros
- **`ui/PadLayout.h`** — struct `PadLayout` con todos los valores de posición/tamaño/color.
  Defaults = valores calibrados del Pro 3 PS. Sin dependencias externas.
- **`data/pad_layouts.json`** — layout "pro3_ps" con todos los valores explícitos.

#### Ficheros modificados
- **`input/ControllerConfig.h`** — campo `layout_id` añadido a `ControllerConfig` (vacío = defaults).
- **`config/ConfigLoader.h/.cpp`** — `ConfigLoader.h` incluye `PadLayout.h`; parseo de `layout_id`
  en `loadControllerConfigs`; nuevas funciones `loadPadLayouts()` y `findLayout()`.
- **`PadEngine.h/.cpp`** — `m_activeLayoutId` (mutex-protected) + `getActiveLayoutId()` accessor.
  Se establece al encontrar cfgBase (junto al setDevice), nunca en hot-swap (el layout es por hardware).
- **`ui/PadView.h/.cpp`** — namespace `Layout` eliminado; toda la lógica usa `m_layout: PadLayout`.
  `load()` guarda el D3D device y carga templates con defaults. `setLayout()` recarga solo las
  texturas que cambian (templates/home icon) e ignora cambios si el id es igual.
- **`AppWindow.h/.cpp`** — `m_padLayouts` + `m_currentLayoutId`; carga layouts al startup;
  `renderPadTab()` detecta cambio de layoutId en cada frame y llama `padView.setLayout()`.
- **`data/controllers.json`** — `layout_id: "pro3_ps"` añadido a Pro 3 D/X-mode y Pro 2 D/X-mode.
  F310 y DS4 sin `layout_id` → PadView usa struct default (mismos valores).

#### Diseño arquitectónico
- Un único `pad_layouts.json` con array de layouts.
- El tint (color del mando) vive en el layout, no en controllers.json.
- Añadir un nuevo template para DS4/F310/Switch: solo JSON + assets, sin recompilar.
- Layout cambia automáticamente al cambiar de mando activo (hot-swap entre sesiones).


## 2026/03/27 (2) — Layout Pro 2 + Arquitectura component-based (Fase D4 completa)

### Layout Pro 2 configurado ✓

- **`data/pad_layouts.json`** — añadido layout "pro2_ps":
  - Color cuerpo gris [0.62, 0.62, 0.62]
  - Botones cara: negros en reposo, colores DualShock al pulsar (A=verde, B=rojo, X=rosa, Y=azul)
  - L1/R1/L2/R2/L5/R5: blancos en reposo, negros al pulsar (invertido respecto a los de cara)
  - Sin L4/R4 (simplemente no aparecen en la lista de componentes)
  - Labels Nintendo en posición física: X arriba, A derecha, B abajo, Y izquierda
  - Home: círculo gris [0.45,0.45,0.45], icono negro
- **`data/controllers.json`** — Pro 2 D/X-mode apuntan a `layout_id: "pro2_ps"`
  - Corrección L5/R5 Pro 2: botón 3 = RP (0x04), botón 6 = LP (0x20)
  - Cross-mapping A/B: `{ "physical": "a", "virtual": "b" }` / `{ "physical": "b", "virtual": "a" }`
  - El mapping físico→virtual lo resuelve controllers.json; el layout solo declara posición y estado

### Refactor arquitectónico: sistema totalmente data-driven ✓

**Problema identificado**: añadir el Pro 2 requería tocar código (nuevos flags `hasL4R4`, `swapAB`, `swapXY`,
colores de shoulder separados, etc.). La arquitectura correcta debe ser: añadir mando = solo JSON.

**Solución implementada — component-based rendering**:

#### `ui/PadLayout.h` — reescrito
- `PadComponent`: cada elemento visual es autónomo (type, image, overlay, colors inactive/active, position, size, state binding)
- `PadLayout`: solo canvas (W, FrontH, TopH) + `std::vector<PadComponent> components`
- Tipos de componente: `template` | `button` | `stick` | `dpad`
- `overlay_scale`: escalar o `[sx, sy]` para escalas asimétricas

#### `data/pad_layouts.json` — reescrito
- Cada layout es una lista ordenada de componentes (back-to-front)
- Cada componente lleva: image, overlay, overlay_scale, cx, cy, w, h, state binding, colors [r,g,b,a]
- Sin flags de comportamiento. `hasL4R4` → omitir esos componentes. `swapAB` → poner directamente el overlay y state correctos en cada posición.
- Dpad: `image_up/down/left/right` + `state_up/down/left/right`
- Stick: `image`, `size`, `max_offset`, `state_x/y/click`

#### `config/ConfigLoader.cpp` — `loadPadLayouts()` reescrito
- Parsea lista de componentes. Ignora entradas sin `"type"`.
- `overlay_scale`: si array → `[sx, sy]`; si número → ambos iguales.
- `parseColor4` local: lee `[r,g,b]` o `[r,g,b,a]` uniformemente.

#### `ui/PadView.h` — reescrito
- Textures en `std::unordered_map<std::string, PadTexture>` en lugar de miembros individuales.
- `PadTexture`: añadidos move constructor y move assignment para ser almacenable en el mapa.
- `getTex(name)`: helper que devuelve `const PadTexture*` por nombre.

#### `ui/PadView.cpp` — reescrito
- `load()`: solo inicializa device. No carga texturas (hasta primer `setLayout()`).
- `setLayout()`: calcula el conjunto de imágenes necesarias, descarga las sobrantes, carga las nuevas.
- `unload()`: libera todo el mapa.
- `render()`: itera `L.components` en orden. Lambdas: `img(texture, cx, cy, w, h, tint)` + `resolveState()` + `resolveFloat()`.
  - `button`: `getTex(image)` + `getTex(overlay)`; overlay escalado por `overlayScaleX/Y`.
  - `stick`: base imagen + dot (color = `color + 0.35` inactivo, `activeColor` pulsado).
  - `dpad`: 4 arms con posiciones derivadas del tamaño natural de las texturas (mismo cálculo que antes).
  - `template`: imagen centrada con `color` como tint.

#### Eliminado del código
- Todos los campos planos de `PadLayout` (tintR/G/B, hasL4R4, swapAB, swapXY, bumperW, stickSize, todos los Cx/Cy...)
- La cadena de flags y condiciones en el render (if hasL4R4, swapAB logic, kShldInact vs kInactiveL...)
- Los 20+ miembros PadTexture individuales de PadView

### Nota de calidad
- `overlay_scale: 0.70` para L1/R1 — el símbolo CharactersL1/R1 no encaja perfectamente.
  Pendiente ajustar el artwork. Ideal: dibujos nativamente del tamaño correcto sin escalar.
- Sin compilar aún — listo para primera prueba.

## 2026/03/31 — Mando virtual Xbox One + Marquesina de eventos

### Mando virtual en pestaña Pads ✓

**Concepto**: el mando físico a la izquierda, el mando virtual Xbox One a la derecha.
El virtual muestra el estado POST-bot/macro (lo que realmente envía ViGEm).

**`PadEngine.h/.cpp`**
- `m_lastVirtualState` — GamepadState capturado justo antes de `output->update(state)`.
- `getLastVirtualState()` — getter thread-safe.

**`AppWindow.h/.cpp`**
- `m_virtualPadView` (PadView) + `m_virtualPadInitialized` — carga layout `xbox_one` una sola vez.
- `renderPadsTab()` — render lado a lado con `BeginGroup/SameLine/EndGroup` (patrón correcto ImGui).
- Ventana: 1000 → 1150px ancho, 650 → 780px alto.

**`data/pad_layouts.json`** — layout `xbox_one` (líneas 647-913):
- Assets `TempalteFrontXboxOne.png` / `TempalteTopXboxOne.png` ya existían.
- Overlays de botones de cara corregidos (estaban cruzados en parejas: A↔B, X↔Y).

### Marquesina de eventos ✓

**Arquitectura**: el engine publica hechos tipados, la UI los interpreta. Sin strings de presentación en el dominio.

**`PadEngine.h`** — `PadEventType` enum + `PadEvent` struct:
- `BotToggle`, `MacroToggle`, `KeyboardAction`, `MouseAction`
- `m_eventQueue` (deque, max 16) + `pushEvent()` privado + `pollEvents()` público.

**`PadEngine.cpp`** — emite eventos en:
- Bot toggle (on/off)
- Macro toggle por botón + macro auto-off
- Keyboard press (construye string "alt+tab" desde `action.keys`)
- Mouse click press
- Mouse movement start (edge: stick entra en dead zone)

**`AppWindow.h/.cpp`** — `MarqueeEntry` con `MarqueeEntryType` (Macro/BotOn/BotOff/Keyboard/Mouse):
- 4 slots fijos siempre visibles (huecos = `Dummy` con altura de línea).
- Fade: slot 0 (oldest) = alpha 0.25, slot 3 (newest) = alpha 1.0.
- Colores: Macro=amarillo, BotOn=azul, BotOff=naranja, KB=cian, Mouse=verde claro.
- `pollEvents()` llamado cada frame; la UI interpreta el tipo y forma el texto.

### Perfiles de juego: soporte de axes ✓

**Problema**: `applyProfile` solo fusionaba `buttons`, ignoraba `axes`. El bloque `axes` en
perfiles de juego era silenciosamente descartado.

**`ConfigLoader.h`** — `GameProfile::Override` añade `axes: unordered_map<string, AxisMapping>`.
**`ConfigLoader.cpp`**:
- `loadGameProfile()` — parsea bloque `axes` de cada override.
- `applyProfile()` — fusiona axes del perfil sobre los de la config base (misma lógica que buttons).

**`FinalFantasyX.json`** — Pro 2 D-mode: `hid_z/hid_rz → mouse_x/mouse_y` (stick derecho = ratón).
- Stick derecho solo se usaba para Lulu's Overdrive (controlado por macro, no por eje virtual) — sin pérdida funcional.
- Corrección de eje: `dwZpos/dwRpos` (WinMM) → `hid_z/hid_rz` (HID correcto para Pro 2 D-mode).

---

## 2026/04/01 — Diseño del editor de templates de mandos (sin código)

Sesión de diseño. Nada compilado. Ver sección **EDITOR DE LAYOUTS** en SESSION_CONTEXT.md para el spec completo.

---

## 2026/04/02 — Implementación del Editor de Layouts (Fase D5 parcial)

### Nuevos archivos
- `ui/LayoutEditor.h` / `ui/LayoutEditor.cpp` — editor visual de pad_layouts.json
- Nueva pestaña "Layout" en AppWindow

### Cambios en archivos existentes
- `config/ConfigLoader.h/.cpp` — añadido `savePadLayouts()` (serialización inversa a JSON)
- `ui/PadView.h/.cpp` — añadidos `forceSetLayout()`, `updateLayout()`, `hitTest()`, `getTextureSize()`, `render()` con highlight de componente seleccionado
- `AppWindow.h/.cpp` — pestaña Layout, fallback `.bak` en loadPadLayouts, `renderLayoutTab()`
- `VirtualPad.vcxproj` — añadidos LayoutEditor.h/.cpp

### Funcionalidades implementadas
- Tres paneles: lista de layouts (izq.), lienzo canvas (centro), propiedades (der.)
- Templates (front+top) auto-generadas en nuevo layout, no arrastrables, seleccionables para editar imagen/tinte
- Click en canvas → selección del componente (hitTest)
- Drag en canvas → mover componente
- Flechas del teclado → mover 1px (requiere foco en canvas)
- Auto-fill w/h al seleccionar imagen de botón/decoración
- Imágenes organizadas en 5 carpetas fijas: `templates/`, `cross/`, `buttons/`, `analogics/`, `decorations/`
- Combo de IDs sugeridos por tipo de componente (sin compartir estado entre componentes)
- Colores compactos en fila con `NoInputs` (un click)
- InputFloat para posición/tamaño (un click), con checkbox "Mantener proporción" para w/h
- Guardar con backup automático (no sobreescribe `.bak` existente) + botón "Backup manual"
- Popup modal para ID al crear nuevo layout

### Bugs conocidos / pendientes
- **Crash al desconectar mando** (`0xC0000374` heap corruption): ocurre cuando el mando se desconecta mientras el editor está activo. Requiere call stack de debugger para diagnosticar.
- Crash inicial por `json::parse_error` sin try/catch en loadPadLayouts — corregido.
- Conflictos de ID ImGui en botones "Guardar"/"Descartar" — corregidos con sufijos `##`.
- `imgui.h` faltaba en `PadView.h` y `LayoutEditor.h` — corregido.

## 2026/04/02 (2) — Layout xbox_one dado por completado

Sin cambios de código. Revisión del estado del layout: todos los componentes están definidos en JSON
(templates, LT/RT, LB/RB, cruceta, botones cara, sticks, Back/Start/Home). El cuerpo verde es color
definitivo por decisión de diseño (sin mando Xbox físico de referencia). Assets L1/R1/L2/R2/cruceta/
select/start aceptados como válidos. Fase D3 (xbox_one) cerrada.

---

## 2026/04/02 (3) — Fix: lock de proporción en editor de layouts

### Problema
El checkbox "Mantener proporción" se volvía loco: a veces sumaba, a veces restaba, sin control.
Causa: el ratio `w/h` se recalculaba cada frame a partir de los valores ya modificados → deriva acumulativa.

### Solución
- `m_lockedRatio` (ya declarado en `.h`) ahora se captura **una sola vez** al activar el lock (`lockJustEnabled`).
- Mientras el lock está activo, siempre se usa `m_lockedRatio` (valor fijo), nunca se recalcula.
- El overlay_scale también corregido: usaba su propio ratio inline, ahora usa `m_lockedRatio`.

### Archivos modificados
- `ui/LayoutEditor.cpp` — `renderRightPanel()`: lógica de aspect ratio lock y overlay scale lock.

---

## 2026/04/03 — Asistente de emparejamiento de mando (BindingWizard) + mejoras al editor

### Nuevos archivos
- `ui/BindingWizard.h` / `ui/BindingWizard.cpp` — asistente de emparejamiento paso a paso
- `input/RawHIDReader.h` / `input/RawHIDReader.cpp` — lector HID raw sin ControllerConfig (solo para wizard)
- `data/state_map.json` — mapeo de campos GamepadState → nombre físico + tipo + prompt de usuario

### Cambios en el editor de layouts
- **Toast notifications**: ventana flotante (3s) en la parte inferior central del canvas, reemplaza la barra de estado fija. Flags cuidados para que no quede detrás de la ventana principal.
- **Dirty tracking**: flag `m_dirty` activado en drag, nudge, edición de propiedades, añadir/eliminar componente. Se limpia al guardar o descartar. Modal de confirmación al cambiar de layout con cambios pendientes.
- **Botón "Emparejar mando"** en el panel izquierdo → lanza `m_wizard.start(m_editLayout)`.
- El wizard reemplaza los tres paneles mientras está activo; al terminar se vuelve al editor normal.

### Arquitectura del BindingWizard
- Máquina de estados: `Idle → SelectController → NameController → WarnNoState → Binding → Review`
- `scanControllers()` — escanea HID + WinMM, filtra VID:5650/PID:0001 (ViGEm)
- `buildSteps()` — recorre componentes del layout:
  - `type == "stick"`: añade paso de botón para `stateClick` (L3/R3)
  - `type == "dpad"`: un solo paso (usa `stateUp/Down/Left/Right`, toma el primero disponible)
  - Botones/triggers: paso individual por componente
  - Ejes: bucle fijo `{leftX, leftY, rightX, rightY}` busca componentes stick con stateX/stateY coincidente
- `captureButton()` — detección de flanco de subida (`newBits = mask & ~prevMask`)
- `captureAxis()` — mayor delta entre 6 ejes vs baseline; filtra ejes ya asignados en pasos anteriores para evitar que un gatillo "robe" el eje del stick
- `captureDpad()` — hat != 0xFFFFFFFF (HID) o pov != JOY_POVCENTERED (WinMM)
- Cooldown de 45 frames (~750ms a 60fps) después de cada commit de eje/gatillo; re-snapshot del baseline al expirar
- `saveResult()` — construye JSON, reemplaza por VID/PID en controllers.json; activa `m_savedFlag`

### Cadena de recarga tras guardar
- `BindingWizard::pollSaved()` → `LayoutEditor::pollControllersSaved()` → `AppWindow::renderLayoutTab()`
- AppWindow recarga `m_controllerConfigs` y llama `PadEngine::reloadConfigs()` (nuevo método)
- `PadEngine::reloadConfigs()` — carga controllers.json bajo mutex, actualiza `m_configs`

### Bugs detectados en pruebas
- **Eje X stick izquierdo no calibra**: el paso se genera y el prompt aparece, pero empujar el stick a la derecha no activa la captura. Los demás ejes (leftY, rightX, rightY, gatillos) funcionan. Hipótesis: el eje físico X mapea a un HID usage inesperado, o la baseline se contamina con el gatillo anterior. Se añadió filtro de ejes ya-asignados como mitigación, pero el bug persiste.
- **Pads no refresca tras wizard**: la cadena de recarga está implementada pero la pestaña Pads sigue sin mostrar el nuevo mando hasta reiniciar la app. El flag `m_savedFlag` se activa en `saveResult()`. Pendiente depuración.

### Pendiente / mejoras UX anotadas
- Indicadores más amigables en el wizard: flechas visuales para ejes, botones que cambian de color al asignarse en vez de solo overlay numérico

---

## 2026/04/04 — DS4: fix BT eje X + soporte touchpad completo + fixes wizard

### Fix: DS4 Bluetooth eje X siempre 0

**Problema**: DualShock 4 conectado por Bluetooth: el eje X del stick izquierdo (hid_x) siempre reportaba 0.
El resto de ejes y botones funcionaban correctamente tanto por USB como por BT.

**Diagnóstico**: Añadidas trazas en `applyAxes`. Resultado BT: `axis hid_x page=0xFF00 usage=0x30 status=0xC011000A` (HIDP_STATUS_INCOMPATIBLE_REPORT_ID).

Causa raíz: el descriptor HID del DS4 en BT expone **dos ValueCaps con usage 0x30**:
- ReportID=1, Page=0x01 (Generic Desktop X — el eje correcto)
- ReportID=25, Page=0xFF00 (vendor-specific BT)

El bucle de construcción de `m_usagePage` usaba `=` sin comprobar colisiones, por lo que el último (Page=0xFF00) machacaba al correcto. En USB solo existe una entrada, por eso USB funcionaba.

**Solución**: `insertCap()` lambda en `HIDInputSource.cpp` constructor — al colisionar usage, prefiere la página estándar (< 0xFF00) sobre vendor-specific (≥ 0xFF00).

### Soporte DS4 touchpad (Fase 1 completa)

#### Nuevas estructuras
- `GamepadState.h` — campos: `btnTouch`, `touch1Active`, `touch1X/Y`, `touch2Active`, `touch2X/Y`, `touchDeltaX/Y`
- `input/ControllerConfig.h` — struct `TouchpadConfig` (enabled, dataOffset, maxX, maxY, mouseEnabled); campo `touchpad` en `ControllerConfig`
- `config/ConfigLoader.cpp` — parseo de bloque opcional `"touchpad"` en controllers.json

#### Lectura del touchpad
`HIDInputSource` — método `applyTouchpad(buf, bytesRead, state)`:
- Decodifica coordenadas 12-bit: `X = buf[1] | ((buf[2] & 0x0F) << 8)`, `Y = ((buf[2] & 0xF0) >> 4) | (buf[3] << 4)`
- Detección de contacto: bit7 del primer byte = 0 → activo
- Delta mouse por diferencia normalizada inter-frame (sin zona muerta, escala por maxX/Y)
- Dos dedos: finger 1 en offset, finger 2 en offset+4
- `data_offset = 35` (bytes 35-38 finger 1, 39-42 finger 2). **Bug previo**: offset=34 capturaba timestamp → movimiento caótico. Corregido.

#### Render en PadView
- Componente tipo `"touchpad"` en `pad_layouts.json`: imagen de fondo + dot azul (finger1) + dot naranja (finger2)
- `resolveState`/`resolveFloat` extendidos para campos de touchpad

#### Mouse desde touchpad
`PadEngine.cpp` — bloque post-sticks: si `touchDeltaX/Y != 0`, acumula en `mouseAccumX/Y` con factor `kTouchpadScale=1.5f`. Sin zona muerta (el delta solo existe si hay movimiento real).

#### Wizard: paso de touchpad
- `state_map.json` — añadida entrada `"btnTouch": { "physical": "touch_btn", "type": "physical_only" }`
- `data/controllers.json` (DS4) — botón 14: `{ "physical": "touch_btn" }` (sin virtual — no tiene equivalente Xbox)
- `data/pad_layouts.json` (dualshock4) — componente "touch" cambiado de type `"button"` a `"touchpad"`, añadido `"state": "btnTouch"` y `active_color`
- `BindingWizard::buildSteps()` — case `"touchpad"` añadido: genera paso de captura de botón para `btnTouch`

#### Config DS4 final (controllers.json)
```json
"touchpad": {
  "enabled": true, "data_offset": 35,
  "max_x": 1919, "max_y": 942, "mouse_enabled": true
}
```
Ejes hid_rx/hid_ry → trigger_l/trigger_r con `invert: false`.

### Fix: wizard BindingWizard — dos bugs estructurales

#### Bug 1: captureAxis guarda invert incorrecto para gatillos HID
**Causa**: los gatillos HID reposan en -1.0 (raw=0 normalizado). Al pulsar levemente, val=-0.7. La comparación `val < 0.0f = true` → guardaba `invert=true` incorrectamente. Los gatillos quedaban siempre pulsados (triggerL = (v+1)*0.5 con v=+1 invertido desde -1 = 1.0).

**Fix** (`BindingWizard.cpp`, `captureAxis`): usar `deltaSigned = cur - baseline` en lugar del valor absoluto. `outInvert = invertIfPositive ? (deltaSigned > 0.0f) : (deltaSigned < 0.0f)`. Al pulsar L2: baseline=-1.0, cur=-0.7 → deltaSigned=+0.3 → outInvert=false ✓.

#### Bug 2: saveResult() borra campos no gestionados (touchpad, etc.)
**Causa**: `e = entry` reemplazaba el objeto JSON completo, perdiendo campos como `"touchpad"`.

**Fix** (`BindingWizard.cpp`, `saveResult`): en lugar de `e = entry`, iterar `entry.items()` y sobrescribir solo las claves que el wizard gestiona, preservando el resto del objeto antiguo.

### Backlog DS4 anotado
- **BT full report (0x11)**: DS4 en BT simplificado solo expone report 0x01 (~10 bytes, sticks+botones). Para touchpad, giroscopio y acelerómetro completos se necesita wake-up con output report + parseo raw del report 0x11 (78 bytes). Requiere dos entradas en controllers.json (USB vs BT) o detección dinámica del tipo de conexión.

### Archivos modificados en la sesión
- `input/HIDInputSource.cpp/.h` — insertCap, applyTouchpad, m_lastTouch*
- `GamepadState.h` — campos touchpad
- `input/ControllerConfig.h` — TouchpadConfig
- `config/ConfigLoader.cpp` — parseo touchpad
- `PadEngine.cpp` — mouse delta desde touchpad
- `ui/PadView.cpp` — render componente touchpad
- `ui/BindingWizard.cpp` — case touchpad en buildSteps, fix captureAxis, fix saveResult
- `data/state_map.json` — btnTouch
- `data/pad_layouts.json` — componente touchpad dualshock4
- `data/controllers.json` — DS4: btn14 physical, invert triggers, touchpad section

---

## 2026/04/05 — Giroscopio DS4: lectura + visualización texto (opción C)

### Análisis previo
Revisión de qué mandos tienen IMU (DS4, Joy-Con, Pro 2, Pro 3) y decisión de arquitectura.
**Decisión clave**: no usar herencia para `GamepadState` — el patrón correcto es superconjunto plano, igual que el touchpad. Campos inactivos simplemente valen false/0.

### Implementación

#### Nuevas estructuras
- `GamepadState.h` — campos: `gyroActive`, `gyroX`, `gyroY`, `gyroZ`
- `input/ControllerConfig.h` — struct `ImuConfig` (enabled, gyroOffset=13, gyroScale=1/32768); campo `imu` en `ControllerConfig`
- `config/ConfigLoader.cpp` — parseo de bloque opcional `"imu"` en controllers.json

#### Lectura del giroscopio
`HIDInputSource` — método `applyIMU(buf, bytesRead, state)`:
- Lee 3 × int16 little-endian desde `imu.gyroOffset` (DS4 USB: offset 13)
- Normaliza a [-1..1] con `gyroScale`; clamp para saturar si el sensor llega al límite
- Guard: si `offset+6 > bytesRead`, no hace nada (protege contra reports cortos como BT simplificado)
- Llamado desde `read()` junto con `applyButtons`, `applyAxes`, `applyTouchpad`

#### Config DS4 (controllers.json)
```json
"imu": { "enabled": true, "gyro_offset": 13, "gyro_scale": 0.00006103515625 }
```
`gyro_scale = 1/16384` (2× el rango máximo int16) — da valores más vivos para movimiento normal.

#### Visualización (opción C — texto)
`AppWindow::renderPadsTab()` — bloque gyro entre los pads y la marquesina:
- Solo visible cuando `gyroActive == true` (mandos sin IMU no lo muestran)
- Texto azul claro: `Gyro  X: +0.xxx   Y: +0.xxx   Z: +0.xxx`

### Ejes confirmados por observación con DS4 USB
- **X**: giro horizontal del mando (pitch)
- **Y**: rotación del mando sobre la superficie (yaw)
- **Z**: inclinación lateral (roll)

### Crash 0xC0000374 — investigación
Durante pruebas apareció el crash pre-existente. Intentado debuggear en Debug build pero ViGEmClient.lib es Release-only (MT) → incompatibilidad de CRT con el proyecto en Debug. Solución: usar F5 en Release (genera PDB). Crash no reproducible en esa sesión — pendiente.

### Pendiente (backlog)
- **Componente visual "gyro"** (opción A): círculo con dot que se desplaza según gyroX/gyroY, en vista top del layout
- **Calibración de offsets Pro 2 / Pro 3**: verificar si exponen IMU en D-mode HID
- **Joy-Con**: protocolo diferente, requiere output report para activar IMU

### Archivos modificados
- `GamepadState.h` — gyroActive, gyroX/Y/Z
- `input/ControllerConfig.h` — ImuConfig, campo imu en ControllerConfig
- `input/HIDInputSource.h/.cpp` — applyIMU(), llamada en read(), dump raw extendido a 20 bytes
- `config/ConfigLoader.cpp` — parseo sección imu
- `AppWindow.cpp` — readout texto gyro en renderPadsTab
- `data/controllers.json` — sección imu en DS4

---

## 2026/04/07 (00:00) — BindingWizard + LayoutEditor: fixes UI y nuevas funciones

### Fix: gatillos DS4 invertidos al capturarlos en el wizard
Los gatillos del DualShock 4 se capturaban con `invert` incorrecto al usar el BindingWizard: al guardar el resultado, el eje quedaba invertido y los gatillos aparecían siempre pulsados o al revés.
**Fix**: corrección en la lógica de `captureAxis` / `saveResult` del wizard. Los gatillos DS4 (hid_rx / hid_ry) ahora se guardan con `invert: false` correctamente y funcionan con normalidad post-emparejar.

### Refinamiento del giroscopio DS4
Ajuste fino de la lectura y/o escala del giroscopio. Comportamiento más estable y representativo del movimiento real.

### Fix: pestaña Pads no se actualizaba al quitar el giroscopio de un layout
Cuando el layout activo no incluía componente de giroscopio, la pestaña Pads no refrescaba su estado correctamente. Corregido para que la actualización sea coherente con la configuración del layout en todo momento.

### Nueva función: copiar layout
Añadida opción en el LayoutEditor para duplicar un layout existente como punto de partida para uno nuevo.

### Mejoras panel derecho del LayoutEditor
- **Reagrupación de secciones**: los componentes de la columna derecha del editor han sido reorganizados visualmente en grupos más claros.
- **Scroll propio para la lista de elementos**: la lista de componentes del mando en el panel derecho tiene ahora su propio scroll independiente, similar al apartado de layouts.

### Cambio de UI en BindingWizard — paso Emparejar
Antes: el paso de emparejamiento mostraba un cuadrado genérico como referencia visual.
**Ahora**: se muestra la imagen activa del propio botón (o componente) que se está asignando. Para los pasos de ejes analógicos, se añaden flechas visuales que indican la dirección del movimiento esperado.

### Cambio de UI en BindingWizard — menú de asignación de botones
- El menú de asignación (lista de botones + mensaje "pulsa el botón") se ha recolocado pegado al mando.
- Tamaño de fuente aumentado para mejor legibilidad.
- El botón "Atrás" del wizard pierde el carácter flecha (no renderizaba correctamente) y se muestra en gris para que no desplace los demás botones cuando está deshabilitado.

### Pendiente para próxima sesión
- **Apartado "Layouts" en pestaña Layout**: reducir su altura aproximadamente 2 botones y añadir scroll propio (igual que el apartado "Elementos").
- **Último menú al terminar asignación**: cuando el wizard termina la asignación de botones, el menú final debe alinearse al lado del mando.

### Fix: macros con múltiples botones simultáneos `(L1, R1)*N/fps`
En macros del tipo `(L1, R1)*5000/30` (varios botones dentro de un grupo repetido), solo se pulsaba el primer botón del grupo. Los botones adicionales de la misma instrucción eran ignorados.
**Fix**: corrección en el parser/ejecutor de macros para que todos los botones del grupo se apliquen correctamente en cada iteración.

### DualShock 4 USB — considerado completo
La cadena de inputs DS4 por USB queda cerrada como funcional en su totalidad:
- **Botones** ✓ — todos los botones de cara, hombros, gatillos digitales, PS, touchpad click
- **Analógicos** ✓ — sticks L/R, gatillos analógicos (hid_rx/hid_ry) sin invert
- **Touchpad** ✓ — lectura de 2 dedos, render en Pads, mouse delta, wizard step
- **Giroscopio (IMU)** ✓ — gyroX/Y/Z normalizados, visualización texto en Pads
- **Pendiente**: DS4 Bluetooth — report simplificado (0x01), falta wake-up + parseo raw 0x11

### Archivos modificados (estimado — sesión cerrada sin guardar contexto)
- `ui/BindingWizard.cpp/.h` — fix captura gatillos, UI emparejar (imagen activa + flechas), menú asignación pegado al mando, botón atrás en gris, fuente mayor
- `ui/LayoutEditor.cpp/.h` — reagrupación panel derecho, scroll lista elementos, función copiar layout
- `AppWindow.cpp` — fix refresh pestaña Pads con giroscopio
- `input/HIDInputSource.cpp` — refinamiento giroscopio (posible)
- `macros/MacroParser.cpp` o `Macro.cpp` — fix botones simultáneos en grupos repetidos

---

## 2026/04/07 (1) — LayoutEditor + BindingWizard: mejoras UI

### G3: Apartado "Layouts" más compacto con scroll visible
La lista de layouts en el panel izquierdo del LayoutEditor tenía un máximo de 6 líneas de altura y sin borde.
**Cambio**: altura máxima reducida a 4 líneas (`lineH * 4.0f`). Añadido borde al child (`true`) para que el scroll sea visible cuando hay más de 4 layouts. No afecta al foco ni a la selección de layouts.

### LayoutEditor: renombrado "Descartar" y nuevo botón "Borrar layout"
- Botón "Descartar" renombrado a "Descartar cambios" para mayor claridad.
- Nuevo botón "Borrar layout" debajo de "Descartar cambios": elimina el layout activo del JSON con popup de confirmación. Actualiza `m_selectedLayout = -1` y sale del modo edición tras el borrado.
- `bottomH` del panel actualizado de `lineH * 7.0f` a `lineH * 8.0f` para reservar espacio al nuevo botón.

### F4: Menú final del wizard pegado al mando
En `renderReview()`, el canvas tomaba todo el espacio disponible menos los 300px del panel derecho, dejando el mando a la izquierda con espacio vacío.
**Fix**: mismo patrón que `renderBinding()` — canvas dimensionado a `m_layout.W` con fallback si no cabe.

### Panel derecho del wizard ampliado a 350px
`rightW` subido de 300px a 350px en `renderBinding()` y `renderReview()`.

### Archivos modificados
- `ui/LayoutEditor.cpp` — G3 (lista layouts compacta), Descartar cambios, Borrar layout, bottomH
- `ui/BindingWizard.cpp` — F4 (renderReview canvas pegado al mando), rightW 350px

---

## 2026/04/08 — Fase H: Editor de mapping físico→virtual (H1-H3) + fixes arquitecturales

### H1 — Subtab [Mapear] + dos PadView lado a lado
Añadida subpestaña `[Mapear]` dentro de la pestaña Pads (junto a `[Ver]`). Muestra el mando físico a la izquierda y el Xbox virtual a la derecha, con una imagen ArrowRight.png de separación. Mismo patrón de flecha añadido también en `[Ver]`.

### H2 — Click-to-select físico + click-to-assign virtual
- Click en botón físico → lo selecciona (highlight igual que modo "activado" de ImGui).
- Click en botón virtual → asigna el mapping en `m_mappingEdits` (mapa en memoria `physShort → virtShort`).
- Pre-population: al abrir [Mapear] con un mando activo nuevo (VID/PID diferente), carga los mappings existentes de `controllers.json` para mostrar el estado guardado.
- Helpers estáticos: `shortToState`, `stateToShort`, `findCompByState`, `activateState`, tabla `kBtnNames[]`.

### H3 — Guardar en controllers.json
- Botón "Guardar" escribe `m_mappingEdits` en `controllers.json` (formato `{"physical":"x","virtual":"y"}`).
- **Write atómica**: escribe a `.tmp`, luego `MoveFileExA(..., MOVEFILE_REPLACE_EXISTING)` → evita truncación si el engine lee el fichero al mismo tiempo.
- Llama `m_engine.reloadConfigs()` tras guardar.

### IInputSource / HIDInputSource — getPhysicalState()
- `IInputSource::getPhysicalState()` — nueva virtual con implementación default vacía.
- `HIDInputSource::getPhysicalState()` — devuelve `m_physicalState` (campo nuevo, privado).

### Bug fix: virtual remapping no llegaba a ViGEm
**Causa**: en `applyButtons()`, Loop 1 (acción virtual, `action.name`) se ejecutaba primero, Loop 2 (nombre físico, `action.physical`) se ejecutaba después y sobreescribía el estado → ViGEm recibía siempre el botón físico, no el virtual.

**Fix en `HIDInputSource::applyButtons()`**: 
- Loop físico corre **primero** y escribe en un `GamepadState physDisplay` separado → guardado en `m_physicalState`.
- Loop virtual corre **después** y escribe en `state` (el que sale de `read()`).
- Resultado: `state` tiene el mapping virtual correcto para ViGEm; `m_physicalState` tiene el estado real del hardware.

**Fix en `PadEngine.cpp` línea 684**: `m_lastState = input->getPhysicalState()` para que el pad físico de la UI muestre botones reales.

### Bug fix: Ver tab no actualizaba tras guardar mapping
**Causa**: el `HIDInputSource` activo seguía usando la config vieja; `reloadConfigs()` actualizaba `m_configs` pero no la llamaba `input->setConfig()`.

**Fix**: flag atómico `m_configsDirty` en `PadEngine`:
- `reloadConfigs()` lo pone a `true` tras actualizar `m_configs`.
- El run loop del engine (junto al hot-swap de perfil) lo detecta (`exchange(false)`), recopia `configs = m_configs`, re-busca `cfgBase`, reconstruye `effectiveCfg`, y llama `input->setConfig(*cfg)`.
- El mapping nuevo se aplica en el siguiente tick sin reiniciar el dispositivo.

### Bugs críticos encontrados y resueltos durante H3
1. **Crash al guardar por primera vez**: modificar un valor JSON de `string` a `object` durante la iteración del mapa → UB/crash. Fix: acumular cambios en `vector<pair>`, aplicar tras el bucle.
2. **Race condition**: el engine leía `controllers.json` en fase Configuring mientras la UI lo escribía → JSON truncado → parse error → engine muerto. Fix: write atómica con archivo temporal.
3. **char8_t error** (C++20): `u8"\u2192"` produce `const char8_t*`, incompatible con `const char*`. Fix: `"\xE2\x86\x92"` (UTF-8 literal estándar).

### Archivos modificados
- `input/IInputSource.h` — `getPhysicalState()` virtual default
- `input/HIDInputSource.h` — `getPhysicalState()` override + `m_physicalState` privado
- `input/HIDInputSource.cpp` — `applyButtons()` reestructurado: loop físico primero + `m_physicalState`, loop virtual después
- `PadEngine.h` — `m_configsDirty` atómico
- `PadEngine.cpp` — `m_lastState = input->getPhysicalState()`, `reloadConfigs()` pone dirty flag, run loop detecta dirty y llama `input->setConfig()`
- `AppWindow.h` — miembros mapping editor (`m_mappingEdits`, `m_mappingSelPhysComp`, VID/PID, origins, arrow tex)
- `AppWindow.cpp` — `renderMappingSubtab()`, `saveMappingEdits()`, helpers estáticos, flecha en [Ver] y [Mapear]

---

## 2026/04/09 — Fase H: H4 hold-to-select, H5 acciones no-botón, H9 whitelist Xbox

### H4 — Hold 1s para seleccionar botón físico desde el mando
En lugar de solo hacer click en el pad físico de la UI, el usuario puede mantener pulsado un botón en el mando real 1 segundo para seleccionarlo. Barra de progreso visible mientras se mantiene pulsado.
- `m_h9HoldComp` / `m_h9HoldTimer` — componente bajo hold y tiempo acumulado.
- Detección por `isStateActive()` frame a frame sobre `physNow` (estado físico real vía `getPhysicalState()`).
- Al alcanzar 1.0s: `m_mappingSelPhysComp = activeComp` → mismo estado que click en UI.

### H5 — Selector de tipo de acción: Xbox / Macro / Teclado / Ratón
Cuando un botón físico queda seleccionado, aparece una barra de tipo debajo de los mandos:
`[ Xbox ] [ Macro ] [ Teclado ] [ Ratón ]`

**Xbox** (por defecto): comportamiento anterior — asignar botón virtual Xbox. No hay cambio respecto a H2/H3.

**Macro**: desplegable con todos los nombres de `macros.json` (carga lazy, `m_h5MacroNamesLoaded`). Botón "Asignar" → escribe `ButtonAction{Macro, name}` en `m_h5ActionEdits[physShort]`.

**Teclado**: modo captura de tecla — escanea `ImGuiKey_NamedKey_BEGIN..END` con `IsKeyPressed(..., false)`. Tabla `imguiKeyToKeyName()` (estática) traduce ImGuiKey a nombre corto compatible con `keyNameToVK()` y nombre display para el usuario. Cancelación: L1+R1 o A+B pulsados simultáneamente en el mando físico. Tras captura, muestra el nombre de la tecla y botón "Asignar".

**Ratón**: 5 botones `[ Izq ] [ Der ] [ Centro ] [ Atrás ] [ Adelante ]` → escribe `ButtonAction{MouseClick, mouseButton}` al pulsar.

**`m_h5ActionEdits`**: mapa `physShort → ButtonAction` paralelo a `m_mappingEdits` (que solo cubre Xbox). Al guardar, `saveMappingEdits()` da prioridad a `m_h5ActionEdits` sobre `m_mappingEdits` para cada physShort. `reloadMappingEdits()` rellena ambos mapas desde `controllers.json`.

### H9 — Whitelist de botones Xbox válidos
Sustituye la antigua lista negra (L4, R4, LP, RP, touch_btn) por una lista blanca configurable.
- `virtualpad.json`: nuevo campo `"accepted_xbox_buttons": ["a","b","x","y","l1","r1","select","start","home","l3","r3"]`
- `ConfigLoader.h`: `VirtualPadConfig::acceptedXboxButtons` con los mismos defaults.
- `ConfigLoader.cpp`: parsea el array en `loadVirtualPadConfig()`.
- `AppWindow.h`: `m_acceptedXboxButtons` — cargado al arranque desde la config.
- `AppWindow.cpp renderMappingSubtab()`: itera `m_acceptedXboxButtons` en lugar del antiguo `kValidVirt` estático.

**Comportamiento al pulsar botón no-whitelist en paso 2 (modo Xbox)**:
- Si el physShort ya tiene asignación (Xbox o H5) → la borra (deselect / "desasignar").
- Si no tenía nada → muestra mensaje de error en rojo 2 segundos.

### Archivos modificados
- `data/virtualpad.json` — campo `accepted_xbox_buttons`
- `config/ConfigLoader.h` — `VirtualPadConfig::acceptedXboxButtons`
- `config/ConfigLoader.cpp` — parseo de `accepted_xbox_buttons`
- `AppWindow.h` — `m_acceptedXboxButtons`; `H5ActionType` enum; `m_h5ActionType`, `m_h5CapturedKey*`, `m_h5MacroSel`, `m_h5MacroNames`, `m_h5MacroNamesLoaded`; `m_h5ActionEdits`; `m_h9HoldComp`, `m_h9HoldTimer`, `m_h9ErrorTimer`, `m_h9PrevPhysState`
- `AppWindow.cpp` — `renderMappingSubtab()` ampliado con H4/H5/H9; `imguiKeyToKeyName()` tabla estática; `saveMappingEdits()` con soporte H5; `reloadMappingEdits()` con H5

---

## 2026/04/10 — H5 completo: combos de teclado + mensaje instruccional

### Combos de teclado (cierre H5)
La captura de teclado pasó de una sola tecla a acumulación libre de teclas.

**Flujo**: pulsar "Teclado" → ir pulsando teclas una a una → aparecen en verde como `Ctrl + Z` o `Alt + Tab` → botón "Asignar" para confirmar, "Borrar" para empezar de cero. Cancelación con L1+R1 o A+B en el mando físico.

**Implementación**:
- `m_h5CapturedKeyName` + `m_h5CapturedKeyDisplay` (dos strings) reemplazados por `m_h5CaptureKeys` (`vector<pair<string,string>>` — `{json_name, display_name}`).
- Cada frame en modo Teclado: `IsKeyPressed(k, false)` sobre el rango de ImGuiKey nombradas → `imguiKeyToKeyName()` → push si no está duplicado.
- Display: join de `p.second` con `" + "`, mostrado en color verde junto a los botones Asignar/Borrar.
- Al asignar: `act.keys` se rellena con `p.first` de cada par → JSON `"keys": ["ctrl","z"]`.

### Mensaje instruccional mejorado
- Paso 2 (botón físico seleccionado, modo Xbox): *"Elige en el virtual o pulsa el botón físico que quieras asignarle"* — aclara que puedes usar el mando directamente.
- Modo Teclado sin teclas aún: *"Pulsa las teclas del combo (L1+R1 o A+B para cancelar)"*
- Modo Teclado con teclas: *"Pulsa más teclas o haz clic en Asignar"*

### Archivos modificados
- `AppWindow.h` — `m_h5CaptureKeys` sustituye a `m_h5CapturedKeyName` + `m_h5CapturedKeyDisplay`
- `AppWindow.cpp` — bloque Keyboard de `renderMappingSubtab()` reescrito; mensajes instruccionales actualizados; limpieza en todos los puntos de reset

---

## 2026/04/11 — H6 backend completo + inicio UI flechas de sticks

### Contexto
Sesión accidentada: se cerró una ventana de Claude Code en mitad del desarrollo y hubo que recuperar el hilo desde los ficheros JSONL de historial (`C:\Users\pooka\.claude\projects\G--C--\`). Se perdió tiempo pero se recuperó todo el contexto.

### H6 backend (completado en sesión anterior, cerrada a medias)
Toda la infraestructura de datos para asignar acciones por dirección de eje ya estaba implementada pero sin UI:
- `HalfAxisAction` / `HalfAxisActionType` en `ControllerConfig.h`
- `ControllerConfig::axis_actions` y `GameProfile::Override::axis_actions`
- `parseAxisActionsJson()` en `ConfigLoader.cpp`
- `getActiveAxisActions()` en `IInputSource` + implementado en `HIDInputSource` y `EightBitDoInputSource`
- `PadEngine.cpp`: edge-detection y dispatch para Macro/Keyboard/Mouse por dirección de eje

### H6 UI — diseño decidido
Tras un par de enfoques descartados (panel de cruz ImGui, widget de 5 botones), se optó por el enfoque más natural:
- Flechas como **imágenes sobre el pad**, no en un panel aparte
- Mismo flujo que los botones: seleccionar origen en el físico → elegir destino en el virtual
- Origen: click en flecha → dirección; click en cuerpo del stick → L3/R3; hold 2s → equivalente al click

### H6 UI — implementado hoy
**`PadView.h/.cpp`**
- `load()` carga las 4 texturas `images/decorations/Arrow*.png`
- `renderStickArrows(canvasOrigin, selectedComp, selDir)` — dibuja flechas alrededor de todos los sticks; la dirección seleccionada se resalta en amarillo, el resto en blanco translúcido
- `hitTestStickArrow(mousePos, canvasOrigin, outDir)` — devuelve componente + dirección si el click cae sobre una flecha

**`AppWindow.h`**
- `m_axisActionEdits` — mapa `axis_key → HalfAxisAction` (pendiente cargar/guardar)
- `m_selStickDir` — dirección seleccionada en el stick físico activo
- `H5ActionType::Analog` añadido al enum

**`AppWindow.cpp`**
- Llama `renderStickArrows()` tras cada `render()` en el subtab Mapear (físico y virtual)
- Click handler: `hitTestStickArrow()` tiene prioridad sobre `hitTest()` en el pad físico
- Panel H6 (Stick/Cruceta/Botones) restaurado sin cambios como fallback para el click en el cuerpo del stick

### Pendiente (ver SESSION_CONTEXT)
Pasos 4-8: H9 hold para sticks, asignación desde el virtual, cruceta editable, cargar/guardar `axis_actions`.

---

## 2026/04/11 (tarde) + 2026/04/12 — H9 fixes mapping + dpad H5 completo

### Sesión accidentada (contexto)
Dos sesiones cortadas por cierre de ventana y límite de uso. Se recuperó el hilo con `tail` del JSONL de conversación.

### Fix: flash de confirmación al asignar L3/R3/cruceta desde el mando (H9)
**Problema:** Al usar el mando (H9 paso 2) para asignar L3, R3 o una dirección de cruceta como destino virtual, no se mostraba el flash de 0.5s de confirmación, aunque la asignación sí se guardaba.

**Causa:** `findCompByState()` solo comparaba `component.state == stateName`. Para sticks, el click está en `stateClick` (no en `state`). Para dpad, el componente tiene `state = "dpad"` pero los inputs son `"dpadUp"/"dpadDown"` etc. → devolvía -1 → `m_mappingFlashComp = -1` → condición de flash fallaba.

**Fix:** `findCompByState()` ampliado para buscar también `stateClick` en sticks y aceptar cualquier `"dpad*"` para componentes de tipo dpad.

### Fix: cruceta como origen en H9 no guardaba la asignación
**Problema:** Seleccionar una dirección de cruceta como componente físico (paso 1 H9) y luego asignar un botón virtual desde el mando (paso 2) no guardaba nada.

**Causa:** En paso 2, `selState = selComp2.state` usaba el estado genérico del componente dpad (e.g. "dpad"), no la dirección concreta. `physShort` resultante era incorrecto. El path del ratón ya usaba `dpadDirToState(selPC, m_selDpadDir)` correctamente.

**Fix:** Paso 2 H9 usa `dpadDirToState(selComp2, m_selDpadDir)` cuando el componente es dpad y hay dirección seleccionada.

### H10 — Dpad: acciones Macro/Teclado/Ratón (nuevo)
Hasta ahora la cruceta solo admitía remapping Xbox. Se añade soporte completo de H5 (Macro, Teclado, Ratón) para las 4 direcciones de la cruceta.

**Arquitectura:**
- `ControllerConfig.h` — nuevo `dpadActions: unordered_map<string, ButtonAction>` ("up/down/left/right" → acción)
- `ConfigLoader.cpp` — `dpad_remap` ahora puede tener valores objeto (Keyboard/Mouse/Macro) además de string (Xbox remap) → van a `dpadActions`
- `AppWindow.cpp` — `reloadMappingEdits()`: carga `dpadActions` en `m_h5ActionEdits["dpad_*"]`; `saveMappingEdits()`: serializa acciones H5 como objetos JSON en `dpad_remap`, con prioridad sobre el remap Xbox
- `PadEngine.cpp` — nuevos mapas `dpadMacros/Prev/Names`, `dpadKbPrev`, `dpadMousePrev`; `initMacros()` los rellena desde `cfg->dpadActions`; loop principal: procesa dpad H5 edge-triggered y limpia el flag de `state` para que no llegue al ViGEm

**JSON resultante en controllers.json:**
```json
"dpad_remap": {
  "up": "a",                             // Xbox remap (string)
  "down": { "type": "keyboard", "keys": ["ctrl","z"] },  // H5 (objeto)
  "right": { "type": "macro", "name": "MyMacro" }
}
```

### Archivos modificados
- `input/ControllerConfig.h` — `dpadActions`
- `config/ConfigLoader.cpp` — parseo objeto en `dpad_remap`
- `AppWindow.cpp` — save + load dpad H5
- `PadEngine.cpp` — variables, `initMacros()`, loop dpad H5

---

## 2026/04/12 + 2026/04/13 + 2026/04/14 — H7 gatillos: implementación incompleta, todos los tests rotos (ver también sesión 14 continuación abajo)

### Contexto
Tres sesiones (12, 13, 14 abril) intentando implementar H7 (gatillos como fuente y como destino).
El token limit cortó la sesión 12 a medias; la 13 y 14 continuaron desde resumen comprimido.
El trabajo acumulado no está estable: **los 3 tests de H7 no funcionan en este momento.**

### Qué se implementó (código está en rama, compila)

**Primera parte — botón/cruceta → gatillo (Test 1)**
- Un botón o dirección de cruceta puede mapearse para que active L2 o R2 como trigger analógico (valor 1.0).
- Asignable desde la UI (click o hold 2s con el mando).
- Guardado en JSON como `"type": "trigger", "target": "l2"/"r2"`.

**Segunda parte — gatillo físico → acción (Tests 2 y 3)**
- `ControllerConfig.h`: `TriggerRange` struct + campos `triggerLHasAction`/`triggerRHasAction`, `triggerLAction`/`triggerRAction`, `triggerLRanges`/`triggerRRanges`.
- `ConfigLoader.cpp`: `parseTrigSide` lambda que parsea `trigger_actions.l2/r2` como simple o rangos.
- `PadEngine.cpp`: `applyTrigAct` lambda + `applyTrigRanges` lambda + vectores de estado `trigLRangePrev` etc. (uint8_t para evitar vector<bool> proxy).
- `AppWindow.h`: `RangeEdit` struct + `m_trigLRangeEdits`, `m_trigRRangeEdits`, modal state fields, `renderRangosModal()`.
- `AppWindow.cpp`: `reloadMappingEdits` carga trigger actions; UI H7 con 5 tipos de acción + botón "Rangos"; `renderRangosModal()` ~350 líneas; `saveMappingEdits` escribe `trigger_actions`.

### Errores de compilación corregidos durante las sesiones
- `std::max` → conflicto con macro Windows.h → reemplazado por ternario
- `vector<bool>` proxy reference → cambiado a `vector<uint8_t>`
- Lambda `-> json` trailing return type → MSVC no lo soporta bien → eliminado
- `static constexpr` con tipo local → cambiado a `static const`

### Bug identificado al final (sesión 14)
`actToJson` en `saveMappingEdits` serializa `VirtualButton` como:
```json
{ "type": "virtual_button", "name": "a" }
```
pero `parseButtonAction` en ConfigLoader **no tiene caso `"virtual_button"`** — espera `{"virtual": "a"}`.
Resultado: tras hot-reload, `action.name` queda vacío → `applyVirtualBtnByName` no activa nada.

**Fix aplicado (sesión 14):** `actToJson` cambiado a `j["virtual"] = act.name` para VirtualButton.

### Estado al cerrar sesión 14
- Código compila.
- **Test 1** (botón → trigger): **estado desconocido** — funcionaba antes pero no se ha reverificado después de los cambios.
- **Test 2** (gatillo → botón/trigger): **NO funciona** — pendiente verificar tras el fix de actToJson.
- **Test 3** (rangos): **NO funciona** — pendiente verificar.

### Plan para la próxima sesión
Ir paso a paso:
1. Verificar primero que el JSON guardado es correcto (abrir `data/controllers.json` tras guardar).
2. Test 1 aislado.
3. Test 2 aislado.
4. Test 3 (rangos) al final.
No tocar más cosas hasta que los tests anteriores pasen uno a uno.

### Archivos modificados (H7 completo)
- `input/ControllerConfig.h` — `TriggerRange`, campos trigger en `ControllerConfig`
- `config/ConfigLoader.cpp` — `parseTrigSide` en `trigger_actions`
- `AppWindow.h` — `RangeEdit`, campos rangos modal, `renderRangosModal()`
- `AppWindow.cpp` — UI H7 completa, `renderRangosModal()`, save/load trigger_actions
- `PadEngine.cpp` — `applyTrigAct`, `applyTrigRanges`, estado de triggers

---

## 2026/04/14 (continuación) — H7 debug y correcciones en cadena

### Contexto
Sesión de debugging intensivo de H7. Se resolvieron ~8 bugs en cadena.

### Bugs encontrados y corregidos

**1. actToJson serializaba VirtualButton con formato incorrecto**
`{"type":"virtual_button","name":"a"}` → parseButtonAction no tiene ese caso → name quedaba vacío.
Fix: cambiar a `j["virtual"] = act.name` (formato estándar que ya usa el parser).

**2. saveMappingEdits no limpiaba campo `target` al guardar VirtualButton**
Al cambiar un botón de Trigger a VirtualButton, `target:"l2"` quedaba en el JSON.
Fix: añadir `newBtn.erase("target")` en el path VirtualButton.

**3. La cruceta virtual no era seleccionable como destino de gatillo**
El handler de click de ratón y el loop de hardware no tenían caso `"dpad"`.
Fix (3 partes):
- `applyVirtualBtnByName`: acepta `"dpad_up"/"dpad_down"/"dpad_left"/"dpad_right"` además de `"up"/"down"/"left"/"right"`.
- Handler click ratón: añadido caso `virtComp.type == "dpad"` con `dpadDirFromMouse` + `"dpad_" + vdir`.
- Loop hardware: `candStates` incluye ahora los 4 estados de componentes dpad.

**4. TriggerPassthrough R2→R2 zeroeaba el trigger**
`srcTrig = 0.0f` en el mismo-trigger passthrough borraba el valor recién asignado.
Fix: sólo aplicar `srcTrig = 0.0f` en passthrough CRUZADO (source != target).
Reescritura simétrica que también arregla R2→L2 (que antes no funcionaba).

**5. No se podían quitar los rangos de un gatillo (L2 bloqueada)**
`buildTrigSideJson` priorizaba `m_trigLRangeEdits` sobre `m_trigActionEdits`.
Al asignar acción simple mientras había rangos, la acción se ignoraba al guardar.
Fix: `buildTrigSideJson` ahora prioriza la acción simple de `m_trigActionEdits`.

**6. 1 rango en modal = acción directa (comportamiento simplificado)**
Con 1 rango, `buildTrigSideJson` guarda como acción simple (no como array `ranges`).
1 rango sin acción → null → passthrough analógico.

**7. Passthrough cruzado aplicaba trigger_actions del destino**
R2→L2 passthrough: el valor de R2 se ponía en `state.triggerL`, luego `applyTrigRanges` del L2 físico lo procesaba (activando A/B aunque R2 no debería saber nada de eso).
Fix: flags `trigLWasCrossTarget`/`trigRWasCrossTarget` en PadEngine; `applyTrigRanges` se salta si el trigger fue destino de passthrough cruzado.

### Pendiente al cierre
- **Bug activo**: guardar combinación de teclado en un rango guarda ratón en vez de teclado.
  Causa probable: en `renderRangosModal`, el `actToJson` / la lógica de acción del rango no distingue bien Keyboard vs MouseClick al serializar. Pendiente investigar.

---

## 2026/04/15 — H7 rangos: 3 bugs de UI/estado corregidos, H7 cerrado

### Bugs encontrados y corregidos

**1. Keyboard en rango guardaba mouse_click**
Causa real: al cerrar el modal con `m_h5ActionType == Mouse` activo, los botones de ratón del panel H7 de fondo quedaban visibles y recibían el click del "Aceptar" (el modal no bloquea `ImGui::IsMouseClicked`), escribiendo una acción `MouseClick` en `m_trigActionEdits` que tenía prioridad sobre los rangos en `buildTrigSideJson`.
Fix (3 partes):
- `TriggerRange` añadido campo `hasAction = false` en `ControllerConfig.h`.
- `ConfigLoader.cpp`: `parseTrigSide` sólo pone `tr.hasAction = true` cuando el JSON tiene campo `"action"`.
- `AppWindow.cpp` "Aceptar" en `renderRangosModal`: añadido `m_selTriggerSrc.clear()`, `m_h5ActionType = Xbox`, `m_h5CaptureKeys.clear()`, `m_h5MacroSel.clear()` → el panel H7 desaparece al cerrar el modal, foco vuelve al pad físico.
- `PadEngine.cpp`: `applyTrigRanges` salta rangos con `!r.hasAction`.

**2. Al abrir modal de rangos, el panel H7 de fondo capturaba teclas simultáneamente**
Causa: `ImGui::IsKeyPressed` es global (no respeta modales). Si `m_h5ActionType == Keyboard` al abrir el modal, el bucle de captura del panel H7 acumulaba las mismas teclas que `m_rangosCaptureKeys` del modal.
Fix: al pulsar "Rangos##h7t4", resetear `m_h5ActionType = Xbox`, `m_h5CaptureKeys.clear()`, `m_h5MacroSel.clear()` → el panel H7 vuelve a estado neutro mientras el modal está activo.

**3. Clic en "Asignar" dentro del modal movía el foco al pad físico esporádicamente**
Causa: `mouseClicked = ImGui::IsMouseClicked(0)` (línea 1237) es global. Al hacer clic en cualquier botón del modal, `mouseClicked = true` en el frame principal. El bloque "Gestión de clicks" ejecutaba `m_virtualPadView.hitTest(mouse, ...)` con la posición del ratón (encima del modal), que a veces coincidía con un componente del pad virtual de fondo → `m_selTriggerSrc.clear()`.
Fix: `if (mouseClicked && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))` — se omite toda la gestión de clicks mientras cualquier modal esté abierto.

### Estado al cerrar
- **H7 completado** — todos los tipos de asignación de gatillo funcionan: botón/dpad→trigger, gatillo→botón/dpad/passthrough/macro/teclado/ratón, rangos con cualquier tipo de acción.
- Siguiente: H6 UI (asignación de direcciones de stick analógico) o H8 (touchpad/giroscopio).

---

## 2026/04/15 (continuación) — Refactoring plan + S0

### Sesión de análisis
Análisis OOP de AppWindow.cpp (3089 líneas, 60% mapping editor). Definido plan de refactoring en 6 sesiones documentado en SESSION_CONTEXT.md. Patrones identificados: MVC (MappingModel + MappingSelection + MappingView), Chain of Responsibility (hit-testing de componentes), Adapter (TriggerRange/DpadDirection como IButtonSource).

### S0 — Eliminación de subtabs Ver/Mapear en Pads ✅
**Cambios**:
- `AppWindow.h`: añadido `bool m_mappingActive = false`
- `renderPadsTab()`: eliminados `BeginTabBar`/`BeginTabItem`. Modo normal muestra pad físico + virtual + marquee + botón "Mapear". Modo mapping oculta todo lo anterior y muestra solo el editor.
- Botones Guardar/Cancelar: renombrado "Guardar mapping"→"Guardar", ambos añaden `m_mappingActive = false`

Sin cambios funcionales. Comportamiento: pulsar "Mapear" oculta la vista y muestra el editor; Guardar/Cancelar cierran el editor y devuelven la vista.

---

## 2026/04/16 — Refactoring S1-S4: extracción completa del mapping editor

### S1 — ActionPanel ✅
`ui/ActionPanel.h/.cpp` con 3 componentes reutilizables:
- `renderKeyboardCapture`, `renderMacroCombo`, `renderMouseButtons`
- `imguiKeyToKeyName` movido desde AppWindow.cpp
- 9 bloques duplicados reemplazados (~200 líneas eliminadas de AppWindow.cpp)
- Fix include path: `"imgui.h"` → `"../imgui/imgui.h"` en los archivos del subdirectorio `ui/`

### S2 — MappingModel ✅
`ui/MappingModel.h/.cpp` con todos los mapas de edits pendientes:
- `buttonEdits`, `h5ActionEdits`, `h6AxisEdits`, `axisActionEdits`, `trigActionEdits`, `trigLRangeEdits`, `trigRRangeEdits`
- `RangeEdit` struct movido aquí desde AppWindow.h
- `reload(configs)`, `save(path)`, `clear()` implementados
- AppWindow queda con `MappingModel m_mappingModel` (delegación simple)

### S3 — MappingSelection ✅
`ui/MappingSelection.h` (solo header, struct puro):
- 19 campos de estado de selección e interacción (physComp, stickDir, dpadDir, triggerSrc, captureKeys, h9HoldTimer, etc.)
- `H5ActionType` enum movido aquí a scope global
- `clear()` implementado inline

### S4 — MappingEditor ✅
`ui/MappingEditor.h/.cpp` + `ui/MappingHelpers.h`:

**MappingHelpers.h**: funciones helper estáticas de AppWindow.cpp convertidas a `inline` (readStickXY, isStateActive, shortToState, stateToShort, dpadDirToState, dpadDirFromMouse, xboxBtnLabel, findCompByState, activateState). ODR safety: kBtnNames como static local en cada función inline.

**MappingEditor**: clase autocontenida que posee MappingModel + MappingSelection + estado del modal de rangos + caché de macros + textura de flecha.
- `init(device, engine, layouts, acceptedXbox, threshold, holdMs)`
- `setConfigs(configs)` — actualizable desde AppWindow tras guardar
- `render(phys, virt)` — lógica completa H5-H9 + pads + click handling + Guardar/Cancelar
- `pollConfigsSaved()` — patrón idéntico a LayoutEditor
- `unload()` — libera textura D3D11

**AppWindow** queda con:
- `renderPadsTab()` delega a `m_mappingEditor.render()` / `isActive()` / `activate()`
- `pollConfigsSaved()` recarga `m_controllerConfigs` y llama `setConfigs()`
- `MappingEditor m_mappingEditor` + `PadTexture m_arrowRightTex` (vista normal)

**Resultado**: AppWindow.cpp 3089 → 1080 líneas (−65%). AppWindow.h 137 → 113 líneas.

### Pendiente
- S5: extraer RangosModal de MappingEditor (~370 líneas)

---

## 2026/04/16 (continuación) — Refactoring S5-S6: RangosModal + click handler chain

### S5 — RangosModal ✅
`ui/RangosModal.h/.cpp` extraído de `MappingEditor::renderRangosModal()`:
- Interface: `open(trigger, ranges)`, `render()` → bool, `result()`, `forTrigger()`, `isOpen()`
- Estado propio: `m_work`, `m_selSect`, `m_actType`, `m_captureKeys`, `m_macroSel`, `m_xboxSel`
- Caché de macros propia (lazy-load independiente de la de H5/H7)
- Cuando `render()` devuelve `true`, MappingEditor aplica `result()` al modelo
- Bug en el include: `RangeEdit` está en `MappingModel.h`, no en `ControllerConfig.h` → corregido

MappingEditor pierde 8 campos `m_rangos*` y `renderRangosModal()`. Queda con `RangosModal m_rangos`.

### S6 — Click handler chain ✅
Hit-testing extraído de `render()` en 8 métodos privados de MappingEditor:
- `handleClick(phys, virt, mouse)` — dispatcher: arrow → physHit → virtHit con phys/trigger seleccionado
- `onArrowHit` — flecha de stick
- `onPhysButtonHit` — botón físico (incluye gatillo como source)
- `onPhysStickHit` — cuerpo de stick
- `onPhysDpadHit` — dpad
- `onVirtHitPhysButton` — virtual cuando botón/stick-btn/dpad seleccionado (H5)
- `onVirtHitPhysStick` — virtual cuando eje stick seleccionado (H6)
- `onVirtHitTriggerSrc` — virtual cuando gatillo como fuente (H7)

`render()` pasa de ~790 a ~560 líneas. Sin cambios funcionales.

### Refactoring completo — estadísticas finales
- AppWindow.cpp: 3089 → ~1080 líneas (−65%)
- MappingEditor.cpp: creado, ~1050 líneas (contiene lógica H5-H9)
- Ficheros nuevos: ActionPanel, MappingModel, MappingSelection, MappingHelpers, MappingEditor, RangosModal

---

## 2026/04/17 — H6 Tarea 2: dpad → slot

`dpad_remap` con valor que es `isStickSlotDir` → va a `stickSlots` en ConfigLoader.
`MappingModel::reload()` reconstruye `buttonEdits["dpad_up"] = "left_y_pos"` desde `stickSlots`.
`MappingEditor`: click en flecha del stick virtual con dpad seleccionado llama `onVirtHitPhysButton` → slot asignado.
Supresión ya estaba en `applyStickSlots`.

---

## 2026/04/18 — H6 Tarea 3: gatillo → slot + modal de rangos con analógico

### H6 Tarea 3 ✅
**Problema**: `MappingModel::reload()` reconstruía dpad→slot desde `stickSlots` pero no trigger→slot.
**Fix**: añadido bloque simétrico al de dpad en `reload()` — detecta `src == "l2" || src == "r2"` y reconstruye `trigActionEdits[src]` como `VirtualButton(slotDir)`.
Save ya funcionaba: `buildTrigSideJson` → `actToJson` escribe `"virtual": "left_y_pos"` → ConfigLoader lo detecta con `deriveTrigSlot`.
UI ya funcionaba: `onVirtHitTriggerSrc` inclina stick virtual con gatillo seleccionado.
Motor ya funcionaba: `stickSlotSourceValue` en `StickSlotsHelper.h` tenía `l2`/`r2` desde antes.

### Modal de rangos: direcciones analógico ✅
**UI**: añadidas 8 opciones al combo "Mando" (antes "Xbox") del `TriggerRangeModal`:
L Arriba/Abajo/Derecha/Izquierda, R Arriba/Abajo/Derecha/Izquierda → nombres internos `left_y_pos`, etc.
Restauración de `m_xboxSel` al abrir sección ya asignada a slot.
**Motor**: `applyVirtualBtnByName` en `PadEngine.cpp` extendida con 8 casos de slot → asignación directa en `state.leftX/Y/rightX/Y = ±1.0f`. Estado se reconstruye cada frame, reset automático al salir del rango.

### Otros
- `controllers_template_es.json` y `controllers_template_en.json` creados en `data/` como referencia de estructura JSON.
- SESSION_CONTEXT actualizado con matriz de remapeo completa como contrato del sistema.

---

## 2026/04/21 — Diseño del Component System

### Sesión de diseño (sin tocar código)

Sesión dedicada a diseñar el sistema de componentes físicos→virtuales que reemplazará
los loops + switches centrales de `HIDInputSource::read()` y `EightBitDoInputSource::read()`.

### Decisiones tomadas

**Patrón**: `std::variant` (conjunto cerrado de tipos, sin heap, sin vtable).
`GamepadState` sigue plana — requerimiento ViGEm inamovible.

**ComponentId**: enum con `_Count` al final para dimensionar el array.
**Mapa interno**: `std::array<std::optional<PhysicalComponent>, kComponentCount>` — acceso O(1), sin hash de strings en hot path.

**VirtualTarget**: descriptor (no objeto) de qué escribir en `GamepadState`.
Tipos: VirtualButton, VirtualDpadDir, VirtualTrigger, VirtualStickSlot, VirtualKeyboard,
VirtualMacro, VirtualMouseClick, VirtualMouseMove, VirtualBot, VirtualPassthrough.
Regla analógico/binario emerge del tipo del target, no de un flag.

**RangedHalfAxis**: struct compartido por PhysicalTrigger, PhysicalAnalogDir y PhysicalGyro.
Lista vacía = passthrough proporcional implícito.

**StickAccumulator**: los 4 semiejes de un stick coordinan antes de escribir en GamepadState.
Normaliza si magnitud > 1. Elimina el bug de "golpes" en analógicos detectado esta sesión.

**GyroAccumulator**: igual que StickAccumulator pero 3 ejes independientes (clamp, sin normalización).

**PhysicalController**: contiene el array de componentes, playerPosition (1..N, LEDs del mando),
VibrationConfig y LEDConfig (PS4). `process()` itera el array, llama `std::visit` por componente,
luego hace flush de acumuladores.

### Bug detectado
Analógicos van "a golpes" in-game desde H6. Causa probable: escritura doble de semiejes
sin coordinación. El StickAccumulator del nuevo diseño lo resuelve estructuralmente.

### Ficheros nuevos
- `ARCHITECTURE.md` — diseño completo con todos los tipos y decisiones
- SESSION_CONTEXT.md — plan de migración P1-P6 con ficheros exactos por paso

### Pendiente
Plan de migración P1-P6 en SESSION_CONTEXT.md. No se ha tocado código de producción.

---

## 2026/04/22 — Diseño del sistema de capas de modificador

### Sesión de diseño (sin tocar código)

Definición completa del sistema de **modifier layers**: cualquier componente físico puede actuar
como modificador held, activando un delta de overrides encima del estado activo. Al soltar, vuelve.

### Modelo de tres niveles acordado

```
Base (controllers.json)
  └── + Perfil activo (manual, un slot)   ← mismo mecanismo que perfiles de juego actuales
        └── + Capa de modificador (held)  ← runtime, delta sobre (base + perfil)
```

**Perfil activo**: no hay distinción técnica entre "perfil de jugador" y "perfil de juego".
Son el mismo mecanismo — un conjunto de overrides cargado manualmente. El usuario decide el contenido.

### Decisiones clave

- **Cualquier `ComponentId` puede ser modificador**: botón, dpad, gatillo, dirección de analógico, touchpad, giroscopio. Para fuentes analógicas se usa un threshold para determinar "activo".
- **`ModifierMask` dinámico**: `uint8_t`, el bit `i` corresponde al modificador en posición `i` de `modifierSources`. Hasta 8 modificadores simultáneos (256 combinaciones). Solo existen en el mapa las combinaciones explícitamente definidas.
- **Delta sobre estado combinado**: la capa se aplica sobre (base + perfil activo), no solo sobre la base.
- **Sin exclusividad**: un modificador puede tener además su propio `VirtualTarget`. No es exclusivamente modificador salvo que el usuario lo configure así.
- **Parcial o completo a elección del usuario**: el sistema no distingue — aplica los overrides que encuentra y hereda el resto.

### Impacto en la migración

`PhysicalController` en **P1** ya debe incluir `modifierSources`, `baseLayer` y `modifierLayers`.
Diseño completo en `ARCHITECTURE.md` → sección "Sistema de capas de configuración".
UI queda para después de P1-P6.

### Ficheros actualizados
- `ARCHITECTURE.md` — nueva sección completa con tipos, process() y JSON de ejemplo
- `SESSION_CONTEXT.md` — entrada en backlog con orden de implementación sugerido

---

## 2026/04/24 — Sesión de pruebas post-refactor + dimensiones templates

### Lo que se hizo
- Sesión dedicada a probar los cambios de la sesión anterior (P4/P5 Component System, H6 T4, hot-reload).
- Ajuste de dimensiones de imágenes de template: FRONT 480×200, TOP 480×320.
  Archivos tocados: `PadLayout.h`, `LayoutEditor.cpp`, `data/pad_layouts.json` (4 layouts), `README.md`, `README.es.md`.
- Creado un mando desde cero con el wizard + layout editor: mapeado completo, reasignación, movimiento de templates, todos los tipos de botón/eje. Funcionó correctamente.

### Bugs encontrados (pendientes de análisis)

**[BUG-SCANNER-F310D] Pad Scanner falla con F310 D-mode**
- Scanner da errores con el F310 en D-mode. Versiones anteriores funcionaban.
- No cambia de mando automáticamente — hay que ir a la pestaña Engine, seleccionar el mando activo manualmente, y entonces el scanner funciona.
- Pendiente: revisar qué cambió en la ruta de inicialización del scanner.

**[BUG-GYRO-DS4] Giroscopio DS4 ha dejado de funcionar**
- La burbuja del giroscopio ya no se mueve. Antes funcionaba.
- Hipótesis: posiblemente relacionado con la migración P4 (HIDInputSource → PhysicalController::process()) o con un cambio en el layout del DS4.
- Pendiente análisis con DS4 conectado.

**[DESIGN-F310X-TRIGGER] F310 X-mode: gatillo compartido en layout**
- El F310 X-mode usa trigger_combined (L2 y R2 en el mismo eje, valor neutro = 0.5).
- El layout actual no tiene un componente que represente esto visualmente.
- No es un bug de regresión — el mando siempre funcionó así. Pendiente diseñar representación adecuada.

**[DESIGN-LT-RT-BUTTON] Falta componente LT/RT como botón digital**
- El F310 D-mode manda los gatillos como botones digitales (no analógicos).
- Los layouts solo tienen componentes de gatillo analógico. No hay tipo "botón LT/RT".
- Pendiente: añadir soporte de gatillo-como-botón en el sistema de layouts.

### Estado al cerrar
- Todos los issues son específicos de mandos concretos, no problemas del refactor en general.
- Análisis más detallado previsto para la siguiente sesión.

---

## 2026/04/24 (continuación) — H6-T4-MOUSE: bug de movimiento de ratón corregido

### Qué se hizo

**Bug: axis_actions mouse_move no funcionaba correctamente**

Tres bugs encadenados, descubiertos en orden:

**Bug 1 — Ratón no paraba al soltar el stick (legacy path)**
- `state.mouseX/mouseY` persiste entre frames (declarado fuera del loop en PadEngine).
- El legacy path reseteaba botones/dpad explícitamente pero no `mouseX/mouseY`.
- Fix: añadir `state.mouseX = state.mouseY = 0.0f` al bloque de resets en ambos sources.
- Archivos: `HIDInputSource.cpp`, `EightBitDoInputSource.cpp`.

**Bug 2 — `ha.speed` ignorado en legacy path**
- El código hacía `mouseX += absV` sin multiplicar por `ha.speed`.
- Fix trivial: `mouseX += absV * ha.speed` → luego cambiado a `halfV * ha.speed` por el bug 3.

**Bug 3 (raíz) — Ratón siempre derecha/abajo en Component System path**
- El controlador usa el Component System path (P4/P5 completados), por lo que `applyAxes` nunca se ejecuta.
- `physHalfAxis(RightXNeg)` devuelve la magnitud siempre positiva `[0,1]`.
- `applyVirtualTarget` para `VirtualMouseMove` usaba `value * speed` sin signo → siempre positivo → siempre derecha/abajo.
- Fix: añadir `float dirSign` a `applyVirtualTarget` y `applyRangedHalfAxis` (parámetro con default 1.0f para no romper otros callers). En `PhysicalAnalogDir::process()`, calcular `dirSign = -1.0f` para slots `_Neg` y propagarlo.
- Archivo: `ComponentTypes.cpp`.

**Bug 4 — Eje Y invertido (convención Windows)**
- En Windows, dy positivo = cursor baja. El eje Y del stick tiene `invert: true` en axes, así que "arriba" genera valor positivo → cursor bajaba.
- Fix: negar `my` en PadEngine antes del `SendInput`.
- Archivo: `PadEngine.cpp`.

**Mejoras de UI (MappingEditor.cpp)**
- Al asignar MouseMove, se auto-asigna el semieje opuesto automáticamente (bidireccional con una sola acción).
- Al limpiar, se limpia también el semieje opuesto si era MouseMove.
- Texto informativo "Asigna ambas direcciones del eje" en el panel MouseMove.

### Observaciones
- Mando físico se apagó durante prueba de ratón — identificado como auto-shutoff firmware 8BitDo (no bug nuestro).

### Estado al cerrar
- H6-T4-MOUSE ✅ cerrado.
- H6 T4 pendiente: gatillo virtual analógico, rangos por semieje, analógico→analógico H9.

---

## 2026/04/26 — Investigación bugs DS4 + limpieza

### Trabajo realizado

**Fix: touch y giroscopio no aparecían en pad físico (regresión ddf83bf)**
- Causa raíz: commit `ddf83bf` cambió `m_lastState = state` → `m_lastState = input->getPhysicalState()`.
  `applyTouchpad` y `applyIMU` escribían en `state`, no en `m_physicalState` → PadView nunca los veía.
- Fix: `applyTouchpad` y `applyIMU` ahora también escriben los campos de display en `m_physicalState`.
  (`touchDeltaX/Y` se quedan solo en `state` — son para routing de ratón, no para display.)
- Archivos: `input/HIDInputSource.cpp`.

**Análisis: giroscopio DS4 nunca funcionó por config**
- La sección `imu` nunca estuvo en `controllers.json`. `applyIMU` sale si `!enabled`.
- El commit `ddf83bf` ("DS4 completo") añadió la visualización pero no la config JSON.
- Conclusión: implementación incompleta, no regresión. Requiere que el wizard/scanner detecte
  el offset del giroscopio y lo escriba en controllers.json. Pendiente.

**Análisis: `hid_rz invert: false` — regresión commit dd66a2b**
- El commit `dd66a2b` (fix mouse_move) cambió accidentalmente `hid_rz invert: true → false` en la
  entrada DS4 de `controllers.json`. El eje Y del stick derecho quedó invertido.
- El wizard al re-guardar preserva los campos que no gestiona (merge, no reemplazo).
- Bug del wizard de inversión detectado también en USB (no solo BT).
  No se corrigió — DS4 inoperativo por USB (posible puerto dañado o batería agotada).

**Limpieza: traza botón A**
- Eliminado `spdlog::info("[MAN] Manual A press")` y variable `btnAPrev` de `PadEngine.cpp`.
- Log de diagnóstico de touchpad degradado a `spdlog::debug`.

### Estado al cerrar
- Fix touch display ✅ (pendiente verificar con DS4 USB operativo).
- Giroscopio ⬜ pendiente wizard/scanner.
- `hid_rz invert` ⬜ pendiente — corregir en wizard o en JSON cuando DS4 esté operativo.
- DS4 inoperativo por USB — investigar hardware (batería/puerto).

---

## 2026/04/27 — H6 T4 completo: gatillo analógico, rangos semieje, fix axis range actions

### Hardware DS4
- Puerto micro-USB dañado (pines sin contacto). Batería 70% OK (verificado por BT).
- Intentos de reparación: limpieza IPA, ajuste pines con pinzas cerámica — sin resultado.
- Conclusión: necesita resoldar el conector. Pendiente con soldador cuando haya espacio.

### H6 T4 — Gatillo virtual con recorrido analógico
- `HIDInputSource.cpp` y `EightBitDoInputSource.cpp`: case `Trigger` en `processHalf`/`checkHalf`
  cambia `state.triggerL/R = 1.0f` → `= absV`. Proporcional en lugar de binario.

### H6 T4 — H9 por mando: gatillo como target de semieje
- `MappingEditor.cpp`: nuevo lambda `doAxisTrigAssign` en el bloque doTrigAssign.
  Cuando `stickDir` está activo y se pulsa L2/R2, asigna `HalfAxisAction::Trigger`
  en `axisActionEdits` en lugar de `ButtonAction::Trigger` en `h5ActionEdits`.

### H6 T4 — Rangos por semieje
- `TriggerRangeModal`: `forTrigger()` → `forKey()` (generalizado para cualquier clave).
  Header dinámico muestra la clave activa (antes hardcodeado "L2"/"R2").
- `MappingEditor`: botón "Rangos" añadido como 6ª opción en panel de semieje.
  Colorea activo si hay rangos. Convierte `TriggerRange` ↔ `RangeEdit` al abrir/cerrar.
  Resultado del modal distingue "l2"/"r2" (H7) de clave de eje → guarda en mapa correcto.
- `MappingModel save`: ya manejaba `HalfAxisActionType::Ranges` — sin cambios necesarios.
- Mouse move excluido de rangos (por diseño, no aplica).

### Bug: axis_actions rangos — Teclado/Ratón/Macro no disparaban
- Causa: `m_activeAxisRangeActions` (mapa nuevo) no era consultado por PadEngine.
  El path del Component System (`checkHalf`) seguía usando `m_activeAxisActions.push_back`
  en lugar del nuevo mapa — único path activo para Pro 3, Pro 2, DS4.
- Fix completo:
  - `IInputSource.h`: nuevo virtual `getActiveAxisRangeActions()`.
  - `HIDInputSource`/`EightBitDo`: campo `m_activeAxisRangeActions`, getter, clear por frame,
    populate en Ranges case (ambos paths). Comentario DEPRECATED en legacy path.
  - `PadEngine`: `axisRangePrev` (optional<ButtonAction> por clave), `axisRangeMacros`,
    `axisRangeMacroOk`. Init en `initMacros`. Proceso edge-triggered por frame:
    Teclado press/release, Ratón down/up, Macro start/stop + tick.

### Pendiente para próxima sesión
- Rename `EightBitDoInputSource` → `WinMMInputSource` (h y cpp).
- Flash flecha analógico al asignar dirección (confirmación visual).
- H9: analógico → analógico por mando (stick físico como target desde semieje).
- H8: touchpad y giroscopio en editor de mapping.

---

## 2026/04/28-29 — Bugs críticos: crash de arranque + filtro HID estricto

### Bugs corregidos

**[BUG-VIGEM-HUERFANO] — Crash 0xc0000374 al arrancar tras crashes previos**
- **Síntoma**: la app crasheaba en startup con heap corruption (0xc0000374) después de un crash anterior que no limpiaba el estado de ViGEm.
- **Causa**: `HIDScanner::scan()` abría handle HID y llamaba `HidD_GetPreparsedData` + `HidP_GetCaps` sobre controladores ViGEm virtuales huérfanos (VID=0x5650) dejados por crashes anteriores. Esos dispositivos tenían datos HID malformados → heap corruption.
- **Fix**: `input/HIDScanner.cpp` — saltar VID=0x5650 antes de cualquier llamada HidP. Idempotente.

**[REFACTOR-INWINMM] — Eliminada lógica `inWinMM` del filtro HID**
- **Problema**: `PadEngine.cpp` `monitorFunc()` y `threadFunc()` calculaban si un dispositivo estaba activo en WinMM para decidir si añadirlo como candidato HID. Lógica innecesaria y frágil.
- **Fix**: regla estricta — si `mode != "hid"` en config → nunca se añade como candidato HID. Sin cálculos, sin fallbacks.
- **Arquitectura resultante**: `mode="xinput"` → WinMMInputSource; `mode="hid"` → HIDInputSource. Sin solapamiento.

### Bug identificado (sin fix aún)

**Pro 3 X-mode — dos entradas WinMM zombie cuando el mando muestra 2 luces**
- XInput usa slots de jugador (0-3). Cuando el Pro 3 muestra 2 luces, está registrado en 2 slots XInput simultáneamente → Windows crea 2 bridges WinMM para el mismo dispositivo físico.
- Ambos bridges son zombie (no entregan datos porque el dispositivo solo tiene 1 stream).
- Workaround: reiniciar el mando hasta que muestre 1 sola luz (1 slot XInput).
- Solución definitiva identificada: usar `XInputGetState()` directamente para `mode="xinput"` en lugar de `joyGetPosEx`.

### Estado al cerrar
- BUG-VIGEM-HUERFANO ✅ fix defensivo aplicado.
- REFACTOR-INWINMM ✅ arquitectura limpia.
- Bug zombie WinMM ⬜ sin fix — workaround disponible.

---

## 2026/04/29-30 — WinMMInputSource rename + XInputInputSource + wizard XInput

### Rename: EightBitDoInputSource → WinMMInputSource
- `git mv` de `.h` y `.cpp`. Clase y referencias internas actualizadas.
- Nombre anterior era confuso — la clase lee cualquier joystick WinMM, no solo 8BitDo.
- Nota: los `.obj` huérfanos del nombre anterior requieren Build → Clean Solution.

### Nueva clase XInputInputSource

Subclase de `WinMMInputSource` para mandos `mode="xinput"`:
- Lee via `XInputGetState(slot)` en lugar de `joyGetPosEx`.
- Construye un `JOYINFOEX` sintético para reutilizar toda la lógica de mapping de `WinMMInputSource`.
- `findActiveSlot()`: escanea slots 0-3, excluye VID=0x5650 (ViGEm) via `joyGetDevCaps`.
- Slot cacheado en `mutable m_xInputSlot` para no re-escanear cada frame.
- `processJoyInfo()` extraído como `protected` en `WinMMInputSource` para reutilizarlo en la subclase.
- PadEngine y Scanner filtran entradas WinMM zombie con `XInputGetState(port)` + skip VID=0x5650.
- Engine tab: muestra slot XInput `[N]` junto al nombre del dispositivo.
- `AppWindow.cpp`: `#include <xinput.h>` + `#pragma comment(lib, "XInput.lib")`.

### Wizard — detección automática XInput

Problema previo: el wizard guardaba `mode="hid"` para Pro 3 X-mode porque `HIDScanner` lo detectaba como HID antes de que el scan WinMM/XInput llegara.

**Fix en `BindingWizard.cpp`**:
1. Pre-scan WinMM antes del scan HID.
2. Lambda `isXInputDevice(vid, pid)`: `true` si un port WinMM responde a `XInputGetState` y no es ViGEm.
3. HID scan salta dispositivos con config existente `mode!="hid"` O que sean XInput.
4. WinMM scan: `c.isXInput = isXInputDevice(...)` → campo en `DetectedController`.
5. Radio buttons D-input/X-input eliminados → modo detectado automáticamente, mostrado como texto.
6. `saveResult()` usa `ctrl.isXInput` para escribir `"xinput"` vs `"dinput"`.

### Commits
- `6e3d295` fix(BUG-WINMM-XSLOT): XInput devices read via XInputGetState instead of WinMM bridge
- `2e9cc1b` refactor: rename EightBitDoInputSource → WinMMInputSource, fix ViGEm slot feedback

### Estado al cerrar
- Rename ✅ — Build → Clean Solution necesario para limpiar .obj huérfanos.
- XInputInputSource ✅ — Pro 3 X-mode lee por XInput, sin entradas zombie.
- Wizard XInput ✅ — detecta automáticamente, no ofrece XInput devices como HID.
- Pads tab frozen para XInput ⬜ — path legacy no actualiza `m_physicalState` (irrelevante en 05/03 tras migrar todo a HID).

---

## 2026/05/01-02 — Scanner/Engine: 7 bugs XInput + decisión migración HID

### Bugs resueltos

1. **Scanner HID depende del Engine** — Fix C: panel HID solo usa estado del Engine si tiene el mismo VID:PID activo.
2. **XInput devices aparecen N veces en scanner/engine** — Fix A: deduplicación por VID:PID en `buildScanDevices` + `monitorFunc` + `threadFunc`.
3. **Scanner WinMM devuelve datos zombie para XInput** — Fix B: leer via `XInputGetState` en vez de `joyGetPosEx` cuando `mode="xinput"`.
4. **joyGetPosEx no funciona para XInput en Windows moderno** — Fix Phase2: scan directo de slots XInput 0-3, independiente del bridge WinMM.
5. **Pro 3 X-mode axes/dpad frozen** — config tenía nombres HID (`hid_x/y/z`) en vez de WinMM (`dwXpos/dwYpos`). Corregido en `controllers.json`. También `trigger_l` → `trigger_combined`.
6. **F310 X-mode trigger L2 pulsado en reposo** — `dwZpos` neutral = 32768 → `trigger_l` = 0.5. Corregido: `trigger_combined` en `controllers.json`.
7. **Scanner muestra 32 botones / 0 ejes para XInput** — caps WinMM incorrectas para multi-slot. Fix: forzar 10 botones y 5 ejes para `mode="xinput"` en display.

### Bug abierto — XInput slots agotados

Causa raíz: Windows reserva slots XInput a nivel de OS, no por dispositivo.
- Slot 0: ViGEm (siempre)
- Slot 1: Pro 3 (luz 1)
- Slot 2: Pro 3 (luz 2) ← Con 2 luces, consume 2 slots simultáneos
- Slot 3: único libre para mandos adicionales

Apagar el Pro 3 NO libera los slots — Windows mantiene la reserva hasta reiniciar o desconectar físicamente el USB.

### Decisión arquitectónica — migrar X-mode a HID

Decisión tomada: migrar Pro 3 X-mode (y F310 X-mode) a `mode="hid"`:
- HID no consume slots XInput → sin problema de sleep ni de multi-slot.
- Pro 3 X-mode ya tiene `_hid_prototype` documentado con los ejes correctos como referencia.
- Implementado en 05/03: cambiar `mode="xinput"` → `"hid"`, restaurar nombres HID, verificar.

### Estado al cerrar
- 7 bugs WinMM/XInput resueltos.
- Decisión de migración a HID tomada. Implementación ejecutada en 05/03.

---

## 2026/05/03 — Migración total a HID: eliminación completa de WinMM/XInput

### Contexto
Sesión de limpieza y refactoring arquitectónico. Objetivo: hacer la app completamente
gestionable sin tocar JSON, y eliminar la dependencia de WinMM/XInput para lectura de
mandos físicos. Todos los mandos físicos pasan a leer por HID.

---

### Revisión de issues — cerrados sin código

**[RENAME] WinMMInputSource** — ya estaba completado en sesión anterior. Solo quedaban
`.obj` huérfanos en el directorio de build. Solución: Build → Clean Solution.

**[DESIGN-LT-RT-BUTTON] F310 D-mode gatillos digitales** — CERRADO como limitación
hardware. El F310 en D-mode no expone LT/RT en HID: todos los bytes del report HID son
idénticos en reposo y al pulsar LT o RT (`00 7C 7F 80 7F 08 00 00 FF` en ambos casos).
El firmware D-mode no los reporta ni como botones ni como ejes. Para LT/RT usar X-mode.
El movimiento que se veía antes era el tail del X-mode al cambiar de modo con el switch.

**[BUG-WIZARD-INVERT]** y **[F5] Eje X no calibra** — BLOQUEADOS por [BUG-DS4-USB].
El conector micro-USB del DS4 está roto. Requieren hardware operativo para reproducir.

---

### Fase 1 — Migración de configs X-mode a HID

Los tres mandos X-mode tenían `mode: "xinput"` con nombres de ejes WinMM (`dwXpos` etc.).
Se migran a `mode: "hid"` con nombres HID estándar, verificando con el scanner.

**Problema descubierto**: con `mode: "xinput"`, el scanner HID detecta el dispositivo
(el handle HID existe) pero muestra "engine not running" porque el engine lo lee via
WinMMInputSource. Al migrar a `mode: "hid"`, el engine y el scanner hablan el mismo
idioma y el panel live funciona.

**F310 X-mode (046D:C21D)**
- `hid_x/y` → left stick, `hid_rx/ry` → right stick, `hid_z` → `trigger_combined`
- `invert: false` en hid_z
- Confirmado: LT → L2, RT → R2, ambos a 0 al pulsar simultáneamente (eje combinado)
- `dpad: "hid_hat"`, botones 1-10 correlativos

**Pro 3 X-mode (2DC8:310B)**
- Mismo layout que F310 X-mode
- `invert: false` en hid_z — el `_hid_prototype` tenía `invert: true` que era incorrecto
  (con true, LT movía R2 y RT movía L2)
- Verificado y corregido: LT → L2, RT → R2

**Pro 2 X-mode (045E:02E0)**
- Mismo layout (mismo fabricante, misma estructura HID)
- Verificado y funcional

**Lección**: todos los XInput controllers del proyecto (8BitDo y Logitech) usan el
mismo layout HID en X-mode: hid_x/y/rx/ry para sticks, hid_z para trigger_combined.

---

### Fase 2 — Eliminación completa del stack WinMM/XInput

**Ficheros eliminados del proyecto:**
- `input/WinMMInputSource.h/.cpp` — lector WinMM para mandos D/X-input via `joyGetPosEx`
- `input/XInputInputSource.h/.cpp` — subclase WinMM que usaba `XInputGetState`
- `PadScanner.h/.cpp` — enumerador WinMM de slots, solo existía para WinMM

**PadEngine** (`PadEngine.h/.cpp`)
- `DeviceCandidate` simplificado: eliminados `Source` enum, `source`, `port`, `axes`, `buttons`
- `scanPorts()` eliminado (enumeraba slots WinMM)
- `monitorFunc()` y scan loop de `threadFunc()`: eliminadas Phase 1 (WinMM) y Phase 2 (XInput)
- Creación de input source: siempre `HIDInputSource`, sin ramificación por mode
- Imports: eliminados `<mmsystem.h>`, `WinMMInputSource.h`, `XInputInputSource.h`

**AppWindow** (`AppWindow.h/.cpp`)
- Eliminado `#include <xinput.h>` y `#pragma comment(lib, "XInput.lib")`
- Eliminados `m_scanDevices`, `m_scanSelected`, `m_lastScanTime`
- `buildScanDevices()` lambda eliminado (scan WinMM + Phase 2 XInput)
- Panel izquierdo scanner: sección "WinMM (N)" eliminada, "HID-only" renombrado a "HID"
- Monitor derecho: sección WinMM eliminada (~160 líneas de UI + lógica XInput gamepad)
- `normalizeAxis()` eliminado (helper solo para ejes WinMM [0..65535])
- Engine tab display: simplificado, siempre muestra "[HID]"

**BindingWizard** (`ui/BindingWizard.h/.cpp`)
- `DetectedController::Source` enum eliminado (solo existía HID/WinMM)
- Campos `port`, `isXInput`, `m_winmmPort`, `m_winmmBaseline`, `m_modeIsXInput` eliminados
- Pre-scan WinMM + lambda `isXInputDevice` eliminados del scan de controladores
- Sección "WinMM devices" del scan eliminada
- `kWinMMAxisNames[]`, `kWinMMAxisValues()` eliminados
- `captureButton/captureAxis/captureDpad/openReader/snapshotBaseline`: eliminadas ramas WinMM
- `saveResult()`: siempre escribe `mode: "hid"` (sin ramificación xinput/dinput)
- Import `<Xinput.h>` eliminado

**Resultado**: 2101 líneas eliminadas, 123 insertadas. Commit `71a7cdc`.

---

### Bug nuevo identificado — [BUG-SCANNER-ENGINE]

Al tener dos mandos HID conectados simultáneamente (p.ej. Pro 3 X-mode + Pro 2 D-mode),
el scanner solo puede mostrar datos en vivo del mando que tiene activo el engine. El otro
mando muestra "Start the engine with this device to monitor its inputs here."

**Causa**: el scanner HID toma prestado el estado del engine para no competir por el
handle HID del dispositivo activo. Si quieres monitorizar Pro 2 D-mode mientras el engine
corre con Pro 3 X-mode, no hay ruta de datos.

**Fix futuro**: el scanner debería abrir su propio handle HID para los dispositivos que
NO están activos en el engine (sin riesgo de competir por el handle).

---

### Pendiente para próxima sesión
- `[BUG-SCANNER-ENGINE]` — Scanner independiente del Engine para mandos no activos
- `[WIZARD-TRIGGER-COMBINED]` — Wizard maneja trigger_combined (hid_z compartido LT/RT)
- `[WIZARD-PADS-REFRESH]` — Pestaña Pads no refresca al terminar el wizard
- Pro 2 X-mode — verificación pendiente (mando se puso zombie al cambiar de modo, se configuró a ciegas con el mismo layout que Pro 3; funciona según el usuario)

---

## 2026/05/04 — Limpieza trazas de diagnóstico

### Trazas diagnóstico HIDInputSource — resuelto ✅
**Problema**: `HIDInputSource::applyAxes()` tenía un bloque que disparaba `spdlog::info` para cada eje configurado durante las primeras 3 lecturas (`m_readCount <= 3`). Con 6-7 ejes, eso eran 18-21 líneas de log cada vez que se pulsaba "Activar" en el Scanner.

**Cambios en `input/HIDInputSource.cpp`**:
- Eliminado el bloque `m_readCount <= 3` (traza de status por eje — la burra)
- Bajado a `spdlog::debug` el hex dump del primer report (`m_readCount == 1`)

Las trazas WinMM/XInput (`[Scanner] WinMM scan / XInput Phase2`, `[Scanner][FixB]`) mencionadas en SESSION_CONTEXT ya no existían — las eliminó la migración HID del 2026/05/03.

Las trazas operacionales (dispositivo abierto, config cargada, macros ON/OFF) se mantienen en `info`.

---

## 2026/05/04 — BUG-SCANNER-ENGINE: Scanner independiente del Engine

### Problema
El Scanner solo podía monitorizar el mando activo en el Engine (tomaba prestado su `GamepadState`). Con un mando distinto seleccionado en el Scanner, mostraba "Engine has a different device active" y no funcionaba.

### Solución: arquitectura en capas

**`HIDDevice`** (nuevo — `input/HIDDevice.h/.cpp`):
- RAII wrapper: `CreateFile`, overlapped event, preparsed data, value caps, button report ID
- `ReadResult { Ok, Timeout, Disconnected }` — en `Disconnected` cierra todos los handles limpio (`closeHandles()`), eliminando el bug de handles colgantes que causaba el crash al desconectar
- `normalizeAxis()` extraída de `HIDInputSource::normalizeHIDAxis()`

**`RawHIDReader`** refactorizado para usar `HIDDevice` internamente:
- Eliminado código I/O duplicado (open/read/close era idéntico a HIDInputSource)
- `read()` recibe `timeoutMs` (default 20 ms; el Scanner usa 0 para no bloquear el render thread)

**`HIDInputSource`** refactorizado para usar `HIDDevice` internamente:
- Constructor: 3 líneas en lugar de ~100 — toda la init de HID va a `HIDDevice`
- `isConnected()` delega a `m_hid.isConnected()`
- `read()` usa `m_hid.read()` + `ReadResult`
- `m_valueCaps`, `m_usagePage`, `m_buttonReportId`, `normalizeHIDAxis()` eliminados del header

**Scanner tab** (`AppWindow`):
- `m_scanDevice` (`unique_ptr<RawHIDReader>`) — handle propio, se abre al seleccionar y se destruye al cambiar selección
- Lee con `timeout=0` en el render thread (no bloquea)
- Muestra: botones raw 1-32, ejes HID (X/Y/Z/Rx/Ry/Rz/Brake/Accel), hat compass
- Detecta desconexión y muestra "Disconnected" — ya no depende del Engine en absoluto

**Trazas de log**: per-frame state dumps `[HID][...] lx=...` bajados a `spdlog::trace` para no ensuciar el nivel `debug`.

### Diagnóstico de sesión
Pro 3 X-mode no aparecía en el Engine: el mando estaba en S-mode (057E:2009 = Switch Pro Controller) sin querer. Al apagar y encender volvió a X-mode (2DC8:310B). Sin relación con el refactor.

### Pendiente
- Limpiar caracteres no-ASCII (`â€"`, `â"€`) en ficheros `.cpp`/`.h` del proyecto.
