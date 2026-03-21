# VirtualPad

Lee mandos físicos (WinMM) y los reenvía como un mando Xbox 360 virtual via ViGEm.
Soporta bots y configuración por JSON sin tocar el código.

---

## Requisitos

| Dependencia | Motivo |
|---|---|
| Windows 10/11 | API requeridas: WinMM |
| [ViGEmBus driver](https://github.com/nefarius/ViGEmBus/releases) | Crea el mando Xbox 360 virtual |

### Para compilar

- Visual Studio 2022 (con soporte C++17 y Windows SDK)
- `nlohmann/json` incluido en el repositorio

---

## Configurar un mando — `configs/controllers.json`

Cada entrada describe un mando físico identificado por **VID y PID**.

```json
{
  "vid": "XXXX",
  "pid": "YYYY",
  "source_name": "Nombre descriptivo",
  "mode": "dinput",
  "buttons": { "1": "a", "2": "b" },
  "axes": { "dwXpos": { "target": "left_x", "invert": false } },
  "dpad": "pov"
}
```

El campo `"mode"` determina qué API se usa:

| mode | API | Cuándo usarlo |
|---|---|---|
| `"dinput"` | WinMM (`joyGetPosEx`) | Mando en modo D (DInput) |
| `"xinput"` | WinMM compat layer | Mando en modo X (XInput) |

---

## Ejes disponibles (modo `dinput`)

| Nombre | Campo WinMM | Uso típico |
|---|---|---|
| `"dwXpos"` | `dwXpos` | Stick izquierdo X |
| `"dwYpos"` | `dwYpos` | Stick izquierdo Y |
| `"dwZpos"` | `dwZpos` | Stick derecho X |
| `"dwRpos"` | `dwRpos` | Stick derecho Y |

---

## Tipos de acción para botones

```json
"N": "a"                                    // botón virtual simple
"N": { "type": "trigger", "target": "l2" } // gatillo digital
"N": { "type": "bot", "name": "LightningBot" }
```

---

## Bots incluidos

### LightningBot (Final Fantasy X — Thunder Plains)

Detecta el flash de pantalla del rayo y pulsa automáticamente el botón asignado para esquivar.
Se activa/desactiva con un botón físico definido en la config.

---

## Botones virtuales disponibles

| Valor | Botón virtual |
|---|---|
| `"a"` / `"b"` / `"x"` / `"y"` | Cara |
| `"l1"` / `"r1"` | Bumpers |
| `"select"` / `"start"` | Back / Start |
| `"l3"` / `"r3"` | Click sticks |
| `{ "type": "trigger", "target": "l2" }` | Gatillo digital |
