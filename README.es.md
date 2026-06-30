# PadsWay

Lee mandos físicos (HID) y los reenvía como un mando virtual vía ViGEm — bien un mando
**Xbox 360** (XInput) o un **DualShock 4** (DirectInput), elegible en caliente.
Soporta macros, bots y configuración por JSON sin tocar el código.

[Read in English](README.md)

---

## Requisitos

### Para ejecutar

| Dependencia | Motivo |
|---|---|
| Windows 10/11 | API requeridas: HID, DirectX 11 |
| [ViGEmBus driver](https://github.com/nefarius/ViGEmBus/releases) | Crea el mando Xbox 360 virtual |
| [HidHide driver](https://github.com/nefarius/HidHide/releases) | Oculta el mando físico a los juegos para evitar doble input |

> **ViGEmBus** y **HidHide** son del mismo autor (Nefarius) e instalan como cualquier driver de Windows.
> PadsWay los controlará automáticamente — no hace falta tocar sus interfaces manualmente.

### Para compilar

- **Visual Studio 2022** (con soporte C++17 y Windows SDK).
- El resto de dependencias están incluidas en el repositorio (`imgui/`, `nlohmann/`, `spdlog/`).
- **Compila la configuración `Release | x64`.** `ViGEmClient.lib` es solo Release, así que una
  build Debug no enlaza. Para depurar, ejecuta Release con F5 (genera el PDB).

La salida es `x64\Release\PadsWay.exe`.

### Ejecutar desde el código fuente (desarrollo)

Por defecto PadsWay guarda su configuración y logs en `%LOCALAPPDATA%\PadsWay` (ver *Archivos de
datos → Dónde se guardan estos archivos*). En desarrollo eso separa tus ajustes de la carpeta
`data/` del propio proyecto. Para que todo se quede junto al directorio de trabajo, coloca ahí un
**`portable.txt`** vacío — en un arranque con F5 desde Visual Studio ese directorio es la carpeta
del proyecto (`PadsWay\`). Está en `.gitignore`, así que es un marcador local de desarrollo y
nunca viaja en el paquete.

### Generar una release (instalador + zip portable)

Los distribuibles se generan con **`installer\package.ps1`**, que centraliza en un único sitio las
reglas de "release limpia" (incluye los assets de fábrica y defaults seguros, y **nunca** tu
`controllers.json` personal, perfiles ni macros):

1. Compila la configuración `Release | x64` en Visual Studio.
2. Desde la raíz del repo, ejecuta:
   ```powershell
   powershell -ExecutionPolicy Bypass -File installer\package.ps1
   ```

Esto deja en `dist\`:

- `PadsWay-v<ver>-win64\` — la carpeta de staging portable,
- `PadsWay-v<ver>-win64.zip` — la build portable,
- `PadsWay-v<ver>-setup.exe` — el instalador de Inno Setup.

El paso del instalador necesita [**Inno Setup 6**](https://jrsoftware.org/isdl.php); si `ISCC.exe`
no está en la ruta por defecto, pasa `-Iscc <ruta>`. Otras opciones: `-Version 0.19` fija la
versión de la release (sincronizada con el instalador), `-SkipZip` / `-SkipInstaller` generan solo una parte.

---

## Tipo de salida virtual — Xbox 360 o DualShock 4

El mando virtual puede presentarse al sistema como uno de dos tipos:

| Tipo | Protocolo | Ideal para |
|---|---|---|
| **Xbox (XInput)** | XInput | Mejor compatibilidad con juegos modernos. Es el valor por defecto. |
| **DualShock (DirectInput)** | DirectInput | Juegos antiguos, emuladores y conseguir **más de 4 mandos** (XInput tiene un techo de 4). |

Se elige en el combo junto al selector de perfil en la UI. Cambiarlo muestra un diálogo de
confirmación y se aplica **en caliente**: el mando virtual se desconecta y reconecta al vuelo, así que
**cierra cualquier juego abierto antes de cambiar** o perderá el mando.

### Qué NO transmite la salida DualShock 4

El DS4 virtual reproduce botones, sticks, cruceta, gatillos analógicos y el botón PS. Las
funciones exclusivas de un DualShock 4 real están **deshabilitadas**:

- el **touchpad** — tanto la superficie táctil (seguimiento XY del dedo) **como** el clic del touchpad,
- el **giroscopio** y el acelerómetro (IMU).

El modelo interno es siempre Xbox, así que no hay fuente para esos datos. En consecuencia, la vista
del mando virtual DS4 muestra el touchpad y el giroscopio solo como decoración no asignable.
(El adaptador de salida DS4 sí cablea el bit del clic del touchpad, pero nada lo alimenta y hoy no
tiene uso — ver *Ideas*; solo importaría para un eventual emulador de PS4.)

### Configuración — `data/virtualpad.json`

El tipo elegido se persiste en `"output_type"`:

```json
{ "output_type": "xbox" }       // o "dualshock"
```

Valores aceptados (sin distinguir mayúsculas): `"xbox"` / `"xinput"` seleccionan la salida Xbox 360
(también es el valor seguro por defecto ante cualquier valor desconocido o clave ausente);
`"dualshock"` / `"ds4"` / `"dinput"` / `"directinput"` seleccionan la salida DualShock 4.

Cada tipo tiene su propio VID/PID para que los juegos los distingan:

| Clave | Por defecto | Identidad |
|---|---|---|
| `virtual_x_vid` / `virtual_x_pid` | `5650` / `0001` | Xbox 360 (ViGEm) |
| `virtual_direct_vid` / `virtual_direct_pid` | `054C` / `05C4` | DualShock 4 v1 (Sony) |

> Un VID Sony real exige un PID Sony real: `054C:05C4` (DualShock 4 v1) lo emula ViGEm de forma
> nativa y no choca con un DS4 v2 físico (`054C:09CC`).

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

El campo `"mode"` debe ser `"hid"` — todos los mandos se leen via HID raw (`HidP_*`).

> **Tip:** abre el Tab Scanner con el mando conectado. El log de consola al arrancar lista todos los ejes detectados (Usage ID + rango). Úsalos para rellenar la sección `"axes"`.

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

> PadsWay detecta automáticamente la página HID real del descriptor del dispositivo —
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
Usa el **Tab Scanner** de PadsWay para identificar qué número sale al pulsar cada botón físico.

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
| `"home"` | Guide en Xbox (no inyectable en Xbox 360 virtual) / botón PS en DualShock |
| `"l3"` | Click stick izquierdo |
| `"r3"` | Click stick derecho |

### Formato de botones

Cada entrada es un objeto con dos conceptos independientes:

- **`physical`** — nombre del botón en el mando físico real (`"a"`, `"l4"`, `"rp"`...).
  Se usa para el tracking visual en la UI. Sobrevive a los overrides de perfil — un botón siempre sabe qué es físicamente, independientemente de la acción asignada.
- **`virtual`** / acción — qué ocurre al pulsar el botón.

```json
"7":  { "physical": "l1", "virtual": "l1" }           // botón normal: físico l1 → virtual l1
"3":  { "physical": "rp" }                             // sin equivalente Xbox — solo visual
"9":  { "physical": "l2", "type": "trigger", "target": "l2" }
```

El formato corto `"1": "b"` sigue siendo válido para mandos sin botones extra
y equivale a `{ "physical": "b", "virtual": "b" }`.

### Tipos de acción para botones

```json
"N": "b"                                              // formato corto (physical = virtual = "b")
"N": { "physical": "b", "virtual": "b" }             // explícito
"N": { "physical": "rp" }                             // solo visual, sin output virtual
"N": { "physical": "l2", "type": "trigger", "target": "l2" }
"N": { "physical": "rp", "type": "macro",   "name": "NombreMacro" }
"N": { "physical": "lp", "type": "bot",     "name": "LightningBot" }
"N": { "physical": "home", "type": "keyboard",    "keys": ["alt", "tab"] }
"N": { "physical": "l3",   "type": "mouse_click", "button": "left" }
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
pero el convenio de PadsWay es `+1.0 = arriba`. Ajusta según lo que veas en el scanner.

---

## Cómo descubrir el mapping de un mando nuevo

1. Conecta el mando
2. Abre el **Tab Scanner** en PadsWay y pulsa cada botón — anota qué número se ilumina
3. El log de consola al arrancar muestra todos los `ValCap` (Usage ID y rango) — úsalos para identificar los ejes
4. Mueve cada stick y observa qué eje cambia y en qué dirección
5. Para los gatillos: busca en el log `Usage=0xC4` / `Usage=0xC5` (Simulation Controls) o `Usage=0x33`/`0x34` (Rx/Ry en Generic Desktop)
6. Añade la entrada en `controllers.json` con `"mode": "hid"` y reinicia PadsWay

---

## Perfiles de juego

`controllers.json` es la **config base pura**: cada botón físico mapea a su equivalente Xbox 360 estándar.
Los macros, bots y asignaciones especiales van en un JSON separado por juego.

### Botones sin equivalente Xbox (Lp, Rp, L4, R4)

Algunos mandos tienen botones que no existen en el protocolo Xbox 360 — no pueden producir output virtual.
Se declaran en la config base solo con el campo `physical` (sin `virtual`):

```json
"3":  { "physical": "rp" },
"17": { "physical": "l4" }
```

Esto significa:
- Se **iluminan en la UI** al pulsarlos, independientemente de la acción asignada.
- **No producen output Xbox virtual** por defecto.
- En un perfil de juego se pueden asignar a macros, bots, combos de teclado, etc.
- Incluso con perfil activo, la UI sigue reflejando que el botón físico está pulsado.

| Botón HID | Físico | Virtual Xbox | Uso |
|---|---|---|---|
| 3 | Rp (paddle derecho) | — | macro / bot en perfil de juego |
| 6 | Lp (paddle izquierdo) | — | macro / bot en perfil de juego |
| 17 | L4 | — | macro / teclado en perfil de juego |
| 18 | R4 | — | macro / teclado en perfil de juego |

### Formato del perfil de juego

Los perfiles son **independientes del mando** — las claves usan el nombre virtual del botón (`physShort`) en lugar de un índice específico del hardware. El mismo perfil funciona con cualquier mando conectado.

```json
{
  "profile_name": "Final Fantasy X",
  "buttons": {
    "r3":   { "type": "macro",       "name": "LuluOverdrive"  },
    "home": { "type": "keyboard",    "keys": ["alt", "tab"]   },
    "l3":   { "type": "mouse_click", "button": "left"         }
  },
  "axes": {
    "right_x": { "target": "mouse_x", "invert": false, "speed": 15 },
    "right_y": { "target": "mouse_y", "invert": false, "speed": 15 }
  }
}
```

- Solo se declaran los botones/ejes que cambian respecto a la base. El resto se hereda sin tocar.
- Si el mando conectado no tiene un botón con ese physShort, el override se ignora silenciosamente.
- Si se reasigna un botón estándar (ej. `r3`), ese botón deja de funcionar como botón virtual mientras el perfil esté activo. Es una decisión consciente.
- JSON no admite comentarios nativos. Los campos prefijados con `_` (`_note`, `_hid_prototype`) son ignorados por el parser y sirven como anotaciones.

### Añadir un perfil nuevo

1. Crea `data/profiles/NombreJuego.json` con la estructura anterior.
2. Declara solo los botones/ejes que necesitas cambiar; deja el resto en la base.
3. Selecciona el perfil desde la UI de PadsWay.

---

## Biblioteca de macros

Accesible desde la **pestaña Pads → botón Macros**. Las macros se almacenan en `data/macros.json` y son reutilizables desde cualquier mando o perfil de juego.

### Panel izquierdo

Lista todas las macros de la biblioteca. Al seleccionar una se muestra una vista previa visual de los steps (chips con iconos de botón) directamente en el panel.

| Botón | Acción |
|---|---|
| **Nuevo** | Abre el editor con una macro en blanco |
| **Editar** | Abre la macro seleccionada en el editor |
| **Copiar** | Duplica la macro seleccionada como punto de partida |
| **Eliminar** | Borra la macro seleccionada de la biblioteca |

### Editor visual

El modal de edición permite construir macros sin escribir DSL a mano:

- **Selector de tokens** — botones de cara, direcciones de cruceta, posiciones de analógico y presets de movimiento (QCF/HCF para cruceta y ambos sticks analógicos). Click en un icono para crear un nuevo step Press; click en el mismo icono con el step seleccionado para añadirlo como combo simultáneo.
- **Secuencia de steps** — cada step se muestra como un chip. Click en un chip para seleccionarlo; click en otro para extender el rango de selección.
- **Controles de repetición** — con uno o más steps seleccionados, aplica un bucle (ms), repetición por conteo (`N veces en ms`), mientras pulsado o toggle.
- **Hold ms** — con un step Press seleccionado, fija cuánto tiempo se mantiene el botón pulsado (por defecto 80ms).
- **Campo DSL** — el string de macro en bruto siempre es visible y editable. Editarlo manualmente activa el modo complejo para macros arbitrariamente avanzadas.

Los cambios se escriben en `data/macros.json` y se recargan en caliente de inmediato — sin reiniciar.

Ver [MACROS.md](MACROS.md) para la sintaxis DSL completa y la referencia del editor.

---

## Editor de layouts

La pestaña **Layout** proporciona un editor visual para `data/pad_layouts.json`.

### Abrir un layout

Haz clic en cualquier nombre de layout en el panel izquierdo para abrirlo en el editor de inmediato.
Si hay cambios sin guardar, aparece un diálogo de confirmación antes de cambiar.

### Interacciones en el canvas

| Acción | Resultado |
|---|---|
| **Un clic** sobre un componente | Lo selecciona (resaltado en amarillo). Sus propiedades aparecen en el panel derecho. |
| **Clic + arrastrar** | Mueve el componente seleccionado libremente sobre el canvas. |
| **Teclas de dirección** (con el canvas enfocado) | Desplaza 1 píxel por pulsación. |

### Tres paneles

**Panel izquierdo** — lista de layouts y lista de elementos, cada una con su propio scroll independiente:
- `[+ Botón]` `[+ Cruceta]` `[+ Analógico]` `[+ Decoración]` — añade un componente de ese tipo.
- `[Eliminar elemento]` — borra el componente seleccionado.
- `[Copiar layout]` — duplica el layout actual como punto de partida para uno nuevo.
- `[Guardar]` / `[Descartar]` — guarda en `pad_layouts.json` (crea un `.bak` automático en el primer guardado si no existe) o descarta todos los cambios.
- `[Emparejar mando]` — lanza el Asistente de emparejamiento (ver más abajo).

Tipos de componente soportados: `button`, `stick`, `dpad`, `touchpad`, `gyroscope`, `decoration`, `template`.
El componente `gyroscope` es visible en la pestaña Pads cuando el mando conectado reporta datos IMU (p. ej. DualShock 4 por USB). No requiere calibración — los datos se leen automáticamente desde offsets HID fijos.

**Panel central** — canvas con el layout a escala. Se muestran las zonas FRONT (franja inferior) y TOP (área principal) con sus componentes renderizados en vivo.

**Panel derecho** — propiedades del componente seleccionado:
- **Posición** `cx` / `cy` y **Tamaño** `w` / `h` — campos numéricos. El checkbox junto a cada par bloquea la proporción de aspecto.
- **Imagen / Overlay** — combos filtrados por subcarpeta (`templates/`, `buttons/`, `cross/`, `analogics/`, `decorations/`). Seleccionar una imagen rellena automáticamente el tamaño con las dimensiones de la imagen.
- **State bindings** — `state`, `state_x`, `state_y`, `state_click`, `state_up/down/left/right` — combos con los nombres de campo de `GamepadState`.
- **Colores** — colores activo/inactivo para la imagen base y el overlay.

### Imágenes de template

El canvas tiene dos zonas apiladas verticalmente:

| Zona | Propósito | Tamaño de imagen recomendado |
|---|---|---|
| **FRONT** | Franja inferior — cara frontal del mando | 480 × 200 px |
| **TOP** | Área principal — vista superior | 480 × 320 px |

Las imágenes se estiran para rellenar exactamente su zona. Usar imágenes a estas dimensiones evita pérdida de calidad por escalado.
Los archivos van en `images/templates/`.

---

### Control de cambios

Un indicador `*` aparece cuando hay cambios sin guardar. Cambiar de layout sin guardar activa un diálogo de confirmación.

---

## Asistente de emparejamiento de mando

El botón **Emparejar mando** del Editor de layouts lanza un asistente paso a paso para mapear un mando físico al layout abierto y generar la entrada correspondiente en `data/controllers.json`.

### Cómo funciona

El asistente guía a través de cinco etapas:

1. **Seleccionar mando** — lista todos los dispositivos físicos detectados (WinMM y HID). El mando virtual de ViGEm se filtra automáticamente.
2. **Nombrar mando** — introduce un nombre descriptivo. Para mandos WinMM, alterna entre modo DInput y XInput.
3. **Asignar botones** — para cada botón, gatillo y clic de stick del layout, el asistente muestra la imagen activa de ese botón como referencia visual y pide que pulses el botón físico correspondiente. A medida que se asigna cada botón, aparece su número superpuesto en el canvas.
4. **Asignar ejes y gatillos** — para cada eje analógico (stick izquierdo X/Y, stick derecho X/Y) y gatillo (L2, R2), el asistente muestra la imagen activa del componente junto con flechas que indican la dirección de movimiento esperada.
   - **Stick izquierdo X**: empuja totalmente hacia la **derecha**.
   - **Stick izquierdo Y**: empuja totalmente hacia **abajo**.
   - **Stick derecho X/Y**: mismo convenio.
   - **Gatillos**: aprieta completamente.
   - **Los componentes de giroscopio se omiten** — los datos del giroscopio se leen automáticamente desde offsets HID fijos y no requieren ningún paso de calibración.
5. **Revisión** — muestra todos los botones, ejes y cruceta asignados. Confirma para guardar o reinicia desde el paso de nombre.

### Resultado

Al guardar, se escribe o reemplaza la entrada en `data/controllers.json` por VID/PID. La pestaña Pads recarga la configuración automáticamente.

### Mapa de estados (`data/state_map.json`)

El asistente usa `state_map.json` para saber el nombre físico del botón (`physical`), el target del eje (`axis_target`) y el texto de instrucción a mostrar para cada campo de `GamepadState`. Los componentes cuyo campo `state` no tenga entrada en el mapa se omiten en silencio.

```json
{
  "state_map": {
    "btnA":    { "physical": "a",   "type": "button" },
    "leftX":   { "type": "axis",    "axis_target": "left_x",
                 "prompt": "Empuja el stick izquierdo a la DERECHA",
                 "invert_if_positive": false },
    "dpadUp":  { "type": "dpad",    "direction": "up" }
  }
}
```

### Problemas conocidos (en progreso)

| Problema | Estado |
|---|---|
| El eje X del stick analógico izquierdo puede no capturarse (empujar a la derecha no calibra) | En investigación |
| La pestaña Pads no se refresca hasta reiniciar la app después de que el asistente guarda | En investigación |

---

## Archivos de datos

| Archivo | Descripción |
|---|---|
| `data/controllers.json` | Configuración base de mandos físicos |
| `data/profiles/` | Perfiles de juego — un JSON por juego |
| `data/macros.json` | Biblioteca de macros reutilizables |
| `data/virtualpad.json` | Tipo de salida virtual y VID/PID por tipo, idioma, nivel de log y umbrales de stick (ver *Tipo de salida virtual*) |
| `data/strings/strings_en.json` | Textos de la interfaz — inglés |
| `data/strings/strings_es.json` | Textos de la interfaz — español |
| `images/input_tokens/` | Iconos PNG para el creador de macros (24×24) |

### Dónde se guardan estos archivos

Los archivos que creas o editas — `controllers.json`, `profiles/`, `macros.json`,
`pad_layouts.json`, `virtualpad.json` y la carpeta `logs/` — se guardan en una
carpeta por usuario, para que la app funcione incluso instalada en una ubicación
protegida como `Archivos de programa`:

```
%LOCALAPPDATA%\PadsWay\
```

(normalmente `C:\Users\<tú>\AppData\Local\PadsWay\`). En el primer arranque se
copian ahí automáticamente las copias de fábrica que vienen junto al ejecutable,
así empiezas con los layouts de mando incluidos. Los recursos de solo lectura que
acompañan a la app (`data/strings/`, `data/state_map.json`, `images/`,
`data/bots/`) permanecen junto al ejecutable y nunca se modifican.

**Modo portable.** Coloca un archivo vacío llamado `portable.txt` junto a
`PadsWay.exe` y la app guardará todo junto al ejecutable (el comportamiento
anterior) — útil para ejecutarla desde un USB sin dejar rastro en el equipo. Sin
él, se usa la ubicación por usuario de arriba.

Ver [MACROS.md](MACROS.md) para la sintaxis completa de macros.

---

## Localización

El idioma activo de la interfaz se configura con `"locale"` en `data/virtualpad.json`:

```json
{ "locale": "es" }
```

| Valor | Idioma |
|---|---|
| `"en"` | Inglés |
| `"es"` | Español |

Los textos se cargan de `data/strings/strings_{locale}.json` al arrancar. Si falta alguna clave en el fichero, se muestra el nombre de la clave como texto de reserva — no hay crash.

Para añadir un idioma nuevo: copia `strings_en.json`, renómbralo `strings_xx.json`, traduce los valores (nunca las claves) y pon `"locale": "xx"` en `virtualpad.json`.

---

## Mapa de botones por mando

Las entradas en **negrita** son botones que no tienen equivalente en el protocolo Xbox 360.
No producen ningún output virtual por defecto — solo son útiles asignándolos a macros o bots en un perfil de juego.

### 8BitDo Pro 3 — D-mode (VID:2DC8 PID:6009)

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

| Botón HID | Físico | Virtual Xbox |
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

> L2 y R2 comparten un único eje (trigger_combined) — limitación del firmware en X-mode. No se pueden pulsar ambos a la vez.

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

| Botón HID | Físico | Virtual Xbox |
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

> L2/R2 comparten un único eje (trigger_combined) — limitación del firmware en X-mode.

---

### Logitech F310 — D-mode (VID:046D PID:C216)

| Botón HID | Físico | Virtual Xbox |
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

| Botón HID | Físico | Virtual Xbox |
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

> L2 y R2 son analógicos independientes vía HID (`hid_rx` / `hid_ry`). Se pueden pulsar ambos a la vez.
> **USB completamente soportado**: botones, sticks analógicos, gatillos analógicos, touchpad (seguimiento XY + clic, emulación de ratón), giroscopio (ejes X/Y/Z visibles en la pestaña Pads).
> **Bluetooth**: report simplificado (sticks + botones de cara). Soporte BT completo pendiente.

---

### 8BitDo Zero 2 — Bluetooth D-mode (VID:2DC8 PID:3230)

La cruceta se reporta como dos ejes analógicos binarios (X/Y), no como hat switch.
PadsWay los mapea a direcciones de cruceta virtual mediante `axis_actions`.

| Botón HID | Físico | Virtual Xbox |
|---|---|---|
| 1 | A | a |
| 2 | B | b |
| 4 | X | x |
| 5 | Y | y |
| 7 | LB | l1 |
| 8 | RB | r1 |
| 11 | Select | select |
| 12 | Start | start |

---

### 8BitDo Zero 2 — Bluetooth X-mode (VID:045E PID:02E0)

Se distingue de otros mandos que comparten este VID/PID por `product_name: "8BitDo Zero 2 gamepad"`.

Mismo layout de botones que D-mode.

---

### 8BitDo Zero 2 — USB (VID:2DC8 PID:9018)

D-mode y X-mode por USB comparten el mismo VID/PID (`9018`). El layout de botones difiere entre modos, por lo que **solo puede haber un perfil USB activo a la vez**.

> **Recomendación:** configura el perfil USB con el mando conectado en **modo Android (D-mode)**. Mantén pulsada la combinación de D-mode antes de enchufar el cable USB, y ejecuta el Asistente de emparejamiento. La configuración resultante funcionará correctamente para D-mode USB. El X-mode USB usa el mismo VID/PID pero numeración de botones diferente — configurarlo sobreescribiría el perfil D-mode.

---

## Licencia

PadsWay se distribuye bajo la [Licencia MIT](LICENSE) — © 2026 pooka-v1.

Se apoya en el trabajo (también MIT) de [Nefarius](https://github.com/nefarius)
([ViGEmBus](https://github.com/nefarius/ViGEmBus), [HidHide](https://github.com/nefarius/HidHide))
e incluye las librerías MIT [Dear ImGui](https://github.com/ocornut/imgui),
[nlohmann/json](https://github.com/nlohmann/json) y [spdlog](https://github.com/gabime/spdlog).
