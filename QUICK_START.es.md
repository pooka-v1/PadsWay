# PadsWay — Guía de instalación (Quick Start)

Guía rápida para dejar PadsWay funcionando desde cero: instalar los drivers, descomprimir
la app y emparejar tu mando. Para el detalle técnico (formato de `controllers.json`, edición
manual, mapa de botones por mando, etc.) consulta el [README completo](README.es.md).

[Read in English](QUICK_START.md)

---

## Resumen en 4 pasos

1. Instala **ViGEmBus** (obligatorio) y **HidHide** (recomendado).
2. Descomprime el `.zip` de PadsWay en una carpeta con permiso de escritura.
3. Ejecuta `PadsWay.exe` y empareja tu mando con el asistente.
4. Abre tu juego: aparecerá un mando Xbox 360 virtual.

---

## 1. Requisitos

| Requisito | Detalle |
|---|---|
| Sistema | Windows 10 de 64 bits (o posterior) |
| GPU | Cualquiera — la app usa DirectX 11 con fallback a DX10 y a render por software (WARP) |
| Hardware | Modesto. Funciona en equipos de gama baja (probado en Intel Atom de 1,44 GHz con 4 GB de RAM) |
| ViGEmBus | **Obligatorio** — crea el mando Xbox 360 virtual |
| HidHide | **Recomendado** — oculta el mando físico a los juegos para evitar doble entrada |

> ViGEmBus y HidHide son del mismo autor (Nefarius) y se instalan como cualquier driver de
> Windows. PadsWay los controla automáticamente: no tienes que abrir sus interfaces a mano.

---

## 2. Instalar los drivers

Solo se hace una vez.

1. **ViGEmBus** — descarga el instalador desde los releases oficiales:
   https://github.com/nefarius/ViGEmBus/releases
   Ejecútalo y sigue los pasos (pedirá permisos de administrador).

2. **HidHide** — descarga el instalador desde:
   https://github.com/nefarius/HidHide/releases
   Ejecútalo igual. Reinicia el equipo si el instalador lo pide.

> **Versiones validadas con PadsWay:** `ViGEmBus_1.22.0_x64_x86_arm64` y
> `HidHide_1.5.230_x64`. Instala exactamente estas versiones; no actualices los drivers por tu
> cuenta a menos que sepas lo que haces.

---

## 3. Descargar y descomprimir PadsWay

1. Descarga el paquete `PadsWay-vX.Y-win64.zip`.
2. Descomprímelo en una carpeta **con permiso de escritura** — por ejemplo en `Documentos`,
   en el `Escritorio`, o en una carpeta propia tipo `C:\PadsWay`.
   - **No** lo dejes dentro de `C:\Archivos de programa`: ahí Windows (UAC) bloquea el guardado
     de la configuración.
3. PadsWay es **portable**: no instala nada, no toca el registro. Para desinstalarlo basta
   con borrar la carpeta.

La carpeta descomprimida contiene:

```
PadsWay.exe
data/      (textos, layouts, mapa de estados)
images/    (iconos del mando y de las macros)
```

---

## 4. Primer arranque

1. Ejecuta `PadsWay.exe`.
2. La interfaz arranca **en inglés** por defecto. Para cambiarla a español, edita (o crea) el
   fichero `data/virtualpad.json` y deja dentro:

   ```json
   { "locale": "es" }
   ```

   Guarda y vuelve a abrir la app.
3. Al ser una versión limpia, arranca **sin ningún mando configurado**. Es normal: lo
   configuras tú en el paso siguiente.

---

## 5. Emparejar tu mando (Binding Wizard)

El asistente mapea tu mando físico y genera su entrada en `data/controllers.json`.

1. Conecta tu mando físico (por USB o Bluetooth).
2. Abre el **Editor de layouts** y abre el layout que corresponda a tu tipo de mando.
3. Pulsa **Pair controller** (Emparejar mando). El asistente te guía en 5 pasos:
   1. **Seleccionar mando** — elige tu dispositivo de la lista (el mando virtual de ViGEm se
      oculta solo).
   2. **Nombre** — ponle un nombre descriptivo.
   3. **Botones** — el asistente muestra cada botón y te pide pulsar el correspondiente en el
      mando físico.
   4. **Ejes y gatillos** — para cada stick y gatillo, muévelo/aprieta a fondo en la dirección
      que indican las flechas (stick izquierdo X → a la **derecha**; Y → hacia **abajo**;
      gatillos → a fondo).
   5. **Revisar** — comprueba el resultado y guarda.
4. Al guardar se crea/actualiza `data/controllers.json`.

> Si la pestaña **Pads** no muestra el mando tras guardar, **reinicia la app** (problema
> conocido, en investigación).

---

## 6. Usarlo en un juego

1. Con el mando ya emparejado, PadsWay emite un **mando Xbox 360 virtual**.
2. Abre tu juego o Steam: debería detectar un "Xbox 360 Controller".
3. HidHide oculta el mando físico para que el juego no reciba la entrada por duplicado.

---

## 7. (Opcional) Macros y perfiles de juego

- Desde la propia UI puedes crear **macros** y **perfiles por juego**.
- La sintaxis completa de macros está en [MACROS.es.md](MACROS.es.md).
- Habrá un repositorio de referencia con `controllers.json` y perfiles/macros listos por juego
  _(enlace pendiente de publicar)_.

---

## Solución de problemas

| Síntoma | Causa probable / solución |
|---|---|
| El juego no detecta ningún mando | ViGEmBus no está instalado o el equipo no se reinició tras instalarlo |
| El juego recibe entrada doble | Falta HidHide, o no está ocultando el mando físico |
| No se guarda la configuración | La carpeta está en `Archivos de programa`; muévela a una con permiso de escritura |
| El stick izquierdo X no calibra en el asistente | Problema conocido (en investigación); reintenta el paso |
| El mando no aparece tras emparejar | Reinicia la app (la pestaña Pads no refresca sola, en investigación) |
| Arranque con problemas gráficos | El fallback a render por software debería cubrirlo; actualiza los drivers de la GPU |

---

> Documentación técnica completa → [README.es.md](README.es.md) · Sintaxis de macros → [MACROS.es.md](MACROS.es.md)
