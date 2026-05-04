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

### DualSense / DualSense Edge (PS5)
- DualSense estándar: VID 054C / PID 0CE6
- DualSense Edge: VID 054C / PID 0DF2
- Ambos exponen HID raw — protocolo bien documentado por la comunidad
- Touchpad (más grande que DS4) + giroscopio: mismo trabajo que H8
- Gatillos adaptativos: el eje se lee normal; la resistencia háptica requiere output propietario (irrelevante para VirtualPad — solo leemos)
- Pendiente: adquirir hardware para implementar y probar

### Steam Controller 2 (lanzamiento 2026/05/04)
- Cruceta, 2 analógicos, gatillos, muchos botones, 2 touchpads
- Diseño ergonómico en 3 alturas (pad / botones / analógicos)
- Si expone HID raw: soporte sencillo (VID/PID + descriptor + layout)
- Riesgo: Valve puede usar protocolo propietario como hizo con SC1 (requería driver Steam)
- Verificar en Device Manager al conectar: ¿aparece como HID genérico?
- Los 2 touchpads necesitarán diseño equivalente al DS4 (H8 es el precedente)

### Nintendo Joy-Con (L+R)
- VID 057E / PID 2006 (L), 2007 (R) — Bluetooth HID
- Protocolo propietario pero completamente documentado (dekuNukem/Nintendo_Switch_Reverse_Engineering en GitHub)
- IMU 6 ejes en cada mitad, HD rumble, IR camera (R), NFC (R)
- **Reto 1**: Bluetooth HID — hidapi lo soporta en Windows, verificar que el OS lo presenta como HID device
- **Reto 2**: Parser de reports custom (no es gamepad HID estándar) — documentación completa disponible
- **Reto 3** (arquitectónico): dos dispositivos HID separados → un único GamepadState. Misma arquitectura que "jugador en prácticas" (ver sección Multijugador) — implementar una facilita la otra
- **Reto 4**: calibración de fábrica en flash SPI interna, legible por subcomandos HID — el wizard tendría que entenderlo
- Proyecto de varios días pero resultado muy llamativo

### Wii U Pro Controller
- VID 057E / PID 0330, Bluetooth HID, protocolo documentado
- Layout completo: 2 analógicos, cruceta, 4 botones, +/-/Home, ZL/ZR
- ZL/ZR son digitales (no analógicos) — diferente de triggers modernos
- Sin IMU ni sensores especiales — el más sencillo de los Nintendo de esta lista
- Complejidad similar a Joy-Con (BT + custom protocol) pero sin el reto de fusión L+R

### Wiimote / Wiimote Plus
- VID 057E / PID 0306 (ambas versiones), Bluetooth HID
- Wiimote: acelerómetro 3 ejes, cámara IR (apuntado), rumble, altavoz — sin analógicos
- Wiimote Plus: añade giroscopio 3 ejes integrado (MotionPlus built-in)
- La cámara IR requiere sensor bar (barra LEDs IR) para funcionar
- **Sin extensión es inútil como gamepad** — necesita Nunchuk (stick + accel) o Classic Controller (layout completo)
- El sistema de extensiones requiere detección dinámica y parseo adicional
- El más complejo de la lista: IR + extensiones + motion — proyecto largo

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
- **Arquitectura compartida con Joy-Con**: ambas ideas requieren N fuentes físicas → 1 GamepadState virtual. Implementar una facilita la otra.

---

## UI / Calidad de vida

### Layout genérico como fallback
- Crear layout_id `"generic"` para mandos sin layout configurado
- Actualmente si falta layout_id no se muestra nada en la pestaña Pads

### Giroscopio — componente visual tipo bola
- Opción A pendiente: componente `gyro` en PadLayout/PadView
- Muestra orientación como bola en esfera, en lugar del texto actual
- Verificar también si Pro 2 / Pro 3 exponen IMU en D-mode HID
