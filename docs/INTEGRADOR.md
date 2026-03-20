# Cerradura Kaizen — Documentación Técnica para el Integrador

> **Versión firmware**: 1  
> **Hardware**: M5Dial (M5StampS3 + GC9A01 + WS1850S + BM8563)  
> **Protocolo**: ESP-NOW canal 1, misma capa que TiempoRespuestaMantenimiento  
> **Última compilación verificada**: firmwire 850 976 bytes, ~13% flash  

---

## 1. Arquitectura general del sistema

```
RNA / Servidor Kaizen
        │
     [Bridge]  ←──────── misma infraestructura que TRM
        │  ESP-NOW canal 1
        ▼
  [M5Dial Cerradura]
   · WS1850S (RFID I2C)
   · Relé GPIO1  → cerradura eléctrica 12 V
   · BM8563 (RTC)
   · GC9A01 (pantalla 240×240)
```

El Bridge **identifica** este dispositivo por su dirección MAC Wi-Fi STA. La MAC se imprime en el monitor serie en cada arranque:

```
[ESPNOW] MAC: XX:XX:XX:XX:XX:XX
```

> **Acción requerida**: registrar esa MAC en el Bridge la primera vez que se conecte el dispostiivo.

---

## 2. Protocolo ESP-NOW

### 2.1 Estructura de trama

Idéntica a TiempoRespuestaMantenimiento (TRM):

| Byte 0 | Byte 1 | Bytes 2–3 | Bytes 4…N |
|---|---|---|---|
| SEQ | CRC | CMD (little-endian) | DATA |

- **SEQ**: número de secuencia incremental (wraps 1→254, salta el 0 y 255).
- **CRC**: suma `SEQ + CMD_low + CMD_high + DATA[0..n]` en `uint8_t`.
- **CMD**: identificador del comando 16 bits.
- **DATA**: payload variable.

### 2.2 Handshake inicial

1. Bridge envía `K_ACK` (0xFFF8) con cualquier SEQ.
2. Cerradura responde `K_DISCONNECT` (0xFFFB) → resetea sus contadores de secuencia.
3. A partir de ese momento, SEQ debe ser incremental.

### 2.3 Timeout

Si la cerradura no recibe ningún mensaje del Bridge durante **2 minutos** (`KAIZEN_TIMEOUT_BRIDGE_MS = 120 000 ms`), pone `bridgeOK = false` y la pantalla vuelve al modo reloj sin estado.

---

## 3. Comandos Bridge → Cerradura

### 3.1 `KAIZEN_SYNC` — 0x0C00

Polling periódico. El Bridge lo envía para obtener el estado actual y los eventos pendientes.

**DATA de envío**: ninguno (solo cabecera de 4 bytes).

**DATA de respuesta** (`K_OK`):

| Offset | Tamaño | Descripción |
|---|---|---|
| 0 | 1 byte | Estado: `0x00` = LIBRE, `0x01` = OCUPADO |
| 1 | 8 bytes | Matrícula del ocupante (ASCII, rellenado con `\0`) |
| 9 | 1 byte | Número de eventos en cola (`n`) |
| 10 + (i·13) | 1 byte | Tipo de evento `i` (ver tabla 4.1) |
| 11 + (i·13) | 8 bytes | Matrícula del evento `i` |
| 19 + (i·13) | 4 bytes | Timestamp del evento `i` (`millis()`, little-endian) |

Una vez la cerradura recibe el `K_OK` de vuelta del Bridge, **vacía la cola de eventos**. Si el Bridge no confirma (envío fallido), los eventos se mantienen en cola hasta el siguiente SYNC.

**Periodicidad recomendada**: cada 10–30 segundos.

---

### 3.2 `KAIZEN_CONFIG` — 0x0C01

Configura el nombre del espacio y la lista de matrículas autorizadas. La cerradura **reemplaza** su lista EEPROM completa.

**DATA de envío**:

| Offset | Tamaño | Descripción |
|---|---|---|
| 0 | 1 byte | `flags`: bit 0 = `modo_estado` |
| 1 | 1 byte | Longitud del nombre del espacio (`len_nombre`, máx. 31) |
| 2 | `len_nombre` bytes | Nombre del espacio (ASCII sin `\0`) |
| 2 + len_nombre | 1 byte | Número de matrículas (`n_mats`, máx. 255) |
| 3 + len_nombre | `n_mats × 8` bytes | Matrículas (8 bytes ASCII cada una, sin `\0`) |

**flags bit 0 — modo_estado:**
- `0` → solo acceso: la cerradura abre/deniega pero no gestiona el estado LIBRE/OCUPADO.
- `1` → acceso + estado: la cerradura muestra LIBRE/OCUPADO en pantalla y registra quién ocupa el espacio.

**DATA de respuesta**: `K_OK` sin datos.

**Efecto en la cerradura**:
1. Establece `_kModoEstado`.
2. Guarda el nombre del espacio en `_kNombre[32]`.
3. Llama al callback de configuración → borra EEPROM y escribe las nuevas matrículas.
4. Fuerza redibujado de pantalla.

---

### 3.3 `KAIZEN_LIBERAR` — 0x0C02

Fuerza el estado del espacio a **LIBRE** desde la RNA (fin de ensayo, cancelación de reserva, etc.).

**DATA de envío**: ninguno.

**DATA de respuesta**: `K_OK` sin datos.

**Efecto**: limpia el ocupante, pone estado LIBRE, redibuja pantalla.

---

### 3.4 `KAIZEN_OCUPAR` — 0x0C03

Fuerza el estado del espacio a **OCUPADO** desde la RNA (reserva anticipada, etc.).

**DATA de envío** (opcional):

| Offset | Tamaño | Descripción |
|---|---|---|
| 0 | 8 bytes | Matrícula del ocupante (opcional; si se omite, ocupante queda vacío) |

**DATA de respuesta**: `K_OK` sin datos.

---

### 3.5 `K_ASK_VERSION` — 0xFFEE

Solicita la versión de firmware.

**DATA de respuesta** (`K_OK`): 4 bytes big-endian con `KAIZEN_FIRMWARE_VERSION` (actualmente `0x00000001`).

---

## 4. Tipos de evento (cola KAIZEN_SYNC)

| Valor | Nombre | Descripción |
|---|---|---|
| `0x01` | `ACCESO_OK` | Tarjeta válida presentada → puerta abierta |
| `0x02` | `ACCESO_DENEGADO` | Tarjeta no autorizada o fuera de horario |
| `0x03` | `APERTURA_MANUAL` | Pulsador interior pulsado (apertura sin RFID) |

El campo `timestamp` es el valor de `millis()` en el momento del evento. **No es una hora real**. Para correlacionar con hora real, el Bridge debe sumar la diferencia desde el último SYNC.

> **Nota para el integrador**: Si necesitáis timestamps absolutos, en TRM se implementó una sincronización de hora — es factible añadirla a Cerradura Kaizen si se requiere.

---

## 5. Comportamiento de la lógica de acceso

### 5.1 Tarjeta maestra

Las tarjetas maestras tienen acceso siempre (sujeto a la franja horaria configurada localmente):
- Abren la puerta.
- Si `modo_estado = true` y el espacio está OCUPADO → **libera** el espacio automáticamente.
- Si el acceso es denegado por horario → se registra `ACCESO_DENEGADO`.

### 5.2 Tarjeta habilitada — modo solo acceso (`flags bit0 = 0`)

Abre sin modificar el estado LIBRE/OCUPADO. No se envía información de ocupación al Bridge.

### 5.3 Tarjeta habilitada — modo estado (`flags bit0 = 1`)

| Situación | Resultado |
|---|---|
| Espacio LIBRE | Abre + marca OCUPADO con esa matrícula |
| Espacio OCUPADO + misma persona | Muestra pantalla de confirmación 3 s. Toque = libera + abre. Timeout = entra de nuevo sin cambiar estado |
| Espacio OCUPADO + persona diferente | Solo abre; estado sigue OCUPADO por la persona original |

### 5.4 Tarjeta desconocida

Deniega, muestra pantalla ACCESS DENY, registra `ACCESO_DENEGADO`.

---

## 6. Matrículas: formato y almacenamiento

- **Formato**: 8 bytes ASCII, sin terminador. La cerradura las maneja internamente con `\0` en posición 9 (`char[9]`).
- **Almacenamiento local**: EEPROM emulada en flash NVS. Byte 0 = contador. Bytes 1…N = matrículas de 8 bytes consecutivas. Máximo 255 matrículas.
- **Lectura RFID**: la matrícula se lee del bloque 4 de la tarjeta MIFARE, bytes 8–15. Clave de autenticación: `0xFF 0xFF 0xFF 0xFF 0xFF 0xFF` (clave de fábrica), Key A.
- **Sincronización**: cuando el Bridge envía `KAIZEN_CONFIG`, la cerradura **borra toda la EEPROM** y escribe la nueva lista. Las matrículas maestras (`00405106`, `88040220`) están hardcodeadas en firmware y no se ven afectadas por `KAIZEN_CONFIG`.

---

## 7. Gestión local de matrículas (sin Bridge)

La cerradura puede funcionar en modo autónomo. Con la tarjeta maestra se accede al menú de gestión:

1. Presentar tarjeta maestra → puerta abre.
2. Mantener tarjeta maestra en el campo RF **sin retirarla** durante la lectura → **modo AÑADIR** (pitido 2 tonos, pantalla azul).
3. Mantener de nuevo → **modo ELIMINAR** (pitido 3 tonos, pantalla naranja).
4. Mantener de nuevo → **BORRAR TODAS** (alarma descendente, pantalla roja).

En modo AÑADIR/ELIMINAR, cada tarjeta que se acerque se añade o elimina. El modo expira tras 3 s sin actividad.

---

## 8. Franja horaria de acceso

Configurable en código (variable `franjaAcceso` en `main.cpp`). Por defecto está deshabilitada (`enabled = false`), lo que equivale a acceso 24/7. Si se habilita, solo se permite el acceso entre `startHour:startMinute` y `endHour:endMinute` en los días marcados en `enabledDays[7]` (Domingo=0, Lunes=1, …, Sábado=6).

> Actualmente no existe un comando ESP-NOW para modificar la franja horaria en caliente. Si se requiere, añadir un comando `KAIZEN_HORARIO` con el payload de `AccessSchedule`.

---

## 9. Pines GPIO relevantes

| Pin | Función | Notas |
|---|---|---|
| GPIO1 | Relé (RELE) | Activo HIGH, 3 s por defecto |
| GPIO3 | Buzzer (PWM LEDC canal 0) | — |
| GPIO11 | I2C SDA (interno M5Dial) | Bus compartido: RTC + RFID |
| GPIO12 | I2C SCL (interno M5Dial) | Bus compartido: RTC + RFID |

> GPIO2 (`PULSADOR_PUERTA`) está definido en `pins_config.h` pero no se usa en la versión actual. El pulsador de puerta fue eliminado del diseño final.

---

## 10. Parámetros de configuración en firmware

Los siguientes valores son constantes en `main.cpp`. Para modificarlos hay que recompilar:

| Constante | Valor actual | Descripción |
|---|---|---|
| `MAX_MATRICULAS` | 255 | Máximo de tarjetas en EEPROM |
| `TIEMPO_ABIERTA_MS` | 3000 ms | Tiempo que el relé permanece activo |
| `TIEMPO_MODO_MS` | 3000 ms | Timeout de los modos de gestión |
| `KAIZEN_TIMEOUT_BRIDGE_MS` | 120 000 ms | Tiempo sin mensaje antes de declarar Bridge perdido |
| `KAIZEN_WIFI_CHANNEL` | 1 | Canal Wi-Fi ESP-NOW |
| `KAIZEN_FIRMWARE_VERSION` | 1 | Versión que responde a `K_ASK_VERSION` |

---

## 11. Respuestas de error del protocolo

| CMD | Código | Significado |
|---|---|---|
| `K_OK` | 0xFFFE | Operación correcta |
| `K_BAD_CRC` | 0xFFF3 | CRC de la trama no coincide |
| `K_BAD_SECUENCE` | 0xFFF4 | Número de secuencia inesperado |
| `K_DISCONNECT` | 0xFFFB | Reset de secuencias (respuesta al primer K_ACK) |
| `K_NOTFOUND` | 0xFFF6 | Comando no reconocido |

---

## 12. Arranque — checklist de integración

- [ ] Conectar M5Dial, abrir monitor serie (115 200 baud).
- [ ] Anotar la MAC impresa: `[ESPNOW] MAC: XX:XX:XX:XX:XX:XX`.
- [ ] Registrar esa MAC en el Bridge como dispositivo Cerradura Kaizen.
- [ ] Enviar `K_ACK` desde el Bridge para iniciar handshake.
- [ ] Enviar `KAIZEN_CONFIG` con el nombre del espacio, las matrículas autorizadas y el flag de modo.
- [ ] Verificar que la cerradura responde `K_OK` y actualiza la pantalla con el nombre del espacio.
- [ ] Enviar `KAIZEN_SYNC` periódico para mantener `bridgeOK = true`.
- [ ] Verificar timeout: detener el Bridge 2 min → la pantalla debe volver al modo reloj simple.
