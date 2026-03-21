# VirtualPad

Lee mandos físicos (WinMM, HID, XInput) y los reenvía como un mando Xbox 360 virtual via ViGEm.
Soporta macros, bots y configuración por JSON sin tocar el código.
Interfaz gráfica con Dear ImGui (Win32 + DirectX 11).

---

## Requisitos

### Para ejecutar

| Dependencia | Motivo |
|---|---|
| Windows 10/11 | API requeridas: WinMM, HID, DirectX 11 |
| [ViGEmBus driver](https://github.com/nefarius/ViGEmBus/releases) | Crea el mando Xbox 360 virtual |
| [HidHide driver](https://github.com/nefarius/HidHide/releases) | Oculta el mando físico a los juegos para evitar doble input |

> **ViGEmBus** y **HidHide** son del mismo autor (Nefarius). VirtualPad los controlará automáticamente.

### Para compilar

- Visual Studio 2022 (con soporte C++17 y Windows SDK)
- `nlohmann/json`, `imgui/`, `spdlog/` incluidos en el repositorio

---

## Añadir un mando nuevo — `data/controllers.json`

| mode | API | Cuándo usarlo |
|---|---|---|
| `"dinput"` | WinMM | Tab Scanner → WinMM |
| `"xinput"` | WinMM compat | Mando XInput en modo compatibilidad |
| `"hid"` | HID raw | Tab Scanner → HID-only |

### Ejes WinMM: `"dwXpos"`, `"dwYpos"`, `"dwZpos"`, `"dwRpos"`, `"dwUpos"`, `"dwVpos"`
### Ejes HID: `"hid_x/y/z/rz"`, `"hid_brake"` (0xC4), `"hid_accel"` (0xC5)

---

## Tipos de acción para botones

```json
"N": "a"
"N": { "type": "trigger",    "target": "l2" }
"N": { "type": "macro",      "name": "NombreMacro" }
"N": { "type": "bot",        "name": "LightningBot" }
"N": { "type": "keyboard",   "keys": ["alt", "tab"] }
"N": { "type": "mouse_click","button": "left" }
```

Ver [MACROS.md](MACROS.md) para la sintaxis completa de macros.

---

## Movimiento del ratón desde un stick analógico

```json
"axes": {
  "dwZpos": { "target": "mouse_x", "invert": false, "speed": 15 },
  "dwRpos": { "target": "mouse_y", "invert": false, "speed": 15 }
}
```

`speed` = píxeles por tick (8ms) a deflexión máxima. Con 15 → ~1875 px/s a full stick.

---

## Perfiles de juego

`controllers.json` es la config base pura. Los macros, bots y asignaciones especiales van en un JSON separado por juego.

```json
{
  "profile_name": "Final Fantasy X",
  "overrides": [
    {
      "vid": "2DC8", "pid": "6009",
      "buttons": {
        "3":  { "type": "macro", "name": "BanishingBlade" },
        "6":  { "type": "bot",   "name": "LightningBot" }
      }
    }
  ]
}
```

Hot-swap en tiempo real. El perfil se selecciona desde la UI de VirtualPad.

---

## Archivos de datos

| Archivo | Descripción |
|---|---|
| `data/controllers.json` | Configuración base de mandos físicos |
| `data/FinalFantasyX.json` | Perfil de juego para FFX |
| `data/macros.json` | Biblioteca de macros reutilizables |
| `data/virtualpad.json` | VID/PID del mando virtual + nivel de log |

Ver [MACROS.md](MACROS.md) para la sintaxis completa de macros.
