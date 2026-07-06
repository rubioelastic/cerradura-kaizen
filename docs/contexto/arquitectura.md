# Arquitectura — Cerradura Kaizen

## Stack

| Capa | Tecnología |
|---|---|
| MCU | ESP32-S3 vía M5StampS3 (dentro del M5Dial) |
| Framework | Arduino (PlatformIO, `env:m5dial`) |
| BSP | `m5stack/M5Dial @ ^1.0.2` |
| Gráficos | M5GFX (incluido en M5Dial) + `bodmer/TFT_eSPI @ ^2.5.43` |
| Persistencia | EEPROM emulada en flash NVS del ESP32-S3 |
| Build | PlatformIO / VS Code |
| Comunicación externa | ESP-NOW con Bridge externo (`espnow_kaizen.h/.cpp`) |

---

## Mapa de carpetas

```
platformio.ini        ← configuración de compilación y librerías
FICHA_MINIMOS.md      ← documento de referencia operativa del proyecto
Doc_ini/
  AccesoKaizen.ino    ← código original ESP32+MFRC522 SPI (legado, referencia)
  cerradura electrica.pdf ← esquemático de la cerradura física
  ReactionTime/       ← proyecto relacionado TRM (TiempoRespuestaMantenimiento)
docs/
  DESARROLLO.md       ← apuntes técnicos internos, bugs resueltos, pendientes
  INTEGRADOR.md       ← guía para integrar con el Bridge / RNA
  USUARIO.md          ← manual de uso para el operador del sistema
  db_kaizen.sql       ← esquema de base de datos del sistema Kaizen
  contexto/           ← documentos de contexto generados (este directorio)
.github/
  agents/
    kaizen-firmware.agent.md ← agente de Copilot especializado en el proyecto
include/
  pins_config.h       ← definición de todos los GPIOs y direcciones I2C
  rtc_bm8563.h        ← driver RTC BM8563 (clase KaizenRTC, delega en M5Dial.Rtc)
  ui_display.h        ← módulo de interfaz gráfica TFT (clase KaizenUI, 7+ estados)
  espnow_kaizen.h     ← protocolo ESP-NOW con Bridge externo (header-only)
src/
  main.cpp            ← lógica principal del firmware M5Dial (setup + loop)
  espnow_kaizen.cpp   ← implementación ESP-NOW
```

---

## Hardware

```
220 V AC
  └─► Fuente compacta → 12 V DC
        └─► Vin M5Dial (acepta 6-36 V)
        └─► +5VOUT (conector J2) → bobina relé 5 V
              └─► contacto NO del relé → cerradura 12 V

M5Dial (M5StampS3 / ESP32-S3)
  ├── GC9A01 TFT 240×240 px (SPI, gestionado por BSP)
  ├── WS1850S RFID (I2C 0x28, GPIO 11/12)
  ├── BM8563 RTC  (I2C 0x51, GPIO 11/12, bus compartido con RFID)
  ├── Encoder rotatorio (gestionado por BSP)
  ├── Buzzer LS1 (GPIO 3, LEDC PWM canal 0)
  ├── GPIO 1 → RELÉ (header P3)
  └── GPIO 2 → PULSADOR_PUERTA (header P3, INPUT_PULLUP)
```

---

## Flujo de datos principal

```
setup()
  ├─ M5Dial.begin()         → inicializa BSP (RFID, pantalla, RTC, buzzer, encoder)
  ├─ kaizen_begin()         → WiFi STA + ESP-NOW
  ├─ EEPROM.begin(2297)
  ├─ rtc.begin()            → delega en M5Dial.Rtc (no usa Wire directo)
  ├─ InicializarMaestrasDefecto()
  └─ ActualizarPantallaIdle()

loop()
  ├─ M5Dial.update()
  ├─ kaizen_tick()          → procesa mensajes ESP-NOW entrantes
  ├─ Cada 1 s → rtc.getDateTime() → ui.updateIdleTime() / ui.updateLibreTime()
  ├─ Si kaizen_hayEstadoCambio() → redibujar pantalla de estado
  └─ ReadEmpleado(matricula)
       ├─ M5Dial.Rfid.PCD_Init()
       ├─ PICC_IsNewCardPresent() + PICC_ReadCardSerial()
       ├─ PCD_Authenticate(bloque 4, clave 0xFF×6)
       └─ MIFARE_Read(bloque 4) → bytes 8-15 = matrícula (8 chars)
            ├─ EsMaestra(matricula) → AbrirCerradura() + kaizen_marcarOcupado()
            ├─ Habilitado() → EEPROM → AbrirCerradura() + kaizen_marcarOcupado()
            └─ Desconocida → ui.drawAccessDeny() + Buzzer_AccessDeny()
                              + kaizen_registrarEvento(ACCESO_DENEGADO)
```

---

## Persistencia EEPROM

```
Byte 0          → número de matrículas almacenadas (uint8, máx 255)
Bytes 1..2040   → matrículas, 8 bytes cada una (máx 255 × 8 = 2040)
Byte 2041       → separador (no usado)
Bytes 2042..2296 → flags de maestra por posición (1=es maestra, 0=no); 255 bytes
Total           → 2042 + 255 = 2297 bytes (EEPROM.begin(2297))
```

El campo de flags de maestras (`EEPROM_MAESTRO_FLAGS_BASE = 2042`) fue añadido en el commit
`fix: corregir EEPROM size y lógica de maestras por defecto`. Las matrículas maestras ya no
son literales hardcodeados en el código: se cargan de `MAESTRAS_DEFECTO[]` y se marcan con
`MarcarComaMaestra()` en cada arranque.

---

## Qué NO existe

- Tests (unitarios ni de integración)
- CI/CD
- Logs persistentes de acceso (solo Serial)
- Interfaz para ajustar la hora RTC desde UI (se usa `rtc.setBuildTime()` temporal en setup)
- Uso real del encoder rotatorio (inicializado por BSP, sin lógica propia en `loop()`)
- `rfid_ws1850s.h` (mencionado en `platformio.ini` como no usado)
- **`PULSADOR_PUERTA` (GPIO2)**: definido en `pins_config.h` pero **eliminado del diseño físico**;
  no se usa en el loop activo (ver `docs/DESARROLLO.md` §10)
