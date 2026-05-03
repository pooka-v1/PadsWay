# IDEAS — VirtualPad
> Backlog de ideas futuras sin fecha. Añadir aquí cuando surja algo durante el desarrollo.
> No son compromisos — son recordatorios de posibilidades.

---

## Sesión de diseño de UI (pendiente)
Antes de añadir soporte UI para nuevos componentes (giroscopio, touchpad surface, etc.),
hacer una sesión de diseño equivalente a la del Component System (2026/04/21):
- Jerarquía de componentes UI (qué clases, qué responsabilidades)
- Cómo el MappingEditor crece sin convertirse en God Object de nuevo
- Diseño de pantallas para los nuevos componentes ricos (touchpad gestures, gyro)

---

## Macros — UI de creación (futura fase)

### Pasos de teclado y ratón en macros
Las macros en `macros.json` hoy definen secuencias de botones de gamepad virtual.
Cuando hagamos la UI de creación de macros, añadir soporte para pasos de tipo teclado y ratón:
- `{ "type": "keyboard", "key": "a" }` en la secuencia
- Permite hacer a → b → a → b uno tras otro (distinto de combo simultáneo)
- MacroParser / Macro.h necesitarían entender esos pasos además de botones Xbox.
- Abordar cuando llegue la tarea de UI de macros, no antes.

---

## Hardware adicional

### 8BitDo GBros adapter
- VID/PID compartido GameCube/Classic Controller
- Discriminar por nº de ejes o selección manual al emparejar

### DS4 Touchpad — mappings adicionales (tras E4 BT)
- Zonas de toque como botones (mitad izq. / mitad der.)
- Emulación stick analógico derecho
- Gestos (swipe, dos dedos)
- Combos de dos dedos

---

## Output / Virtualización

### Output DInput virtual (ViGEm DS4)
- Útil para juegos viejos (GOG, itch.io, emuladores) que esperan DInput en lugar de XInput

### Mando virtual seleccionable
- Permitir cambiar el layout del virtual por cualquier mando con al menos los botones Xbox
- Actualmente siempre muestra `xbox_one`

---

## Multijugador / colaboración

### Jugador en prácticas
- Mando físico 2 puede tomar el control del mando virtual compartido
- Caso de uso: jugador veterano ayuda a novato (o padre con niño)
- El mando "auxiliar" tiene prioridad temporal mientras mantiene pulsado un botón de "tomar control"

---

## UI / Calidad de vida

### Layout genérico como fallback
- Crear layout_id `"generic"` para mandos sin layout configurado
- Actualmente si falta layout_id no se muestra nada en la pestaña Pads

### Giroscopio — componente visual tipo bola
- Opción A pendiente: componente `gyro` en PadLayout/PadView
- Muestra orientación como bola en esfera, en lugar del texto actual
- Verificar también si Pro 2 / Pro 3 exponen IMU en D-mode HID
