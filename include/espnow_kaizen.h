#pragma once
// ============================================================
// espnow_kaizen.h — Comunicación ESP-NOW con el Bridge
//                   para la Cerradura Kaizen (M5Dial)
//
// PROTOCOLO MENSAJE_COMPLETO_ESPACIO:
//
//   Bridge → Cerradura (data):
//     flags(1)      bit0=1 → acceso libre; bit0=0 → whitelist
//                   bit1=1 → apertura remota inmediata (10 s)
//                   bit2=1 → modo ocupación; bit2=0 → modo apertura
//                   bit3=1 → forzar OCUPACION de la sala
//                   bit4=1 → forzar LIBERACION de la sala
//                   (bit3 y bit4 simultáneos = inválido, se ignoran)
//     n_mats(1)     número de matrículas en whitelist
//     mats(n×8)     matrículas autorizadas (8 chars cada una)
//     len_nombre(1) longitud del nombre del espacio
//     nombre(N)     nombre del espacio (sin null terminator)
//     mat_ocup(8)   SOLO si bit3=1: matrícula del nuevo ocupante (8 chars)
//
//   Cerradura → Bridge (OK data):
//     status(1)       bit0=1 → nombre del ocupante pendiente de recibir
//     n_fichajes(1)   máx KAIZEN_MAX_FICHAJES_RESP
//     fichajes(N×14)  matricula(8) + epoch(4LE) + autorizado(1) + tipo(1)
//                     tipo: 0=acceso, 1=ocupar, 2=liberar
//     Buffer NVS se borra solo tras confirmar el envío.
//
// PROTOCOLO KAIZEN_NOMBRE_OCUPANTE (0x00B1):
//   Bridge → Cerradura (data):
//     len_nombre(1) longitud del nombre de la persona
//     nombre(N)     nombre del ocupante (sin null terminator)
//   Cerradura → Bridge: OK sin datos
// ============================================================

#include <Arduino.h>

// ─────────────────────────────────────────────────────────────
// Comandos
// ─────────────────────────────────────────────────────────────
#define MENSAJE_COMPLETO_ESPACIO         0x00B0  // Mensaje único cíclico Bridge↔Cerradura
#define KAIZEN_NOMBRE_OCUPANTE  0x00B1  // Bridge envía nombre del ocupante actual
#define KAIZEN_SET_TIME         0x00B2  // Bridge envía hora local como epoch(4LE)

#define OK              0xFFFE
//#define DISCONNECT      0xFFFB
//#define ACK             0xFFF8
#define NOTFOUND        0xFFF6
#define BAD_SECUENCE    0xFFF4
#define BAD_CRC         0xFFF3
#define ASK_VERSION     0xFFEE
#define UPDATE          0xFFED

// ─────────────────────────────────────────────────────────────
// Tipo de evento de un fichaje
// ─────────────────────────────────────────────────────────────
#define KAIZEN_EVENTO_ACCESO   0  // La tarjeta abre la puerta
#define KAIZEN_EVENTO_OCUPAR   1  // Sala marcada ocupada (mantuvo 5 s)
#define KAIZEN_EVENTO_LIBERAR  2  // Sala liberada (mantuvo 5 s)

// ─────────────────────────────────────────────────────────────
// Registro de fichaje
// ─────────────────────────────────────────────────────────────
struct KaizenFichaje {
    char     matricula[8];  // 8 chars, sin null terminator
    uint32_t epoch;         // segundos Unix desde 1970-01-01
    uint8_t  autorizado;    // 1=acceso concedido, 0=acceso denegado
    uint8_t  tipo;          // KAIZEN_EVENTO_*
};

// ─────────────────────────────────────────────────────────────
// Estructura de mensaje ESP-NOW
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
#define KAIZEN_WIFI_CHANNEL       1
#define KAIZEN_MAX_FICHAJES       50    // Capacidad del buffer offline en NVS
#define KAIZEN_MAX_FICHAJES_RESP  17    // Máx fichajes por respuesta (1+17×14=239 ≤ 246)
#define KAIZEN_MAX_WHITELIST      200   // Máx matrículas en whitelist
#define KAIZEN_FICHAJE_SIZE       14    // Bytes en wire: mat(8)+epoch(4LE)+aut(1)+tipo(1)
#define KAIZEN_TIMEOUT_BRIDGE_MS  120000UL
#define KAIZEN_FIRMWARE_VERSION   1
#define KAIZEN_TIMEOUT_SEND_MS    2000

// ============================================================
// API PÚBLICA
// ============================================================

// Inicializa ESP-NOW y restaura whitelist + fichajes pendientes de NVS
bool         kaizen_begin();

// Llamar en cada loop(): procesa mensajes entrantes y timeout del Bridge
void         kaizen_tick();

bool         kaizen_isBridgeOK();
uint32_t     kaizen_tiempoSinBridge();

// Devuelve true UNA sola vez cuando llegó nueva config del Bridge
bool         kaizen_hayEstadoCambio();

const char*  kaizen_getNombre();

// ── Acceso ────────────────────────────────────────────────────
// true si el Bridge indicó modo acceso libre (cualquiera entra)
bool         kaizen_accesoLibre();

// true si el Bridge configuró modo ocupación (false = modo apertura)
bool         kaizen_modoOcupacion();

// true si el Bridge solicitó apertura remota (se limpia al leer)
bool         kaizen_hayAperturaRemota();

// Consume-on-read: true UNA sola vez si el Bridge solicitó forzar OCUPACION
bool         kaizen_consumirForzarOcupar();

// Consume-on-read: true UNA sola vez si el Bridge solicitó forzar LIBERACION
bool         kaizen_consumirForzarLiberar();

// Matrícula del ocupante recibida con el último forzar OCUPAR del Bridge (8 chars + '\0')
const char*  kaizen_getMatriculaForzada();

// true si la matrícula (8 chars) está en la whitelist
bool         kaizen_estaAutorizado(const char *matricula);

// ── Ocupante ─────────────────────────────────────────────────
// Activa o desactiva el flag «nombre de ocupante pendiente» de recibir.
// Al desactivar, borra también el nombre almacenado.
void         kaizen_marcarOcupantePendiente(bool pendiente);

// true mientras no se ha recibido aún el nombre del ocupante actual
bool         kaizen_ocupantePendiente();

// Nombre del ocupante enviado por el Bridge (cadena vacía si no disponible)
const char*  kaizen_getNombreOcupante();

// ── Fichajes ─────────────────────────────────────────────────
// Encola un fichaje en el buffer NVS; se enviará en el próximo MENSAJE_COMPLETO_ESPACIO
void         kaizen_registrarFichaje(const char *matricula, bool autorizado, uint32_t epoch, uint8_t tipo);

// ── Sincronización de hora ──────────────────────────────────
// Consume-on-read: devuelve true y rellena *epoch si el Bridge envió nueva hora
bool         kaizen_consumirHoraPendiente(uint32_t *epoch);
