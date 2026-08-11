# Flujo de Trabajo — Cerradura Kaizen

## Prerrequisitos del entorno

1. **VS Code** con extensión **PlatformIO IDE** instalada.
2. **Placa M5Dial** conectada por USB-C al PC.
3. Entorno `env:m5dial` configurado en `platformio.ini` (ya está).
4. Librería `m5stack/M5Dial @ ^1.0.2` descargada por PlatformIO (automático al compilar).
5. Partición `default_8MB.csv` disponible (incluida en el framework Espressif32).

---

## Hacer un cambio

### 1. Localizar el archivo correcto

| Qué cambiar | Archivo |
|---|---|
| Lógica de acceso, EEPROM, loop principal, ESP-NOW | `src/main.cpp` |
| Pantalla TFT, estados de UI, iconos | `include/ui_display.h` |
| RTC, franjas horarias, timestamps | `include/rtc_bm8563.h` |
| Protocolo ESP-NOW, comandos Bridge | `include/espnow_kaizen.h` + `src/espnow_kaizen.cpp` |
| Pines GPIO, frecuencias I2C | `include/pins_config.h` |
| Librerías, flags de compilación, partición | `platformio.ini` |

> `Doc_ini/AccesoKaizen.ino` es **código legado**. No modificar; existe como referencia histórica.

### 2. Editar

- Añadir `#define` nuevos en `pins_config.h`, no en `main.cpp`.
- Si el cambio afecta a la UI, añadir o modificar un método `draw*()` en `KaizenUI` y actualizar el enum `UIState` si procede.
- Si el cambio afecta al RTC, trabajar dentro de `KaizenRTC`.
- Si el cambio afecta al protocolo ESP-NOW, modificar `espnow_kaizen.h` y añadir el `case` correspondiente en `_kProcesar()`. Ver `docs/DESARROLLO.md` §4.
- **No incluir `espnow_kaizen.h` en más de un `.cpp`** (es header-only; sus variables `static` se duplicarían).

### 3. Compilar

```
PlatformIO: Build   (Ctrl+Alt+B)
```

Verificar que no hay errores. Las advertencias del framework Espressif pueden ignorarse salvo que afecten al código propio.

### 4. Subir el firmware

```
PlatformIO: Upload  (Ctrl+Alt+U)
```

- Puerto: USB-C del M5Dial.
- Velocidad: 921600 bps (configurada en `platformio.ini`).

### 5. Verificar en monitor serie

```
PlatformIO: Monitor (Ctrl+Alt+M)  115200 bps
```

Mensajes esperados en arranque:
```
=== Cerradura Kaizen M5Dial ===
[RFID] WS1850S detectado, versión: 0xXX
[ESP-NOW] Iniciado. MAC: XX:XX:XX:XX:XX:XX
Matricula habilitada 0: 88040220
```

> **Nota sobre RTC:** si la primera línea de `setup()` contiene `rtc.setBuildTime()`, el reloj se reseteará a la hora de compilación en cada arranque. Eliminar esa línea una vez confirmado que la pila del RTC funciona. Ver `docs/DESARROLLO.md` §3.

---

## Checklist de "terminado"

- [ ] El código compila sin errores en `env:m5dial`.
- [ ] La pantalla muestra el splash y pasa a idle con hora.
- [ ] Una tarjeta maestra abre la cerradura (relé activo 3 s).
- [ ] Una tarjeta desconocida muestra ACCESS_DENY en pantalla y suena el buzzer.
- [ ] `MostrarMatriculasHabilitadas()` imprime las matrículas correctas por serial.
- [ ] Si hay Bridge disponible: `kaizen_isBridgeOK()` devuelve `true` tras el primer SYNC.
- [ ] El cambio está documentado en el commit con formato Conventional Commits.

---

## Deploy (producción)

No hay entorno de staging. El deploy es directo al dispositivo físico:

1. Conectar M5Dial por USB-C.
2. `PlatformIO: Upload`.
3. Verificar arranque en monitor serie (115200 bps).
4. Verificar funcionamiento con tarjeta real y Bridge activo.
5. Desconectar USB-C e instalar el dispositivo en su ubicación física.

> OTA vía ESP-NOW está implementado en el protocolo (`UPDATE 0xFFED`), pero el mecanismo de aplicación no está documentado en el repo. [PENDIENTE: confirmar si existe código OTA activo]

---

## Borrar la flash completa (si EEPROM falla al arrancar)

```bash
pio run -t erase
# Luego volver a subir el firmware
pio run -t upload
```

Esto borra todas las matrículas almacenadas. Las maestras se reañaden en el siguiente arranque por el bloque de inicialización de `setup()`.
