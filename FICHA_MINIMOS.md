# AccesoKaizen - Ficha de Mínimos
*Fecha: 2026-06-29 | Estado: Desarrollo | Autor: N/A*

## 1. Propósito y Archivos Base
* **Qué hace:** Controla una cerradura eléctrica mediante tarjetas RFID, validando matrículas almacenadas en EEPROM y activando un relé cuando el acceso está autorizado.
* **Recursos/Rutas:** `c:\Programs\Proyectos\Cerradura Kaizen\`
* **Destinatarios/Notificaciones:** N/A

## 2. Entornos y Accesos
| Entorno | Enlace | ID Entorno / Servidor |
| :--- | :--- | :--- |
| PROD | N/A | `M5Dial (M5StampS3 / ESP32-S3)` — Alimentación: 220 V AC → 12 V DC → Vin M5Dial (6–36 V); +5VOUT → bobina relé 5 V; contacto NO → cerradura 12 V |
| DEV | N/A | `env:m5dial` — PlatformIO / VS Code, upload 921600 bps, monitor 115200 bps |

## 3. Funcionamiento y Arquitectura
* **Origen de Datos:** EEPROM emulada en flash NVS del ESP32-S3 -> `byte 0 = nMatrículas; bytes 1..2040 = matrículas de 8 bytes; máximo 255 entradas`
* **Automatizaciones:**
  * `setup()`: Inicializa BSP M5Dial (`M5Dial.begin(cfg, true, false)`), configura relé GPIO1 (OUTPUT LOW), pulsador GPIO2 (INPUT\_PULLUP), buzzer GPIO3 (LEDC ch0, 8 bit), pantalla KaizenUI (splash 2 s), EEPROM 2041 bytes, RTC BM8563 y verifica RFID WS1850S (`PCD_ReadRegister(VersionReg)` ≠ 0x00/0xFF) -> `src/main.cpp`
  * `loop()`: Refresca reloj en pantalla cada 1000 ms, lee pulsador GPIO2, lee tarjetas RFID, valida acceso (maestras o EEPROM), verifica franja horaria si `franjaAcceso.enabled = true`, gestiona altas/bajas de matrículas y vuelve al estado idle -> `src/main.cpp`
* **Mapa de pines (header P3 / conector J2 del M5Dial):**

| Señal | GPIO | Tipo | Detalle |
| :--- | :---: | :--- | :--- |
| `RELE` | 1 | OUTPUT | Activa relé 5 V (contacto NO → cerradura 12 V) |
| `PULSADOR_PUERTA` | 2 | INPUT\_PULLUP | Apertura manual; espera nivel HIGH en `setup()` |
| `BUZZER_PIN` | 3 | LEDC PWM ch0 | 8 bit; tono OK 1000 Hz, denegado 400 Hz, añadir 1200 Hz, eliminar 600 Hz |
| `I2C_SDA` | 11 | I2C (Wire) | Bus compartido WS1850S (0x28) + BM8563 (0x51) @ 400 kHz |
| `I2C_SCL` | 12 | I2C (Wire) | Bus compartido WS1850S (0x28) + BM8563 (0x51) @ 400 kHz |
| `+5VOUT` (J2) | — | Alimentación | Bobina del relé de 5 V |

* **Componentes:** `m5stack/M5Dial @ ^1.0.2` (incluye `M5Dial.Rfid` WS1850S I2C · `M5Dial.Display` GC9A01 1.28" 240×240 px · `M5Dial.Rtc` BM8563 · `M5Dial.Speaker` buzzer · `M5Dial.Encoder`), `EEPROM` (emulación NVS ESP32-S3), `KaizenUI` (`include/ui_display.h`, 7 estados UI), `KaizenRTC` (`include/rtc_bm8563.h`, driver BM8563 / PCF8563-compatible, estructura `AccessSchedule`)
* **Franja horaria por defecto:** `startHour=7`, `endHour=22`, días Lun–Vie; `franjaAcceso.enabled = false` (sin restricción activa)

## 4. Accesos y Permisos (IDM)
* **Rol Usuarios:** N/A
* **Rol Devs:** N/A
* **Licencias:** N/A

## 5. Guía de Uso Detallada
1. **Prerrequisitos:** VS Code con extensión PlatformIO; placa M5Dial conectada por USB-C; entorno `env:m5dial` y librerías declaradas en `platformio.ini`; partición flash `default_8MB.csv`; tarjetas RFID Mifare compatibles; conexión física según `include/pins_config.h`: fuente 220 V AC → 12 V DC al Vin del M5Dial, bobina relé 5 V al pin +5VOUT del conector J2, cerradura 12 V al contacto NO del relé; matrículas maestras hardcodeadas en `src/main.cpp` (`master[]` = `00405106`, `master2[]` = `88040220`).
2. **Ejecución y Flujo:** Compilar y subir el firmware con PlatformIO (`env:m5dial`). Al arrancar: (1) splash screen 2 s; (2) inicialización EEPROM/RTC/RFID con pantalla de error si alguno falla (el sistema continúa sin ese módulo); (3) pantalla idle con hora y fecha actualizadas cada 1 s. Si se pulsa GPIO2, abre la cerradura directamente. Si se acerca una tarjeta RFID, lee la matrícula (8 bytes), la compara con las maestras o busca en EEPROM, verifica franja horaria si está habilitada, muestra el resultado en pantalla (color según estado `UIState`) y activa el buzzer con el tono correspondiente. Si acceso OK: activa relé GPIO1 durante 3000 ms. Con tarjeta maestra: (1.ª presentación larga → modo ADD, 2.ª → modo REMOVE, 3.ª → modo REMOVE ALL).
3. **Resultado:** Acceso correcto → pantalla verde `UI_STATE_ACCESS_OK`, buzzer 1000 Hz, relé activo 3000 ms, serial `[ACCESO] Cerradura abierta`, vuelta a idle. Acceso denegado → pantalla roja `UI_STATE_ACCESS_DENY`, buzzer 400 Hz, relé sin activar.

## 6. Historial (v1.x.x)
* **Added:** Migración a M5Dial con M5StampS3, WS1850S RFID I2C, pantalla GC9A01 240×240, RTC BM8563, buzzer LEDC y encoder rotatorio — Solicitado por N/A
* **Added:** Interfaz gráfica `KaizenUI` con 7 estados (IDLE, ACCESS\_OK, ACCESS\_DENY, ADD\_MODE, REMOVE\_MODE, REMOVE\_ALL, ERROR) — Solicitado por N/A
* **Added:** Driver `KaizenRTC` para BM8563 con lectura/escritura fecha-hora, flag `VL` de batería baja y estructura `AccessSchedule` para franjas horarias — Solicitado por N/A
* **Added:** Mapa de pines documentado en `include/pins_config.h`; arquitectura de alimentación sin convertidor externo (220 V AC → 12 V DC → Vin M5Dial) — Solicitado por N/A
* **Fixed:** Sustitución del lector MFRC522 SPI externo (GPIO21/22) por `M5Dial.Rfid` (WS1850S I2C GPIO11/12); relé movido de GPIO5 a GPIO1; pulsador de GPIO4 a GPIO2 — Reportado por N/A

## 7. Troubleshooting (Solución de problemas)
* **Fallo EEPROM en arranque:** `EEPROM.begin()` devuelve `false` → Verificar `board_build.partitions = default_8MB.csv` en `platformio.ini`, borrar flash completa (`pio run -t erase`) y volver a subir firmware. El sistema continúa sin persistencia si el fallo no se resuelve.
* **Fallo RTC — no responde:** El BM8563 no responde en dirección `0x51` → Comprobar bus I2C compartido (SDA GPIO11 / SCL GPIO12 @ 400 kHz), verificar que `M5Dial.begin()` se ejecutó antes de `rtc.begin()` y que el pin no está en conflicto.
* **Fallo RTC — batería baja (`VL flag`):** `rtc.isVoltLow()` devuelve `true` → Pantalla muestra "Ajuste la hora RTC"; ajustar mediante `KaizenRTC::setDateTime()` desde serial o desde el encoder. Sustituir batería de botón del M5Dial si el problema persiste.
* **Fallo RFID — versión inválida:** `PCD_ReadRegister(VersionReg)` devuelve `0x00` o `0xFF` → Verificar que `M5Dial.begin()` inicializó correctamente el WS1850S; comprobar continuidad en bus I2C (GPIO11/12); dirección esperada `0x28`.
* **Cerradura no abre:** El relé no se activa → Comprobar GPIO1 con multímetro (debe subir a 3.3 V durante 3000 ms), verificar alimentación 5 V en conector J2 (bobina relé), comprobar que contacto NO del relé está conectado a la cerradura 12 V y que `AbrirCerradura()` llega a ejecutarse (añadir `Serial.println` temporal).
* **Tarjeta denegada inesperadamente:** La matrícula no está en EEPROM o la franja horaria bloquea el acceso → Registrar la tarjeta con tarjeta maestra en modo ADD, o verificar que `franjaAcceso.enabled = false` si no se desea restricción horaria; usar `MostrarMatriculasHabilitadas()` por serial para listar las matrículas almacenadas.
* **Matrículas maestras comprometidas:** `master[]` y `master2[]` están hardcodeadas en `src/main.cpp` como texto plano → Cambiar los valores en el código, recompilar y subir firmware; en versiones futuras migrar a NVS protegido por contraseña.
