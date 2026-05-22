#pragma once
// ============================================================
// espnow_kaizen.h — Comunicación ESP-NOW con el Bridge
//                   para la Cerradura Kaizen (M5Dial)
//
// PROTOCOLO (compatible con TiempoRespuestaMantenimiento):
//   | SEQ (1B) | CRC (1B) | CMD (2B) | DATA (N bytes) |
//
// ROL:
//   Bridge → Cerradura : KAIZEN_SYNC periódico (poll de estado + eventos)
//                        KAIZEN_CONFIG (nombre espacio + lista matrículas)
//                        KAIZEN_LIBERAR / KAIZEN_OCUPAR (control desde RNA)
//   Cerradura → Bridge : respuestas con estado actual + cola de eventos
//
// IDENTIFICACIÓN (Opción B):
//   El Bridge identifica este M5Dial por su MAC WiFi STA.
//   No hay ID fijo en el firmware — el Bridge asigna el nombre del espacio.
//
// ESTADO DEL ESPACIO:
//   LIBRE   → nadie dentro (o liberado por Bridge / tarjeta maestra)
//   OCUPADO → alguien entró con tarjeta válida (o Bridge forzó OCUPADO)
//   El pulsador interior abre la puerta física pero NO cambia el estado.
//   Solo el Bridge puede liberar (salvo tarjeta maestra de emergencia local).
// ============================================================

#include <Arduino.h>

// ─────────────────────────────────────────────────────────────
// Comandos Bridge → Cerradura
// ─────────────────────────────────────────────────────────────
#define KAIZEN_SYNC     0x0C00  // Poll periódico: pide estado + recoge eventos
#define KAIZEN_CONFIG   0x0C01  // Nombre espacio + lista matrículas autorizadas
#define KAIZEN_LIBERAR  0x0C02  // Fuerza estado LIBRE (fin ensayo, etc.)
#define KAIZEN_OCUPAR   0x0C03  // Fuerza estado OCUPADO desde RNA
#define KAIZEN_COMPLETO 0x00B0  // Mensaje único cíclico (reemplaza SYNC+CONFIG+LIBERAR+OCUPAR)
                                //   data recibida: flags(1) + tiempo_ms(4LE) + id(4LE) +
                                //                  n_mats(1) + mats(n×8) + len_nombre(1) + nombre
                                //   flags bit0: estado forzado (0=LIBRE,1=OCUPADO)
                                //   flags bit1: modo_estado activo
                                //
                                //   respuesta OK — formato compatible Bridge/ReactionTime:
                                //     mix(1) + n_accesos(1) + [mat(8)] × n_accesos
                                //     mix bit0 = estado actual (0=LIBRE, 1=OCUPADO)
                                //     mix bit1 = nombre configurado
                                //     mix bit2 = hubo apertura manual en este ciclo
                                //     mix bit3 = hubo acceso denegado en este ciclo
                                //   Solo se envían matriculas de ACCESO_OK (8 bytes c/u).
                                //   APERTURA_MANUAL y ACCESO_DENEGADO se notifican via mix bits.

// Comandos del protocolo base (reutilizados de TRM)
#define K_OK            0xFFFE
#define K_DISCONNECT    0xFFFB
#define K_ACK           0xFFF8
#define K_NOTFOUND      0xFFF6
#define K_BAD_SECUENCE  0xFFF4
#define K_BAD_CRC       0xFFF3
#define K_ASK_VERSION   0xFFEE
#define K_UPDATE        0xFFED

// ─────────────────────────────────────────────────────────────
// Estado del espacio
// ─────────────────────────────────────────────────────────────
enum class EstadoEspacio : uint8_t {
    LIBRE   = 0,
    OCUPADO = 1
};

// ─────────────────────────────────────────────────────────────
// Tipos de evento (cola que se envía en la respuesta a KAIZEN_SYNC)
// ─────────────────────────────────────────────────────────────
enum class KaizenEvento : uint8_t {
    ACCESO_OK       = 0x01,  // Tarjeta válida → puerta abierta
    ACCESO_DENEGADO = 0x02,  // Tarjeta rechazada (no autorizada / fuera de horario)
    APERTURA_MANUAL = 0x03   // Pulsador interior pulsado
};

// ─────────────────────────────────────────────────────────────
// Estructura de evento pendiente de enviar al Bridge
// ─────────────────────────────────────────────────────────────
struct KaizenEventoPendiente {
    KaizenEvento tipo;
    char     matricula[9]; // 8 chars + '\0'
    uint32_t timestamp;    // millis() en el momento del evento
};

// ─────────────────────────────────────────────────────────────
// Estructura de mensaje ESP-NOW (igual que TRM)
// ─────────────────────────────────────────────────────────────
#define K_MAX_MSG 250
struct KaizenMsg {
    uint8_t  seq     = 0;
    uint8_t  crc     = 0;
    uint16_t comando = 0;
    uint8_t  data[246];
};

// ─────────────────────────────────────────────────────────────
// Constantes de configuración
// ─────────────────────────────────────────────────────────────
#define KAIZEN_WIFI_CHANNEL      1
#define KAIZEN_MAX_EVENTOS       8
#define KAIZEN_TIMEOUT_BRIDGE_MS 120000UL  // 2 min sin mensaje → sin cobertura
#define KAIZEN_FIRMWARE_VERSION  2  // v2: persistencia de estado en NVS (Preferences)
#define KAIZEN_TIMEOUT_SEND_MS   2000

// ─────────────────────────────────────────────────────────────
// Callback que main.cpp registra para aplicar la CONFIG del Bridge:
//   mats    → puntero a array de matrículas (8 bytes cada una)
//   n       → número de matrículas
//   nombre  → nombre del espacio asignado por el Bridge
// ─────────────────────────────────────────────────────────────
typedef void (*KaizenConfigCb)(const uint8_t *mats, uint8_t n, const char *nombre);

// ============================================================
// API PÚBLICA (Declaraciones)
// ============================================================

// ─────────────────────────────────────────────────────────────
// kaizen_begin — inicializa WiFi en modo STA + ESP-NOW
//   Llamar desde setup(), después de M5Dial.begin()
//   Devuelve false si ESP-NOW no pudo iniciarse.
// ─────────────────────────────────────────────────────────────
bool kaizen_begin();

// ─────────────────────────────────────────────────────────────
// kaizen_setConfigCallback — registra la función que procesa
//   la lista de matrículas enviada por el Bridge en KAIZEN_CONFIG
// ─────────────────────────────────────────────────────────────
void kaizen_setConfigCallback(KaizenConfigCb cb);

// ─────────────────────────────────────────────────────────────
// kaizen_tick — llamar en cada iteración de loop()
//   Procesa mensajes entrantes y detecta timeout del Bridge
// ─────────────────────────────────────────────────────────────
void kaizen_tick();

// ─────────────────────────────────────────────────────────────
// kaizen_marcarOcupado — llamar justo antes de AbrirCerradura()
//   cuando el acceso ha sido concedido por RFID.
//   Marca el espacio como OCUPADO con la matrícula del ocupante.
// ─────────────────────────────────────────────────────────────
void kaizen_marcarOcupado(const char *matricula);

// ─────────────────────────────────────────────────────────────
// kaizen_marcarLibre — liberar localmente (tarjeta maestra de emergencia)
// ─────────────────────────────────────────────────────────────
void kaizen_marcarLibre();

// ─────────────────────────────────────────────────────────────
// kaizen_registrarEvento — encola un evento para el próximo SYNC
//   El Bridge lo recibirá en la siguiente respuesta a KAIZEN_SYNC.
// ─────────────────────────────────────────────────────────────
void kaizen_registrarEvento(KaizenEvento tipo, const char *matricula, uint32_t ts);

// ─────────────────────────────────────────────────────────────
// Getters de estado para la UI y lógica principal
// ─────────────────────────────────────────────────────────────
EstadoEspacio kaizen_getEstado();
bool          kaizen_isBridgeOK();
bool          kaizen_isModoEstado();
const char*   kaizen_getNombre();
const char*   kaizen_getOcupante();

// ─────────────────────────────────────────────────────────────
// kaizen_tiempoSinBridge — milisegundos transcurridos desde el
//   último mensaje recibido del Bridge.
//   - Devuelve 0           si _kBridgeOK es true (hay cobertura).
//   - Devuelve UINT32_MAX  si nunca se ha recibido ningún mensaje.
//   - Devuelve el tiempo real si el Bridge se ha perdido por timeout.
//   Útil para mostrar en pantalla o para lógica de reintentos.
// ─────────────────────────────────────────────────────────────
uint32_t kaizen_tiempoSinBridge();

// ─────────────────────────────────────────────────────────────
// kaizen_hayEstadoCambio — Devuelve true UNA sola vez cuando el
//   estado cambió desde la última consulta
// ─────────────────────────────────────────────────────────────
bool kaizen_hayEstadoCambio();
