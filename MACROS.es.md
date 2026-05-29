# VirtualPad — Sistema de Macros

Las macros se pueden definir de dos formas:

**Opción A — inline en `data/controllers.json`** (la macro vive junto al botón):
```json
"15": { "type": "macro", "name": "NombreMacro", "execution": "A, B, X" }
```

**Opción B — en la librería `data/macros.json`** (reutilizable desde varios botones/mandos):
```json
[
  { "name": "NombreMacro", "execution": "A, B, X" }
]
```
```json
"15": { "type": "macro", "name": "NombreMacro" }
```

Si el botón tiene `"execution"` inline, se usa directamente. Si no, el engine busca la macro por `"name"` en `macros.json`.

---

## Editor visual de macros

Accesible desde **pestaña Pads → botón Macros**. El panel izquierdo lista todas las macros de `data/macros.json` con una vista previa visual de los steps de la macro seleccionada. Usa **Nuevo**, **Editar** o **Copiar** para abrir el editor.

### Construir steps con el selector de tokens

- **Click en un icono** → crea un nuevo step Press al final de la secuencia (sin selección)
- **Click en un step** de la secuencia → lo selecciona (resaltado azul)
- **Click en un icono con un step Press seleccionado** → añade ese token al step (combo simultáneo)
- **Click en el mismo icono otra vez** → elimina ese token del step seleccionado
- **Eliminar el último token** de un step → elimina el step

La secuencia muestra los combos como `[→]+[A]`, igual que el DSL `CR + A`.

### Secciones del selector

| Sección | Contenido |
|---|---|
| **Botones** | Botones de cara, bumpers, gatillos, clicks de stick, Start / Select |
| **Cruz** | 8 direcciones · Spin CCW/CW · HCF ← · HCF → · QCF ← · QCF → |
| **Analógico L / R** | 8 direcciones por stick · Spin CCW/CW · HCF ← · HCF → · QCF ← · QCF → |
| **Macros** | Insertar macro de la biblioteca (MacroRef) |

### Tipos de step

| Tipo | Cómo crearlo |
|---|---|
| **Press** | Click en cualquier icono de botón/dirección |
| **Wait** | Introducir ms → "Añadir espera" |
| **Group** | Seleccionar rango de steps → elegir modo de repetición → "Crear repetición" |
| **MacroRef** | "Insertar macro..." → elegir de la biblioteca |

**Hold ms** — con un step Press seleccionado, fija cuánto tiempo se mantiene el botón. Por defecto 0 usa `DEFAULT_PRESS_MS` (80ms).

### Controles de repetición

Con uno o más steps seleccionados, elige un modo y pulsa **Crear repetición**:

| Modo | DSL | Descripción |
|---|---|---|
| Bucle (ms) | `*5000` | Repite durante N milisegundos |
| N veces en (ms) | `*1000/10` | Exactamente N repeticiones en N ms |
| Mientras pulsado | `*UP` | Repite mientras el botón físico esté pulsado |
| Alternar | `*DO` | Primera pulsación arranca, segunda para |

Selección de **un solo step** → aplica el repeat directamente: `A*UP`.
Selección de **un rango** → envuelve los steps en un Group: `(A, B, C)*5000`.

### Presets de spin

Los botones de spin (CCW / CW) insertan 8 steps Press en orden circular a **30ms cada uno**. El spin de cruceta arranca en Derecha (CW) o Izquierda (CCW); los de analógico hacen lo mismo para el stick L y R por separado. Útil para macros de rotación (ver ejemplo Lulu Overdrive).

### Presets de movimiento

Secuencias direccionales predefinidas para juegos de lucha (**40ms por paso**):

| Botón | Secuencia | Tokens | Uso |
|---|---|---|---|
| HCF ← | Medio círculo izquierda | CR → CDR → CD → CDL → CL | Half-circle back |
| HCF → | Medio círculo derecha | CL → CDL → CD → CDR → CR | Half-circle forward |
| QCF ← | Cuarto de círculo izquierda | CD → CDL → CL | Quarter-circle back |
| QCF → | Cuarto de círculo derecha | CD → CDR → CR | Quarter-circle forward |

Disponibles para cruceta y analógico (stick L y R). Para un movimiento especial completo: inserta el preset de movimiento, luego haz click en el icono del botón de ataque para añadirlo como step separado. Ajusta los tiempos con Hold ms.

### Insertar macro (MacroRef)

**Insertar macro...** abre un desplegable con todas las macros de la biblioteca. Al seleccionar una, su DSL completo se inserta **inline** al final de la secuencia (expandido en el momento de insertar — ediciones posteriores a la macro original no afectan a quien la referenció).

### Campo DSL

El DSL en bruto siempre es visible y editable bajo los steps. Editarlo manualmente activa el **modo complejo**: la lista visual de steps se oculta y los clicks en iconos añaden tokens directamente al string DSL. El modo complejo permite macros arbitrariamente avanzadas manteniendo el picker para inserciones rápidas.

### Guardar

Da nombre a la macro y pulsa **Guardar**. La macro se escribe en `data/macros.json` y el engine la recarga en caliente — no hace falta reiniciar.

---

## Referencia DSL

---

## Tokens — Botones

| Token | Botón                     |
|-------|---------------------------|
| `A`   | A                         |
| `B`   | B                         |
| `X`   | X                         |
| `Y`   | Y                         |
| `L1`  | LB (bumper izq.)          |
| `R1`  | RB (bumper der.)          |
| `L2`  | LT (gatillo izq.)         |
| `R2`  | RT (gatillo der.)         |
| `L3`  | LS (click stick izq.)     |
| `R3`  | RS (click stick der.)     |
| `CU`  | Cruceta arriba            |
| `CD`  | Cruceta abajo             |
| `CL`  | Cruceta izquierda         |
| `CR`  | Cruceta derecha           |
| `CUR` | Diagonal arriba-derecha   |
| `CUL` | Diagonal arriba-izquierda |
| `CDR` | Diagonal abajo-derecha    |
| `CDL` | Diagonal abajo-izquierda  |
| `ST`  | Start                     |
| `SE`  | Select / Back             |
| `HO`  | Home (ignorado — no inyectable en Xbox 360 virtual) |

---

## Tokens — Analógicos

Formato: `LAX`, `LAY`, `RAX`, `RAY` seguido de un valor float entre `-1.0` y `1.0`.

| Token   | Eje                          |
|---------|------------------------------|
| `LAX`   | Stick izquierdo, eje X       |
| `LAY`   | Stick izquierdo, eje Y       |
| `RAX`   | Stick derecho, eje X         |
| `RAY`   | Stick derecho, eje Y         |

**Convenio de valores:**
- X: `-1.0` = tope izquierdo · `0` = centro · `+1.0` = tope derecho
- Y: `-1.0` = tope abajo · `0` = centro · `+1.0` = tope arriba

**Diagonales (círculo unidad):** usar `±0.71` (= cos/sin de 45°), no `±0.5`.
`0.5+0.5` da el ángulo correcto pero el stick solo llega al 70% de recorrido.

---

## Operadores

| Sintaxis       | Significado                                                 |
|----------------|-------------------------------------------------------------|
| `A, B, C`      | Secuencia: pulsa A, luego B, luego C (200ms entre cada uno) |
| `A + Y`        | Combo: A e Y a la vez                                       |
| `B=1000`       | Hold: mantén B pulsado 1000ms                               |
| `500`          | Wait: pausa de 500ms (número suelto en la secuencia)        |
| `A*5000`       | Repite A durante 5000ms (intervalo por defecto: 200ms)      |
| `A*1000/10`    | Repite A 10 veces en 1000ms                                 |
| `A*UP`         | Repite A mientras el botón físico esté pulsado              |
| `A*DO`         | Toggle: empieza al pulsar, para al volver a pulsar          |
| `(A, B)*5000`  | Repite la secuencia A,B en bucle durante 5000ms             |
| `(A, B)*UP`    | Repite la secuencia mientras el botón esté pulsado          |
| `(A, B)*DO`    | Toggle de la secuencia                                      |
| `(A, B)*1000/5`| Repite la secuencia 5 veces en 1000ms                       |

**Combinable:** `A, 200, (B + X)*3000` — pulsa A, espera 200ms, luego bucle de B+X durante 3s.

---

## Timings por defecto

| Constante        | Valor  | Descripción                                      |
|------------------|--------|--------------------------------------------------|
| `DEFAULT_STEP_MS`  | 200ms  | Slot de cada ítem en una secuencia (provisional) |
| `DEFAULT_PRESS_MS` | 80ms   | Duración de pulsación dentro del slot            |

> El valor de 200ms es provisional. Medir la pulsación media real del usuario
> y ajustar `DEFAULT_STEP_MS` en `MacroParser.h`.

Para usar una duración diferente a la por defecto: `A=150` (hold+slot = 150ms).

### Los steps de cruceta suprimen el input físico

Cuando un step de macro incluye cualquier token de cruceta (`CU`, `CD`, `CL`, `CR`, `CDR`, …), el macro **toma el control total de la cruceta** durante ese step. El input físico del jugador en la cruceta queda suprimido.

Es intencional: un macro de movimiento necesita controlar la dirección con precisión. Si el jugador va hacia la derecha y el macro manda abajo, el juego debe recibir abajo puro — no la diagonal que produciría un simple OR.

Los macros **sin tokens de cruceta** no se ven afectados — el dpad físico pasa libre.

### Duración mínima por paso en juegos

La mayoría de juegos corren a 60 fps y leen el input una vez por frame (~16ms). **Si un paso dura menos de ~16ms, el juego puede no registrarlo.**

- Mínimo seguro por dirección: **~40ms** (≈ 2–3 frames; margen para jitter del polling)
- Mínimo seguro para la pulsación del botón: **~80ms** (≥ 5 frames)

Para movimientos de juegos de lucha, usar `=` para reducir hold y slot a la vez:

```
CD=80, CDL=50, CL + Y=100
```

> Sin `=`, cada paso dura 200ms por defecto — correcto para menús y cutscenes,
> pero demasiado lento para movimientos especiales. `A=80` fija tanto la duración
> de pulsación como el slot (tiempo antes del siguiente paso) a 80ms.

---

## Ejemplos probados

### Límites de Auron (FFX)
Secuencia de pulsaciones del límite. Ajustar botones y tiempos según la secuencia real del juego.
```json
          "name": "BanishingBlade",
          "execution": "CU, L1, CD, R1, CR, CL, Y"
```

### Lulu Overdrive (FFX)
Gira el analógico derecho en 8 puntos del círculo unidad, ~4 rotaciones/segundo.
Cada posición se mantiene 30ms. Dura 10 segundos y para automáticamente.
```json
		  "name": "LuluOverdrive",
          "execution": "A, 1500,(RAX0+RAY1=17, RAX0.383+RAY0.924=17, RAX0.71+RAY0.71=17, RAX0.924+RAY0.383=17, RAX1+RAY0=17, RAX0.924+RAY-0.383=17, RAX0.71+RAY-0.71=17, RAX0.383+RAY-0.924=17, RAX0+RAY-1=17, RAX-0.383+RAY-0.924=17, RAX-0.71+RAY-0.71=17, RAX-0.924+RAY-0.383=17, RAX-1+RAY0=17, RAX-0.924+RAY0.383=17, RAX-0.71+RAY0.71=17, RAX-0.383+RAY0.924=17)*7000",
```

> Resultado: 10 vueltas conseguidas (máximo manual ~6). ✓

### Presa Soul Calibur
Dos botones simultáneos difíciles de pulsar a la vez en mando de PlayStation.
```json
"execution": "A + Y"
```

### Hadouken (Street Fighter / juegos de lucha)
Mirando a la derecha. Quarter circle forward + puñetazo/patada.
Movimiento de cruceta: abajo → abajo-derecha → derecha + botón.

Cada dirección debe mantenerse el tiempo suficiente para que el juego la registre (≥ 2 frames a 60 fps ≈ 30ms). Usar `=` para reducir el hold y el slot de cada paso.

```
CD=80, CDR=50, CR + X=100
```

Mirando a la izquierda (espejo):
```
CD=80, CDL=50, CL + X=100
```

`CDR` / `CDL` son tokens de diagonal únicos (abajo-derecha / abajo-izquierda) — pulsan ambas direcciones a la vez y **no son lo mismo que `CD + CR`** (que sería un combo simultáneo, no una secuencia).

El valor por defecto de 200ms por paso va bien para el código Konami y menús, pero es demasiado lento para los movimientos especiales de juegos de lucha.

### Código Konami
```json
"execution": "(CU, CU, CD, CD, CL, CR, CL, CR, B, A, ST)*1"
```
> Con `*1` se ejecuta una vez (1ms de tiempo total, la secuencia se completa entera).
> Alternativa sin repeat: `"CU, CU, CD, CD, CL, CR, CL, CR, B, A, ST"`

### Ráfaga de botón (estilo Chun-Li)
Pulsa A repetidamente mientras mantengas el botón físico pulsado.
```json
"execution": "A*UP"
```

### Ráfaga con velocidad exacta
10 pulsaciones de A en 1 segundo.
```json
"execution": "A*1000/10"
```

### Uso del ratón con un analógico
Permite mover el ratón con el analógico derecho del Pro 2 (pulsaciones de botón aparte)
```
        "hid_z":  { "target": "mouse_x", "invert": false, "speed": 15 },
        "hid_rz": { "target": "mouse_y", "invert": false, "speed": 15 },
```

