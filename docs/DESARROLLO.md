# Cerradura Kaizen — Notas de Desarrollo

> Apuntes personales para futuras modificaciones, depuración y contexto de las decisiones tomadas.

---

## 1. Resumen del proyecto

Migración de un sistema de cerradura RFID basado en ESP32 genérico + MFRC522 SPI al hardware **M5Dial** (M5StampS3 + WS1850S I2C + GC9A01 + BM8563 RTC). Se añadió comunicación ESP-NOW con el Bridge de la infraestructura Kaizen existente.

**Carpeta del proyecto original (referencia)**: `AccesoKaizen.ino` en la raíz del repositorio.  
**Código activo**: `src/main.cpp` + `include/`.

---

## 2. Hardware — detalles críticos

### Bus I2C interno del M5Dial

**El M5Dial NO usa el `Wire` público para su RTC ni su lector RFID.**

Usa un bus I2C interno gestionado por el BSP (`m5::In_I2C`). Si intentas hacer `Wire.beginTransmission(0x51)` para el BM8563 o cualquier dirección de WS1850S, **no funciona**. Esto causa el error "Fallo RTC" que apareció en la primera prueba.

Solución: usar siempre `M5Dial.Rtc` y `M5Dial.Rfid`. El BSP abstrae completamente el hardware.

```cpp
// ✗ Incorrecto — Wire no llega al BM8563 ni al WS1850S
Wire.beginTransmission(0x51);

// ✓ Correcto — delegar en el BSP
M5Dial.Rtc.getDateTime(&d, &t);
M5Dial.Rfid.PICC_IsNewCardPresent();
```

### Tipos del RTC

Los tipos del RTC del M5Unified requieren namespace explícito:

```cpp
m5::rtc_date_t   d;  // year, month, date, weekDay
m5::rtc_time_t   t;  // hours, minutes, seconds
m5::rtc_datetime_t dt;
```

Sin el `m5::` el compilador no los encuentra aunque estés dentro de la clase `KaizenRTC`.

### Cast en MFRC522

`M5Dial.Rfid.PCD_Authenticate()` y `M5Dial.Rfid.MIFARE_Read()` devuelven `uint8_t` (no `MFRC522::StatusCode`). Hay que hacer cast explícito o el compilador da error de conversión:

```cpp
MFRC522::StatusCode status = (MFRC522::StatusCode)M5Dial.Rfid.PCD_Authenticate(...);
```

### GPIO — pines ocupados

- **GPIO1** → relé. No usar para otro propósito.
- **GPIO3** → buzzer PWM. No conectar nada que no tolere señal PWM.
- **GPIO2** (`PULSADOR_PUERTA`) → definido en `pins_config.h` pero **no está en uso** en el código actual. El pulsador se eliminó del diseño final. Está disponible si se quiere reutilizar para otro fin.
- **GPIO11/12** → bus I2C interno. No tocar.

---

## 3. RTC BM8563 — cómo poner en hora

### Situación actual

En `setup()` hay una línea temporal:
```cpp
rtc.setBuildTime();
```

Esta línea **pone el RTC a la hora en que se compiló el firmware** en cada arranque. Es útil para configurar el RTC por primera vez, pero hace que el reloj se resetee a ese momento cada vez que se reinicia el dispositivo.

### Cuándo eliminarla

Una vez que la batería de la pila del RTC esté cargada (o confirmada buena), quitar esa línea. El RTC conservará la hora entre reinicios.

Línea a eliminar en `setup()`:
```cpp
} else {
    // ELIMINAR esta línea cuando el RTC ya tenga la hora correcta:
    rtc.setBuildTime();
}
```

### Si la hora es incorrecta tras un reinicio

Significa que la pila del BM8563 se ha agotado. El BM8563 tiene una pila de reserva en el M5Dial. Comprobar voltaje con `M5Dial.Rtc.getVoltLow()` → si devuelve `true`, la batería está baja (la función `begin()` ya llama a `setBuildTime()` automáticamente en ese caso).

### Cómo ajustar la hora manualmente desde código

```cpp
m5::rtc_date_t d = { .year = 2026, .month = 3, .date = 20, .weekDay = 5 }; // Viernes
m5::rtc_time_t t = { .hours = 14, .minutes = 30, .seconds = 0 };
M5Dial.Rtc.setDateTime(&d, &t);
```

---

## 4. ESP-NOW — estructura interna de `espnow_kaizen.h`

Todo el módulo está en un único header-only para simplicidad. Si el proyecto crece, conviene moverlo a `.cpp`.

### Variables de estado relevantes

| Variable | Tipo | Descripción |
|---|---|---|
| `_kEstado` | `EstadoEspacio` | LIBRE / OCUPADO |
| `_kNombre[32]` | `char[]` | Nombre del espacio asignado por el Bridge |
| `_kOcupante[9]` | `char[]` | Matrícula de quien ocupa (si OCUPADO) |
| `_kBridgeOK` | `bool` | true si recibió mensaje en los últimos 2 min |
| `_kModoEstado` | `bool` | flag recibido en KAIZEN_CONFIG |
| `_kEstadoCambio` | `bool` | flag consume-once para forzar redibujado |
| `_kNEventos` | `uint8_t` | cola de eventos (máx. 8) |

### Flujo de mensajes

```
Bridge                              Cerradura
  │── K_ACK ─────────────────────────► │
  │◄── K_DISCONNECT ──────────────────  │  ← reset secuencias
  │── KAIZEN_CONFIG + matrículas ────► │
  │◄── K_OK ──────────────────────────  │  ← actualiza EEPROM + pantalla
  │── KAIZEN_SYNC ─────────────────► │
  │◄── K_OK + estado + eventos ───────  │  ← vacía cola de eventos
  │── KAIZEN_LIBERAR ───────────────► │
  │◄── K_OK ──────────────────────────  │
```

### Añadir un nuevo comando

1. Definir valor en `#define KAIZEN_NUEVO 0x0C04`.
2. Añadir `case KAIZEN_NUEVO:` en `_kProcesar()` dentro del `switch`.
3. Llamar `_kResponder(K_OK)` o `_kResponder(K_OK, datos, len)`.
4. En el Bridge añadir el envío correspondiente.

### Cola de eventos desbordada

La cola tiene un máximo de 8 eventos (`KAIZEN_MAX_EVENTOS`). Si hay más de 8 eventos sin que el Bridge haga SYNC, los eventos nuevos se descartan silenciosamente. En producción normal no debería ocurrir si el Bridge hace SYNC cada 10–30 s.

---

## 5. UI — pantallas y estados

Todas las pantallas están en `include/ui_display.h`. La clase es `KaizenUI`, instancia `ui`.

### Cómo añadir una pantalla nueva

1. Añadir el valor al enum `UIState` en `ui_display.h`.
2. Crear un método `drawNuevaPantalla(...)` en la clase `KaizenUI`.
3. Asignar `_currentState` al principio del método.
4. Llamar desde `main.cpp` donde sea necesario.

### Actualización parcial de pantalla (sin flicker)

Para evitar parpadeo al actualizar solo el reloj en IDLE y LIBRE, existen métodos `updateIdleTime()` y `updateLibreTime()` que dibujan solo los campos de hora/fecha con fondo del mismo color.

Si se añaden nuevas pantallas con reloj, seguir el mismo patrón: dibujar el fondo del área de texto con el color de fondo antes de escribir el nuevo valor.

### Drawback conocido en pantallas Sprite

Actualmente los dibujos se hacen directamente en el display (`_disp`). Si en el futuro aparece flicker notable, considerar usar un `M5Canvas` como sprite fuera de pantalla y hacer `pushSprite()`. El M5GFX lo soporta nativamente.

---

## 6. EEPROM — layout de memoria

```
Byte 0        : número de matrículas almacenadas (uint8_t, 0–255)
Bytes 1–8     : matrícula #0 (8 bytes ASCII)
Bytes 9–16    : matrícula #1
...
Bytes (n*8+1)–(n*8+8) : matrícula #n
```

Tamaño total reservado: `(255 × 8) + 1 = 2 041 bytes`.

La función `habilitarMatricula()` no comprueba duplicados (es una limitación del código original que se mantuvo). `AddMatricula()` sí comprueba con `Habilitado()` antes de añadir.

### Inicialización en setup

La secuencia:
```cpp
habilitarMatricula("88040220");
{ char _m[] = "88040220"; RemoveMatricula(_m); }
habilitarMatricula("88040220");
```
Es una "chapuza" heredada del código original. Garantiza que la EEPROM tiene al menos una matrícula al arrancar (evita un estado corrupto de contador 0 con datos basura). No tiene efecto secundario en condiciones normales.

---

## 7. Franja horaria de acceso

La variable `franjaAcceso` en `main.cpp` controla el horario de acceso. Por defecto `enabled = false` (sin restricción). Si se activa:

```cpp
AccessSchedule franjaAcceso = {
    .startHour   = 8,
    .startMinute = 0,
    .endHour     = 20,
    .endMinute   = 0,
    .enabledDays = {false, true, true, true, true, true, false}, // Lun–Vie
    .enabled     = true
};
```

La comprobación se hace en `rtc.isAccessAllowed(ahora, franjaAcceso)`. Si el RTC falla y no hay hora válida, `isAccessAllowed` devuelve `true` (por diseño: fallo seguro = acceso permitido).

Pendiente: añadir un comando ESP-NOW para modificar la franja en caliente desde la RNA.

---

## 8. Matrículas maestras hardcodeadas

```cpp
char master[9]  = {'0','0','4','0','5','1','0','6','\0'};  // 00405106
char master2[9] = {'8','8','0','4','0','2','2','0','\0'};  // 88040220
```

Están en `main.cpp`. En el futuro sería mejor moverlas a NVS con lectura en setup, para poder cambiarlas sin recompilar. La tarjeta `88040220` también se añade en EEPROM al arranque (ver sección 6).

---

## 9. Compilación y subida

```bash
# En la carpeta del proyecto:
~/.platformio/penv/bin/pio run --target upload --upload-port /dev/cu.usbmodem101

# Solo compilar (sin subir):
~/.platformio/penv/bin/pio run

# Monitor serie:
~/.platformio/penv/bin/pio device monitor --port /dev/cu.usbmodem101 --baud 115200
```

Puerto habitual en Mac: `/dev/cu.usbmodem101`. Puede variar si hay otros dispositivos conectados.

Tiempos aproximados:
- Solo compilación: ~25 s (primera vez) / ~10 s (incremental)
- Subida: ~15 s

El aviso `TFT_eSPI: TOUCH_CS pin is not set` es previo al proyecto (viene de la librería TFT_eSPI que incluye M5Dial internamente) y es inofensivo.

### `platformio.ini` relevante

```ini
[env:m5stack-stamps3]
platform  = espressif32@6.13.0
board     = m5stack-stamps3
framework = arduino
lib_deps  = M5Dial@^1.0.3
board_build.partitions = default_8MB.csv
board_build.filesystem = littlefs
```

---

## 10. Decisiones de diseño que pueden sorprender

### ¿Por qué no hay pulsador de puerta?

Se eliminó del diseño físico. El GPIO2 (`PULSADOR_PUERTA`) está definido pero no se usa activamente en el loop principal. Si alguien lo conecta, el código del modo de gestión (añadir/eliminar matrículas) contiene referencias a `digitalRead(PULSADOR_PUERTA)` por herencia del código original, pero no hace nada en el flujo normal.

### ¿Por qué la confirmación de liberación tiene 3 s y no más?

Fue una decisión de experiencia de usuario: suficiente para decidir conscientemente, no tan largo que sea molesto en el uso cotidiano. Ajustable cambiando el `3000` (ms) en el bloque de confirmación del loop.

### ¿Por qué el mismo archivo header para todo ESP-NOW?

Para simplicidad de integración en PlatformIO con un solo `#include`. En proyectos más grandes lo ideal sería separar en `.cpp` + `.h`. Si `espnow_kaizen.h` se incluye en más de una unidad de compilación, las variables `static` se duplicarán → problema. Actualmente solo se incluye en `main.cpp`.

### ¿Por qué `kaizen_tick()` en lugar de un task FreeRTOS?

Decisión consciente de mantener el código en un único hilo (loop de Arduino) para evitar problemas de concurrencia con la EEPROM y la UI. El flag `_kMsgRecibido` actúa como señalización simple entre el callback de ESP-NOW (que corre en el hilo Wi-Fi de FreeRTOS) y el loop principal. No usar `_kMsgIn` dentro del callback más allá de copiar datos y poner el flag.

---

## 11. Pendientes conocidos

| Tarea | Prioridad | Notas |
|---|---|---|
| Eliminar `rtc.setBuildTime()` de setup tras confirmar pila RTC | Alta | Ver sección 3 |
| Comando ESP-NOW para modificar franja horaria | Media | Añadir `KAIZEN_HORARIO` |
| Mover matrículas maestras a NVS | Baja | Mayor seguridad |
| Timestamps absolutos en eventos (sincronización de hora con Bridge) | Baja | Ya implementado en TRM, replicable |
| Pantalla de inventario de matrículas habilitadas vía encoder | Baja | `MostrarMatriculasHabilitadas()` solo imprime por serie actualmente |

---

## 12. Historial de bugs resueltos

| Síntoma | Causa | Solución |
|---|---|---|
| Error compilación: conversión `uint8_t → MFRC522::StatusCode` | API de M5Dial devuelve uint8_t | Cast explícito `(MFRC522::StatusCode)` |
| "Fallo RTC" en pantalla al arrancar | `KaizenRTC` usaba `Wire` directo; el M5Dial no expone ese bus | Reescribir delegando en `M5Dial.Rtc` |
| Tipos `rtc_date_t` no reconocidos por el compilador | Falta namespace | Usar `m5::rtc_date_t`, `m5::rtc_time_t` |
| El reloj muestra siempre 00:00 | RTC tenía fecha/hora de epoch (sin batería o nunca configurado) | `setBuildTime()` desde `__DATE__`/`__TIME__` |
| Warning "-Wwrite-strings" en `RemoveMatricula("88040220")` | Literal de string no puede convertirse a `char*` no const | `{ char _m[] = "88040220"; RemoveMatricula(_m); }` |
