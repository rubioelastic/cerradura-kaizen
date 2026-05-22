#include "espnow_kaizen.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_idf_version.h>
#include <Preferences.h>
#include <Update.h>            // OTA via ESP-NOW (mismo protocolo que ReactionTime)
#include <esp_task_wdt.h>      // hard_reset via watchdog

extern "C" {
#include "esp_wifi.h"
}

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

// ── OTA via ESP-NOW
static uint32_t          _kOtaTotal    = 0;
static uint32_t          _kOtaProgress = 0;
static bool              _kOtaStarted  = false;

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
// Reset forzado via watchdog (idéntico a ReactionTime)
// ─────────────────────────────────────────────────────────────
static void _kHardReset() {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_task_wdt_config_t cfg = { .timeout_ms = 1000, .idle_core_mask = 0, .trigger_panic = true };
    esp_task_wdt_deinit();
    ESP_ERROR_CHECK(esp_task_wdt_init(&cfg));
#else
    ESP_ERROR_CHECK(esp_task_wdt_init(1, true));
#endif
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    while (true) {}
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

    Serial.printf("[ESPNOW] RX cmd=0x%04X seq=%u/%u len=%u\n",
                  _kMsgIn.comando, _kMsgIn.seq, _kSeqEsperada, _kLenMsgIn);

    if (_kMsgIn.seq != _kSeqEsperada && _kMsgIn.seq != 0) {
        uint8_t seqPrev = _kSeqEsperada - 1;
        if (seqPrev == 0) seqPrev--;
        if (seqPrev == _kMsgIn.seq || _kMsgIn.seq == 0) {
            Serial.printf("[ESPNOW] SEQ duplicada (%u), reenviando ultimo mensage\n", _kMsgIn.seq);
            _kMsgEnviado = false;
            esp_now_send(_kMacBridge, (uint8_t *)&_kMsgResp, _kLenMsgResp);
            uint32_t lim = millis() + KAIZEN_TIMEOUT_SEND_MS;
            while (!_kMsgEnviado && millis() < lim) vTaskDelay(5);
        } else {
            Serial.printf("[ESPNOW] BAD_SEQ recibida=%u esperada=%u\n", _kMsgIn.seq, _kSeqEsperada);
            _kResponder(K_BAD_SECUENCE, _kSeqEsperada);
        }
        return;
    }

    // ── CRC
    uint8_t crcCalc = _kCalcCRC(_kMsgIn, _kLenMsgIn);
    if (_kMsgIn.crc != crcCalc) {
        Serial.printf("[ESPNOW] BAD_CRC recibido=0x%02X calculado=0x%02X\n",
                      _kMsgIn.crc, crcCalc);
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

            Serial.printf("[BRIDGE] Config: '%s', %u matrículas, modo_estado=%d\n",
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

        case KAIZEN_COMPLETO: {
            // ── Desempaquetar flags
            uint8_t  flags     = _kMsgIn.data[0];
            bool     forzarOcupado = (flags & 0x01) != 0;
            _kModoEstado           = (flags & 0x02) != 0;

            // ── Tiempo y reserva (little-endian, info para futuros usos)
            // uint32_t tiempoMs = ...; // data[1..4] — no usado localmente aún
            // uint32_t reservaId = ...; // data[5..8]

            // ── Lista de matrículas autorizadas
            uint8_t  nMats  = _kMsgIn.data[9];
            uint16_t cursor = 10;
            if (_kCbConfig) {
                _kCbConfig(&_kMsgIn.data[cursor], nMats, _kNombre); // nombre aún no leído
            }
            cursor += nMats * 8;

            // ── Nombre del espacio
            uint8_t lenNombre = _kMsgIn.data[cursor];
            if (lenNombre > 31) lenNombre = 31;
            if (lenNombre > 0) {
                memcpy(_kNombre, &_kMsgIn.data[cursor + 1], lenNombre);
                _kNombre[lenNombre] = '\0';
            }

            // ── Aplicar estado forzado del Bridge
            EstadoEspacio estadoBridge = forzarOcupado ? EstadoEspacio::OCUPADO : EstadoEspacio::LIBRE;
            if (estadoBridge != _kEstado) {
                _kEstado = estadoBridge;
                if (!forzarOcupado) memset(_kOcupante, 0, sizeof(_kOcupante));
                _kEstadoCambio = true;
                _kNvsSave();
                Serial.printf("[BRIDGE] Estado forzado a: %s\n",
                              forzarOcupado ? "OCUPADO" : "LIBRE");
            }
            _kEstadoCambio = true; // Forzar redibujado siempre (puede haber cambiado nombre/mats)

            // ── Construir respuesta compatible con Bridge (mismo formato que ReactionTime):
            //    mix(1) + n_accesos(1) + [mat(8)] × n_accesos
            uint8_t mix = 0;
            if (_kEstado == EstadoEspacio::OCUPADO) mix |= 0x01; // bit0 = estado actual
            if (lenNombre > 0)                      mix |= 0x02; // bit1 = nombre configurado

            // Clasificar eventos: OK → matriculas a enviar; otros → flags en mix
            uint8_t nOK = 0;
            for (uint8_t i = 0; i < _kNEventos; i++) {
                if      (_kEventos[i].tipo == KaizenEvento::ACCESO_OK)       nOK++;
                else if (_kEventos[i].tipo == KaizenEvento::APERTURA_MANUAL) mix |= 0x04; // bit2
                else if (_kEventos[i].tipo == KaizenEvento::ACCESO_DENEGADO) mix |= 0x08; // bit3
            }

            uint8_t buf[2 + KAIZEN_MAX_EVENTOS * 8];
            uint16_t pos = 0;
            buf[pos++] = mix;
            buf[pos++] = nOK;
            for (uint8_t i = 0; i < _kNEventos; i++) {
                if (_kEventos[i].tipo != KaizenEvento::ACCESO_OK) continue;
                memcpy(&buf[pos], _kEventos[i].matricula, 8); pos += 8;
            }
            if (_kResponder(K_OK, buf, (uint8_t)pos)) {
                _kNEventos = 0; // Eventos confirmados
            }
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

        case K_UPDATE: {
            // Protocolo OTA idéntico al de ReactionTime:
            //   Primer paquete (8 bytes data): indice(4LE) + total(4LE)
            //   Paquetes siguientes:           indice(4LE) + chunk(N bytes)
            //   El Bridge espera OK con el offset confirmado en cada paso.
            uint8_t lenDatos = _kLenMsgIn - 4;
            uint32_t indiceRecibido = ((uint32_t)_kMsgIn.data[3] << 24)
                                    | ((uint32_t)_kMsgIn.data[2] << 16)
                                    | ((uint32_t)_kMsgIn.data[1] << 8)
                                    |  (uint32_t)_kMsgIn.data[0];

            if (!_kOtaStarted) {
                // Primer paquete: handshake / inicio
                if (lenDatos != 8) { _kHardReset(); break; }
                if (indiceRecibido != _kOtaProgress) {
                    // Bridge quiere reanudar desde otro offset — confirmar el nuestro
                    _kResponder(K_OK, (uint8_t *)&_kOtaProgress, 4);
                } else if (indiceRecibido == 0) {
                    _kOtaTotal = ((uint32_t)_kMsgIn.data[7] << 24)
                               | ((uint32_t)_kMsgIn.data[6] << 16)
                               | ((uint32_t)_kMsgIn.data[5] << 8)
                               |  (uint32_t)_kMsgIn.data[4];
                    if (!Update.begin(_kOtaTotal)) {
                        Update.printError(Serial);
                        _kHardReset();
                        break;
                    }
                    _kOtaStarted = true;
                    Serial.printf("[OTA] Inicio. Total: %u bytes\n", _kOtaTotal);
                    _kResponder(K_OK, (uint8_t *)&_kOtaProgress, 4);
                } else {
                    _kResponder(K_OK, (uint8_t *)&_kOtaProgress, 4);
                }
            } else {
                // Paquetes de datos
                int chunkLen = (int)lenDatos - 4;  // quitar los 4 bytes de índice
                if (indiceRecibido == 0 && lenDatos == 8) {
                    // Bridge reinicia desde 0 (reintento)
                    _kResponder(K_OK, (uint8_t *)&_kOtaProgress, 4);
                } else if (indiceRecibido != _kOtaProgress) {
                    // Offset desfasado — indicar el nuestro
                    _kResponder(K_OK, (uint8_t *)&_kOtaProgress, 4);
                } else {
                    if (Update.write(&_kMsgIn.data[4], chunkLen) != (size_t)chunkLen) {
                        Update.printError(Serial);
                        _kHardReset();
                        break;
                    }
                    _kOtaProgress += chunkLen;
                    if (_kOtaProgress >= _kOtaTotal) {
                        Serial.println("[OTA] Completo. Reiniciando...");
                        _kOtaStarted = false;
                        if (Update.end()) {
                            _kResponder(K_OK, (uint8_t *)&_kOtaProgress, 4);
                            _kHardReset(); // reinicio inmediato con nuevo firmware
                        } else {
                            Update.printError(Serial);
                            _kHardReset();
                        }
                    } else {
                        _kResponder(K_OK, (uint8_t *)&_kOtaProgress, 4);
                    }
                }
            }
            break;
        }

        default:
            _kResponder(K_NOTFOUND);
            break;
    }
}

// ============================================================
// API PÚBLICA (Implementaciones)
// ============================================================

bool kaizen_begin() {
    _kNvsLoad(); // Restaurar estado del espacio desde NVS
    _kEstadoCambio = true; // Forzar redibujado con el estado restaurado

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, true); // desconectar de AP (borra credenciales) pero mantiene el radio ON

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

void kaizen_setConfigCallback(KaizenConfigCb cb) { _kCbConfig = cb; }

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

void kaizen_marcarOcupado(const char *matricula) {
    _kEstado = EstadoEspacio::OCUPADO;
    strncpy(_kOcupante, matricula, 8);
    _kOcupante[8] = '\0';
    _kEstadoCambio = true;
    _kNvsSave();
}

void kaizen_marcarLibre() {
    _kEstado = EstadoEspacio::LIBRE;
    memset(_kOcupante, 0, sizeof(_kOcupante));
    _kEstadoCambio = true;
    _kNvsSave();
}

void kaizen_registrarEvento(KaizenEvento tipo, const char *matricula, uint32_t ts) {
    if (_kNEventos >= KAIZEN_MAX_EVENTOS) return;
    _kEventos[_kNEventos].tipo = tipo;
    strncpy(_kEventos[_kNEventos].matricula, matricula ? matricula : "", 8);
    _kEventos[_kNEventos].matricula[8] = '\0';
    _kEventos[_kNEventos].timestamp = ts;
    _kNEventos++;
}

EstadoEspacio kaizen_getEstado()       { return _kEstado; }
bool          kaizen_isBridgeOK()      { return _kBridgeOK; }
bool          kaizen_isModoEstado()    { return _kModoEstado; }
const char*   kaizen_getNombre()       { return _kNombre; }
const char*   kaizen_getOcupante()     { return _kOcupante; }

uint32_t kaizen_tiempoSinBridge() {
    if (_kBridgeOK)       return 0;          // Conectado — sin tiempo perdido
    if (_kTUltimoMsg == 0) return UINT32_MAX; // Nunca hubo contacto
    return millis() - _kTUltimoMsg;
}

bool kaizen_hayEstadoCambio() {
    bool v = _kEstadoCambio;
    _kEstadoCambio = false;
    return v;
}
