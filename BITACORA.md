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
