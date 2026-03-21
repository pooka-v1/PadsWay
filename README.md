# VirtualPad

Lee mandos físicos (WinMM) y los reenvía como un mando Xbox 360 virtual via ViGEm.
Soporta macros, bots y configuración por JSON sin tocar el código.
Interfaz gráfica con Dear ImGui (Win32 + DirectX 11).

---

## Requisitos

### Para ejecutar

| Dependencia | Motivo |
|---|---|
| Windows 10/11 | API requeridas: WinMM, DirectX 11 |
| [ViGEmBus driver](https://github.com/nefarius/ViGEmBus/releases) | Crea el mando Xbox 360 virtual |

### Para compilar

- Visual Studio 2022 (con soporte C++17 y Windows SDK)
- `nlohmann/json` e `imgui/` incluidos en el repositorio

---

## Añadir un mando nuevo — `data/controllers.json`

Cada entrada describe un mando físico identificado por **VID y PID**.
Usa el **Tab Scanner** de VirtualPad para ver los mandos conectados y sus valores raw.

```json
{
  "vid": "XXXX", "pid": "YYYY",
  "source_name": "Nombre descriptivo",
  "mode": "dinput",
  "buttons": { "1": "a", "2": "b" },
  "axes": { "dwXpos": { "target": "left_x", "invert": false } },
  "dpad": "pov"
}
```

| mode | API | Cuándo usarlo |
|---|---|---|
| `"dinput"` | WinMM (`joyGetPosEx`) | Mando en modo D (DInput) |
| `"xinput"` | WinMM compat layer | Mando en modo X (XInput) |

---

## Tipos de acción para botones

```json
"N": "a"                                      // botón virtual simple
"N": { "type": "trigger", "target": "l2" }   // gatillo digital
"N": { "type": "macro",   "name": "NombreMacro" }
"N": { "type": "bot",     "name": "LightningBot" }
```

Ver [MACROS.md](MACROS.md) para la sintaxis completa de macros.

---

## Arquitectura

| Componente | Rol |
|---|---|
| `PadEngine` | Hilo de fondo: scan → config → macro/bot → ViGEm (tick 8ms) |
| `AppWindow` | Hilo principal: Win32 + D3D11 + ImGui (tabs Engine / Scanner) |
| `PadScanner` | Enumera puertos WinMM y lee valores raw para el Tab Scanner |

---

## Archivos de datos

| Archivo | Descripción |
|---|---|
| `data/controllers.json` | Configuración base de mandos físicos |
| `data/macros.json` | Biblioteca de macros reutilizables |
