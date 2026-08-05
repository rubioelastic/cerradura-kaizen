// ============================================================
// main.cpp — Cerradura Kaizen para M5Dial
// "Sistema RFID de Acceso Adaptativo" — Versión M5Dial
//
// DESCRIPCIÓN DE LA MIGRACIÓN:
//   Proyecto original:
//     • ESP32 genérico
//     • MFRC522 externo via SPI (SS=GPIO21, RST=GPIO22)
//     • Relé en GPIO5, Pulsador en GPIO4
//     • Interfaz: Solo Monitor Serie (Serial.print)
//     • Feedback sonoro: pulsos en el relé
//     • Sin RTC, sin pantalla
//
//   Proyecto migrado (M5Dial):
//     • M5StampS3 (ESP32-S3)
//     • WS1850S integrado via I2C (SDA=GPIO11, SCL=GPIO12)
//     • Relé en GPIO1 (header P3), Pulsador en GPIO2 (header P3)
//     • Interfaz: Pantalla TFT redonda GC9A01 240×240 px
//     • Feedback sonoro: buzzer integrado (GPIO3, PWM)
//     • RTC BM8563 para timestamps y control de horarios
//     • Encoder rotatorio gestionado por el BSP de M5Dial
//
// ALIMENTACIÓN SIMPLIFICADA (sin convertidor 12V→5V externo):
//   Fuente compacta 220V AC → 12V DC
//     └─► M5Dial (acepta 6–36V DC directamente en Vin)
//     └─► Relé de 5V ← +5VOUT del M5Dial
//     └─► Cerradura 12V ← contacto NO del relé
//
// ESTRUCTURA DEL LOOP PRINCIPAL (preserva lógica original):
//   1. Espera en idle mostrando hora/fecha
//   2. Lee tarjeta RFID (WS1850S I2C)
//   3. Si es tarjeta maestra: entra en modo gestión
//        - 1ª presentación larga → modo AÑADIR tarjetas
//        - 2ª presentación larga → modo ELIMINAR tarjetas
//        - 3ª presentación larga → BORRAR TODAS las tarjetas
//   4. Si es tarjeta habilitada → abre cerradura
//   5. Si es tarjeta desconocida → acceso denegado
//   6. El pulsador físico siempre abre la cerradura
// ============================================================

// ─────────────────────────────────────────────────────────────
// INCLUDE — Librerías
// ─────────────────────────────────────────────────────────────

// ELIMINADO del código original:
//   #include <SPI.h>         → ya no se usa SPI para el RFID
//   #include <MFRC522.h>     → el BSP M5Dial ya provee M5Dial.Rfid (MFRC522-compatible)
// MANTENIDO del código original:
//   #include <EEPROM.h>      → compatible con ESP32-S3 (emulación en flash)
// AÑADIDO nuevo:
//   #include <M5Dial.h>      → BSP completo del M5Dial:
//                               · M5Dial.Display  → pantalla GC9A01
//                               · M5Dial.Rfid     → WS1850S via I2C (API MFRC522)
//                               · M5Dial.Speaker  → buzzer integrado
//                               · M5Dial.Encoder  → encoder rotatorio
//                               · M5Dial.Rtc      → RTC BM8563
//   #include "ui_display.h"  → módulo de interfaz gráfica TFT (propio)
//   #include "rtc_bm8563.h"  → driver RTC BM8563 (propio, complementa M5Dial.Rtc)
//   #include "pins_config.h" → mapa de pines del M5Dial
//
// NOTA: rfid_ws1850s.h NO se incluye aquí.
//   El M5Dial BSP expone M5Dial.Rfid que es un objeto MFRC522 ya configurado
//   para comunicarse internamente con el WS1850S por I2C.
//   Usar M5Dial.Rfid es equivalente a usar mfrc522 en el código original,
//   con la misma API: PICC_IsNewCardPresent(), PCD_Authenticate(), MIFARE_Read()…

#include <M5Dial.h>           // BSP M5Dial
#include <WiFi.h>             // WiFi.macAddress()
#include <Preferences.h>      // estado de ocupación persistente

#include "pins_config.h"      // Definición de pines GPIO del M5Dial
#include "ui_display.h"       // Módulo de interfaz gráfica TFT
#include "rtc_bm8563.h"       // Módulo RTC BM8563
#include "espnow_kaizen.h"    // Comunicación ESP-NOW con el Bridge

// ─────────────────────────────────────────────────────────────
// CONSTANTES DE CONFIGURACIÓN
// ─────────────────────────────────────────────────────────────

// Tiempo que la cerradura permanece abierta (ms)
#define TIEMPO_ABIERTA_MS  3000

// Frecuencias del buzzer
#define FREQ_ACCESS_OK     1000
#define FREQ_ACCESS_DENY    400
#define FREQ_BEEP_SHORT     800

// Timeout de operaciones RFID (ms)
#define RFID_TIMEOUT_MS    1000

// Tiempo mínimo entre dos lecturas de la misma matrícula (ms)
#define DEDUP_MS           3000

// ─────────────────────────────────────────────────────────────
// INSTANCIAS DE LOS MÓDULOS
// ─────────────────────────────────────────────────────────────

// ELIMINADO del código original:
//   MFRC522 mfrc522(SS_PIN, RST_PIN); → reemplazado por M5Dial.Rfid
//   MFRC522::MIFARE_Key key;          → se usa MFRC522::MIFARE_Key directamente
//
// M5Dial.Rfid ES el módulo WS1850S integrado en el M5Dial.
// El BSP lo configura automáticamente al llamar M5Dial.begin().
// Su API es 100% idéntica a MFRC522:
//   M5Dial.Rfid.PICC_IsNewCardPresent()
//   M5Dial.Rfid.PICC_ReadCardSerial()
//   M5Dial.Rfid.PCD_Authenticate()
//   M5Dial.Rfid.MIFARE_Read()
//   M5Dial.Rfid.PICC_HaltA()
//   M5Dial.Rfid.PCD_StopCrypto1()
//   M5Dial.Rfid.PCD_Reset()
// No hace falta instanciar nada; M5Dial.Rfid ya existe tras M5Dial.begin().

// NUEVO — Pantalla TFT del M5Dial (M5GFX gestionada por BSP)
KaizenUI ui(M5Dial.Display);

// NUEVO — RTC BM8563 (delegado en M5Dial.Rtc, ya inicializado por el BSP)
KaizenRTC rtc;

// ─────────────────────────────────────────────────────────────
// PROTOTIPOS DE FUNCIONES
// ─────────────────────────────────────────────────────────────
bool  SonIguales(char *m1, char *m2);
void  ReadEmpleado(char *empleado);
bool  Es0(char *matricula);
void  AbrirCerradura();
void  AbrirCerraduraRemota();
void  AccesoOcupacion(const char *matricula, uint32_t epoch);
void  GuardarEstadoSala();
void  CopiarMatricula(char *origen, char *destino);
void  Buzzer_AccessOK();
void  Buzzer_AccessDeny();
void  Buzzer_Beep(uint32_t freq, uint32_t durMs);
void  ActualizarPantallaIdle();

// ─────────────────────────────────────────────────────────────
// VARIABLES GLOBALES
// ─────────────────────────────────────────────────────────────
unsigned long ultimaActualizacionHora = 0;
const unsigned long INTERVALO_HORA_MS = 1000;

// Deduplicación: ignorar misma matrícula dentro de DEDUP_MS
char     lastMatricula[9] = "00000000";
uint32_t lastMatriculaMs  = 0;

// Estado de ocupación (persistente en NVS)
Preferences salaPrefs;
bool        salaOcupada = false;

// ============================================================
//  S E T U P
// ============================================================
void setup() {
    auto cfg = M5.config();
    M5Dial.begin(cfg, true, false);

    pinMode(RELE, OUTPUT);
    digitalWrite(RELE, LOW);

    ledcSetup(BUZZER_CHANNEL, FREQ_BEEP_SHORT, BUZZER_RESOLUTION);
    ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);

    ui.begin();

    if (!rtc.begin()) {
        ui.drawError("Fallo RTC");
        Buzzer_AccessDeny();
        delay(3000);
    } else {
        rtc.setBuildTime();
    }

    Serial.begin(115200);
    Serial.println("=== Cerradura Kaizen M5Dial ===");

    byte rfidVersion = M5Dial.Rfid.PCD_ReadRegister(MFRC522::VersionReg);
    if (rfidVersion == 0x00 || rfidVersion == 0xFF) {
        ui.drawError("Fallo RFID");
        Buzzer_AccessDeny();
        delay(3000);
    } else {
        Serial.printf("[RFID] WS1850S detectado, versión: 0x%02X\n", rfidVersion);
    }

    if (!kaizen_begin()) {
        ui.drawError("Fallo ESP-NOW");
        delay(2000);
    }

    salaPrefs.begin("sala", true);
    salaOcupada = salaPrefs.getBool("ocup", false);
    salaPrefs.end();
    Serial.printf("[SALA] Estado recuperado: %s\n", salaOcupada ? "OCUPADA" : "LIBRE");

    {
        String macAddr = WiFi.macAddress();
        Serial.printf("[MAC] %s\n", macAddr.c_str());
        ui.drawIdle("--:--", macAddr.c_str());
        delay(4000);  // mostrar MAC 4 segundos
    }

    ActualizarPantallaIdle();
}

// ============================================================
//  L O O P
// ============================================================
void loop() {
    M5Dial.update();
    kaizen_tick();

    if (kaizen_hayEstadoCambio()) {
        ActualizarPantallaIdle();
    }

    // ── Apertura remota solicitada por el Bridge
    if (kaizen_hayAperturaRemota()) {
        AbrirCerraduraRemota();
        return;
    }

    if (millis() - ultimaActualizacionHora > INTERVALO_HORA_MS) {
        ultimaActualizacionHora = millis();
        if (ui.getState() == UI_STATE_IDLE) {
            ActualizarPantallaIdle();
        }
    }

    // ── Leer tarjeta RFID
    char matricula[9] = {'0','0','0','0','0','0','0','0','\0'};
    ReadEmpleado(matricula);

    if (Es0(matricula)) return;

    // ── Deduplicación: ignorar misma matrícula dentro de DEDUP_MS
    if (SonIguales(lastMatricula, matricula) &&
        (millis() - lastMatriculaMs) < DEDUP_MS) {
        return;
    }
    CopiarMatricula(matricula, lastMatricula);
    lastMatricula[8] = '\0';
    lastMatriculaMs  = millis();

    ui.drawReadingCard(matricula);
    delay(100);

    // ── Obtener timestamp
    DateTime ahora = rtc.getDateTime();
    uint32_t epoch = rtc.toEpoch(ahora);
    char ts[8];
    rtc.formatTime(ahora, ts, sizeof(ts));

    // ── Verificar autorización
    bool autorizado = kaizen_accesoLibre() || kaizen_estaAutorizado(matricula);

    // ── MODO APERTURA (clásico)
    if (!kaizen_modoOcupacion()) {
        kaizen_registrarFichaje(matricula, autorizado, epoch, KAIZEN_EVENTO_ACCESO);
        if (autorizado) {
            ui.drawAccessOK(matricula, ts);
            Buzzer_AccessOK();
            AbrirCerradura();
        } else {
            ui.drawAccessDeny(matricula, ts);
            Buzzer_AccessDeny();
            delay(2000);
            ActualizarPantallaIdle();
        }
        return;
    }

    // ── MODO OCUPACIÓN
    if (!autorizado) {
        kaizen_registrarFichaje(matricula, false, epoch, KAIZEN_EVENTO_ACCESO);
        ui.drawAccessDeny(matricula, ts);
        Buzzer_AccessDeny();
        delay(2000);
        ActualizarPantallaIdle();
        return;
    }
    // Tarjeta autorizada: siempre abre; mantener 5 s alterna ocupar/liberar
    AccesoOcupacion(matricula, epoch);
}



// ============================================================
//  F U N C I O N E S   D E   A C C E S O
// ============================================================

// ─────────────────────────────────────────────────────────────
// AbrirCerradura — activa el relé para abrir la cerradura
// MANTENIDA del código original (misma lógica de tiempo)
// CAMBIO: el GPIO del relé es ahora GPIO1 (header P3) en vez de GPIO5
// ─────────────────────────────────────────────────────────────
void AbrirCerradura() {
    ledcWriteTone(BUZZER_CHANNEL, 0);
    Serial.println("[ACCESO] Cerradura abierta");
    digitalWrite(RELE, HIGH);
    delay(TIEMPO_ABIERTA_MS);
    digitalWrite(RELE, LOW);
    ActualizarPantallaIdle();
}

void AbrirCerraduraRemota() {
    ledcWriteTone(BUZZER_CHANNEL, 0);
    Serial.println("[REMOTO] Apertura remota — 10 segundos");
    DateTime ahora = rtc.getDateTime();
    char ts[8];
    rtc.formatTime(ahora, ts, sizeof(ts));
    ui.drawAccessOK("REMOTO", ts);
    Buzzer_AccessOK();
    digitalWrite(RELE, HIGH);
    delay(10000);
    digitalWrite(RELE, LOW);
    ActualizarPantallaIdle();
}

void AccesoOcupacion(const char *matricula, uint32_t epoch) {
    // 1) Registrar el acceso y abrir la puerta
    kaizen_registrarFichaje(matricula, true, epoch, KAIZEN_EVENTO_ACCESO);
    ledcWriteTone(BUZZER_CHANNEL, 0);
    Buzzer_AccessOK();
    digitalWrite(RELE, HIGH);
    Serial.println("[OCUPACION] Puerta abierta");

    // 2) Detectar si mantiene la tarjeta 5 s para alternar estado.
    //    El relé se cierra a los TIEMPO_ABIERTA_MS aunque siga manteniendo.
    const uint32_t HOLD_MS = 5000;
    uint32_t inicio = millis();
    bool releCerrado = false, mantenida = true, primera = true;
    const char *accion = salaOcupada ? "LIBERAR" : "OCUPAR";
    char m[9];
    while (millis() - inicio < HOLD_MS) {
        if (!releCerrado && (millis() - inicio) >= TIEMPO_ABIERTA_MS) {
            digitalWrite(RELE, LOW);
            releCerrado = true;
        }
        int restante = (int)((HOLD_MS - (millis() - inicio)) / 1000) + 1;
        ui.drawMantener(accion, restante, primera);
        primera = false;
        ReadEmpleado(m);
        if (Es0(m) || !SonIguales((char*)matricula, m)) {
            mantenida = false;
            break;
        }
    }
    if (!releCerrado) digitalWrite(RELE, LOW);

    // 3) Se mantuvo los 5 s → alternar estado de la sala
    if (mantenida) {
        salaOcupada = !salaOcupada;
        GuardarEstadoSala();
        uint8_t tipo = salaOcupada ? KAIZEN_EVENTO_OCUPAR : KAIZEN_EVENTO_LIBERAR;
        kaizen_registrarFichaje(matricula, true, epoch, tipo);
        Serial.printf("[OCUPACION] Sala %s\n", salaOcupada ? "OCUPADA" : "LIBERADA");
        Buzzer_AccessOK();
    }
    ActualizarPantallaIdle();
}

void GuardarEstadoSala() {
    salaPrefs.begin("sala", false);
    salaPrefs.putBool("ocup", salaOcupada);
    salaPrefs.end();
}

// ============================================================
//  F U N C I O N E S   D E   B U Z Z E R
//  (reemplazan Pulsos_AddMode, Pulsos_RemoveMode, etc.)
// ============================================================

// ─────────────────────────────────────────────────────────────
// Buzzer_Beep — emite un tono de duración y frecuencia dadas
// NUEVO: función base de todos los patrones sonoros
// ─────────────────────────────────────────────────────────────
void Buzzer_Beep(uint32_t freq, uint32_t durMs) {
    ledcWriteTone(BUZZER_CHANNEL, freq);
    delay(durMs);
    ledcWriteTone(BUZZER_CHANNEL, 0);
}

// ─────────────────────────────────────────────────────────────
// Buzzer_AccessOK — patrón para acceso concedido
// NUEVO: reemplaza el relé como feedback de apertura exitosa
// Patrón: 2 tonos ascendentes cortos
// ─────────────────────────────────────────────────────────────
void Buzzer_AccessOK() {
    Buzzer_Beep(800,  100); delay(50);
    Buzzer_Beep(1200, 200);
}

// ─────────────────────────────────────────────────────────────
// Buzzer_AccessDeny — patrón para acceso denegado
// NUEVO: reemplaza el silencio del código original
// Patrón: 1 tono grave largo
// ─────────────────────────────────────────────────────────────
void Buzzer_AccessDeny() {
    Buzzer_Beep(FREQ_ACCESS_DENY, 400);
}

// ============================================================
//  F U N C I O N E S   R F I D
// ============================================================

// ─────────────────────────────────────────────────────────────
// ReadEmpleado — lee la matrícula de la tarjeta RFID CON TIMEOUT
//
// NUEVO: Agrega timeout de 1000ms para prevenir bloqueos si el
// módulo RFID (WS1850S) falla o se desconecta.
//
// CAMBIO respecto al código original:
//   mfrc522.PCD_Init()            → M5Dial.Rfid.PCD_Init()
//   mfrc522.PICC_IsNewCardPresent → M5Dial.Rfid.PICC_IsNewCardPresent()
//   mfrc522.PICC_ReadCardSerial() → M5Dial.Rfid.PICC_ReadCardSerial()
//   mfrc522.PCD_Authenticate(...) → M5Dial.Rfid.PCD_Authenticate(...)
//   mfrc522.MIFARE_Read(...)      → M5Dial.Rfid.MIFARE_Read(...)
//   mfrc522.PICC_HaltA()          → M5Dial.Rfid.PICC_HaltA()
//   mfrc522.PCD_StopCrypto1()     → M5Dial.Rfid.PCD_StopCrypto1()
//   mfrc522.PCD_Reset()           → M5Dial.Rfid.PCD_Reset()
//
// La API es IDÉNTICA. M5Dial.Rfid es el mismo objeto MFRC522 que
// el código original usaba, pero apuntando al WS1850S integrado.
// La lógica, el bloque 4 y la clave 0xFF×6 se mantienen sin cambios.
// ─────────────────────────────────────────────────────────────
void ReadEmpleado(char *empleado) {
    delay(300); // Mismo retardo que el código original

    // ── Iniciar contador de timeout
    unsigned long startTime = millis();

    // Reset del lector para detectar retirada de tarjeta
    M5Dial.Rfid.PCD_Init();

    // Limpiar el buffer de salida
    for (byte z = 0; z < 8; z++) empleado[z] = '0';
    empleado[8] = '\0';

    // ── TIMEOUT CHECK: Después de PCD_Init
    if (millis() - startTime > RFID_TIMEOUT_MS) {
        Serial.println("[RFID] Timeout: PCD_Init");
        return;
    }

    // Detectar tarjeta nueva en el campo RF
    if (!M5Dial.Rfid.PICC_IsNewCardPresent()) {
        // ── TIMEOUT CHECK: Lectura normal, salir sin log si es timeout
        if (millis() - startTime > RFID_TIMEOUT_MS) {
            Serial.println("[RFID] Timeout: PICC_IsNewCardPresent");
        }
        return;
    }

    // ── TIMEOUT CHECK: Después de PICC_IsNewCardPresent
    if (millis() - startTime > RFID_TIMEOUT_MS) {
        Serial.println("[RFID] Timeout: antes de PICC_ReadCardSerial");
        return;
    }

    // Leer el UID de la tarjeta
    if (!M5Dial.Rfid.PICC_ReadCardSerial()) {
        // ── TIMEOUT CHECK
        if (millis() - startTime > RFID_TIMEOUT_MS) {
            Serial.println("[RFID] Timeout: PICC_ReadCardSerial");
        }
        return;
    }

    // ── TIMEOUT CHECK: Después de PICC_ReadCardSerial
    if (millis() - startTime > RFID_TIMEOUT_MS) {
        Serial.println("[RFID] Timeout: antes de Authenticate");
        return;
    }

    // ── Configurar clave MIFARE (0xFF×6, clave de fábrica)
    MFRC522::MIFARE_Key key;
    for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

    // ── Autenticar con el bloque 4, clave A
    byte block = 4;
    MFRC522::StatusCode status = (MFRC522::StatusCode)M5Dial.Rfid.PCD_Authenticate(
        MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(M5Dial.Rfid.uid)
    );

    // ── TIMEOUT CHECK: Después de Authenticate
    if (millis() - startTime > RFID_TIMEOUT_MS) {
        Serial.println("[RFID] Timeout: PCD_Authenticate");
        M5Dial.Rfid.PICC_HaltA();
        M5Dial.Rfid.PCD_StopCrypto1();
        return;
    }

    if (status != MFRC522::STATUS_OK) {
        Serial.printf("[RFID] Error autenticación: %s\n",
                      M5Dial.Rfid.GetStatusCodeName(status));
        M5Dial.Rfid.PICC_HaltA();
        M5Dial.Rfid.PCD_StopCrypto1();
        return;
    }

    // ── Leer el bloque 4 (18 bytes: 16 datos + 2 CRC)
    byte buffer1[18];
    byte len = 18;
    status = (MFRC522::StatusCode)M5Dial.Rfid.MIFARE_Read(block, buffer1, &len);

    // ── TIMEOUT CHECK: Después de MIFARE_Read
    if (millis() - startTime > RFID_TIMEOUT_MS) {
        Serial.println("[RFID] Timeout: MIFARE_Read");
        M5Dial.Rfid.PICC_HaltA();
        M5Dial.Rfid.PCD_StopCrypto1();
        return;
    }

    if (status != MFRC522::STATUS_OK) {
        Serial.printf("[RFID] Error lectura: %s\n",
                      M5Dial.Rfid.GetStatusCodeName(status));
        M5Dial.Rfid.PICC_HaltA();
        M5Dial.Rfid.PCD_StopCrypto1();
        return;
    }

    M5Dial.Rfid.PICC_HaltA();
    M5Dial.Rfid.PCD_StopCrypto1();

    // ── Extraer la matrícula de los bytes 8-15 del bloque
    for (int i = 0; i < 8; i++) {
        empleado[i] = (char)buffer1[8 + i];
    }

    // Reset final para detectar retirada de tarjeta
    M5Dial.Rfid.PCD_Reset();

    // Log de lectura exitosa
    unsigned long elapsed = millis() - startTime;
    Serial.printf("[RFID] Matrícula leída: %s (tiempo: %lums)\n", empleado, elapsed);
}

// ─────────────────────────────────────────────────────────────
// SonIguales — compara dos matrículas de 8 caracteres
// MANTENIDA del código original: sin cambios
// ─────────────────────────────────────────────────────────────
bool SonIguales(char *m1, char *m2) {
    for (int i = 0; i < 8; i++) {
        if (m1[i] != m2[i]) return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────
// Es0 — comprueba si la matrícula es "00000000" (sin lectura)
// MANTENIDA del código original: sin cambios
// ─────────────────────────────────────────────────────────────
bool Es0(char *matricula) {
    char m[9] = {'0','0','0','0','0','0','0','0','\0'};
    return SonIguales(matricula, m);
}

// ─────────────────────────────────────────────────────────────
// CopiarMatricula — copia 8 chars de origen a destino
// MANTENIDA del código original: sin cambios
// ─────────────────────────────────────────────────────────────
void CopiarMatricula(char *origen, char *destino) {
    for (int i = 0; i < 8; i++) destino[i] = origen[i];
}

// ============================================================
//  F U N C I O N E S   D E   U I   y   R T C
// ============================================================

void ActualizarPantallaIdle() {
    if (kaizen_modoOcupacion() && salaOcupada) {
        ui.drawOcupado(kaizen_getNombre());
        ui.drawBridgeIndicator(kaizen_isBridgeOK());
        return;
    }
    DateTime ahora = rtc.getDateTime();
    char timeStr[8];
    char dateStr[20];
    rtc.formatTime(ahora, timeStr, sizeof(timeStr));
    rtc.formatDate(ahora, dateStr, sizeof(dateStr));

    if (ui.getState() == UI_STATE_IDLE) {
        ui.updateIdleTime(timeStr, dateStr);
    } else {
        ui.drawIdle(timeStr, dateStr);
    }
    ui.drawBridgeIndicator(kaizen_isBridgeOK());
}
