# Decisiones Técnicas — Cerradura Kaizen

---

## D1 · Migrar de MFRC522 SPI externo a WS1850S I2C integrado

**Decisión:** usar `M5Dial.Rfid` (WS1850S) en lugar de instanciar una librería MFRC522 separada.

**Porqué:** El M5Dial lleva el WS1850S soldado en placa. El BSP expone exactamente la misma API que la librería MFRC522 (`PICC_IsNewCardPresent`, `MIFARE_Read`, etc.), por lo que el cambio es de una línea por llamada. Elimina cables, reduce puntos de fallo y simplifica el `platformio.ini`.

**Descartado:** mantener la librería `miguelbalboa/rfid` externa. Incompatible con el hardware integrado y redundante con lo que provee el BSP.

---

## D2 · Alimentación directa 12 V DC → Vin M5Dial (sin convertidor extra)

**Decisión:** una única fuente 220 V→12 V alimenta el Vin del M5Dial (que acepta 6-36 V). El +5VOUT del conector J2 alimenta la bobina del relé.

**Porqué:** simplifica el cableado, elimina un componente (convertidor 12 V→5 V externo) y reduce puntos de fallo. El regulador interno del M5Dial genera los 5 V necesarios.

**Descartado:** convertidor DC-DC externo 12 V→5 V para alimentar el relé.

---

## D3 · Persistencia en EEPROM emulada (NVS flash), no en SPIFFS/LittleFS

**Decisión:** usar la API `EEPROM.h` (emulación sobre NVS del ESP32-S3), con un layout fijo: byte 0 = contador, bytes 1..N = matrículas de 8 bytes.

**Porqué:** el código original ya usaba EEPROM. Migrar a un sistema de ficheros requeriría reescribir la lógica de lectura/escritura. El tamaño máximo (255 × 8 + 1 = 2041 bytes) es perfectamente manejable en NVS.

**Descartado:** LittleFS / SPIFFS para almacenar las matrículas. Mayor complejidad sin beneficio real a esta escala.

---

## D4 · Reemplazar pulsos del relé como feedback sonoro por buzzer LEDC

**Decisión:** toda la señalización sonora (antes: pulsos del relé de 100 ms) se hace con el buzzer integrado del M5Dial vía `ledcWriteTone()`.

**Porqué:** el relé es un actuador mecánico para la cerradura, no un dispositivo de audio. Usarlo como buzzer provoca desgaste prematuro y ruido indeseable. El M5Dial tiene buzzer nativo.

**Descartado:** mantener los `Pulsos_AddMode()`, `Pulsos_RemoveMode()`, etc. del código original.

---

## D4b · KaizenRTC delega en M5Dial.Rtc (no usa Wire directo)

**Decisión:** la clase `KaizenRTC` usa internamente `M5Dial.Rtc` del BSP en lugar de comunicarse directamente con el BM8563 por `Wire`.

**Porqué:** el M5Dial expone el RTC y el RFID en un bus I2C interno (`m5::In_I2C`) que no es el `Wire` pública del ESP32. Intentar `Wire.beginTransmission(0x51)` directamente no funciona. Esto causó el error "Fallo RTC" documentado en `docs/DESARROLLO.md` §12.

**Descartado:** driver propio sobre `Wire` (la primera versión del código lo intentaba y fallaba).

---

## D5 · Interfaz gráfica TFT con 7 estados de UI

**Decisión:** la clase `KaizenUI` centraliza todos los estados visuales (IDLE, ACCESS_OK, ACCESS_DENY, ADD_MODE, REMOVE_MODE, REMOVE_ALL, ERROR). Cada estado tiene su propia función `draw*()`.

**Porqué:** reemplaza los `Serial.println()` del código original. La pantalla GC9A01 circular de 240×240 px del M5Dial es el canal de feedback principal para el usuario físico.

**Descartado:** LVGL. Se declaró `LVGL_CONF_INCLUDE_SIMPLE` en los build flags, pero en el código actual se usa directamente M5GFX. LVGL no está implementado.

---

## D6 · Bus I2C compartido entre WS1850S (RFID) y BM8563 (RTC)

**Decisión:** ambos chips comparten el mismo bus I2C (GPIO 11/12, 400 kHz). Se diferencian por dirección: RFID `0x28`, RTC `0x51`.

**Porqué:** el M5Dial los conecta así en el esquemático hardware. No es una decisión de software sino una restricción de hardware que el código debe respetar. Se gestiona mediante `Wire` estándar.

---

## D7 · Matrículas maestras migradas a EEPROM (flags de maestro)

**Decisión:** las matrículas maestras ya no son literales hardcodeados. Se definen en `MAESTRAS_DEFECTO[]` y se marcan mediante `MarcarComaMaestra()` en el área de flags (`EEPROM_MAESTRO_FLAGS_BASE = 2042`) en cada arranque. La función `EsMaestra(matricula)` consulta ese área.

**Porqué:** eliminar la exposición directa de las matrículas en el código fuente. Además, el bug `EsMaestra() = false siempre` (por tamaño de EEPROM incorrecto) obligaba a ampliar el área reservada.

**Pendiente:** mover `MAESTRAS_DEFECTO[]` a NVS protegido para que no sean visibles en el binario compilado.

---

## D8 · Agregar ESP-NOW con Bridge externo

**Decisión:** `espnow_kaizen.h` + `espnow_kaizen.cpp` implementan la comunicación con un Bridge externo. El protocolo es compatible con TiempoRespuestaMantenimiento (TRM). El M5Dial se identifica por su MAC WiFi STA (no hay ID fijo en firmware).

**Porqué:** permite gestión remota de matrículas, estado LIBRE/OCUPADO y recogida de eventos (acceso OK, denegado, apertura manual) sin acceso físico al dispositivo.

**Descartado:** MQTT u otro protocolo de red. ESP-NOW es sin infraestructura de red (no necesita router), baja latencia y ya es familiar en el ecosistema Kaizen.

---

## D9 · Eliminar el pulsador de puerta del diseño físico

**Decisión:** `PULSADOR_PUERTA` (GPIO2) permanece definido en `pins_config.h` por compatibilidad pero no se usa en el loop activo.

**Porqué:** se eliminó del diseño físico definitivo. Ver `docs/DESARROLLO.md` §10.

---

## D10 · State machine en confirmación de liberación (sin `while` bloqueante)

**Decisión:** la espera de confirmación del usuario se implementa con variables de estado (`confirmacionActiva`, `confirmStartTime`) en lugar de un bucle `while` bloqueante.

**Porqué:** los `while` bloqueantes impiden que `kaizen_tick()` procese mensajes ESP-NOW durante la espera, lo que rompería la sincronización con el Bridge.
