# Contexto de sesión — VirtualPad

> Ficheros relacionados:
> - `BITACORA.md` — historial de sesiones (lo que se hizo y por qué)
> - `REFERENCE.md` — detalles técnicos: DS4 bytes, APIs, Wizard, Layouts, Perfiles
> - `IDEAS.md` — backlog de ideas futuras sin fecha

---

## Regla de backups
Antes de editar cualquiera de estos ficheros, crear una copia en `backup/` con la fecha del día:
- `BITACORA.md` → `backup/BITACORA_20260504.md`
- `SESSION_CONTEXT.md` → `backup/SESSION_CONTEXT_20260504.md`
- `IDEAS.md` → `backup/IDEAS_20260504.md`
- `ARCHITECTURE.md` → `backup/ARCHITECTURE_20260504.md`
- `REFERENCE.md` → `backup/REFERENCE_20260504.md`

La carpeta `backup/` está en `.gitignore` — los backups son locales, no van al repo.
Solo crear el backup si no existe ya uno del día actual para ese archivo (no duplicar dentro de la misma sesión).

---

## BITACORA
- Última entrada: `## 2026/05/03 — Migración total a HID`
- **Actualizar BITACORA siempre antes que SESSION_CONTEXT al cerrar sesión**
- **NUNCA usar Write en BITACORA.md — no está en git, Write destruye el historial. Usar Edit/append.**

### Índice de secciones (para ir directo por línea)
| Línea | Sección |
|---|---|
| 8 | 2026/04/23 — P1→P5: Component System completo |
| ~85 | V1→V10 — reconstrucción histórica |
| 481 | 2026/03/22 — Repo GitHub + limpieza |
| 537 | 2026/03/25 — Fase D1: tab Pad operativo |
| 581 | 2026/03/26 — Fase D1/D2: vista frontal |
| 648 | 2026/03/26(2) — Calibración D2 + cruceta 4 brazos |
| 691 | 2026/03/27 — Layouts JSON (D4 parcial) |
| 721 | 2026/03/27(2) — Layout Pro 2 + component-based |
| 787 | 2026/03/31 — Mando virtual + marquesina |
| 844 | 2026/04/01 — Diseño editor templates |
| 850 | 2026/04/02 — Editor de Layouts (D5 parcial) |
| 907 | 2026/04/03 — BindingWizard |
| 948 | 2026/04/04 — DS4: fix BT + touchpad + wizard |
| 1031 | 2026/04/05 — Giroscopio DS4 |
| 1085 | 2026/04/07 — BindingWizard + LayoutEditor: fixes UI |
| 1138 | 2026/04/07(1) — LayoutEditor + BindingWizard: mejoras |
| 1162 | 2026/04/08 — Fase H: H1-H3 |
| 1216 | 2026/04/09 — H4 hold-to-select + H5 + H9 whitelist |
| 1259 | 2026/04/10 — H5 completo: combos teclado |
| 1283 | 2026/04/11 — H6 backend + UI flechas sticks |
| 1323 | 2026/04/11(tarde)+04/12 — H9 fixes + H10 dpad |
| 1368 | 2026/04/12-14 — H7 gatillos: implementación |
| 1428 | 2026/04/14(cont.) — H7 debug: 7 bugs |
| 1474 | 2026/04/15 — H7 rangos: 3 bugs, H7 cerrado |
| 1500 | 2026/04/15(cont.) — Refactoring plan + S0 |
| 1515 | 2026/04/16 — Refactoring S1-S4 |
| 1561 | 2026/04/16(cont.) — S5+S6 |
| 1593 | 2026/04/17 — H6 T2: dpad → slot |
| 1602 | 2026/04/18 — H6 T3: gatillo → slot |
| 1623 | 2026/04/21 — Diseño Component System |
| 1668 | 2026/04/22 — Diseño capas modificador |
| 1706 | 2026/04/24 — Pruebas post-refactor + dimensiones |
| 1742 | 2026/04/24(cont.) — H6-T4-MOUSE bug |
| 1786 | 2026/04/26 — Bugs DS4 + limpieza |
| 1822 | 2026/04/27 — H6 T4 completo |
| 1867 | 2026/04/28-29 — BUG-VIGEM-HUERFANO + REFACTOR-INWINMM |
| 1896 | 2026/04/29-30 — WinMMInputSource rename + XInputInputSource |
| 1939 | 2026/05/01-02 — Scanner/Engine XInput: 7 bugs + decisión HID |
| 1974 | 2026/05/03 — Migración total a HID |

---

## Estado actual (2026/05/04)

**Cadena funcional:** Pro 3 D/X, Pro 2 D/X, F310 D/X, DS4 → VirtualPad → ViGEm → Steam ✓

**Todos los mandos físicos leen por HID.** WinMMInputSource, XInputInputSource y PadScanner eliminados del código (commit `71a7cdc`). Commit `7545d04` añade BITACORA, SESSION_CONTEXT e IDEAS al git.

### Completado (sesiones recientes)
- **H6 T1-T4** ✅ — analógico físico como fuente: todos los tipos de acción, rangos por semieje, H9 analógico→analógico
- **Component System P1-P5** ✅ — PhysicalController::process() en ambos input sources. P6 aplazado.
- **Refactoring S0-S6** ✅ — MappingEditor, TriggerRangeModal, ActionPanel, MappingModel, MappingSelection, CoR hit-testing
- **Migración HID completa** ✅ — todos los X-mode a `mode="hid"`, WinMM/XInput stack eliminado
- **Layout Editor** ✅ — completo (botones, sticks, dpad, gatillos, gyro, touchpad DS4)
- **BUG-SCANNER-ENGINE** ✅ — `HIDDevice` extrae I/O HID; `RawHIDReader` e `HIDInputSource` lo usan; Scanner independiente con handle propio; disconnect cierra limpio

### ⚠️ REGLA CRÍTICA DE CÓDIGO
**NUNCA usar comillas tipográficas `"` `"` (Unicode) en código C++, JSON ni ningún fichero de código.**
Usar SIEMPRE `"` ASCII recto (U+0022). El compilador no reconoce las tipográficas — produce errores.
Violada en 2026/05/04 en edits de AppWindow.cpp. Revisar CADA string literal antes de guardar.

### Pendiente inmediato (próxima sesión)
- **Limpiar caracteres no-ASCII** — `â€"` (em dash), `â"€` (box-drawing) en ficheros `.cpp`/`.h`. Afecta a AppWindow.cpp y posiblemente otros. Usar grep para localizarlos todos antes de editar.
- **[WIZARD-TRIGGER-COMBINED]** — Wizard no soporta mandos con `trigger_combined` (hid_z compartido LT/RT, neutro=0.5): el wizard prohíbe asignar el mismo eje dos veces, pero LT y RT deben compartirlo. Requiere: (1) detectar eje ya asignado al otro gatillo, (2) reconocer como `trigger_combined` válido, (3) calibración conjunta en un único paso.
- **[WIZARD-PADS-REFRESH]** — Pestaña Pads no refresca al terminar el wizard.
- ~~**[BUG-SCANNER-ENGINE]**~~ ✅ — Scanner usa `RawHIDReader` propio, completamente independiente del Engine. `HIDDevice` extrae lógica I/O; cierra handles limpio en desconexión (2026/05/04)
- ~~**Trazas de diagnóstico activas**~~ ✅ — bloque `m_readCount <= 3` eliminado; first-report bajado a debug (2026/05/04)

---

## Plan de migración — Component System

### P6 — Eliminar campos migrados de ControllerConfig ⬜ (no urgente)
`ControllerConfig` sigue siendo necesaria para tres sitios:
- `PadEngine`: detección macro/bot
- `applyAxesResidual`: lee `m_config.axes`
- Mapping editor UI: lee y escribe la config

Hay que migrar los tres sitios antes de poder eliminar nada. Aplazado hasta que los tres estén listos.

---

## Plan de desarrollo — pendiente

### Capas de modificador (diseñado 2026/04/22, pendiente implementar)

Diseño completo en `ARCHITECTURE.md` → sección "Sistema de capas de configuración".

**Resumen**: LP (L5) y RP (R5) actúan como modificadores held. Mientras se mantienen pulsados, el mando usa un delta de overrides encima del estado activo (base + perfil). Al soltar, vuelve. Tres estados: LP, RP, LP+RP.

**Orden de implementación sugerido**:
1. P6: eliminar ControllerConfig campos migrados
2. Post-P6: ConfigLoader parsea `modifier_buttons` y `layers` del JSON
3. Post-P6: UI en MappingEditor para editar capas (selector de capa activa en cabecera)

### Deuda técnica
- **AUDIT** ⬜ — Revisar todos los identificadores del proyecto: todo en inglés. Origen: `m_rangos`, `RangosModal` se colaron en español sin detectarse.

### Fase H — Editor mapping
- **H7** ✅ — Completo
- **H6** ✅ — Completo (T1-T4 incluido H9 analógico→analógico)
- **H8** ⬜ — Touchpad y giroscopio en editor de mapping (requiere DS4 USB operativo para completar)

### Fase E
- **E4** ⬜ — DS4 Bluetooth completo

### Fase F
- **F5** ⬜ — [BUG] Eje X stick izquierdo no calibra en wizard. ⛔ Bloqueado por [BUG-DS4-USB].

---

## ⚠️ Lección arquitectónica clave — XInput vs HID (2026/05/01)

**XInput está limitado a 4 slots.** ViGEm siempre ocupa 1 slot. Para 4 jugadores (4 ViGEm virtuales),
el bus XInput queda lleno y los mandos físicos en X-mode no tienen dónde registrarse.

**Conclusión**: para escalar a multijugador, los mandos físicos deben leerse por **HID**, no por XInput.
D-mode ya lo hacía. X-mode ahora también (migración 05/03). Problema resuelto en origen.

**Regla de diseño futura**: físicos → HID siempre que sea posible. Virtuales → ViGEm (XInput).

---

## Matriz de remapeo físico → virtual (contrato del sistema)

| Fuente física | Targets posibles |
|---|---|
| Botón | botón, dirección dpad, gatillo (L2/R2), dirección analógico |
| Dirección dpad | dirección dpad, botón, gatillo (L2/R2), dirección analógico |
| Gatillo (L2/R2) | otro gatillo, botón, dirección dpad, dirección analógico *(H6-3)*, + rangos con cualquiera |
| Dirección analógico | botón, cruceta, gatillo (analógico), teclado, ratón, macro, mov. ratón + rangos con todos excepto mov. ratón |

**Invariante de rangos**: cuando el target dentro de un rango es un gatillo o un analógico, se comporta **binario** (0 o 1, sin valores intermedios).

**Template de referencia**: `data/controllers_template.json` — estructura vacía con ejemplos de todos los tipos de remapeo. Leer esto antes que el controllers.json real.

---

## Arquitectura — archivos clave

```
VirtualPad/
├── VirtualPad.cpp
├── PadEngine.h/.cpp
├── AppWindow.h/.cpp
├── GamepadState.h
├── input/
│   ├── ControllerConfig.h      ← TriggerRange, triggerL*/R* fields
│   ├── ComponentTypes.h/.cpp   ← PhysicalController, process(), enums, accumulators
│   ├── HIDInputSource.h/.cpp   ← único lector físico activo
│   └── HIDScanner.h/.cpp
├── config/ConfigLoader.h/.cpp
├── macros/, bots/
└── ui/
│   ├── MappingEditor.h/.cpp
│   ├── MappingModel.h/.cpp
│   ├── MappingSelection.h
│   ├── TriggerRangeModal.h/.cpp
│   ├── ActionPanel.h/.cpp
│   ├── LayoutEditor.h/.cpp
│   ├── PadView.h/.cpp
│   └── BindingWizard.h/.cpp
└── data/
    ├── controllers.json
    ├── pad_layouts.json
    ├── state_map.json
    ├── macros.json
    └── virtualpad.json
```

---

## Controllers configurados

| Config | VID | PID | API | layout_id |
|---|---|---|---|---|
| 8BitDo Pro 3 D-mode | 2DC8 | 6009 | HID | 8BitDo-Pro3 |
| 8BitDo Pro 3 X-mode | 2DC8 | 310B | HID | 8BitDo-Pro3 |
| 8BitDo Pro 2 D-mode | 2DC8 | 6006 | HID | 8BitDo-Pro2 |
| 8BitDo Pro 2 X-mode | 045E | 02E0 | HID | 8BitDo-Pro2 |
| Logitech F310 D-mode | 046D | C216 | HID | Logitech-F310 |
| Logitech F310 X-mode | 046D | C21D | HID | Logitech-F310 |
| Sony DualShock 4 v2 | 054C | 09CC | HID | dualshock4 |

**Todos los mandos físicos leen por HID desde 2026/05/03.**

---

## Problemas conocidos

| Problema | Estado |
|---|---|
| Crash al desconectar mando (heap corruption) | pendiente — necesita F5 Release + PDB para stack trace |
| [WIZARD BUG F5] Eje X stick izquierdo no calibra | ⛔ bloqueado por [BUG-DS4-USB] |
| [WIZARD-PADS-REFRESH] Pestaña Pads no refresca al terminar asistente | pendiente |
| [WIZARD-TRIGGER-COMBINED] Mandos con trigger_combined (hid_z compartido LT/RT) — wizard no soporta asignar el mismo eje a LT y RT | pendiente diseño wizard |
| [BUG-SCANNER-ENGINE] Scanner HID solo puede monitorizar el mando activo en el Engine | pendiente diseño |
| [BUG-SCANNER-F310D] Scanner falla con F310 D-mode + no cambia mando automáticamente | pendiente análisis |
| [BUG-GYRO-DS4] Giroscopio DS4 — sección `imu` nunca en controllers.json, implementación incompleta | requiere wizard/scanner para detectar offset y escribirlo |
| [BUG-TOUCH-DS4] Touch no aparecía en pad físico — fix aplicado en m_physicalState (2026/04/26) | pendiente verificar con DS4 USB operativo |
| [BUG-DS4-HID_RZ] hid_rz invert: false — eje Y stick derecho invertido (regresión dd66a2b) | pendiente — corregir en wizard o JSON cuando DS4 operativo |
| [BUG-WIZARD-INVERT] Wizard invierte analógicos — afecta BT y USB | ⛔ bloqueado por [BUG-DS4-USB] |
| [BUG-DS4-USB] DS4 inoperativo por USB — conector micro-USB dañado (pines sin contacto), batería 70% OK | pendiente hardware: resoldar conector |
| [DESIGN-F310X-TRIGGER] F310 X-mode trigger_combined (neutro=0.5) sin representación en layout | pendiente diseño |
| 8BitDo Zero 2 (BT) comparte VID/PID 2DC8:6006 con Pro 2 | sin solución conocida |

---

## Nota Debug build
ViGEmClient.lib es Release-only. Para depurar: F5 en Release (genera PDB).

---

> **NOTA PARA CLAUDE**: Este fichero es contexto operativo diario.
> - Detalles técnicos → `REFERENCE.md`
> - Ideas futuras → `IDEAS.md`
> - Historial → `BITACORA.md`
> - **Al final de cada sesión**: actualizar estado, pendientes, y añadir entrada a BITACORA.
