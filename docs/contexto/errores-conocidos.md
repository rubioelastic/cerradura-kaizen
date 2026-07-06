# Errores Conocidos — Cerradura Kaizen

---

## E1 · Matrículas maestras parcialmente expuestas en el código fuente

**Archivo:** `src/main.cpp` (`MAESTRAS_DEFECTO[]`)

**Problema:** las matrículas maestras por defecto siguen siendo literales en el código. Ya no son `char master[]` globales (se eliminaron), ahora están en `const char* MAESTRAS_DEFECTO[]`. Se almacenan en EEPROM con flags de maestro, pero los valores originales siguen visibles en el código fuente y en el binario compilado.

**Mejora pendiente:** mover `MAESTRAS_DEFECTO[]` a NVS protegido (ver `docs/DESARROLLO.md` §11).

---

## E2 · "Chapuza primer arranque" — EEPROM inicializada con add+remove+add

**Archivo:** `src/main.cpp` → bloque final de `setup()`

```cpp
habilitarMatricula("88040220");
RemoveMatricula("88040220");
habilitarMatricula("88040220");
```

**Problema:** este bloque se ejecuta en **cada arranque**, no solo la primera vez. Si la EEPROM ya tiene matrículas almacenadas, el conteo del byte 0 aumenta/disminuye/aumenta en cada reset. Puede provocar registros fantasma o corrupción del contador si el ciclo falla a mitad.

**Riesgo:** bajo en condiciones normales (el add comprueba `!Habilitado()` antes de escribir), pero puede dejar la EEPROM en estado inconsistente si se interrumpe la alimentación durante un `EEPROM.commit()`.

---

## E3 · ~~`RemoveAllMatriculas()` vacía en el `.ino` legado~~ — RESUELTO

**Estado:** resuelto. En `src/main.cpp` la función `RemoveAllMatriculas()` escribe `EEPROM.write(0, 0)` correctamente.
El código legado en `Doc_ini/AccesoKaizen.ino` sigue tenêndo el stub vacío, pero ese archivo ya no se compila.

---

## E4 · ~~`Serial.begin()` se llama después de `ui.begin()` en `setup()`~~ — RESUELTO

**Estado:** resuelto. En la versión actual de `src/main.cpp`, `Serial.begin(115200)` es una de las primeras instrucciones de `setup()`.

---

## E5 · `delay()` bloqueante en el loop principal

**Archivo:** `src/main.cpp` → `ReadEmpleado()` y varios puntos del loop

`ReadEmpleado()` incluye `delay(300)` al inicio para detectar retirada de tarjeta. Esto bloquea el loop completo 300 ms en cada llamada, lo que significa que el pulsador y el refresco del reloj no se procesan durante ese tiempo.

**Impacto real:** bajo a frecuencias de uso normales, pero puede hacer que la pantalla idle no se actualice o que el pulsador se pierda durante la lectura RFID.

---

## E6 · Detección de modo gestión basada en timing frágil

**Archivo:** `src/main.cpp` → bloque de gestión en `loop()`

La distinción ADD / REMOVE / REMOVE ALL se basa en cuánto tiempo el master permanece en el campo RF después del primer acceso (`tiempo < millis()` tras esperar `TIEMPO_MODO_MS = 3000 ms`). Si el lector tarda más de lo esperado o la tarjeta se acerca accidentalmente dos veces, puede activarse un modo no deseado.

**No hay confirmación visual:** el modo se activa sin que el usuario lo confirme explícitamente. El encoder rotatorio disponible podría usarse como confirmación, pero no está implementado.

---

## E7 · LVGL declarado en build flags pero no implementado

**Archivo:** `platformio.ini`

```ini
-DLVGL_CONF_INCLUDE_SIMPLE
-DLV_CONF_INCLUDE_SIMPLE
```

Estos flags sugieren que LVGL se consideró. Sin embargo, el código usa M5GFX directamente, no LVGL. Si en algún momento se añade LVGL, hará falta un archivo `lv_conf.h` en `include/`.

---

## E8 · ~~Rama local 5 commits por detrás de `origin/main`~~ — RESUELTO

**Estado:** resuelto con `git pull` (2026-07-06). La rama local está al día con `origin/main`.

---

## E9 · No hay log persistente de accesos

El RTC está disponible y `formatTimestamp()` existe, pero los eventos de acceso solo se imprimen por serial y se envían al Bridge (si hay conexión ESP-NOW). No se almacenan en EEPROM, NVS ni ningún otro medio local persistente. Un corte de luz borra el historial local del dispositivo.

---

## E10 · `rtc.setBuildTime()` en setup resetea el reloj en cada arranque

**Archivo:** `src/main.cpp` → `setup()`

Hay una línea temporal `rtc.setBuildTime()` que pone el RTC a la hora de compilación en cada arranque. Útil para inicializar el RTC, pero hace que el reloj retroceda cada vez que se reinicia el dispositivo.

**Solución:** eliminar esa línea una vez confirmado que la pila del RTC funciona. Ver `docs/DESARROLLO.md` §3.

---

## E11 · `habilitarMatricula()` no comprueba duplicados

**Archivo:** `src/main.cpp`

La función `habilitarMatricula(String m)` heredada del código original escribe directamente sin verificar si la matrícula ya existe. `AddMatricula()` sí comprueba con `Habilitado()`, pero cualquier llamada directa a `habilitarMatricula()` puede generar duplicados en EEPROM.

---

## E12 · Header-only ESP-NOW con riesgo de símbolos duplicados

**Archivo:** `include/espnow_kaizen.h`

El módulo ESP-NOW está implementado como header-only con variables `static`. Si se incluye en más de una unidad de compilación (`.cpp`), las variables de estado se duplicarán silenciosamente. Actualmente solo se incluye en `main.cpp`, lo que es seguro.

**Si el proyecto crece:** mover las implementaciones a `espnow_kaizen.cpp` (ya existe el archivo; mover la lógica interna ahí, ver `docs/DESARROLLO.md` §10).

---

## Bugs ya resueltos (referencia)

Documentados en `docs/DESARROLLO.md` §12:

| Síntoma | Causa | Solución |
|---|---|---|
| Error compilación: conversión `uint8_t → MFRC522::StatusCode` | API de M5Dial devuelve uint8_t | Cast explícito `(MFRC522::StatusCode)` |
| "Fallo RTC" en pantalla al arrancar | `KaizenRTC` usaba `Wire` directo; M5Dial no expone ese bus | Reescribir delegando en `M5Dial.Rtc` |
| Tipos `rtc_date_t` no reconocidos | Falta namespace | Usar `m5::rtc_date_t`, `m5::rtc_time_t` |
| El reloj muestra siempre 00:00 | RTC sin pila o no configurado | `setBuildTime()` desde `__DATE__`/`__TIME__` |
| Warning "-Wwrite-strings" en `RemoveMatricula("88040220")` | Literal no puede convertirse a `char*` | `{ char _m[] = "88040220"; RemoveMatricula(_m); }` |
| `EsMaestra()` devuelve siempre `false` | Tamaño EEPROM incorrecto (2041 en vez de 2297) | Ampliar `EEPROM.begin()` e inicializar flags |
