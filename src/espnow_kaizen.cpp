#include "espnow_kaizen.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_idf_version.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_task_wdt.h>

extern "C" {
#include "esp_wifi.h"
}

// ============================================================
// Variables internas del módulo (prefijo _ = privadas)
// ============================================================
static Preferences  _kPrefs;
static char         _kNombre[32]    = "Sin config";
static bool         _kBridgeOK      = false;
static uint8_t      _kMacBridge[6]  = {0};
static uint32_t     _kTUltimoMsg    = 0;
static bool         _kEstadoCambio  = false;

// ── Whitelist (restaurada de NVS al arrancar)
static char    _kWhitelist[KAIZEN_MAX_WHITELIST][8];
static uint8_t _kNWhitelist   = 0;
static bool    _kAccesoLibre    = false;
static bool    _kAperturaRemota = false;
static bool    _kModoOcupacion  = false;

// ── Buffer de fichajes pendientes (respaldado en NVS)
static KaizenFichaje _kFichajes[KAIZEN_MAX_FICHAJES];
static uint8_t       _kNFichajes = 0;

static volatile bool    _kMsgRecibido  = false;
static volatile bool    _kMsgEnviado   = false;
static volatile bool    _kMsgEnviadoOK = false;
static KaizenMsg        _kMsgIn;
static volatile uint8_t _kLenMsgIn    = 0;
static KaizenMsg        _kMsgResp;
static uint8_t          _kLenMsgResp  = 0;
static uint8_t          _kSeqEsperada = 0;
static bool             _kPrimerACK   = true;

// ── OTA via ESP-NOW
static uint32_t _kOtaTotal    = 0;
static uint32_t _kOtaProgress = 0;
static bool     _kOtaStarted  = false;

// ─────────────────────────────────────────────────────────────
// Persistencia NVS — Whitelist
// ─────────────────────────────────────────────────────────────
static void _kSaveWhitelist() {
    _kPrefs.begin("kaizen", false);
    _kPrefs.putBool("libre",    _kAccesoLibre);
    _kPrefs.putBool("modo",     _kModoOcupacion);
    _kPrefs.putUChar("nwl",     _kNWhitelist);
    if (_kNWhitelist > 0) {
        _kPrefs.putBytes("wl", _kWhitelist, _kNWhitelist * 8);
    }
    _kPrefs.putString("nombre", _kNombre);
    _kPrefs.end();
}

static void _kLoadWhitelist() {
    _kPrefs.begin("kaizen", true);
    _kAccesoLibre   = _kPrefs.getBool("libre", false);
    _kModoOcupacion = _kPrefs.getBool("modo",  false);
    _kNWhitelist    = _kPrefs.getUChar("nwl",  0);
    if (_kNWhitelist > KAIZEN_MAX_WHITELIST) _kNWhitelist = KAIZEN_MAX_WHITELIST;
    if (_kNWhitelist > 0) {
        _kPrefs.getBytes("wl", _kWhitelist, _kNWhitelist * 8);
    }
    String n = _kPrefs.getString("nombre", "Sin config");
    strncpy(_kNombre, n.c_str(), 31);
    _kNombre[31] = '\0';
    _kPrefs.end();
    Serial.printf("[NVS] Whitelist: %u mats, libre=%d, nombre='%s'\n",
                  _kNWhitelist, (int)_kAccesoLibre, _kNombre);
}

// ─────────────────────────────────────────────────────────────
// Persistencia NVS — Buffer de fichajes
// ─────────────────────────────────────────────────────────────
static void _kSaveFichajes() {
    _kPrefs.begin("kaizen", false);
    _kPrefs.putUChar("fcnt", _kNFichajes);
    if (_kNFichajes > 0) {
        _kPrefs.putBytes("fdata", _kFichajes,
                         _kNFichajes * sizeof(KaizenFichaje));
    }
    _kPrefs.end();
}

static void _kLoadFichajes() {
    _kPrefs.begin("kaizen", true);
    _kNFichajes = _kPrefs.getUChar("fcnt", 0);
    if (_kNFichajes > KAIZEN_MAX_FICHAJES) _kNFichajes = KAIZEN_MAX_FICHAJES;
    if (_kNFichajes > 0) {
        _kPrefs.getBytes("fdata", _kFichajes,
                         _kNFichajes * sizeof(KaizenFichaje));
    }
    _kPrefs.end();
    Serial.printf("[NVS] Fichajes pendientes: %u\n", _kNFichajes);
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

    if (!_kBridgeOK) {
        Serial.println("[ESPNOW] *** BRIDGE ONLINE ***");
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


        case KAIZEN_COMPLETO: {
            // flags(1) + n_mats(1) + mats(n×8) + len_nombre(1) + nombre
            uint8_t flags = _kMsgIn.data[0];
            _kAccesoLibre   = (flags & 0x01) != 0;
            if (flags & 0x02) {
                _kAperturaRemota = true;
                Serial.println("[BRIDGE] Apertura remota solicitada");
            }
            _kModoOcupacion = (flags & 0x04) != 0;

            uint8_t nMats = _kMsgIn.data[1];
            if (nMats > KAIZEN_MAX_WHITELIST) nMats = KAIZEN_MAX_WHITELIST;
            _kNWhitelist = nMats;
            for (uint8_t i = 0; i < nMats; i++) {
                memcpy(_kWhitelist[i], &_kMsgIn.data[2 + i * 8], 8);
            }

            uint16_t cursor   = 2 + nMats * 8;
            uint8_t lenNombre = _kMsgIn.data[cursor];
            if (lenNombre > 31) lenNombre = 31;
            if (lenNombre > 0) {
                memcpy(_kNombre, &_kMsgIn.data[cursor + 1], lenNombre);
            }
            _kNombre[lenNombre] = '\0';

            _kSaveWhitelist();
            _kEstadoCambio = true;

            Serial.printf("[BRIDGE] libre=%d mats=%u nombre='%s'\n",
                          (int)_kAccesoLibre, nMats, _kNombre);

            // Respuesta: n_fichajes(1) + [mat(8)+epoch(4LE)+aut(1)]×n
            uint8_t nResp = (_kNFichajes < KAIZEN_MAX_FICHAJES_RESP)
                          ? _kNFichajes : KAIZEN_MAX_FICHAJES_RESP;
            uint8_t buf[1 + KAIZEN_MAX_FICHAJES_RESP * KAIZEN_FICHAJE_SIZE];
            uint16_t pos = 0;
            buf[pos++] = nResp;
            for (uint8_t i = 0; i < nResp; i++) {
                memcpy(&buf[pos], _kFichajes[i].matricula, 8); pos += 8;
                buf[pos++] = (_kFichajes[i].epoch >>  0) & 0xFF;
                buf[pos++] = (_kFichajes[i].epoch >>  8) & 0xFF;
                buf[pos++] = (_kFichajes[i].epoch >> 16) & 0xFF;
                buf[pos++] = (_kFichajes[i].epoch >> 24) & 0xFF;
                buf[pos++] = _kFichajes[i].autorizado;
                buf[pos++] = _kFichajes[i].tipo;
            }

            if (_kResponder(K_OK, buf, (uint8_t)pos)) {
                if (nResp > 0) {
                    uint8_t remaining = _kNFichajes - nResp;
                    if (remaining > 0) {
                        memmove(_kFichajes, _kFichajes + nResp,
                                remaining * sizeof(KaizenFichaje));
                    }
                    _kNFichajes = remaining;
                    _kSaveFichajes();
                    Serial.printf("[BRIDGE] %u fichajes enviados, %u pendientes\n",
                                  nResp, remaining);
                }
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
    _kLoadWhitelist();
    _kLoadFichajes();

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, true); // desconectar de AP (borra credenciales) pero mantiene el radio ON

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESPNOW] Error al inicializar ESP-NOW");
        return false;
    }

    esp_now_register_recv_cb(_kOnRecv);
    esp_now_register_send_cb(_kOnSent);

    // Mostrar MAC por serie para que el Bridge pueda registrar este dispositivo
    String mac = WiFi.macAddress();
    Serial.printf("[ESPNOW] MAC: %s\n", mac.c_str());

    Serial.println("[ESPNOW] Esperando Bridge...");
    return true;
}

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

bool        kaizen_isBridgeOK()       { return _kBridgeOK; }
const char* kaizen_getNombre()        { return _kNombre; }
bool        kaizen_accesoLibre()      { return _kAccesoLibre; }
bool        kaizen_modoOcupacion()    { return _kModoOcupacion; }

bool kaizen_hayAperturaRemota() {
    bool v = _kAperturaRemota;
    _kAperturaRemota = false;
    return v;
}

bool kaizen_estaAutorizado(const char *matricula) {
    for (uint8_t i = 0; i < _kNWhitelist; i++) {
        if (memcmp(_kWhitelist[i], matricula, 8) == 0) return true;
    }
    return false;
}

void kaizen_registrarFichaje(const char *matricula, bool autorizado,
                             uint32_t epoch, uint8_t tipo) {
    if (_kNFichajes >= KAIZEN_MAX_FICHAJES) {
        // Buffer lleno: descartar el más antiguo
        memmove(_kFichajes, _kFichajes + 1,
                (KAIZEN_MAX_FICHAJES - 1) * sizeof(KaizenFichaje));
        _kNFichajes = KAIZEN_MAX_FICHAJES - 1;
    }
    memcpy(_kFichajes[_kNFichajes].matricula, matricula, 8);
    _kFichajes[_kNFichajes].epoch      = epoch;
    _kFichajes[_kNFichajes].autorizado = autorizado ? 1u : 0u;
    _kFichajes[_kNFichajes].tipo       = tipo;
    _kNFichajes++;
    _kSaveFichajes();
    Serial.printf("[FICHAJE] %c%.8s epoch=%u tipo=%u\n",
                  autorizado ? '+' : '-', matricula, epoch, tipo);
}

uint32_t kaizen_tiempoSinBridge() {
    if (_kBridgeOK)        return 0;
    if (_kTUltimoMsg == 0) return UINT32_MAX;
    return millis() - _kTUltimoMsg;
}

bool kaizen_hayEstadoCambio() {
    bool v = _kEstadoCambio;
    _kEstadoCambio = false;
    return v;
}
