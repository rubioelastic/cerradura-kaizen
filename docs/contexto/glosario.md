# Glosario — Cerradura Kaizen

## Entidades principales

| Término | Descripción |
|---|---|
| **Matrícula** | Identificador de 8 caracteres leído del bloque 4 (bytes 8-15) de una tarjeta RFID Mifare. Es la unidad de acceso del sistema. |
| **Tarjeta maestra** | Tarjeta RFID cuya matrícula está marcada como maestra en el área de flags de EEPROM (`EEPROM_MAESTRO_FLAGS_BASE`). Abre la cerradura y activa el modo de gestión si se mantiene en el campo. |
| **Matrícula habilitada** | Matrícula almacenada en EEPROM que tiene acceso permitido. |
| **Modo gestión** | Secuencia de 3 niveles activada por la tarjeta maestra: ADD → REMOVE → REMOVE ALL. |
| **Franja horaria** (`AccessSchedule`) | Restricción temporal opcional (horas + días de la semana) que filtra los accesos. Por defecto `enabled = false`. |
| **Estado del espacio** | Valores: `LIBRE` / `OCUPADO`. Controlado por el Bridge vía ESP-NOW; solo el Bridge o una tarjeta maestra pueden liberar. |

---

## Módulos propios

| Nombre | Archivo | Qué es |
|---|---|---|
| `KaizenUI` | `include/ui_display.h` | Clase que gestiona los estados visuales de la pantalla TFT GC9A01. Incluye `UI_STATE_LIBRE` y `UI_STATE_OCUPADO` para integración con ESP-NOW. |
| `KaizenRTC` | `include/rtc_bm8563.h` | Driver para el BM8563. Delega en `M5Dial.Rtc` (no usa `Wire` directo). Lee/escribe fecha-hora y evalúa franjas horarias. |
| `espnow_kaizen` | `include/espnow_kaizen.h` + `src/espnow_kaizen.cpp` | Módulo de comunicación ESP-NOW con el Bridge. |

---

## Hardware

| Sigla / Nombre | Qué es |
|---|---|
| **M5Dial** | Dispositivo compacto circular de M5Stack con pantalla TFT redonda, encoder rotatorio y hardware integrado (RFID, RTC, buzzer). |
| **M5StampS3** | Módulo ESP32-S3 que actúa como MCU dentro del M5Dial. |
| **WS1850S** | Chip lector RFID integrado en el M5Dial, compatible con la API MFRC522, comunicación I2C (dirección `0x28`). |
| **MFRC522** | Chip lector RFID externo usado en el código original (SPI). La API es idéntica a la del WS1850S. |
| **BM8563** | Chip RTC (Real Time Clock) integrado en el M5Dial, compatible con el PCF8563, I2C dirección `0x51`. |
| **GC9A01** | Controlador del display TFT circular 1.28" 240×240 px del M5Dial. |
| **LEDC** | Controlador PWM del ESP32-S3, usado para generar tonos en el buzzer. |
| **BSP** | Board Support Package. La librería `m5stack/M5Dial` que inicializa y abstrae todo el hardware del M5Dial. |
| **NVS** | Non-Volatile Storage. Flash interna del ESP32-S3 donde se emula la EEPROM. |
| **VL flag** | Bit del registro de segundos del BM8563 que indica tensión baja en la batería de botón del RTC (hora no fiable). |
| **Vin** | Pin de alimentación del M5Dial. Acepta 6-36 V DC directamente. |
| **J2** | Conector HY-2.0 de 2 pines del M5Dial que provee +5VOUT y GND para la bobina del relé. |
| **P3** | Header de 2.54 mm del M5Dial con pines GPIO libres (GPIO1 → relé, GPIO2 → pulsador). |
| **NO** | Normalmente Abierto. Contacto del relé que controla la cerradura eléctrica. |

---

## Protocolo ESP-NOW

| Sigla | Valor | Descripción |
|---|---|---|
| `KAIZEN_SYNC` | `0x0C00` | Poll periódico del Bridge para recoger eventos. |
| `KAIZEN_CONFIG` | `0x0C01` | Envío de nombre del espacio + lista de matrículas autorizadas. |
| `KAIZEN_LIBERAR` | `0x0C02` | Fuerza estado LIBRE desde el Bridge. |
| `KAIZEN_OCUPAR` | `0x0C03` | Fuerza estado OCUPADO desde el Bridge. |
| `MENSAJE_COMPLETO_ESPACIO` | `0x00B0` | Mensaje único cíclico que reemplaza SYNC+CONFIG+LIBERAR+OCUPAR. |
| **Bridge** | — | Dispositivo externo que orquesta uno o varios M5Dial vía ESP-NOW. Identifica cada M5Dial por su MAC WiFi STA. |
| **TRM** | — | TiempoRespuestaMantenimiento. Proyecto hermano cuyo protocolo reutiliza la Cerradura Kaizen. Referencia: `Doc_ini/ReactionTime/`. |
| **RNA** | — | [PENDIENTE: definición exacta en el contexto del sistema padre] |
| **`_kBridgeOK`** | `bool` | `true` si se recibió mensaje del Bridge en los últimos 2 min. |
| **`_kModoEstado`** | `bool` | Flag recibido en KAIZEN_CONFIG que indica si el Bridge controla el estado activamente. |

---

## Estados UI (`UIState`)

| Constante | Color | Cuándo |
|---|---|---|
| `UI_STATE_IDLE` | Negro / blanco | Espera con reloj y candado cerrado. |
| `UI_STATE_ACCESS_OK` | Verde | Acceso concedido. |
| `UI_STATE_ACCESS_DENY` | Rojo | Acceso denegado. |
| `UI_STATE_ADD_MODE` | Azul | Modo añadir matrículas (tarjeta maestra activa). |
| `UI_STATE_REMOVE_MODE` | Naranja | Modo eliminar matrículas individuales. |
| `UI_STATE_REMOVE_ALL` | Rojo | Borrar todas las matrículas. |
| `UI_STATE_ERROR` | Amarillo | Error de inicialización (EEPROM, RTC o RFID). |
