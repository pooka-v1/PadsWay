# VirtualPad

Lee mandos físicos (WinMM, HID, XInput) y los reenvía como un mando Xbox 360 virtual via ViGEm.
Soporta macros, bots y configuración por JSON sin tocar el código.

---

## Requisitos

### Para ejecutar

| Dependencia | Motivo |
|---|---|
| Windows 10/11 | API requeridas: WinMM, HID, DirectX 11 |
| [ViGEmBus driver](https://github.com/nefarius/ViGEmBus/releases) | Crea el mando Xbox 360 virtual |
| [HidHide driver](https://github.com/nefarius/HidHide/releases) | Oculta el mando físico a los juegos para evitar doble input |

> **ViGEmBus** y **HidHide** son del mismo autor (Nefarius) e instalan como cualquier driver de Windows.
> VirtualPad los controlará automáticamente — no hace falta tocar sus interfaces manualmente.

### Para compilar

- Visual Studio 2022 (con soporte C++17 y Windows SDK)
- El resto de dependencias están incluidas en el repositorio (`imgui/`, `nlohmann/`)

---

## Añadir un mando nuevo — `data/controllers.json`

Cada entrada del array `"controllers"` describe un mando físico.
El campo obligatorio es el **VID y PID** del mando (se ven en el Tab Scanner o en el Administrador de dispositivos).

```json
{
  "vid": "XXXX",
  "pid": "YYYY",
  "source_name": "Nombre descriptivo",
  "mode": "...",
  "buttons": { },
  "axes": { },
  "dpad": "..."
}
```

El campo `"mode"` determina qué API se usa para leerlo:

| mode | API | Cuándo usarlo |
|---|---|---|
| `"dinput"` | WinMM (`joyGetPosEx`) | Mando que aparece en `joy.cpl` y en WinMM |
| `"hid"` | HID raw (`HidP_*`) | Mando que aparece en `joy.cpl` pero **no** en WinMM |
| `"xinput"` | WinMM compat layer | Mando XInput (pasa por WinMM en modo compatibilidad) |

> **Tip:** abre el Tab Scanner con el mando conectado. Si aparece en la sección **WinMM**, usa `"dinput"`. Si solo aparece en **HID-only**, usa `"hid"`.

---

## Modo `"dinput"` — WinMM

Los ejes usan los nombres de `JOYINFOEX`:

| Nombre eje | Campo WinMM | Uso típico |
|---|---|---|
| `"dwXpos"` | `dwXpos` | Stick izquierdo X |
| `"dwYpos"` | `dwYpos` | Stick izquierdo Y |
| `"dwZpos"` | `dwZpos` | Stick derecho X (D-mode) o gatillos combinados (X-mode) |
| `"dwRpos"` | `dwRpos` | Stick derecho Y |
| `"dwUpos"` | `dwUpos` | Stick derecho X (X-mode en algunos mandos) |
| `"dwVpos"` | `dwVpos` | Eje auxiliar |

El D-pad en DInput suele ser un POV hat:
```json
"dpad": "pov"
```

### Ejemplo — 8BitDo Pro 3 (D-mode, Bluetooth)
```json
{
  "vid": "2DC8", "pid": "6009",
  "source_name": "8BitDo Pro 3 (D-mode)",
  "mode": "dinput",
  "buttons": {
    "1":  "b",
    "2":  "a",
    "_3": "Rp (paddle derecho) — sin equivalente Xbox, usar en perfil de juego",
    "4":  "y",
    "5":  "x",
    "_6": "Lp (paddle izquierdo) — sin equivalente Xbox, usar en perfil de juego",
    "7":  "l1",
    "8":  "r1",
    "9":  { "type": "trigger", "target": "l2" },
    "10": { "type": "trigger", "target": "r2" },
    "11": "select",
    "12": "start",
    "13": "home",
    "14": "l3",
    "15": "r3",
    "_17": "L4 — sin equivalente Xbox, usar en perfil de juego",
    "_18": "R4 — sin equivalente Xbox, usar en perfil de juego"
  },
  "axes": {
    "dwXpos": { "target": "left_x",  "invert": false },
    "dwYpos": { "target": "left_y",  "invert": true  },
    "dwZpos": { "target": "right_x", "invert": false },
    "dwRpos": { "target": "right_y", "invert": true  }
  },
  "dpad": "pov"
}
```

---

## Modo `"xinput"` — WinMM compat layer

Los mandos XInput (Pro 3/Pro 2 en X-mode, F310 en X-mode) no exponen ejes separados para
L2 y R2 — los comparten en un único eje Z: positivo → L2, negativo → R2.
Esto es una limitación de WinMM; los dos gatillos no se pueden pulsar a la vez.

```json
"dwZpos": { "target": "trigger_combined", "invert": false }
```

La cruceta en X-mode también va como POV hat:
```json
"dpad": "pov"
```

### Ejemplo — 8BitDo Pro 3 (X-mode, Bluetooth)
```json
{
  "vid": "2DC8", "pid": "310B",
  "source_name": "8BitDo Pro 3 (X-mode)",
  "mode": "xinput",
  "buttons": {
    "1": "a", "2": "b", "3": "x", "4": "y",
    "5": "l1", "6": "r1",
    "7": "select", "8": "start", "9": "l3", "10": "r3"
  },
  "axes": {
    "dwXpos": { "target": "left_x",  "invert": false },
    "dwYpos": { "target": "left_y",  "invert": true  },
    "dwZpos": { "target": "trigger_combined", "invert": false },
    "dwRpos": { "target": "right_y", "invert": true  },
    "dwUpos": { "target": "right_x", "invert": false }
  },
  "dpad": "pov"
}
```

---

## Modo `"hid"` — HID raw

Para mandos que no aparecen en WinMM pero sí en la sección **HID-only** del scanner.
Los ejes usan **HID Usage IDs** en vez de nombres WinMM:

### Ejes — Generic Desktop (página HID 0x01)

| Nombre eje | Usage ID | Uso típico |
|---|---|---|
| `"hid_x"`  | 0x30 | Stick izquierdo X |
| `"hid_y"`  | 0x31 | Stick izquierdo Y |
| `"hid_z"`  | 0x32 | Stick derecho X (o gatillo en algunos mandos) |
| `"hid_rx"` | 0x33 | Stick derecho X / Gatillo L (varía por mando) |
| `"hid_ry"` | 0x34 | Stick derecho Y / Gatillo R (varía por mando) |
| `"hid_rz"` | 0x35 | Stick derecho Y (frecuente en D-mode) |

### Ejes — Simulation Controls (página HID 0x02)

Algunos mandos exponen los gatillos como controles de simulación (el mismo estándar que los volantes):

| Nombre eje | Usage ID | Uso típico |
|---|---|---|
| `"hid_brake"` | 0xC4 | Gatillo L2 (Brake) |
| `"hid_accel"` | 0xC5 | Gatillo R2 (Accelerator) |

> VirtualPad detecta automáticamente la página HID real del descriptor del dispositivo —
> no hay que preocuparse por si el mando usa 0x01 o 0x02 internamente.

### Targets de eje disponibles

| Target | Descripción |
|---|---|
| `"left_x"` / `"left_y"` | Stick izquierdo |
| `"right_x"` / `"right_y"` | Stick derecho |
| `"trigger_l"` | Gatillo L2 analógico **[0.0 .. 1.0]** |
| `"trigger_r"` | Gatillo R2 analógico **[0.0 .. 1.0]** |
| `"trigger_combined"` | L2/R2 compartidos en un eje (+ = L2, - = R2) |

### D-pad en modo HID

Los mandos HID suelen tener el D-pad como **hat switch**:
```json
"dpad": "hid_hat"
```

### Ejemplo — 8BitDo Pro 2 (D-mode, Bluetooth) con gatillos analógicos
```json
{
  "vid": "2DC8", "pid": "6006",
  "source_name": "8BitDo Pro 2 (D-mode)",
  "mode": "hid",
  "buttons": {
    "1": "b", "2": "a", "3": "x", "4": "y",
    "7": "l1", "8": "r1",
    "11": "select", "12": "start", "13": "home",
    "14": "l3", "15": "r3"
  },
  "axes": {
    "hid_x":     { "target": "left_x",   "invert": false },
    "hid_y":     { "target": "left_y",   "invert": true  },
    "hid_z":     { "target": "right_x",  "invert": false },
    "hid_rz":    { "target": "right_y",  "invert": true  },
    "hid_brake": { "target": "trigger_r", "invert": false },
    "hid_accel": { "target": "trigger_l", "invert": false }
  },
  "dpad": "hid_hat"
}
```

> **Nota:** en este mando concreto, Brake→R2 y Accel→L2 (invertido respecto al nombre).
> Si los gatillos salen al revés, intercambia `trigger_l` y `trigger_r`.

---

## Mapeo de botones

Los índices son **1-based** y corresponden al bit N-1 de la máscara de botones del dispositivo.
Usa el **Tab Scanner** de VirtualPad para identificar qué número sale al pulsar cada botón físico.

### Botones virtuales disponibles

| Valor | Botón en el mando virtual |
|---|---|
| `"a"` | A |
| `"b"` | B |
| `"x"` | X |
| `"y"` | Y |
| `"l1"` | LB (bumper izquierdo) |
| `"r1"` | RB (bumper derecho) |
| `"select"` | Back / Select |
| `"start"` | Start |
| `"home"` | Guide (no inyectable en Xbox 360 virtual) |
| `"l3"` | Click stick izquierdo |
| `"r3"` | Click stick derecho |

### Tipos de acción para botones

```json
"N": "a"                                      // botón virtual simple
"N": { "type": "trigger", "target": "l2" }   // gatillo digital (L2 o R2)
"N": { "type": "macro",   "name": "NombreMacro" }
"N": { "type": "bot",     "name": "LightningBot" }
"N": { "type": "keyboard",    "keys": ["alt", "tab"] }
"N": { "type": "mouse_click", "button": "left" }
```

#### Acción `keyboard` — combos de teclado

Envía una combinación de teclas al sistema al pulsar el botón, y la suelta al soltarlo.
Útil para botones sin equivalente Xbox (Home, Lp, Rp, L4, R4) o para cualquier atajo de sistema.

Teclas disponibles: `alt`, `ctrl`, `shift`, `win`, `tab`, `enter`, `esc`, `space`, `backspace`,
`delete`, `insert`, `home_key`, `end`, `pageup`, `pagedown`, `up`, `down`, `left`, `right`,
`f1`–`f12`, letras `a`–`z`, dígitos `0`–`9`.

```json
"13": { "type": "keyboard", "keys": ["alt", "tab"] }
```

> **Nota**: `"home_key"` se refiere a la tecla Inicio del teclado. `"home"` (sin `_key`) es el botón
> Guide/Home del mando virtual (Xbox).

#### Acción `mouse_click` — clicks de ratón

```json
"14": { "type": "mouse_click", "button": "left" }
"14": { "type": "mouse_click", "button": "right" }
"14": { "type": "mouse_click", "button": "middle" }
```

---

## Movimiento del ratón desde un stick analógico

Mapea un eje del mando al cursor del ratón usando el target especial `mouse_x` / `mouse_y`:

```json
"axes": {
  "dwZpos": { "target": "mouse_x", "invert": false, "speed": 15 },
  "dwRpos": { "target": "mouse_y", "invert": false, "speed": 15 }
}
```

El parámetro `speed` controla los píxeles por tick (8ms) a deflexión máxima. Con `speed: 15` el
cursor recorre ~1875 px/segundo a full stick — ajusta según preferencia.

> **Inversión de eje Y**: la pantalla tiene Y positivo hacia abajo, el convenio del mando lo tiene
> hacia arriba. Si el cursor se mueve al revés en vertical, usa `"invert": true` para ese eje.
> Normalmente es el contrario del valor que tenías para `right_y` con el mismo eje físico.

### Ejemplo — Pro 2 D-mode con ratón en stick derecho

```json
"hid_z":  { "target": "mouse_x", "invert": false, "speed": 15 },
"hid_rz": { "target": "mouse_y", "invert": false, "speed": 15 }
```

Estos targets se pueden usar en la config base (`controllers.json`) o como override de eje
en un perfil de juego cuando esté implementado el soporte de axis overrides en perfiles.

---

## Inversión de ejes

Añade `"invert": true` en el mapping del eje para invertirlo:
```json
"hid_y": { "target": "left_y", "invert": true }
```

Muchos mandos reportan el eje Y del stick de forma que "arriba" da valor máximo,
pero el convenio de VirtualPad es `+1.0 = arriba`. Ajusta según lo que veas en el scanner.

---

## Cómo descubrir el mapping de un mando nuevo

1. Conecta el mando
2. Abre el **Tab Scanner** en VirtualPad
3. Si aparece en **WinMM**: pulsa cada botón y anota qué número se ilumina → usa `mode: "dinput"` o `"xinput"`
4. Si aparece en **HID-only**: el log de consola al arrancar muestra todos los `ValCap` (Usage ID y rango) → usa `mode: "hid"` y mapea los usages que veas
5. Mueve cada stick y observa qué eje cambia y en qué dirección
6. Para los gatillos: busca en el log `Usage=0xC4` / `Usage=0xC5` (Simulation Controls) o `Usage=0x33`/`0x34` (Rx/Ry en Generic Desktop)
7. Añade la entrada en `controllers.json` y reinicia VirtualPad

---

## Perfiles de juego

`controllers.json` es la **config base pura**: cada botón físico mapea a su equivalente Xbox 360 estándar.
Los macros, bots y asignaciones especiales van en un JSON separado por juego.

### Por qué botones como Lp, Rp, L4, R4 no están en la base

Algunos mandos tienen botones que no existen en el protocolo Xbox 360 (no hay equivalente virtual).
En la base quedan sin mapear — no tienen efecto. Se usan exclusivamente desde perfiles de juego, asignándolos a macros o bots.

Ejemplo — Pro 3 D-mode (botones WinMM):

| Botón WinMM | Físico | En base | Motivo |
|---|---|---|---|
| 3 | Rp (paddle derecho) | — | no tiene equivalente Xbox 360 |
| 6 | Lp (paddle izquierdo) | — | no tiene equivalente Xbox 360 |
| 17 | L4 | — | no tiene equivalente Xbox 360 |
| 18 | R4 | — | no tiene equivalente Xbox 360 |

> Para futuro: si se extiende `GamepadState` con campos propios (l4, r4, lp, rp) y `ViGEmOutputAdapter` los mapea, estos botones podrían tener función virtual propia. Por ahora, solo son útiles como disparadores de macros/bots.

### Formato del perfil de juego

```json
{
  "profile_name": "Final Fantasy X",
  "overrides": [
    {
      "vid": "2DC8",
      "pid": "6009",
      "_note": "Pro 3 D-mode: Rp=BanishingBlade, Lp=LightningBot, R3=LuluOverdrive",
      "buttons": {
        "3":  { "type": "macro", "name": "BanishingBlade" },
        "6":  { "type": "bot",   "name": "LightningBot"   },
        "15": { "type": "macro", "name": "LuluOverdrive"  }
      }
    }
  ]
}
```

- Solo se declaran los botones que cambian respecto a la base. El resto se hereda sin tocar.
- Se puede añadir una entrada `"overrides"` por cada mando que se quiera personalizar (distinto VID/PID).
- Si en un perfil se reasigna un botón estándar (como el 15 = R3), ese botón deja de funcionar como botón virtual mientras el perfil esté activo. Es una decisión consciente.
- JSON no admite comentarios nativos. Los campos prefijados con `_` (`_note`, `_button_map`, `_hid_prototype`) son ignorados por el parser y sirven como anotaciones.

### Añadir un perfil nuevo

1. Crea `data/NombreJuego.json` con la estructura anterior.
2. Declara solo los botones que necesitas cambiar; deja el resto en la base.
3. Selecciona el perfil desde la UI de VirtualPad (pendiente de implementar) o desde `virtualpad.json`.

---

## Archivos de datos

| Archivo | Descripción |
|---|---|
| `data/controllers.json` | Configuración base de mandos físicos |
| `data/FinalFantasyX.json` | Perfil de juego para FFX (overrides sobre la base) |
| `data/macros.json` | Biblioteca de macros reutilizables |
| `data/virtualpad.json` | VID/PID del mando virtual creado por ViGEm + nivel de log |

Ver [MACROS.md](MACROS.md) para la sintaxis completa de macros.

---

## Mapa de botones por mando

Las entradas en **negrita** son botones que no tienen equivalente en el protocolo Xbox 360.
No producen ningún output virtual por defecto — solo son útiles asignándolos a macros o bots en un perfil de juego.

### 8BitDo Pro 3 — D-mode (VID:2DC8 PID:6009)

| Botón WinMM | Físico | Virtual Xbox |
|---|---|---|
| 1 | B | b |
| 2 | A | a |
| **3** | **Rp (paddle derecho)** | **— sin equivalente** |
| 4 | Y | y |
| 5 | X | x |
| **6** | **Lp (paddle izquierdo)** | **— sin equivalente** |
| 7 | LB | l1 |
| 8 | RB | r1 |
| 9 | L2 | trigger_l2 (digital) |
| 10 | R2 | trigger_r2 (digital) |
| 11 | Select | select |
| 12 | Start | start |
| 13 | Home | home |
| 14 | L3 | l3 |
| 15 | R3 | r3 |
| **17** | **L4** | **— sin equivalente** |
| **18** | **R4** | **— sin equivalente** |

---

### 8BitDo Pro 3 — X-mode (VID:2DC8 PID:310B)

En X-mode el firmware solo expone los 10 botones estándar Xbox. Los botones Home, Lp, Rp, L4 y R4 no están accesibles.

| Botón WinMM | Físico | Virtual Xbox |
|---|---|---|
| 1 | A | a |
| 2 | B | b |
| 3 | X | x |
| 4 | Y | y |
| 5 | LB | l1 |
| 6 | RB | r1 |
| 7 | Select | select |
| 8 | Start | start |
| 9 | L3 | l3 |
| 10 | R3 | r3 |

> L2 y R2 son analógicos pero comparten el eje Z (trigger_combined). No se pueden pulsar ambos a la vez.

---

### 8BitDo Pro 2 — D-mode (VID:2DC8 PID:6006)

| Botón HID | Físico | Virtual Xbox |
|---|---|---|
| 1 | B | b |
| 2 | A | a |
| **3** | **Rp (paddle derecho)** | **— sin equivalente** |
| 4 | Y | y |
| 5 | X | x |
| **6** | **Lp (paddle izquierdo)** | **— sin equivalente** |
| 7 | LB | l1 |
| 8 | RB | r1 |
| 11 | Select | select |
| 12 | Start | start |
| 13 | Home | home |
| 14 | L3 | l3 |
| 15 | R3 | r3 |

> L2 y R2 son analógicos independientes vía HID (hid_brake / hid_accel). Se pueden pulsar ambos a la vez.
> Botones 3 y 6 (Rp/Lp) verificados experimentalmente — coinciden con Pro 3 D-mode.
> El Pro 2 no tiene L4/R4.

---

### 8BitDo Pro 2 — X-mode (VID:045E PID:02E0)

Mismo layout que Pro 3 X-mode. 10 botones estándar Xbox, sin extras.

| Botón WinMM | Físico | Virtual Xbox |
|---|---|---|
| 1 | A | a |
| 2 | B | b |
| 3 | X | x |
| 4 | Y | y |
| 5 | LB | l1 |
| 6 | RB | r1 |
| 7 | Select | select |
| 8 | Start | start |
| 9 | L3 | l3 |
| 10 | R3 | r3 |

> L2/R2 comparten el eje Z (trigger_combined).

---

### Logitech F310 — D-mode (VID:046D PID:C216)

| Botón WinMM | Físico | Virtual Xbox |
|---|---|---|
| 1 | X | x |
| 2 | A | a |
| 3 | B | b |
| 4 | Y | y |
| 5 | LB | l1 |
| 6 | RB | r1 |
| 7 | L2 | trigger_l2 (digital) |
| 8 | R2 | trigger_r2 (digital) |
| 9 | Select | select |
| 10 | Start | start |
| 11 | L3 | l3 |
| 12 | R3 | r3 |

> El F310 D-mode no tiene Home físico ni botones extra. Los gatillos son solo digitales.

---

### Sony DualShock 4 v2 (VID:054C PID:09CC)

Funciona igual en USB y Bluetooth (mismo VID/PID).

| Botón WinMM | Físico | Virtual Xbox |
|---|---|---|
| 1 | Square | x |
| 2 | Cross | a |
| 3 | Circle | b |
| 4 | Triangle | y |
| 5 | L1 | l1 |
| 6 | R1 | r1 |
| 9 | Share | select |
| 10 | Options | start |
| 11 | L3 | l3 |
| 12 | R3 | r3 |
| 13 | PS | home |
| **14** | **Touchpad click** | **— sin equivalente** |

> L2 y R2 son analógicos independientes (dwUpos / dwVpos). Se pueden pulsar ambos a la vez.
> Features DS4 avanzadas (touchpad XY, giroscopio, LEDs, rumble) pendientes para fases futuras.
