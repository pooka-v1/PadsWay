# Contexto de sesión — VirtualPad

> Ficheros relacionados:
> - `BITACORA.md` — historial de sesiones (lo que se hizo y por qué)
> - `REFERENCE.md` — detalles técnicos: DS4 bytes, APIs, Wizard, Layouts, Perfiles
> - `IDEAS.md` — backlog de ideas futuras sin fecha

---

## BITACORA
- Última entrada: `## 2026/05/03 — Migración total a HID` → línea 1 (fichero reconstruido — historial anterior perdido por sobreescritura accidental)
- **Actualizar BITACORA siempre antes que SESSION_CONTEXT al cerrar sesión**
- **NUNCA usar Write en BITACORA.md — no está en git, Write destruye el historial. Usar Edit/append.**

### Índice de secciones (para ir directo por línea)
| Línea | Sección |
|---|---|
| 14 | V1→V10 — reconstrucción histórica |
| 161 | 2026/03/19 — HID X-mode, fixes normalización ejes |
| 233 | 2026/03/20 — Sistema de perfiles de juego |
| 271 | 2026/03/20(2) — F310 D-mode + DS4 v2 |
| 368 | 2026/03/21 — Fase C: salida teclado/ratón |
| 413 | 2026/03/22 — Repo GitHub + limpieza |
| 469 | 2026/03/25 — Fase D1: tab Pad operativo |
| 513 | 2026/03/26 — Fase D1/D2: vista frontal + formato botones |
| 580 | 2026/03/26(2) — Calibración D2 + cruceta 4 brazos |
| 623 | 2026/03/27 — Layouts JSON (D4 parcial) |
| 653 | 2026/03/27(2) — Layout Pro 2 + component-based (D4 completo) |
| 719 | 2026/03/31 — Mando virtual + marquesina de eventos |
| 776 | 2026/04/01 — Diseño editor de templates |
| 782 | 2026/04/02 — Editor de Layouts (D5 parcial) |
| 839 | 2026/04/03 — BindingWizard |
| 880 | 2026/04/04 — DS4: fix BT eje X + touchpad completo + fixes wizard |
| 963 | 2026/04/05 — Giroscopio DS4 |
| 1017 | 2026/04/07 — BindingWizard + LayoutEditor: fixes UI |
| 1070 | 2026/04/07(2) — LayoutEditor + BindingWizard: mejoras UI |
| 1094 | 2026/04/08 — Fase H: editor mapping físico→virtual (H1-H3) |
| 1146 | 2026/04/09 — H4 hold-to-select + H5 acciones no-botón + H9 whitelist Xbox |
| 1188 | 2026/04/10 — H5 completo: combos de teclado + mensaje instruccional |
| 1215 | 2026/04/11 — H6 backend + UI flechas sticks |
| 1253 | 2026/04/11-12 — H9 fixes flash/cruceta + H10 dpad Macro/Teclado/Ratón |
| 1298 | 2026/04/12-14 — H7 gatillos: implementación inicial |
| ~1360 | 2026/04/14 (cont.) — H7 debug: 7 bugs corregidos, 1 pendiente |
| ~1404 | 2026/04/15 — H7 rangos: 3 bugs UI/estado corregidos, H7 cerrado |
| ~1447 | 2026/04/16 — Refactoring S1-S4: extracción completa del mapping editor |
| ~1490 | 2026/04/16 (cont.) — S5 RangosModal + S6 click handler chain |
| ~1783 | 2026/04/26 — Bugs DS4: fix touch display, análisis gyro/invert, limpieza traza botón A |

---

## Estado actual (2026/04/16)

**Cadena funcional:** Pro 3 D/X, Pro 2 D/X, F310 D, DS4 → VirtualPad → ViGEm → Steam ✓

**Refactoring S0 ✅ S1 ✅ S2 ✅ S3 ✅ S4 ✅ S5 ✅ S6 ✅ — COMPLETO**
- S5: TriggerRangeModal extraído de MappingEditor (~280 líneas → `ui/TriggerRangeModal.h/.cpp`)
- S6: hit-testing de clicks extraído en 8 métodos privados (`handleClick` + 7 handlers). `render()` −230 líneas.

**H7 — COMPLETO ✓**
- Botón físico → trigger virtual ✓
- Gatillo físico → botón/cruceta/trigger virtual ✓
- Gatillo físico → passthrough (mismo y cruzado) ✓
- Gatillo físico → macro/teclado/ratón ✓
- Rangos: todos los tipos de acción funcionan ✓
- Foco se mueve correctamente al aceptar/cancelar rangos ✓

---

## H7 — resumen de arquitectura implementada

### JSON en controllers.json
```json
"trigger_actions": {
  "l2": { "virtual": "a" },                          // simple: botón A
  "l2": { "type": "trigger_passthrough", "target": "r2" },  // passthrough L2→R2
  "r2": { "ranges": [                                // rangos
    { "from": 0.1, "to": 0.55, "action": { "virtual": "a" } },
    { "from": 0.55, "to": 1.0,  "action": { "virtual": "b" } }
  ]}
}
```

### Reglas de prioridad en save (buildTrigSideJson)
1. `m_trigActionEdits[key]` presente → acción simple (gana sobre rangos)
2. `m_trigLRangeEdits/m_trigRRangeEdits` con 1 rango y acción → acción simple
3. `m_trigLRangeEdits/m_trigRRangeEdits` con 1 rango sin acción → null (passthrough)
4. 2+ rangos → array `ranges`

### Archivos implicados
- `input/ControllerConfig.h` — `TriggerRange`, campos `triggerL*`/`triggerR*`
- `config/ConfigLoader.cpp` — `parseTrigSide` en `trigger_actions`
- `AppWindow.h` — `RangeEdit`, campos modal rangos, `renderRangosModal()`
- `AppWindow.cpp` — UI H7, `renderRangosModal()`, `buildTrigSideJson`
- `PadEngine.cpp` — `applyTrigAct` (passthrough cruzado simétrico), `applyTrigRanges`, flags `trigLWasCrossTarget`/`trigRWasCrossTarget`

---

## Plan de migración — Component System (diseñado 2026/04/21)

Diseño completo en `ARCHITECTURE.md`. Cada paso especifica exactamente qué leer y qué tocar.
**Regla**: al empezar un paso, leer solo los ficheros listados. No explorar nada más.

### P1 — Crear `input/ComponentTypes.h` ⬜
Todos los enums, structs, variant, accumulators y skeleton de PhysicalController.
Sin tocar nada existente. Riesgo: ninguno.
- **Lee**: `ARCHITECTURE.md`
- **Crea**: `input/ComponentTypes.h`

### P2 — ConfigLoader: construir PhysicalController desde JSON ⬜
Nueva función `parsePhysicalController()`. Corre en paralelo con `ControllerConfig`, no lo toca.
- **Lee**: `config/ConfigLoader.h`, `config/ConfigLoader.cpp` (solo sección `parseButtons`), `input/ControllerConfig.h`
- **Toca**: `config/ConfigLoader.h`, `config/ConfigLoader.cpp`, `input/ComponentTypes.h`
- Riesgo: bajo

### P3 — Implementar `process()` por tipo de componente ⬜
Trasladar lógica de `applyButtons` + `applyAxes` a los métodos `process()` de cada tipo.
- **Lee**: `input/HIDInputSource.cpp` (solo métodos `applyButtons` y `applyAxes`)
- **Toca**: `input/ComponentTypes.h` / nuevo `input/ComponentTypes.cpp`
- Riesgo: medio — verificar lógica de rangos y StickAccumulator

### P4 — HIDInputSource delega en PhysicalController::process() ✅ (2026/04/23)
`read()` bifurca: nuevo path si `m_hasPhysicalController`, legacy como fallback.
`buildPhysicalButtons` + `buildPhysicalAxes` → `process()` → `applyAxesResidual`.
`applyTouchpad` y `applyIMU` se llaman en ambos paths.
Bug de ejes "a golpes" resuelto por `StickAccumulator` (coordina semiejes antes de escribir).

### P5 — EightBitDoInputSource misma migración ✅ (2026/04/23)
Mismo patrón que P4 pero adaptado a WinMM (`JOYINFOEX`).
`buildPhysicalButtons/Axes` toman `const JOYINFOEX&`. POV → `m_physicalState` antes de `process()`.
`trigger_combined` manejado en `applyAxesResidual`.

### fix: hot-reload re-inyecta PhysicalController ✅ (2026/04/23)
El bloque `m_configsDirty` ahora también recarga `physCtrls` y llama `setPhysicalController()`.
Sin este fix, el mapper guardaba pero Pads seguía mostrando la asignación anterior.

### P6 — Eliminar campos migrados de ControllerConfig ⬜
Solo cuando P4 + P5 estén verificados en juego. **No urgente** — ControllerConfig sigue siendo
necesaria para PadEngine (detección macro/bot), applyAxesResidual, y el mapping editor (UI).
Requiere migrar esos tres sitios antes de poder eliminar nada.

---

## Plan de desarrollo — pendiente

### Capas de modificador (diseñado 2026/04/22, pendiente de implementar)

Diseño completo en `ARCHITECTURE.md` → sección "Sistema de capas de configuración".

**Resumen**: LP (L5) y RP (R5) actúan como modificadores held. Mientras se mantienen pulsados, el mando usa un delta de overrides encima del estado activo (base + perfil). Al soltar, vuelve. Tres estados: LP, RP, LP+RP.

**Impacto en la migración**: `PhysicalController` en **P1** ya debe incluir `modifierButtons`, `baseLayer` y `modifierLayers`. Ver diseño en ARCHITECTURE.md antes de escribir P1.

**Orden de implementación sugerido**:
1. P1–P6: Component System con estructura de capas ya incluida (datos + process())
2. Post-migración: ConfigLoader parsea `modifier_buttons` y `layers` del JSON
3. Post-migración: UI en MappingEditor para editar capas (selector de capa activa en cabecera)

### Deuda técnica
- **AUDIT** ⬜ — Revisar todos los identificadores del proyecto (campos, métodos, variables, enums). Todo debe estar en inglés. Origen: `m_rangos`, `RangosModal` se colaron en español y pasaron desapercibidos.

### Fase H — Editor mapping físico→virtual
- **H7** ✅ — Completo
- **H6** ✅ — Completo (Tareas 1-3: botón/dpad/gatillo → slot de stick virtual)
- **H6 Tarea 4** 🔄 — Analógico físico como fuente (`axis_actions`) — parcialmente completo, ver estado detallado arriba
- **H8** ⬜ — Touchpad y giroscopio

### Fase E
- **E4** ⬜ — DS4 Bluetooth completo

### Fase F
- **F5** ⬜ — [BUG] Eje X stick izquierdo no calibra en wizard

---

## Plan de refactoring AppWindow (pendiente, no urgente)

**Motivación**: AppWindow.cpp tiene 3089 líneas. El 60% es el mapping editor. Objetivo: clases pequeñas, menos ruido, sesiones de trabajo con menor carga de contexto.

**Análisis OOP**: AppWindow es un God Object. El mapping editor contiene un MVC completo (modelo de edits + estado de selección + render) que no le pertenece. Ya existe precedente: PadView y LayoutEditor son clases independientes; el mapping editor debe serlo también.

**Patrones identificados**:
- **MVC**: MappingModel (edits pendientes) + MappingSelection (estado interacción) + MappingView (render)
- **Chain of Responsibility**: la cadena de hit-testing del pad físico (flecha stick → cuerpo stick → botón → dpad → gatillo)
- **Adapter**: TriggerRange y DpadDirection adaptan fuentes no-botón a semántica de botón (IButtonSource)

### Sesiones de refactoring (en orden estricto)

#### S0 ✅ S1 ✅ — UX prerequisito: eliminar subtabs Ver/Mapear en Pads
**Antes**:
- Pestaña "Ver": pad físico + pad virtual (monitorización en tiempo real)
- Pestaña "Mapear": pad físico + pad virtual + paneles de acción

**Después** (sin subtabs):
- Modo normal: pad físico + pad virtual + botón "Mapear" (funcionamiento idéntico al actual "Ver")
- Modo mapping: pad físico + pad virtual + paneles de acción (igual que el actual "Mapear")
  - "Guardar" → guarda cambios + cierra modo mapping → vuelve a modo normal
  - "Cancelar" → descarta cambios + cierra modo mapping → vuelve a modo normal
  - El contenido visual del modo mapping no cambia, solo desaparecen las subtabs y
    Guardar/Cancelar pasan a cerrar el modo además de su función actual.

**Estructura actual** (`AppWindow.cpp` ~línea 869):
```cpp
void AppWindow::renderPadsTab() {
    // setup layouts físico y virtual ...
    if (ImGui::BeginTabBar("##PadsSubTabs")) {
        if (ImGui::BeginTabItem("Ver")) {
            // pad físico + flecha + pad virtual + marquee
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Mapear")) {
            renderMappingSubtab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}
```
Botones al final de `renderMappingSubtab()` (~línea 2381):
- `"Guardar mapping"` → llama `saveMappingEdits()`
- `"Cancelar"` → resetea selección + llama `reloadMappingEdits()`

**Cambios en código**:
1. `AppWindow.h`: añadir `bool m_mappingActive = false`
2. `renderPadsTab()`: eliminar `BeginTabBar` y ambos `BeginTabItem`.
   - Renderizar siempre el contenido de "Ver" (pad físico + flecha + pad virtual + marquee)
   - Añadir botón `"Mapear"` al final (solo visible si `!m_mappingActive`) → `m_mappingActive = true`
   - Renderizar `renderMappingSubtab()` solo si `m_mappingActive`
3. En `renderMappingSubtab()` (~línea 2381):
   - `"Guardar mapping"` → renombrar a `"Guardar"` → añadir `m_mappingActive = false` tras `saveMappingEdits()`
   - `"Cancelar"` → añadir `m_mappingActive = false` al bloque existente (el reset de selección + `reloadMappingEdits()` no cambian)

**Ficheros afectados**: `AppWindow.h` (1 línea), `AppWindow.cpp` (solo `renderPadsTab` + 2 botones en `renderMappingSubtab`)
**Riesgo**: muy bajo. Sin cambios funcionales.

---

#### S1 ✅ — Extraer ActionPanel (componentes de acción reutilizables)
**Completado 2026/04/15.**
`ui/ActionPanel.h` y `ui/ActionPanel.cpp` creados con:
- `renderKeyboardCapture(contextId, keys, availW, showWhenEmpty=false)`
- `renderMacroCombo(contextId, sel, names, availW)`
- `renderMouseButtons(contextId, result, availW)`
- `imguiKeyToKeyName` movido desde AppWindow.cpp

9 bloques reemplazados (~200 líneas eliminadas): H5 buttons ×3, H7 trigger ×3, rangos modal ×3.
`#include` corregido a `"../imgui/imgui.h"` (patrón del resto del directorio `ui/`).

---

#### S2 — Extraer MappingModel
**Ficheros nuevos**: `ui/MappingModel.h`, `ui/MappingModel.cpp`
Mueve todos los mapas de edits pendientes y la lógica load/save:
- `mappingEdits`, `h5ActionEdits`, `h6AxisEdits`, `axisActionEdits`
- `trigActionEdits`, `trigLRangeEdits`, `trigRRangeEdits`
- `activeVid`, `activePid`
- `reload(cfgs)`, `save(path, cfgs)`, `reset()`

AppWindow pasa a tener `MappingModel m_mappingModel`.
**Reducción estimada**: ~295 líneas + ~10 campos de AppWindow.h. **Riesgo**: bajo.

---

#### S3 — Extraer MappingSelection
**Fichero nuevo**: `ui/MappingSelection.h` (struct, solo header)
Mueve todo el estado de selección e interacción activa:
- `physComp`, `stickDir`, `stickAsButton`, `dpadDir`, `triggerSrc`
- `actionType`, `captureKeys`, `macroSel`
- Estado H9: `h9HoldComp`, `h9HoldTimer`, `h9ErrorTimer`, etc.
- `clear()`, `clearKeepTrigger()`

**Reducción estimada**: ~15 campos de AppWindow.h. **Riesgo**: bajo-medio.

---

#### S4 — Extraer MappingEditor (clase contenedora)
Depende de S1+S2+S3.
**Ficheros nuevos**: `ui/MappingEditor.h`, `ui/MappingEditor.cpp`

```cpp
class MappingEditor {
public:
    void init(const std::vector<PadLayout>&, const std::vector<std::string>& acceptedXbox);
    void reload(const std::vector<ControllerConfig>&);
    void update(const GamepadState& physState, float dt);   // H9 hardware input
    void render(PadView& phys, PadView& virt, ImVec2 physOrigin, ImVec2 virtOrigin);
    void save(const std::string& path, const std::vector<ControllerConfig>&);
    void cancel();
private:
    MappingModel     m_model;
    MappingSelection m_selection;
    // RangosModal: ver S5
};
```

AppWindow.h pierde ~45 campos y 3 métodos. Queda con `MappingEditor m_mappingEditor`.
**Reducción estimada**: AppWindow.cpp pierde ~1840 líneas. AppWindow.h baja de 165 a ~100 líneas. **Riesgo**: medio (refactor mayor; S1-S3 lo preparan).

---

#### S5 ✅ — Extraer TriggerRangeModal dentro de MappingEditor
Depende de S4. Completado 2026/04/16.
**Ficheros**: `ui/TriggerRangeModal.h`, `ui/TriggerRangeModal.cpp`
**Reducción**: ~280 líneas fuera de MappingEditor.cpp.

---

#### S6 — Chain of Responsibility para hit-testing (opcional)
Refactoriza el if/else if de selección de componente físico en handlers encadenados.
Solo abordar si MappingEditor sigue siendo difícil de leer después de S4-S5.

---

### Tabla resumen

| Sesión | Paso | Ficheros nuevos | Reducción AppWindow.cpp |
|---|---|---|---|
| S0 ✅ | UX Pads sin subtabs | — | ~0 líneas (solo reorganiza) |
| S1 ✅ | ActionPanel | `ui/ActionPanel.h/.cpp` | ~200 líneas |
| S2 ✅ | MappingModel | `ui/MappingModel.h/.cpp` | ~240 líneas |
| S3 ✅ | MappingSelection | `ui/MappingSelection.h` | ~19 campos + H5ActionType enum |
| S4 ✅ | MappingEditor | `ui/MappingEditor.h/.cpp` | ~1530 líneas |
| S5 ✅ | TriggerRangeModal | `ui/TriggerRangeModal.h/.cpp` | ~280 líneas (de MappingEditor) |
| S6 ✅ | CoR hit-testing | métodos privados en MappingEditor | ~230 líneas de render() |

---

---

## Estado actual (2026/05/01-02 — sesión Scanner/Engine XInput)

### Bugs resueltos esta sesión
1. **Scanner HID depende del Engine** — Fix C: panel HID solo usa estado del Engine si tiene el mismo VID:PID activo.
2. **XInput devices aparecen N veces en scanner/engine** — Fix A: deduplicación por VID:PID en buildScanDevices + monitorFunc + threadFunc.
3. **Scanner WinMM devuelve datos zombie para XInput** — Fix B: leer via XInputGetState en vez de joyGetPosEx cuando mode="xinput".
4. **joyGetPosEx no funciona para XInput en Windows moderno** — Fix Phase2: scan directo de slots XInput 0-3, independiente de WinMM bridge.
5. **Pro 3 X-mode axes/dpad frozen** — config tenía nombres HID (hid_x/y/z) en vez de WinMM (dwXpos/dwYpos). Corregido en controllers.json igual que Pro 2 X-mode. También trigger_l → trigger_combined.
6. **F310 X-mode trigger L2 pulsado en reposo** — dwZpos neutral = 32768 → trigger_l = 0.5. Corregido: trigger_combined en controllers.json.
7. **Scanner muestra 32 botones / 0 ejes para XInput** — caps WinMM incorrectas para multi-slot. Fix: forzar 10 botones y 5 ejes para mode="xinput" en display.

### Bug abierto — XInput slots agotados con Pro 3 (2 luces)
**Causa raíz identificada**: Windows reserva los slots XInput, no el mando.
- Slot 0: ViGEm (siempre)
- Slot 1: Pro 3 (luz 1)
- Slot 2: Pro 3 (luz 2) ← Pro 3 con 2 luces consume 2 slots simultáneamente
- Slot 3: único slot libre para otros mandos XInput (F310)

Apagar el Pro 3 **no libera los slots** — Windows mantiene la reserva.
Solo reiniciar Windows o desconectar físicamente el mando libera el slot.
Con Pro 3 en 2 luces + ViGEm: solo 1 slot disponible para mandos adicionales.

**Decisión tomada**: migrar Pro 3 X-mode (y F310 X-mode) a **mode="hid"**.
- Apagar el Pro 3 SÍ libera los slots (Windows los libera al desconectarse el USB).
- El problema es que con 2 luces ocupa 2 slots simultáneos → bloquea otros mandos.
- La solución definitiva es leer por HID: sin consumo de slots XInput, sin problema de sleep.
- Pro 3 X-mode ya tiene `_hid_prototype` documentado en controllers.json con los ejes correctos.
- **Pendiente (próxima sesión)**: cambiar mode="xinput" → "hid" en Pro 3 X-mode y F310 X-mode,
  restaurar nombres de ejes HID (hid_x/y/z/rx/ry, dpad hid_hat), verificar funcionamiento.

### Trazas de diagnóstico activas (pendiente eliminar)
- `[Scanner] WinMM scan / XInput Phase2` en `buildScanDevices()` — AppWindow.cpp
- `[Scanner][FixB]` throttled 1s en panel WinMM monitor — AppWindow.cpp

---

## ⚠️ Lección arquitectónica clave — XInput vs HID (2026/05/01)

**XInput está limitado a 4 slots.** ViGEm siempre ocupa 1 slot. Para 4 jugadores (4 ViGEm virtuales),
el bus XInput queda lleno y los mandos físicos en X-mode no tienen dónde registrarse.

**Conclusión**: para escalar a multijugador, los mandos físicos deben leerse por **HID**, no por XInput.
D-mode ya lo hace. X-mode tiene HID accesible pero se forzó a WinMM/XInput en [BUG-HID-XINPUT] —
en retrospectiva, mantener HID habría sido más correcto. Los ejes HID del Pro 3 X-mode están
ya corregidos (hid_x/y/z → dwXpos/dwYpos... en la sesión de hoy), así que volver a mode="hid"
sería viable cuando llegue la fase multijugador.

**Regla de diseño futura**: físicos → HID siempre que sea posible. Virtuales → ViGEm (XInput).

---

## Estado actual (2026/05/03 — migración HID + limpieza issues)

### Completado esta sesión
- **[RENAME]** WinMMInputSource ✅ — solo quedaban .obj huérfanos, limpiar con Build→Clean Solution
- **Layout Editor** confirmado completo (botones, sticks, dpad, gatillos, gyro, touchpad DS4)
- **[DESIGN-LT-RT-BUTTON]** cerrado — F310 D-mode no expone LT/RT en HID (limitación hardware/firmware)
- **Migración HID Fase 1** ✅ — todos los X-mode a `mode: "hid"`:
  - F310 X-mode (C21D): hid_x/y/rx/ry + hid_z→trigger_combined (invert:false)
  - Pro 3 X-mode (310B): mismo layout, hid_z invert:false (el prototipo tenía invert:true erróneo)
  - Pro 2 X-mode (02E0): mismo layout, verificado y funcional
- **[BUG-WINMM-XSLOT]** cerrado — HID no consume slots XInput
- **[BUG-WIZARD-INVERT]** y **[F5]** bloqueados por DS4-USB

### Pendiente inmediato
- **Migración HID Fase 2**: eliminar WinMMInputSource + XInputInputSource del código y del engine
- **[WIZARD-TRIGGER-COMBINED]**: diseño e implementación en BindingWizard
- **[WIZARD-PADS-REFRESH]**: Pads no refresca al terminar el wizard

---

## Estado actual (2026/04/30 — sesión XInput + wizard)

### Arquitectura actualizada
- `EightBitDoInputSource` **renombrado** → `WinMMInputSource` (ficheros + clase). git mv preserva historial.
- Nueva clase `XInputInputSource : public WinMMInputSource`:
  - Lee via `XInputGetState()` en lugar de `joyGetPosEx`
  - Construye `JOYINFOEX` sintético para reutilizar toda la lógica de mapping
  - `findActiveSlot()` escanea slots 0-3, excluye ViGEm (VID=0x5650 via `joyGetDevCaps`)
  - Slot cacheado en `mutable m_xInputSlot`
- `processJoyInfo()` extraído como `protected` en WinMMInputSource para reuso por subclases
- PadEngine y Scanner: filtran entradas WinMM zombie con `XInputGetState(port)` + skip VID=0x5650
- Engine tab: muestra slot XInput `[N]` entre modo y nombre para dispositivos xinput
- AppWindow.cpp: incluye `<xinput.h>` + `#pragma comment(lib, "XInput.lib")`

### Wizard — XInput detection
**Problema**: wizard guardaba mode="hid" para Pro 3 X-mode (HIDScanner lo detectaba como HID).
**Fix implementado en BindingWizard.cpp:**
1. Pre-scan WinMM antes del scan HID
2. Lambda `isXInputDevice(vid,pid)`: true si WinMM port responde a XInputGetState y no es ViGEm
3. HID scan salta dispositivos con config existente mode!="hid" O que sean XInput
4. WinMM scan: `c.isXInput = isXInputDevice(...)` → campo en DetectedController
5. Radio buttons D-input/X-input eliminados → modo detectado automáticamente, mostrado como texto
6. `saveResult()` usa `ctrl.isXInput` para escribir "xinput" vs "dinput"

**Estado pendiente**: verificar que el wizard muestre Pro 3 X-mode como WinMM (no HID).
Si sigue apareciendo como HID → los cambios del wizard no se compilaron → recompilar.

### Pads tab — frozen (pendiente)
- Para XInput, `WinMMInputSource` en path **legacy** no actualiza `m_physicalState`
- `getPhysicalState()` devuelve estado congelado → pad físico no se actualiza
- Fix pendiente: actualizar `m_physicalState` en path legacy, o mapear XINPUT_GAMEPAD → physical state
- El path **component-system** sí actualiza `m_physicalState` (si hay PhysicalController inyectado)

### Bugs corregidos esta sesión
1. **[BUG-WINMM-XSLOT]** ✅ RESUELTO — XInput via XInputGetState, zombie slots filtrados
2. **[RENAME]** ✅ EightBitDoInputSource → WinMMInputSource
3. **[WIZARD-HID-XINPUT]** ✅ Wizard no ofrece XInput devices como HID

### Commits (branch feature/mapping-editor)
- `6e3d295` fix(BUG-WINMM-XSLOT): XInput devices read via XInputGetState...
- `2e9cc1b` refactor: rename EightBitDoInputSource → WinMMInputSource, fix ViGEm slot feedback

---

## Estado actual (2026/04/29 — sesión bugs)

### Bugs corregidos esta sesión
1. **[BUG-VIGEM-HUERFANO]** Crash al arrancar tras crashes previos (heap corruption 0xc0000374).
   - Causa probable: `HIDScanner::scan()` abría y llamaba `HidD_GetPreparsedData`/`HidP_GetCaps`
     sobre ViGEm virtual controllers huérfanos dejados por crashes anteriores → datos malformados → heap corruption.
   - Fix defensivo: `input/HIDScanner.cpp` — saltar VID=0x5650 (ViGEm) antes de las llamadas HidP.

2. **[REFACTOR-INWINMM]** Eliminada lógica `inWinMM` del filtro HID en `PadEngine.cpp` (monitorFunc y threadFunc).
   - Regla arquitectónica: si `mode != "hid"` en config → nunca se añade como candidato HID. Sin cálculos, sin fallbacks.
   - Arquitectura definitiva: `mode="xinput"` → WinMM (EightBitDoInputSource). `mode="hid"` → HIDInputSource. No hay solapamiento.

### Bug abierto: Pro 3 X-mode — dos entradas WinMM zombie
**Causa raíz identificada**: el Pro 3 en X-mode usa protocolo XInput (4 slots de jugador, indicados por las luces del mando).
Cuando el mando muestra **2 luces** (registrado en 2 slots XInput simultáneamente), Windows crea **2 entradas WinMM** para el mismo dispositivo físico — ambas zombie porque el bridge WinMM no entrega datos cuando hay conflicto de slot.
Cuando muestra **1 luz** (1 slot XInput), aparece 1 sola entrada WinMM funcional.
- Steam no tiene el problema porque usa `XInputGetState(slot)` directamente, sin bridge WinMM.
- La solución de fondo es usar la API XInput en lugar de `joyGetPosEx` para dispositivos `mode="xinput"`.
- Workaround provisional: asegurarse de que el mando solo tenga 1 slot asignado antes de arrancar VirtualPad (apagar y encender el mando hasta que muestre 1 sola luz).
- Nota: `mode="hidx"` fue considerado y descartado — si en el futuro se usa HID para X-mode, se introduce este mode para evitar que siga el path de cálculo xinput.

### Pendiente (crash-on-disconnect)
El crash-on-disconnect (heap corruption) sigue sin stack trace. Para debuggear:
- Ejecutar con F5 en Release (genera PDB) → el debugger mostrará el call stack exacto al crashear.

---

## Estado actual (2026/04/27 — sesión tarde)

- **H6 Tarea 1** ✅ — botón físico → slot de stick virtual
- **H6 Tarea 2** ✅ — dpad → slot
- **H6 Tarea 3** ✅ — gatillo físico → slot de stick virtual
- **H6 Tarea 4** ✅ — analógico físico como fuente. **COMPLETA incluyendo H9.**
- **H8** ⬜ — touchpad y giroscopio en editor (requiere DS4 USB)

### H6 T4 — Estado final
| Subtarea | Estado |
|---|---|
| Botón virtual | ✅ |
| Cruceta (dpad) | ✅ |
| Macro | ✅ |
| Teclado | ✅ |
| Clic ratón | ✅ |
| Movimiento ratón | ✅ |
| Gatillo virtual (con recorrido analógico, no binario) | ✅ |
| Rangos por semieje | ✅ |
| Analógico → analógico por ratón | ✅ |
| Analógico → analógico por mando (H9) | ✅ (2026/04/27) |

### Feedback UI analógico (2026/04/27)
- `flashSlotKey` + `slotKeyToArrow` → flecha analógica virtual destella 1s al asignar (cualquier fuente → slot)
- `flashPhysArrowComp/Dir` → flecha analógica física (fuente) destella 1s al asignar (cualquier target)
- `applyVirtShort` lambda → steady-state: muestra flecha virtual y bola cuando hay slot asignado y la fuente está seleccionada
- Bugs clave resueltos: `physShort` vacío para sticks impedía H9 stick-tilt; rising-edge en stick tilt previene asignación inmediata

### H6 T4 — Bugs corregidos esta sesión (2026/04/20)
**MappingEditor.cpp — UI:**
1. `onArrowHit`: no reseteaba `actionType` al seleccionar flecha → clicks en pad virtual ignorados si venías de modo Teclado
2. H9 Paso 2: solo aceptaba botones de `m_acceptedXbox`; cruceta física no podía asignarse como target dpad

**HIDInputSource.cpp — Runtime:**
3. Sección hat switch ejecutaba DESPUÉS de `applyAxes` (que contiene `axis_actions`) y machacaba los bits de dpad seteados por `processHalf` → el dpad virtual nunca se activaba. Fix: usar variables temporales `hatUp/Down/Left/Right`, copiarlas al physical state, y hacer OR al virtual state.
4. `m_physicalState.dpadUp` se copiaba desde `state.dpadUp` ya modificado por `axis_actions` → pad físico reaccionaba incorrectamente. Fix: usar variables hat directamente.
5. Bits dpad no se reseteaban entre frames en `applyButtons` → dpad se quedaba pegado al volver el stick a neutro.
6. El valor raw del eje seguía pasando al stick virtual aunque el semieje tuviera `axis_action` asignada → stick virtual se movía además de la cruceta. Fix: suprimir la mitad del eje afectada en `processHalf` para tipos no-analógicos.

**EightBitDoInputSource.cpp:** mismos fixes 5 y 6.

---

## Matriz de remapeo físico → virtual (contrato del sistema)

| Fuente física | Targets posibles |
|---|---|
| Botón | botón, dirección dpad, gatillo (L2/R2), dirección analógico |
| Dirección dpad | dirección dpad, botón, gatillo (L2/R2), dirección analógico |
| Gatillo (L2/R2) | otro gatillo, botón, dirección dpad, dirección analógico *(H6-3)*, + rangos con cualquiera de los anteriores |
| Dirección analógico | botón, cruceta, gatillo (analógico), teclado, ratón, macro, mov. ratón + rangos con todos excepto mov. ratón |

**Invariante de rangos**: cuando el target dentro de un rango es un gatillo o un analógico, se comporta **binario** (0 o 1, sin valores intermedios).

**Template de referencia**: `data/controllers_template.json` — estructura vacía con ejemplos de todos los tipos de remapeo. Leer esto antes que el controllers.json real.

---

## H6 — Diseño del modelo de stick virtual (sesión 2026/04/17)

### Contexto: la realidad del hardware (squircle)
Los sticks físicos NO tienen salida circular perfecta. El espacio de salida es un "squircle":
- A 45° máximo, el valor de cada eje es ~0.78, no 0.71 (que sería lo matemáticamente puro).
- Con Y=1.0 fijo, mover X ~20° no baja Y inmediatamente porque Y ya está saturado.
- Esto es comportamiento normal de hardware. Los juegos lo esperan y están calibrados para ello.
- **Conclusión: no intentar "corregir" el squircle.** El motor debe trabajar con valores reales.

### Dos features distintas — modelo compartido

| Feature | Dirección | Ejemplo |
|---|---|---|
| **H6** (próximo) | fuente física → slot de stick virtual | Botón B → `left_x+`, L2 → `left_y+` |
| **Futuro** | eje analógico → slot de botón virtual | `left_y+` con umbral → botón A virtual |

**El físico siempre vive independiente.** Los ejes HID siguen siendo ejes HID en `axes`. Lo que cambia es solo qué construye el virtual. Un mando puede tener: analógico físico → botones virtuales A/B/X/Y, y botones físicos A/B/X/Y → analógico virtual. Son capas separadas.

### Modelo de datos: cuatro slots independientes

Cada eje de stick virtual se divide en 4 slots (`x+`, `x-`, `y+`, `y-`). Cada slot:
- Solo sabe qué fuente física lo alimenta y de qué tipo es.
- No sabe nada del slot vecino.
- El motor ensambla los 4 slots y normaliza si la magnitud > 1.

```json
"stick_slots": {
  "left_x_pos": { "source": "button_B",  "type": "digital" },
  "left_x_neg": { "source": "button_A",  "type": "digital" },
  "left_y_pos": { "source": "hid_accel", "type": "analog"  },
  "left_y_neg": { "source": "hid_brake", "type": "analog"  }
}
```

Cuando existe `stick_slots` para un eje, el motor ignora la entrada de `axes` para ese eje.  
Si no existe entrada en `stick_slots`, el eje sigue usando `axes` como siempre (compatibilidad total).

### El motor: ensamblado + normalización

```
1. Lee los 4 slots → vector crudo (X, Y)
2. Si magnitud > 1: normaliza → (X/mag, Y/mag)
3. Envía a ViGEm
```

El comportamiento "circular complement" (un analógico + un botón que se complementan para mantenerse en el borde del círculo) **emerge solo** de la normalización — no hay que programarlo explícitamente:

```
L2 = 0.8  →  Y = 0.8
Botón B pulsado  →  X = 1.0
magnitud = sqrt(0.64 + 1.0) = 1.28  →  normaliza
resultado: X = 0.78, Y = 0.625   ← están en el círculo ✓
```

Para **digital → analog** (botón → slot): umbral simple. El valor es 0 o 1. No importa si el físico daría 0.71 o 0.78 — la normalización del motor hace el trabajo.

Para **analog → analog** (eje/trigger → slot): valor crudo del eje, clamped a [0,1] para semiejes. El squircle es aceptable porque los juegos lo esperan.

### Estado actual del modelo JSON
El `axes` existente cubre el caso normal (stick físico → stick virtual 1:1, rango completo -1 a +1):
```json
"axes": {
  "hid_y":  { "invert": true,  "target": "left_y" },
  "hid_x":  { "invert": false, "target": "left_x" },
  "hid_z":  { "invert": false, "target": "right_x" },
  "hid_rz": { "invert": true,  "target": "right_y" },
  "hid_accel": { "invert": false, "target": "trigger_l" },
  "hid_brake": { "invert": false, "target": "trigger_r" }
}
```

`stick_slots` es el nuevo campo que añade H6. **`axes` no se modifica.**

### Pendiente de diseñar para H6
- Estructura de `MappingModel` para los slots (campos + load/save)
- UI en `MappingEditor`: selección de fuente para cada slot, indicadores de tipo (D/A)
- Lógica de ensamblado en `PadEngine` (leer `stick_slots`, combinar, normalizar)
- Qué HID sources son válidos como fuentes analógicas (solo ejes, no botones)

---

## Arquitectura — archivos clave

```
VirtualPad/
├── VirtualPad.cpp
├── PadEngine.h/.cpp
├── AppWindow.h/.cpp
├── GamepadState.h
├── input/
│   ├── ControllerConfig.h   ← TriggerRange, triggerL*/R* fields
│   ├── HIDInputSource.h/.cpp
│   ├── EightBitDoInputSource.h/.cpp
│   └── ...
├── config/ConfigLoader.h/.cpp
├── macros/, bots/, ui/
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
| 8BitDo Pro 3 D-mode | 2DC8 | 6009 | HID | pro3_ps |
| 8BitDo Pro 3 X-mode | 2DC8 | 310B | HID | pro3_ps |
| 8BitDo Pro 2 D-mode | 2DC8 | 6006 | HID | pro2_ps |
| 8BitDo Pro 2 X-mode | 045E | 02E0 | HID | pro2_ps |
| Logitech F310 D-mode | 046D | C216 | HID | — |
| Logitech F310 X-mode | 046D | C21D | HID | — |
| Sony DualShock 4 v2 | 054C | 09CC | HID | dualshock4 |

**Todos los mandos físicos leen por HID desde 2026/05/03. WinMM/XInput eliminados de configs.**
**Fase 2 pendiente: eliminar WinMMInputSource + XInputInputSource del código.**

---

## Problemas conocidos

| Problema | Estado |
|---|---|
| H7: teclado en rango guarda ratón | ✅ Corregido (2026/04/15) |
| Crash al desconectar mando (heap corruption) | pendiente — necesita F5 Release + PDB para stack trace |
| [BUG-HID-XINPUT] Pro 3 X-mode detectado como HID | ✅ Reemplazado (2026/04/29) por REFACTOR-INWINMM — filtro HID estricto: solo mode="hid" entra por HID |
| [BUG-WINMM-XSLOT] Pro 3 X-mode: 2 entradas WinMM zombie cuando mando muestra 2 luces | ✅ Cerrado (2026/05/03) — migración a HID elimina dependencia de slots XInput |
| [BUG-VIGEM-HUERFANO] Startup crash tras crashes previos (heap corruption) | ✅ Fix defensivo (2026/04/28) — HIDScanner salta VID=0x5650 antes de llamadas HidP |
| 8BitDo Zero 2 (BT) comparte VID/PID 2DC8:6006 con Pro 2 | sin solución |
| Botón A trace: spdlog::info en PadEngine.cpp | ✅ Eliminado (2026/04/26) |
| Loop visual duplicado en EightBitDoInputSource::read() | inofensivo |
| [WIZARD BUG] Eje X stick izquierdo no calibra | ⛔ bloqueado por [BUG-DS4-USB] — hipótesis: bug BT DS4, requiere USB operativo para verificar |
| [WIZARD BUG] Pestaña Pads no refresca al terminar asistente | pendiente |
| [BUG-SCANNER-ENGINE] Scanner HID depende del Engine: solo puede monitorizar en vivo el mando que tiene activo el Engine. Si Engine tiene Pro 3 X-mode, Scanner no puede mostrar Pro 2 D-mode (y viceversa). Causa: Scanner toma prestado el estado del Engine para no competir por el handle HID. Fix: hacer Scanner independiente del Engine para cualquier mando que no esté activo en el Engine. | pendiente diseño |
| [BUG-SCANNER-F310D] Scanner falla con F310 D-mode + no cambia mando automáticamente | pendiente análisis — versiones anteriores funcionaban |
| [BUG-GYRO-DS4] Giroscopio DS4 — sección `imu` nunca en controllers.json | implementación incompleta — requiere wizard/scanner para detectar offset y escribirlo |
| [BUG-TOUCH-DS4] Touch no aparecía en pad físico (regresión ddf83bf) | ✅ Fix en m_physicalState (2026/04/26) — pendiente verificar con DS4 USB operativo |
| [BUG-DS4-HID_RZ] hid_rz invert: false — eje Y stick derecho invertido (regresión dd66a2b) | pendiente — corregir en wizard o directamente en JSON cuando DS4 operativo |
| [BUG-WIZARD-INVERT] Wizard invierte analógicos — afecta BT y USB | ⛔ bloqueado por [BUG-DS4-USB] — requiere DS4 USB operativo para reproducir y verificar |
| [BUG-DS4-USB] DS4 inoperativo por USB — puerto dañado (conector micro-USB), batería 70% OK | pendiente hardware: resoldar conector |
| [H6-T4-MOUSE] axis_actions mouse_move: ratón no para al volver stick a neutro | ✅ Corregido (2026/04/24) |
| [H6-T4-RANGES] axis_actions rangos: Teclado/Ratón/Macro no disparaban — axisRangeActions ignoradas en Component System path | ✅ Corregido (2026/04/27) |
| [RENAME] EightBitDoInputSource → WinMMInputSource (nombre más preciso, no es exclusivo de 8BitDo) | ✅ Completado (2026/04/30) — Build → Clean Solution elimina .obj huérfanos |
| [UI-FLASH-ARROW] Flecha de stick analógico no destella al asignar dirección (a diferencia de botones) | ✅ Corregido (2026/04/27) — flashSlotKey + slotKeyToArrow + flashPhysArrowComp/Dir |
| [H9-ANALOG-ANALOG] H9 por mando: analógico físico → analógico virtual no acepta input de stick como target | ✅ Corregido (2026/04/27) — rising-edge check + fix physShort vacío para sticks |
| [DESIGN-F310X-TRIGGER] F310 X-mode trigger_combined (neutro=0.5) no representable en layout | pendiente diseño |
| [WIZARD-TRIGGER-COMBINED] Mandos con `trigger_combined` (F310 X-mode, Pro 3 X-mode, Pro 2 X-mode): LT y RT comparten un solo eje HID (hid_z, neutro=0.5). El wizard prohíbe asignar el mismo eje dos veces, pero aquí LT y RT DEBEN compartirlo. Wizard necesita: (1) detectar eje ya asignado al otro gatillo, (2) reconocerlo como trigger_combined válido, (3) calibración conjunta en un único paso. | pendiente diseño wizard |
| [DESIGN-LT-RT-BUTTON] F310 D-mode: LT/RT no existen en HID — el firmware D-mode no los expone ni como botones ni como ejes (todos los bytes del report son idénticos en reposo y al pulsar). No hay nada que mapear. Para LT/RT usar X-mode. | ✅ Cerrado (2026/05/03) — limitación hardware/firmware, no resoluble por software |

---

## Nota Debug build
ViGEmClient.lib es Release-only. Para depurar: F5 en Release (genera PDB).

---

## H6 Tarea 4 — Diseño: analógico físico como fuente (`axis_actions`)

### Contexto
El analógico físico (eje HID) actualmente solo puede mapearse 1:1 a otro eje virtual via `axes`.
`axis_actions` permite asignar acciones a cada uno de los 8 semiejes (`left/right_x/y_pos/neg`).
Concepto simétrico e inverso a `stick_slots` (que hacía fuente→slot virtual).

### JSON
```json
"axis_actions": {
  "left_x_pos": { "virtual": "a" },
  "left_x_neg": { "virtual": "dpad_left" },
  "left_y_pos": { "target": "mouse_y", "speed": 15 },
  "left_y_neg": { "ranges": [
    { "from": 0.0, "to": 0.5, "action": { "virtual": "b" } },
    { "from": 0.5, "to": 1.0, "action": { "type": "keyboard", "keys": ["ctrl"] } }
  ]}
}
```

**Acciones soportadas por semieje:**
| Tipo | JSON | Comportamiento |
|---|---|---|
| Botón virtual | `{ "virtual": "a" }` | binario (umbral) |
| Dirección dpad | `{ "virtual": "dpad_up" }` | binario |
| Gatillo virtual | `{ "virtual": "trigger_l" }` | binario |
| Dirección analógico | `{ "virtual": "left_y_pos" }` | binario |
| Teclado | `{ "type": "keyboard", "keys": [...] }` | binario |
| Macro | `{ "type": "macro", "name": "..." }` | binario |
| Clic ratón | `{ "type": "mouse_click", "button": "left" }` | binario |
| Movimiento ratón | `{ "target": "mouse_x/mouse_y", "speed": N }` | **proporcional** (valor_eje × speed) |
| Rangos | `{ "ranges": [...] }` | igual que trigger_actions; mouse_move en rangos no aplica |

**Política proporcional/binario:**
- Fuente analógica + target analógico (mouse_move): proporcional — igual que `axes` existente
- Cualquier otro target: binario — umbral de activación, valor siempre 1

### Tareas de implementación (orden estricto)
1. **T1 — ConfigLoader**: parsear `axis_actions` → struct `AxisSideAction` en `ControllerConfig`
2. **T2 — PadEngine**: aplicar por frame — leer valor semieje, aplicar acción
3. **T3 — MappingModel**: load/save `axis_actions`
4. **T4 — MappingEditor UI**: selección de semieje de stick físico + panel de asignación (reutiliza TriggerRangeModal o modal nuevo)

### Relación con `axes` existente
- `axes` sigue funcionando igual (eje completo → eje virtual o mouse)
- Ambas secciones coexisten en el JSON sin tocarse. Si el mismo eje tiene entrada en `axes` y en `axis_actions`, el motor da prioridad a `axis_actions` para el semieje afectado — el valor de `axes` queda ignorado solo para ese semieje en tiempo de ejecución.
- Prioridad: `axis_actions` gana sobre `axes` para el semieje afectado

---

> **NOTA PARA CLAUDE**: Este fichero es contexto operativo diario.
> - Detalles técnicos → `REFERENCE.md`
> - Ideas futuras → `IDEAS.md`
> - Historial → `BITACORA.md`
> - **Al final de cada sesión**: actualizar estado, pendientes, y añadir entrada a BITACORA.
