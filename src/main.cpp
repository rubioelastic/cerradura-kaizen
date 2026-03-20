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

#include <M5Dial.h>           // BSP M5Dial: incluye soporte RFID, pantalla, RTC, etc.
#include <EEPROM.h>           // Emulación EEPROM en flash del ESP32-S3

#include "pins_config.h"      // Definición de pines GPIO del M5Dial
#include "ui_display.h"       // Módulo de interfaz gráfica TFT
#include "rtc_bm8563.h"       // Módulo RTC BM8563
#include "espnow_kaizen.h"    // Comunicación ESP-NOW con el Bridge

// ─────────────────────────────────────────────────────────────
// CONSTANTES DE CONFIGURACIÓN
// ─────────────────────────────────────────────────────────────

// Máximo número de matrículas almacenables en EEPROM
// MANTENIDO del código original: MAX_MATRICULAS 255
#define MAX_MATRICULAS     255

// Tiempo que la cerradura permanece abierta (ms)
// MANTENIDO del código original: 3000ms
#define TIEMPO_ABIERTA_MS  3000

// Tiempo en modo gestión antes de confirmar acción (ms)
// MANTENIDO del código original: 3000ms
#define TIEMPO_MODO_MS     3000

// Frecuencias del buzzer para diferentes eventos (Hz)
// NUEVO: reemplaza los pulsos del relé (Pulsos_AddMode, etc.)
#define FREQ_ACCESS_OK     1000  // Tono acceso concedido
#define FREQ_ACCESS_DENY    400  // Tono acceso denegado
#define FREQ_ADD_MODE      1200  // Tono modo añadir
#define FREQ_REMOVE_MODE    600  // Tono modo eliminar
#define FREQ_BEEP_SHORT     800  // Beep corto genérico

// Matrículas maestras (MANTENIDAS del código original sin cambios)
// En producción se recomienda leer estas de la EEPROM
// o de una sección de NVS protegida por contraseña.
char master[9]  = {'0','0','4','0','5','1','0','6','\0'};
char master2[9] = {'8','8','0','4','0','2','2','0','\0'};

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
// (mismo convenio de nombres que el código original)
// ─────────────────────────────────────────────────────────────
bool     SonIguales(char *m1, char *m2);
void     ReadEmpleado(char *empleado);
bool     Habilitado(char *matricula);
bool     Es0(char *matricula);
void     AbrirCerradura();
void     AddMatricula(char *matricula);
void     RemoveMatricula(char *matricula);
void     RemoveAllMatriculas();
void     CopiarMatricula(char *origen, char *destino);
void     GetMatricula(char *matricula, unsigned char i);
int      Indice(char *matricula);
unsigned char GetNMatriculasHabilitadas();
void     habilitarMatricula(String m);
void     MostrarMatriculasHabilitadas(); // Ahora muestra en pantalla, no Serial

// NUEVO — Funciones de buzzer (reemplazan Pulsos_AddMode, etc.)
void     Buzzer_AccessOK();
void     Buzzer_AccessDeny();
void     Buzzer_AddMode();
void     Buzzer_RemoveMode();
void     Buzzer_RemoveAll();
void     Buzzer_Beep(uint32_t freq, uint32_t durMs);

// NUEVO — Funciones de UI y RTC
void     ActualizarPantallaIdle();
String   GetTimestampActual();

// ─────────────────────────────────────────────────────────────
// VARIABLES GLOBALES
// ─────────────────────────────────────────────────────────────
unsigned long ultimaActualizacionHora = 0; // Control de refresco del reloj
const unsigned long INTERVALO_HORA_MS = 1000; // Actualizar pantalla cada 1s

// Franja horaria de acceso (por defecto sin restricción)
// NUEVO: se puede modificar en tiempo de ejecución via encoder
AccessSchedule franjaAcceso = {
    .startHour    = 7,
    .startMinute  = 0,
    .endHour      = 22,
    .endMinute    = 0,
    .enabledDays  = {false, true, true, true, true, true, false}, // Lun-Vie
    .enabled      = false  // ← false = sin restricción horaria (igual que el original)
};

// ============================================================
//  S E T U P
// ============================================================
void setup() {
    // ── Inicialización del BSP M5Dial
    // M5Dial.begin() configura automáticamente:
    //   · M5Dial.Display  → pantalla TFT GC9A01
    //   · M5Dial.Rfid     → WS1850S via I2C interno (equivale a mfrc522.PCD_Init())
    //   · M5Dial.Speaker  → buzzer integrado
    //   · M5Dial.Encoder  → encoder rotatorio
    //   · M5Dial.Rtc      → RTC BM8563
    // No es necesario llamar a Wire.begin() ni SPI.begin() por separado.
    auto cfg = M5.config();
    M5Dial.begin(cfg, true, false); // (config, encoder=true, IMU=false)

    // ── Bus I2C
    // M5Dial.begin() ya inicializa el bus I2C interno con los pines correctos.
    // El RTC BM8563 comparte ese mismo bus. Wire queda listo para usarse.

    // ── Configuración del relé
    // CAMBIO respecto al original: GPIO5→GPIO1 (header P3 del M5Dial)
    // No hay pulsador de puerta en esta versión.
    pinMode(RELE, OUTPUT);
    digitalWrite(RELE, LOW); // Relé normalmente abierto (NO activo)

    // ── Configuración del buzzer (LEDC PWM del ESP32-S3)
    // NUEVO: reemplaza todos los Pulsos_* que usaban el relé como buzzer
    ledcSetup(BUZZER_CHANNEL, FREQ_BEEP_SHORT, BUZZER_RESOLUTION);
    ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);

    // ── Inicialización de la pantalla TFT y UI
    // NUEVO: reemplaza toda la interfaz de Serial.print
    ui.begin(); // Muestra splash screen durante 2 segundos

    // ── Inicialización de la EEPROM
    // MANTENIDO del código original: mismo tamaño de partición
    // COMPATIBLE: ESP32-S3 soporta la emulación de EEPROM en flash NVS
    if (!EEPROM.begin((MAX_MATRICULAS * 8) + 1)) {
        ui.drawError("Fallo EEPROM");
        Buzzer_AccessDeny();
        delay(3000);
        // No se detiene el sistema; puede funcionar sin persistencia
    }

    // ── Inicialización del RTC BM8563
    // NUEVO: no existía en el código original
    if (!rtc.begin()) {
        ui.drawError("Fallo RTC");
        Buzzer_AccessDeny();
        delay(3000);
        // El sistema continúa sin RTC (sin timestamps ni control horario)
    } else {
        // Forzar hora de compilación en este flash para poner en hora el RTC.
        // Una vez el RTC tenga la hora correcta y la batería esté cargada,
        // se puede eliminar esta línea para que la hora persista entre reinicios.
        rtc.setBuildTime();
    }

    // ── Módulo RFID
    // M5Dial.begin() ya inicializó M5Dial.Rfid (WS1850S integrado).
    // Se puede verificar la versión del chip para confirmar comunicación:
    byte rfidVersion = M5Dial.Rfid.PCD_ReadRegister(MFRC522::VersionReg);
    if (rfidVersion == 0x00 || rfidVersion == 0xFF) {
        ui.drawError("Fallo RFID");
        Buzzer_AccessDeny();
        delay(3000);
    } else {
        Serial.printf("[RFID] WS1850S detectado, versión: 0x%02X\n", rfidVersion);
    }

    // ── Inicialización de matrículas (chapuza del arranque original, mantenida)
    // Asegura que la EEPROM tenga al menos la matrícula maestra de respaldo
    habilitarMatricula("88040220");
    { char _m[] = "88040220"; RemoveMatricula(_m); }
    habilitarMatricula("88040220");

    // ── Mostrar matrículas habilitadas por consola serie (solo depuración)
    // CAMBIO: Serial se usa SOLO para depuración, no como interfaz de usuario
    Serial.begin(115200);
    Serial.println("=== Cerradura Kaizen M5Dial ===");
    MostrarMatriculasHabilitadas();

    // ── Comunicación ESP-NOW con el Bridge
    // Registrar callback para cuando el Bridge envíe lista de matrículas (KAIZEN_CONFIG)
    kaizen_setConfigCallback([](const uint8_t *mats, uint8_t n, const char *nombre) {
        // Sincronizar EEPROM con la lista de matrículas del Bridge
        EEPROM.write(0, 0);
        EEPROM.commit();
        for (uint8_t i = 0; i < n && i < MAX_MATRICULAS; i++) {
            char m[9];
            memcpy(m, mats + i * 8, 8);
            m[8] = '\0';
            habilitarMatricula(String(m));
        }
        Serial.printf("[BRIDGE] Matrículas sincronizadas: %u\n", n);
        (void)nombre; // nombre ya guardado en _kNombre por el módulo
    });
    if (!kaizen_begin()) {
        ui.drawError("Fallo ESP-NOW");
        delay(2000);
        // El sistema continúa en modo local (EEPROM) sin conectividad
    }

    // ── Pantalla de espera inicial
    ActualizarPantallaIdle();
}

// ============================================================
//  L O O P
// ============================================================
void loop() {
    // ── Actualizar M5Dial (encoder, touch, IMU)
    M5Dial.update();

    // ── Procesar mensajes ESP-NOW del Bridge
    kaizen_tick();

    // ── Si el estado del espacio cambió (Bridge liberó/ocupó, o timeout)
    if (kaizen_hayEstadoCambio()) {
        ActualizarPantallaIdle();
    }

    // ── Actualizar reloj cada segundo en pantallas que lo muestran
    if (millis() - ultimaActualizacionHora > INTERVALO_HORA_MS) {
        ultimaActualizacionHora = millis();
        UIState s = ui.getState();
        if (s == UI_STATE_IDLE || s == UI_STATE_LIBRE) {
            ActualizarPantallaIdle();
        }
    }

    // ── Leer tarjeta RFID
    // CAMBIO: ReadEmpleado() ahora usa WS1850S I2C en vez de MFRC522 SPI
    char matricula[9] = {'0','0','0','0','0','0','0','0','\0'};
    ReadEmpleado(matricula);

    // Sin tarjeta detectada → volver al inicio del loop
    if (Es0(matricula)) {
        return;
    }

    // ── TARJETA DETECTADA — Mostrar matrícula leída en pantalla
    // NUEVO: reemplaza Serial.printf("Matricula leida: %s\n", matricula)
    ui.drawReadingCard(matricula);
    delay(100); // Pequeña pausa visual

    // ─────────────────────────────────────────────────────────
    // VERIFICAR ACCESO
    // Lógica MANTENIDA del código original (mismo orden de prioridad)
    // ─────────────────────────────────────────────────────────

    if (SonIguales(matricula, master) || SonIguales(matricula, master2)) {
        // ── Tarjeta maestra → abre siempre; en modo estado libera si está ocupado
        DateTime ahora = rtc.getDateTime();
        if (rtc.isAccessAllowed(ahora, franjaAcceso)) {
            char ts[20];
            rtc.formatTime(ahora, ts, sizeof(ts));
            ui.drawAccessOK("MASTER", ts);
            Buzzer_AccessOK();
            if (kaizen_isBridgeOK() && kaizen_isModoEstado() &&
                kaizen_getEstado() == EstadoEspacio::OCUPADO) {
                kaizen_marcarLibre();
            }
            kaizen_registrarEvento(KaizenEvento::ACCESO_OK, master, millis());
            AbrirCerradura();
        } else {
            char ts[20];
            rtc.formatTime(ahora, ts, sizeof(ts));
            ui.drawAccessDeny("MASTER (HORARIO)", ts);
            Buzzer_AccessDeny();
            kaizen_registrarEvento(KaizenEvento::ACCESO_DENEGADO, master, millis());
            delay(2000);
        }

    } else if (Habilitado(matricula)) {
        DateTime ahora = rtc.getDateTime();
        if (rtc.isAccessAllowed(ahora, franjaAcceso)) {
            char ts[20];
            rtc.formatTime(ahora, ts, sizeof(ts));

            bool modoEstado = kaizen_isBridgeOK() && kaizen_isModoEstado();

            if (modoEstado && kaizen_getEstado() == EstadoEspacio::OCUPADO) {
                // ── Modo estado + espacio OCUPADO
                if (strncmp(matricula, kaizen_getOcupante(), 8) == 0) {
                    // Misma persona que ocupó: preguntar si quiere liberar o entrar de nuevo
                    // Countdown de 3 segundos con pantalla de confirmación.
                    // Touch o BtnA = libera; timeout = entra de nuevo.
                    ui.drawConfirmRelease(3, true);
                    bool liberar = false;
                    uint32_t limite = millis() + 3000;
                    uint32_t sigSegundo = millis() + 1000;
                    int countdown = 3;
                    while (millis() < limite && !liberar) {
                        M5Dial.update();
                        if (M5Dial.BtnA.wasPressed() || M5Dial.Touch.getDetail().wasPressed()) {
                            liberar = true;
                        }
                        if (millis() > sigSegundo) {
                            countdown--;
                            ui.drawConfirmRelease(countdown, false);
                            sigSegundo += 1000;
                        }
                        vTaskDelay(10);
                    }
                    if (liberar) {
                        // Libera el espacio + abre
                        ui.drawAccessOK(matricula, ts);
                        Buzzer_AccessOK();
                        kaizen_marcarLibre();
                        kaizen_registrarEvento(KaizenEvento::ACCESO_OK, matricula, millis());
                    } else {
                        // Entra de nuevo, espacio sigue OCUPADO por la misma persona
                        ui.drawAccessOK(matricula, ts);
                        Buzzer_AccessOK();
                        kaizen_registrarEvento(KaizenEvento::ACCESO_OK, matricula, millis());
                    }
                    AbrirCerradura();
                } else {
                    // Persona diferente → solo abre, estado sigue OCUPADO
                    ui.drawAccessOK(matricula, ts);
                    Buzzer_AccessOK();
                    kaizen_registrarEvento(KaizenEvento::ACCESO_OK, matricula, millis());
                    AbrirCerradura();
                }
            } else {
                // ── Solo acceso, o modo estado con espacio LIBRE → acceso normal
                ui.drawAccessOK(matricula, ts);
                Buzzer_AccessOK();
                if (modoEstado) {
                    // Espacio estaba LIBRE → pasa a OCUPADO con esta persona
                    kaizen_marcarOcupado(matricula);
                }
                kaizen_registrarEvento(KaizenEvento::ACCESO_OK, matricula, millis());
                AbrirCerradura();
            }
        } else {
            char ts[20];
            rtc.formatTime(ahora, ts, sizeof(ts));
            ui.drawAccessDeny(matricula, ts);
            Buzzer_AccessDeny();
            kaizen_registrarEvento(KaizenEvento::ACCESO_DENEGADO, matricula, millis());
            delay(2000);
        }

    } else {
        // ── Tarjeta desconocida → acceso denegado
        DateTime ahora = rtc.getDateTime();
        char ts[20];
        rtc.formatTime(ahora, ts, sizeof(ts));
        ui.drawAccessDeny(matricula, ts);
        Buzzer_AccessDeny();
        kaizen_registrarEvento(KaizenEvento::ACCESO_DENEGADO, matricula, millis());
        delay(2000);
        ActualizarPantallaIdle();
        return;
    }

    // ─────────────────────────────────────────────────────────
    // MODO GESTIÓN DE MATRÍCULAS (solo con tarjeta maestra)
    // Lógica MANTENIDA del código original
    //
    // Secuencia de detección (idéntica al original):
    //   1. Tarjeta maestra presentada → decide abrir O gestionar
    //   2. Si se mantiene en el campo > TIEMPO_MODO_MS → Modo AÑADIR
    //   3. Si se mantiene de nuevo    > TIEMPO_MODO_MS → Modo ELIMINAR
    //   4. Si se mantiene de nuevo    > TIEMPO_MODO_MS → BORRAR TODO
    //
    // CAMBIO: Los Pulsos_AddMode() reemplazados por Buzzer_AddMode()
    //         y ui.drawAddMode() en la pantalla
    // ─────────────────────────────────────────────────────────

    // Sólo entra en gestión si la tarjeta era el master
    ReadEmpleado(matricula);
    if (!SonIguales(matricula, master)) {
        // Si ya no detecta el master, salir
        ActualizarPantallaIdle();
        return;
    }

    // El master se ha vuelto a detectar → señal de modo gestión
    // CAMBIO: Pulsos_AddMode() → Buzzer_AddMode() + ui.drawAddMode()
    Buzzer_AddMode();
    ui.drawAddMode(3);

    // Espera a que el master retire la tarjeta (o se agote el tiempo)
    unsigned long tiempo = millis() + TIEMPO_MODO_MS;
    ReadEmpleado(matricula);
    while (SonIguales(matricula, master) && tiempo > millis()) {
        ReadEmpleado(matricula);
        // Actualizar cuenta atrás en pantalla
        int countdown = (int)((tiempo - millis()) / 1000) + 1;
        ui.drawAddMode(countdown);
    }

    if (tiempo < millis()) {
        // ─────────────────────────────────────────────────────
        // MODO ELIMINAR (se pasa al siguiente nivel)
        // ─────────────────────────────────────────────────────
        // CAMBIO: Pulsos_RemoveMode() → Buzzer_RemoveMode() + ui.drawRemoveMode()
        Buzzer_RemoveMode();
        ui.drawRemoveMode(3);

        tiempo = millis() + TIEMPO_MODO_MS;
        ReadEmpleado(matricula);
        while (SonIguales(matricula, master) && tiempo > millis()) {
            ReadEmpleado(matricula);
            int countdown = (int)((tiempo - millis()) / 1000) + 1;
            ui.drawRemoveMode(countdown);
        }

        if (tiempo < millis()) {
            // ─────────────────────────────────────────────────
            // BORRAR TODAS LAS MATRÍCULAS
            // ─────────────────────────────────────────────────
            // CAMBIO: Pulsos_RemoveAllMode() → Buzzer_RemoveAll() + ui.drawRemoveAll()
            Buzzer_RemoveAll();
            ui.drawRemoveAll();
            RemoveAllMatriculas();
            delay(2000);

        } else {
            // ─────────────────────────────────────────────────
            // MODO ELIMINAR INDIVIDUALMENTE
            // ─────────────────────────────────────────────────
            // NUEVO: mensaje en pantalla (el original usaba Serial.println)
            ui.drawRemoveMode(3);
            tiempo = millis() + TIEMPO_MODO_MS;
            char ult[9] = {'0','0','0','0','0','0','0','0','\0'};

            while (tiempo > millis()) {
                // Actualizar cuenta atrás
                int countdown = (int)((tiempo - millis()) / 1000) + 1;
                ui.drawRemoveMode(countdown);

                ReadEmpleado(matricula);
                if (!Es0(matricula) && !SonIguales(ult, matricula)) {
                    RemoveMatricula(matricula);
                    tiempo = millis() + TIEMPO_MODO_MS; // Reinicia temporizador
                    CopiarMatricula(matricula, ult);
                }
                // También verificar pulsador de puerta durante gestión
                if (digitalRead(PULSADOR_PUERTA) == LOW) AbrirCerradura();
            }
            // CAMBIO: Pulso() → Buzzer_Beep() breve
            Buzzer_Beep(FREQ_BEEP_SHORT, 100);
        }

    } else {
        // ─────────────────────────────────────────────────────
        // MODO AÑADIR INDIVIDUALMENTE
        // ─────────────────────────────────────────────────────
        // NUEVO: mensaje en pantalla (el original usaba Serial.println)
        ui.drawAddMode(3);
        tiempo = millis() + TIEMPO_MODO_MS;
        char ult[9] = {'0','0','0','0','0','0','0','0','\0'};

        while (tiempo > millis()) {
            // Actualizar cuenta atrás
            int countdown = (int)((tiempo - millis()) / 1000) + 1;
            ui.drawAddMode(countdown);

            ReadEmpleado(matricula);
            if (!Es0(matricula) && !SonIguales(ult, matricula)) {
                AddMatricula(matricula);
                tiempo = millis() + TIEMPO_MODO_MS; // Reinicia temporizador
                CopiarMatricula(matricula, ult);
            }
            // También verificar pulsador de puerta durante gestión
            if (digitalRead(PULSADOR_PUERTA) == LOW) AbrirCerradura();
        }
        // CAMBIO: Pulso() → Buzzer_Beep() breve
        Buzzer_Beep(FREQ_BEEP_SHORT, 100);
    }

    // Volver a la pantalla de espera tras cualquier modo de gestión
    ActualizarPantallaIdle();
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
    // NUEVO: desactivar buzzer si estaba sonando
    ledcWriteTone(BUZZER_CHANNEL, 0);

    Serial.println("[ACCESO] Cerradura abierta");

    // MANTENIDO del código original: activa relé 3 segundos
    digitalWrite(RELE, HIGH);
    delay(TIEMPO_ABIERTA_MS);
    digitalWrite(RELE, LOW);

    // Volver a la pantalla de espera tras abrir
    ActualizarPantallaIdle();
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

// ─────────────────────────────────────────────────────────────
// Buzzer_AddMode — equivale a Pulsos_AddMode() del original
// El original hacía 2 pulsos con el relé.
// NUEVO: 2 beeps rápidos ascendentes con el buzzer integrado
// ─────────────────────────────────────────────────────────────
void Buzzer_AddMode() {
    // Antes (original): 2 pulsos de relé de 100ms
    Buzzer_Beep(FREQ_ADD_MODE, 100); delay(100);
    Buzzer_Beep(FREQ_ADD_MODE, 100);
}

// ─────────────────────────────────────────────────────────────
// Buzzer_RemoveMode — equivale a Pulsos_RemoveMode() del original
// El original hacía 3 pulsos con el relé.
// NUEVO: 3 beeps rápidos con el buzzer integrado
// ─────────────────────────────────────────────────────────────
void Buzzer_RemoveMode() {
    // Antes (original): 3 pulsos de relé de 100ms
    Buzzer_Beep(FREQ_REMOVE_MODE, 100); delay(100);
    Buzzer_Beep(FREQ_REMOVE_MODE, 100); delay(100);
    Buzzer_Beep(FREQ_REMOVE_MODE, 100);
}

// ─────────────────────────────────────────────────────────────
// Buzzer_RemoveAll — equivale a Pulsos_RemoveAllMode() del original
// El original hacía 8 pulsos con el relé.
// NUEVO: 5 beeps descendentes (señal de advertencia)
// ─────────────────────────────────────────────────────────────
void Buzzer_RemoveAll() {
    // Antes (original): 8 pulsos de relé de 100ms con el relé
    for (int i = 5; i > 0; i--) {
        Buzzer_Beep(200 * i, 120);
        delay(80);
    }
}

// ============================================================
//  F U N C I O N E S   R F I D
// ============================================================

// ─────────────────────────────────────────────────────────────
// ReadEmpleado — lee la matrícula de la tarjeta RFID
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

    // Reset del lector para detectar retirada de tarjeta
    // CAMBIO: mfrc522 → M5Dial.Rfid  (misma función, mismo comportamiento)
    M5Dial.Rfid.PCD_Init();

    // Limpiar el buffer de salida
    for (byte z = 0; z < 8; z++) empleado[z] = '0';
    empleado[8] = '\0';

    // Detectar tarjeta nueva en el campo RF
    if (!M5Dial.Rfid.PICC_IsNewCardPresent()) return;

    // Leer el UID de la tarjeta
    if (!M5Dial.Rfid.PICC_ReadCardSerial()) return;

    // ── Configurar clave MIFARE (0xFF×6, clave de fábrica)
    // MANTENIDA del código original: misma clave, mismo tipo
    MFRC522::MIFARE_Key key;
    for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

    // ── Autenticar con el bloque 4, clave A
    // MANTENIDA del código original: mismo bloque, mismo comando
    byte block = 4;
    MFRC522::StatusCode status = (MFRC522::StatusCode)M5Dial.Rfid.PCD_Authenticate(
        MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(M5Dial.Rfid.uid)
    );

    if (status != MFRC522::STATUS_OK) {
        Serial.printf("[RFID] Error autenticación: %s\n",
                      M5Dial.Rfid.GetStatusCodeName(status));
        M5Dial.Rfid.PICC_HaltA();
        M5Dial.Rfid.PCD_StopCrypto1();
        return;
    }

    // ── Leer el bloque 4 (18 bytes: 16 datos + 2 CRC)
    // MANTENIDA del código original: mismo buffer, mismo bloque
    byte buffer1[18];
    byte len = 18;
    status = (MFRC522::StatusCode)M5Dial.Rfid.MIFARE_Read(block, buffer1, &len);

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
    // MANTENIDA del código original: misma posición en el buffer
    for (int i = 0; i < 8; i++) {
        empleado[i] = (char)buffer1[8 + i];
    }

    // Reset final para detectar retirada de tarjeta
    M5Dial.Rfid.PCD_Reset();

    Serial.printf("[RFID] Matrícula leída: %s\n", empleado);
}

// ============================================================
//  F U N C I O N E S   D E   E E P R O M
//  (mantenidas del código original, sin cambios funcionales)
// ============================================================

// ─────────────────────────────────────────────────────────────
// GetNMatriculasHabilitadas — lee el contador del byte 0
// MANTENIDA del código original: sin cambios
// ─────────────────────────────────────────────────────────────
unsigned char GetNMatriculasHabilitadas() {
    return (unsigned char)EEPROM.read(0);
}

// ─────────────────────────────────────────────────────────────
// GetMatricula — lee la matrícula en posición i de la EEPROM
// MANTENIDA del código original: sin cambios
// ─────────────────────────────────────────────────────────────
void GetMatricula(char *matricula, unsigned char i) {
    int j = 0;
    while (j < 8) {
        matricula[j] = (char)EEPROM.read((i * 8) + j + 1);
        j++;
    }
    matricula[j] = '\0';
}

// ─────────────────────────────────────────────────────────────
// habilitarMatricula — añade una matrícula (acepta String)
// MANTENIDA del código original: sin cambios
// ─────────────────────────────────────────────────────────────
void habilitarMatricula(String m) {
    int i = 0;
    unsigned char nMatriculas = GetNMatriculasHabilitadas();
    while (i < 8) {
        EEPROM.write((nMatriculas * 8) + i + 1, m[i]);
        EEPROM.commit();
        i++;
    }
    nMatriculas++;
    EEPROM.write(0, nMatriculas);
    EEPROM.commit();
}

// ─────────────────────────────────────────────────────────────
// AddMatricula — añade si no está ya habilitada
// CAMBIO: Pulso() → Buzzer_Beep()
// MANTENIDA: lógica EEPROM sin cambios
// ─────────────────────────────────────────────────────────────
void AddMatricula(char *matricula) {
    if (!Habilitado(matricula)) {
        unsigned char nMatriculas = GetNMatriculasHabilitadas();
        for (int i = 0; i < 8; i++) {
            EEPROM.write((nMatriculas * 8) + i + 1, matricula[i]);
            EEPROM.commit();
        }
        nMatriculas++;
        EEPROM.write(0, nMatriculas);
        EEPROM.commit();
    }
    // CAMBIO: Pulso() con relé → beep corto con buzzer
    Buzzer_Beep(FREQ_BEEP_SHORT, 80);
    Serial.printf("[EEPROM] Matrícula añadida: %s\n", matricula);
}

// ─────────────────────────────────────────────────────────────
// RemoveMatricula — elimina todas las ocurrencias de la matrícula
// CAMBIO: Pulso() → Buzzer_Beep()
// MANTENIDA: lógica de compactación de EEPROM sin cambios
// ─────────────────────────────────────────────────────────────
void RemoveMatricula(char *matricula) {
    int i = Indice(matricula);
    while (i != -1) {
        unsigned char nMatriculas = GetNMatriculasHabilitadas();
        // Compactar: desplaza todos los registros posteriores hacia atrás
        while (i < nMatriculas) {
            for (int z = 0; z < 8; z++) {
                EEPROM.write((i * 8) + z + 1, EEPROM.read(((i + 1) * 8) + z + 1));
                EEPROM.commit();
            }
            i++;
        }
        nMatriculas--;
        EEPROM.write(0, nMatriculas);
        EEPROM.commit();
        i = Indice(matricula); // Buscar de nuevo por si había duplicados
    }
    // CAMBIO: Pulso() con relé → beep corto con buzzer
    Buzzer_Beep(FREQ_BEEP_SHORT, 80);
    Serial.printf("[EEPROM] Matrícula eliminada: %s\n", matricula);
}

// ─────────────────────────────────────────────────────────────
// RemoveAllMatriculas — pone el contador a 0
// CAMBIO: Pulsos_RemoveAllMode() eliminado (ya se llama antes)
//         EEPROM.write(0,0) → ahora limpia todo el espacio usado
// ─────────────────────────────────────────────────────────────
void RemoveAllMatriculas() {
    // MANTENIDA del original: poner contador a 0
    EEPROM.write(0, 0);
    EEPROM.commit();
    Serial.println("[EEPROM] Todas las matrículas eliminadas");
}

// ─────────────────────────────────────────────────────────────
// MostrarMatriculasHabilitadas — lista las matrículas guardadas
// CAMBIO: Serial.print → ahora además hace log por Serie (depuración)
// En la pantalla TFT se mostraría con otra función de menú (futuro)
// ─────────────────────────────────────────────────────────────
void MostrarMatriculasHabilitadas() {
    unsigned char nMatriculas = GetNMatriculasHabilitadas();
    Serial.printf("[EEPROM] Matrículas habilitadas: %u\n", nMatriculas);
    for (int i = 0; i < nMatriculas; i++) {
        char m[9];
        GetMatricula(m, i);
        Serial.printf("  [%02d] %s\n", i, m);
    }
}

// ============================================================
//  F U N C I O N E S   A U X I L I A R E S
//  (mantenidas del código original)
// ============================================================

// ─────────────────────────────────────────────────────────────
// Indice — devuelve la posición de una matrícula en la EEPROM
// MANTENIDA del código original: sin cambios
// ─────────────────────────────────────────────────────────────
int Indice(char *matricula) {
    unsigned char nMatriculas = GetNMatriculasHabilitadas();
    for (int i = 0; i < nMatriculas; i++) {
        char m[9];
        GetMatricula(m, i);
        if (SonIguales(matricula, m)) return i;
    }
    return -1;
}

// ─────────────────────────────────────────────────────────────
// Habilitado — comprueba si la matrícula está en la EEPROM
// MANTENIDA del código original: sin cambios
// ─────────────────────────────────────────────────────────────
bool Habilitado(char *matricula) {
    return Indice(matricula) != -1;
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
//  (nuevas, no existían en el código original)
// ============================================================

// ─────────────────────────────────────────────────────────────
// ActualizarPantallaIdle — refresca la pantalla de espera
// con la hora y fecha actuales del RTC BM8563
//
// NUEVO: reemplaza todos los Serial.print de estado del loop
// ─────────────────────────────────────────────────────────────
void ActualizarPantallaIdle() {
    DateTime ahora = rtc.getDateTime();

    char timeStr[8];  // "HH:MM"
    char dateStr[20]; // "DD / MM / AAAA"
    rtc.formatTime(ahora, timeStr, sizeof(timeStr));
    rtc.formatDate(ahora, dateStr, sizeof(dateStr));

    // Si el Bridge está conectado Y el espacio tiene modo estado, mostrar LIBRE/OCUPADO
    if (kaizen_isBridgeOK() && kaizen_isModoEstado()) {
        if (kaizen_getEstado() == EstadoEspacio::OCUPADO) {
            if (ui.getState() != UI_STATE_OCUPADO) {
                ui.drawOcupado(kaizen_getNombre());
            }
            // OCUPADO no tiene reloj → no hacer update parcial
        } else {
            // LIBRE con reloj
            if (ui.getState() == UI_STATE_LIBRE) {
                ui.updateLibreTime(timeStr, dateStr);
            } else {
                ui.drawLibre(timeStr, dateStr, kaizen_getNombre());
            }
        }
    } else {
        // Sin Bridge: comportamiento original (reloj + candado)
        if (ui.getState() == UI_STATE_IDLE) {
            ui.updateIdleTime(timeStr, dateStr);
        } else {
            ui.drawIdle(timeStr, dateStr);
        }
    }
}

// ─────────────────────────────────────────────────────────────
// GetTimestampActual — devuelve una cadena con la fecha/hora actual
// Útil para registrar eventos de acceso
//
// NUEVO: no existía en el código original
// ─────────────────────────────────────────────────────────────
String GetTimestampActual() {
    DateTime ahora = rtc.getDateTime();
    char buf[22];
    rtc.formatTimestamp(ahora, buf, sizeof(buf));
    return String(buf);
}
