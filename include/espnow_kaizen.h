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
#include <WiFi.h>
#include <esp_now.h>
#include <esp_idf_version.h>
#include <Preferences.h>

extern "C" {
#include "esp_wifi.h"
}

// ─────────────────────────────────────────────────────────────
// Comandos Bridge → Cerradura
// ─────────────────────────────────────────────────────────────
#define KAIZEN_SYNC     0x0C00  // Poll periódico: pide estado + recoge eventos
#define KAIZEN_CONFIG   0x0C01  // Nombre espacio + lista matrículas autorizadas
#define KAIZEN_LIBERAR  0x0C02  // Fuerza estado LIBRE (fin ensayo, etc.)
#define KAIZEN_OCUPAR   0x0C03  // Fuerza estado OCUPADO desde RNA

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
// Variables internas del módulo (prefijo _ = privadas)
// ============================================================
static Preferences           _kPrefs;
static EstadoEspacio         _kEstado         = EstadoEspacio::LIBRE;
static char                  _kNombre[32]     = "Sin config";
static char                  _kOcupante[9]    = {0};
static bool                  _kBridgeOK       = false;
static bool                  _kModoEstado     = false; // false=solo acceso, true=acceso+estado
static uint8_t               _kMacBridge[6]   = {0};
static uint32_t              _kTUltimoMsg     = 0;
static bool                  _kEstadoCambio   = false;

static KaizenEventoPendiente _kEventos[KAIZEN_MAX_EVENTOS];
static uint8_t               _kNEventos       = 0;

static volatile bool     _kMsgRecibido  = false;
static volatile bool     _kMsgEnviado   = false;
static volatile bool     _kMsgEnviadoOK = false;
static KaizenMsg         _kMsgIn;
static volatile uint8_t  _kLenMsgIn    = 0;
static KaizenMsg         _kMsgResp;
static uint8_t           _kLenMsgResp  = 0;
static uint8_t           _kSeqEsperada = 0;
static bool              _kPrimerACK   = true;

static KaizenConfigCb    _kCbConfig    = nullptr;

// ─────────────────────────────────────────────────────────────
// Persistencia NVS del estado del espacio
// ─────────────────────────────────────────────────────────────
static void _kNvsSave() {
    _kPrefs.begin("kaizen", false);
    _kPrefs.putUChar("estado",   (uint8_t)_kEstado);
    _kPrefs.putString("ocupante", _kOcupante);
    _kPrefs.end();
}

static void _kNvsLoad() {
    _kPrefs.begin("kaizen", true); // read-only
    _kEstado = (EstadoEspacio)_kPrefs.getUChar("estado", (uint8_t)EstadoEspacio::LIBRE);
    String ocp = _kPrefs.getString("ocupante", "");
    strncpy(_kOcupante, ocp.c_str(), 8);
    _kOcupante[8] = '\0';
    _kPrefs.end();
    Serial.printf("[NVS] Estado restaurado: %s  Ocupante: '%s'\n",
                  _kEstado == EstadoEspacio::OCUPADO ? "OCUPADO" : "LIBRE",
                  _kOcupante);
}

// ─────────────────────────────────────────────────────────────
// Compatibilidad IDF v4/v5 para los callbacks de ESP-NOW
// ─────────────────────────────────────────────────────────────
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#define KAIZEN_NEW_API 1
#else
#define KAIZEN_NEW_API 0
#endif

#if KAIZEN_NEW_API
static void _kOnRecv(const esp_now_recv_info_t *ri, const uint8_t *data, int len) {
    if (len < 4 || len > K_MAX_MSG) return;
    memcpy(_kMacBridge, ri->src_addr, 6);
    _kLenMsgIn = (uint8_t)len;
    memcpy(&_kMsgIn, data, _kLenMsgIn);
    _kMsgRecibido = true;
}
static void _kOnSent(const wifi_tx_info_t *, esp_now_send_status_t st) {
    _kMsgEnviadoOK = (st == ESP_NOW_SEND_SUCCESS);
    _kMsgEnviado   = true;
}
#else
static void _kOnRecv(const uint8_t *mac, const uint8_t *data, int len) {
    if (len < 4 || len > K_MAX_MSG) return;
    memcpy(_kMacBridge, mac, 6);
    _kLenMsgIn = (uint8_t)len;
    memcpy(&_kMsgIn, data, _kLenMsgIn);
    _kMsgRecibido = true;
}
static void _kOnSent(const uint8_t *, esp_now_send_status_t st) {
    _kMsgEnviadoOK = (st == ESP_NOW_SEND_SUCCESS);
    _kMsgEnviado   = true;
}
#endif

// ─────────────────────────────────────────────────────────────
// CRC (idéntico al de TRM)
// ─────────────────────────────────────────────────────────────
static uint8_t _kCalcCRC(const KaizenMsg &m, uint8_t len) {
    uint8_t c = 0;
    int dLen = len - 4; // SEQ(1) + CRC(1) + CMD(2) = 4 bytes fijos
    for (int i = 0; i < dLen; i++) c += m.data[i];
    c += m.seq;
    c += (uint8_t)(m.comando);
    c += (uint8_t)(m.comando >> 8);
    return c;
}

// ─────────────────────────────────────────────────────────────
// Registrar el Bridge como peer ESP-NOW si no lo está ya
// ─────────────────────────────────────────────────────────────
static void _kEnsurePeer() {
    if (esp_now_is_peer_exist(_kMacBridge)) return;
    esp_now_peer_info_t p;
    memset(&p, 0, sizeof(p));
    p.channel = KAIZEN_WIFI_CHANNEL;
    p.encrypt = 0;
    p.ifidx   = WIFI_IF_STA;
    memcpy(p.peer_addr, _kMacBridge, 6);
    esp_now_add_peer(&p);
}

// ─────────────────────────────────────────────────────────────
// Enviar respuesta con reintentos
// ─────────────────────────────────────────────────────────────
static bool _kResponder(uint16_t cmd, const uint8_t *datos, uint8_t len, uint8_t intentos = 2) {
    if (intentos == 0) return false;
    _kMsgResp.comando = cmd;
    _kLenMsgResp = len + 4;
    if (len && datos) memcpy(_kMsgResp.data, datos, len);
    _kMsgResp.seq = _kSeqEsperada;
    _kMsgResp.crc = _kCalcCRC(_kMsgResp, _kLenMsgResp);

    _kMsgEnviado = false;
    esp_now_send(_kMacBridge, (uint8_t *)&_kMsgResp, _kLenMsgResp);

    uint32_t lim = millis() + KAIZEN_TIMEOUT_SEND_MS;
    while (!_kMsgEnviado && millis() < lim) vTaskDelay(5);

    if (!_kMsgEnviado || !_kMsgEnviadoOK)
        return _kResponder(cmd, datos, len, intentos - 1);

    _kSeqEsperada++;
    if (_kSeqEsperada == 0) _kSeqEsperada++;
    return true;
}
static bool _kResponder(uint16_t cmd) { return _kResponder(cmd, nullptr, 0); }
static bool _kResponder(uint16_t cmd, uint8_t b) { return _kResponder(cmd, &b, 1); }

// ─────────────────────────────────────────────────────────────
// Dispatch de mensajes recibidos
// ─────────────────────────────────────────────────────────────
static void _kProcesar() {
    _kEnsurePeer();

    // ── Gestión de secuencia (idéntica a TRM)
    uint8_t seqAlt = _kSeqEsperada + 1;
    if (seqAlt == 0) seqAlt++;
    if (_kMsgIn.seq == seqAlt) {
        _kSeqEsperada++;
        if (_kSeqEsperada == 0) _kSeqEsperada++;
    }

    if (_kMsgIn.seq != _kSeqEsperada && _kMsgIn.seq != 0) {
        uint8_t seqPrev = _kSeqEsperada - 1;
        if (seqPrev == 0) seqPrev--;
        if (seqPrev == _kMsgIn.seq || _kMsgIn.seq == 0) {
            // Reenviar último mensaje
            _kMsgEnviado = false;
            esp_now_send(_kMacBridge, (uint8_t *)&_kMsgResp, _kLenMsgResp);
            uint32_t lim = millis() + KAIZEN_TIMEOUT_SEND_MS;
            while (!_kMsgEnviado && millis() < lim) vTaskDelay(5);
        } else {
            _kResponder(K_BAD_SECUENCE, _kSeqEsperada);
        }
        return;
    }

    // ── CRC
    if (_kMsgIn.crc != _kCalcCRC(_kMsgIn, _kLenMsgIn)) {
        _kResponder(K_BAD_CRC, _kSeqEsperada);
        return;
    }

    _kTUltimoMsg = millis();
    _kBridgeOK   = true;

    // ── Dispatch de comandos
    switch (_kMsgIn.comando) {

        case K_ACK: {
            // Primer ACK: responder DISCONNECT para resetear secuencias
            if (_kPrimerACK) {
                _kResponder(K_DISCONNECT);
                _kPrimerACK = false;
            } else {
                _kResponder(K_OK);
            }
            break;
        }

        case KAIZEN_SYNC: {
            // Bridge pide estado actual + eventos pendientes
            // Respuesta: estado(1) + ocupante(8) + n_eventos(1) + [eventos: tipo(1)+mat(8)+ts(4)]
            uint8_t buf[10 + KAIZEN_MAX_EVENTOS * 13];
            uint16_t pos = 0;
            buf[pos++] = (uint8_t)_kEstado;
            memcpy(&buf[pos], _kOcupante, 8); pos += 8;
            buf[pos++] = _kNEventos;
            for (uint8_t i = 0; i < _kNEventos; i++) {
                buf[pos++] = (uint8_t)_kEventos[i].tipo;
                memcpy(&buf[pos], _kEventos[i].matricula, 8); pos += 8;
                memcpy(&buf[pos], &_kEventos[i].timestamp, 4); pos += 4;
            }
            if (_kResponder(K_OK, buf, (uint8_t)pos)) {
                _kNEventos = 0; // Eventos confirmados: vaciar cola
            }
            break;
        }

        case KAIZEN_CONFIG: {
            // data: [flags(1)] [len_nombre(1)] [nombre(N)] [n_mats(1)] [mat0(8)] ...
            // flags bit0 = modo_estado (0=solo acceso, 1=acceso+estado)
            uint8_t flags     = _kMsgIn.data[0];
            _kModoEstado      = (flags & 0x01) != 0;
            uint8_t lenNombre = _kMsgIn.data[1];
            if (lenNombre > 31) lenNombre = 31;
            memcpy(_kNombre, &_kMsgIn.data[2], lenNombre);
            _kNombre[lenNombre] = '\0';

            uint8_t offset = 2 + lenNombre;
            uint8_t nMats  = _kMsgIn.data[offset];

            Serial.printf("[BRIDGE] Config: '%s', %u matr\u00edculas, modo_estado=%d\n",
                          _kNombre, nMats, (int)_kModoEstado);

            if (_kCbConfig) {
                _kCbConfig(&_kMsgIn.data[offset + 1], nMats, _kNombre);
            }
            _kEstadoCambio = true; // Forzar redibujado por si cambió el modo
            _kResponder(K_OK);
            break;
        }

        case KAIZEN_LIBERAR: {
            EstadoEspacio prev = _kEstado;
            _kEstado = EstadoEspacio::LIBRE;
            memset(_kOcupante, 0, sizeof(_kOcupante));
            if (prev != _kEstado) { _kEstadoCambio = true; _kNvsSave(); }
            Serial.println("[BRIDGE] Espacio liberado por Bridge");
            _kResponder(K_OK);
            break;
        }

        case KAIZEN_OCUPAR: {
            EstadoEspacio prev = _kEstado;
            _kEstado = EstadoEspacio::OCUPADO;
            // El Bridge puede incluir una matrícula/nombre en data[0..7]
            uint8_t lenData = _kLenMsgIn - 4;
            if (lenData >= 8) {
                memcpy(_kOcupante, _kMsgIn.data, 8);
                _kOcupante[8] = '\0';
            }
            if (prev != _kEstado) { _kEstadoCambio = true; _kNvsSave(); }
            Serial.println("[BRIDGE] Espacio ocupado por Bridge");
            _kResponder(K_OK);
            break;
        }

        case K_ASK_VERSION: {
            const uint32_t v = KAIZEN_FIRMWARE_VERSION;
            uint8_t b[4] = {
                (uint8_t)(v >> 24), (uint8_t)(v >> 16),
                (uint8_t)(v >> 8),  (uint8_t)v
            };
            _kResponder(K_OK, b, 4);
            break;
        }

        default:
            _kResponder(K_NOTFOUND);
            break;
    }
}

// ============================================================
// API PÚBLICA
// ============================================================

// ─────────────────────────────────────────────────────────────
// kaizen_begin — inicializa WiFi en modo STA + ESP-NOW
//   Llamar desde setup(), después de M5Dial.begin()
//   Devuelve false si ESP-NOW no pudo iniciarse.
// ─────────────────────────────────────────────────────────────
bool kaizen_begin() {
    _kNvsLoad(); // Restaurar estado del espacio desde NVS
    _kEstadoCambio = true; // Forzar redibujado con el estado restaurado

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESPNOW] Error al inicializar ESP-NOW");
        return false;
    }

    esp_now_register_recv_cb(_kOnRecv);
    esp_now_register_send_cb(_kOnSent);

    // Mostrar MAC por serie para que el Bridge pueda registrar este dispositivo
    uint8_t mac[6];
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        Serial.printf("[ESPNOW] MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    Serial.println("[ESPNOW] Esperando Bridge...");
    return true;
}

// ─────────────────────────────────────────────────────────────
// kaizen_setConfigCallback — registra la función que procesa
//   la lista de matrículas enviada por el Bridge en KAIZEN_CONFIG
// ─────────────────────────────────────────────────────────────
void kaizen_setConfigCallback(KaizenConfigCb cb) { _kCbConfig = cb; }

// ─────────────────────────────────────────────────────────────
// kaizen_tick — llamar en cada iteración de loop()
//   Procesa mensajes entrantes y detecta timeout del Bridge
// ─────────────────────────────────────────────────────────────
void kaizen_tick() {
    if (_kBridgeOK && (millis() - _kTUltimoMsg >= KAIZEN_TIMEOUT_BRIDGE_MS)) {
        _kBridgeOK     = false;
        _kEstadoCambio = true; // Forzar redibujado de pantalla (SIN COBERTURA)
        Serial.println("[ESPNOW] Bridge sin respuesta → sin cobertura");
    }
    if (!_kMsgRecibido) return;
    _kMsgRecibido = false;
    _kProcesar();
}

// ─────────────────────────────────────────────────────────────
// kaizen_marcarOcupado — llamar justo antes de AbrirCerradura()
//   cuando el acceso ha sido concedido por RFID.
//   Marca el espacio como OCUPADO con la matrícula del ocupante.
// ─────────────────────────────────────────────────────────────
void kaizen_marcarOcupado(const char *matricula) {
    _kEstado = EstadoEspacio::OCUPADO;
    strncpy(_kOcupante, matricula, 8);
    _kOcupante[8] = '\0';
    _kEstadoCambio = true;
    _kNvsSave();
}

// ─────────────────────────────────────────────────────────────
// kaizen_marcarLibre — liberar localmente (tarjeta maestra de emergencia)
// ─────────────────────────────────────────────────────────────
void kaizen_marcarLibre() {
    _kEstado = EstadoEspacio::LIBRE;
    memset(_kOcupante, 0, sizeof(_kOcupante));
    _kEstadoCambio = true;
    _kNvsSave();
}

// ─────────────────────────────────────────────────────────────
// kaizen_registrarEvento — encola un evento para el próximo SYNC
//   El Bridge lo recibirá en la siguiente respuesta a KAIZEN_SYNC.
// ─────────────────────────────────────────────────────────────
void kaizen_registrarEvento(KaizenEvento tipo, const char *matricula, uint32_t ts) {
    if (_kNEventos >= KAIZEN_MAX_EVENTOS) return;
    _kEventos[_kNEventos].tipo = tipo;
    strncpy(_kEventos[_kNEventos].matricula, matricula ? matricula : "", 8);
    _kEventos[_kNEventos].matricula[8] = '\0';
    _kEventos[_kNEventos].timestamp = ts;
    _kNEventos++;
}

// ─────────────────────────────────────────────────────────────
// Getters de estado para la UI y lógica principal
// ─────────────────────────────────────────────────────────────
EstadoEspacio kaizen_getEstado()       { return _kEstado; }
bool          kaizen_isBridgeOK()      { return _kBridgeOK; }
bool          kaizen_isModoEstado()    { return _kModoEstado; }
const char*   kaizen_getNombre()       { return _kNombre; }
const char*   kaizen_getOcupante()     { return _kOcupante; }

// ─────────────────────────────────────────────────────────────
// kaizen_tiempoSinBridge — milisegundos transcurridos desde el
//   último mensaje recibido del Bridge.
//   - Devuelve 0           si _kBridgeOK es true (hay cobertura).
//   - Devuelve UINT32_MAX  si nunca se ha recibido ningún mensaje.
//   - Devuelve el tiempo real si el Bridge se ha perdido por timeout.
//   Útil para mostrar en pantalla o para lógica de reintentos.
// ─────────────────────────────────────────────────────────────
uint32_t kaizen_tiempoSinBridge() {
    if (_kBridgeOK)       return 0;          // Conectado — sin tiempo perdido
    if (_kTUltimoMsg == 0) return UINT32_MAX; // Nunca hubo contacto
    return millis() - _kTUltimoMsg;
}

// Devuelve true UNA sola vez cuando el estado cambió desde la última consulta
bool kaizen_hayEstadoCambio() {
    bool v = _kEstadoCambio;
    _kEstadoCambio = false;
    return v;
}
