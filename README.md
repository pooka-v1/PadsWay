# VirtualPad

Lee mandos físicos (WinMM, HID) y los reenvía como un mando Xbox 360 virtual via ViGEm.
Soporta macros, bots y configuración por JSON sin tocar el código.
Interfaz gráfica con Dear ImGui (Win32 + DirectX 11).

---

## Requisitos

### Para ejecutar

| Dependencia | Motivo |
|---|---|
| Windows 10/11 | API requeridas: WinMM, HID, DirectX 11 |
| [ViGEmBus driver](https://github.com/nefarius/ViGEmBus/releases) | Crea el mando Xbox 360 virtual |

### Para compilar

- Visual Studio 2022 (con soporte C++17 y Windows SDK)
- `nlohmann/json` e `imgui/` incluidos en el repositorio

---

## Añadir un mando nuevo — `data/controllers.json`

El campo `"mode"` determina qué API se usa:

| mode | API | Cuándo usarlo |
|---|---|---|
| `"dinput"` | WinMM (`joyGetPosEx`) | Aparece en Tab Scanner → WinMM |
| `"xinput"` | WinMM compat layer | Mando XInput en modo compatibilidad |
| `"hid"` | HID raw (`HidP_*`) | Aparece en Tab Scanner → HID-only |

> **Tip:** abre el Tab Scanner con el mando conectado. Si aparece en **WinMM** usa `"dinput"`. Si solo aparece en **HID-only** usa `"hid"`.

---

## Modo `"dinput"` — WinMM

Ejes: `"dwXpos"`, `"dwYpos"`, `"dwZpos"`, `"dwRpos"`, `"dwUpos"`, `"dwVpos"` → targets `left_x/y`, `right_x/y`, `trigger_l/r`, `trigger_combined`.

## Modo `"hid"` — HID raw

Ejes por HID Usage ID:

| Nombre | Usage | Uso típico |
|---|---|---|
| `"hid_x"` / `"hid_y"` | 0x30 / 0x31 | Stick izquierdo |
| `"hid_z"` / `"hid_rz"` | 0x32 / 0x35 | Stick derecho |
| `"hid_brake"` | 0xC4 | Gatillo L2 analógico |
| `"hid_accel"` | 0xC5 | Gatillo R2 analógico |

D-pad HID: `"dpad": "hid_hat"`

---

## Tipos de acción para botones

```json
"N": "a"
"N": { "type": "trigger", "target": "l2" }
"N": { "type": "macro",   "name": "NombreMacro" }
"N": { "type": "bot",     "name": "LightningBot" }
```

Ver [MACROS.md](MACROS.md) para la sintaxis completa de macros.

---

## Archivos de datos

| Archivo | Descripción |
|---|---|
| `data/controllers.json` | Configuración base de mandos físicos |
| `data/macros.json` | Biblioteca de macros reutilizables |
| `data/virtualpad.json` | VID/PID del mando virtual + nivel de log |
