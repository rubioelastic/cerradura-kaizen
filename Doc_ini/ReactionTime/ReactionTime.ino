#include <M5Dial.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_idf_version.h>  // para detectar IDF >= 5 y usar la nueva API
#include <esp_task_wdt.h>



extern "C" {
#include "esp_wifi.h"    // esp_wifi_get_mac, WIFI_IF_STA/AP
#include "esp_system.h"  // esp_efuse_mac_get_default (opcional)
}






#include <ArduinoOTA.h>

#include "COMWiFi.hpp"
#include "Logos.hpp"
#include "LCD_Funciones.hpp"

#define MAX_MATRICULAS 64
char matriculasPendientesEnviar[MAX_MATRICULAS][9];  // 8 chars + '\0'
uint8_t numMatriculasPendientesEnviar = 0;

char matriculasConfirmadas[MAX_MATRICULAS][9];  // 8 chars + '\0'
uint8_t numMatriculasConfirmadas = 0;

#define WIFI_CHANNEL 1
#define REINICIO_TRAS_NO_RECIBIR_MENSAJES_EN 120000

#define PERIODO_REFRESCOANTIQUEMAZOS 300000
#define PERIODO_REFRESCOWIFISIGNAL 2000
#define PERIODO_APAGARPANTALLA 600000  //10 minutos

#define TIEMPO_PARPADEO 1000
#define DELAY_BETWEEN_CARDS 0

#define TIMEOUT_SEND 2000
#define INTENTOS_SEND 1



#define ICON_X 90
#define ICON_Y 80
#define ICON_W 60
#define ICON_H 60

#define GENERANDO_OT_Y 176
#define GENERANDO_OT_Y_2 122

const uint32_t version = 10;
static bool DEBUG = true;  // GND para activar

uint32_t totalBytesUpdate = 0;
uint32_t progressBytesUpdate = 0;

// ----- Encoder por sondeo -----
constexpr int PIN_ENC_A = 41;  // M5Dial: A=G41
constexpr int PIN_ENC_B = 40;  //          B=G40
int8_t quad_last = 0;
long enc_value = 0;
int32_t enc_last_for_ui = 0;

// ----- ESP-NOW -----
esp_now_peer_info_t slave;
const esp_now_peer_info_t *peer = &slave;

static uint64_t horaApagarPantalla;
uint8_t macServer[6];

#define LENGTH_SEQ 1
#define LENGTH_CRC 1
#define LENGTH_COMMAND 2
#define MAXMENSAJE 250

struct Mensaje {
  uint8_t seq = 0;
  uint8_t crc = 0;
  uint16_t comando = 0;
  uint8_t data[246];
} mensaje;

uint8_t seq_esperada = 0;
volatile uint8_t lengthMensaje = 0;

volatile uint8_t lengthMensajeRespuesta = 0;
Mensaje mensajeRespuesta;

volatile bool mensajeRecibidoFromBridge = false;
volatile bool mensajeEnviadoCorrectamente = false;
volatile bool mensajeEnviado = false;



unsigned short lengthLinea=0;
char nombreLinea[50];



// El dato que deseas codificar en el QR.
const char* QR_BASE_URL = "https://clp.bshg.com/flp#OPRAMOBILE_OR-search&/detail/ID/"; // Texto por defecto

// El M5Dial tiene una resolución de 240x240.
// Definimos el tamaño del QR (180 píxeles es un buen tamaño centrado).
const int QR_SIZE_BASE = 99; 
 int QR_SIZE = QR_SIZE_BASE; 
const int QR_X = 25;
const int QR_Y = 92; // Posición inicial, dejando espacio para el título








static uint64_t now = millis();

// ----- ESTADOS DE PANTALLA -----
enum class Estado : uint8_t {
  SIN_DEFINIR = 255,
  REPOSO = 0,
  ESPERANDO_MANT = 1,
  ESPERANDO_FIN_PARO = 3,
  REGISTRANDO_LLAMADA = 4,
  SIN_COBERTURA = 5  // ya llegó alguna, pueden llegar más
};

/*Estado estado = Estado::OK_0;
Estado estado_prev = Estado::OK_0;*/

Estado pantalla_requerida = Estado::SIN_COBERTURA;
Estado pantalla_actual = Estado::SIN_DEFINIR;



bool hayLlamada = false;
uint32_t tiempo_ultimo_llamada = 0;
static bool botonLlamadaPulsado = false;
uint32_t marca_tiempo_cronometro = 0;
bool FirstACK = true;
bool seHaRecibidoUnMensajeAlgunaVez = false;
uint32_t last_segundos = 0;
int32_t ot = 0;

// ---- Compat: detección de API nueva de ESP-NOW (IDF >= 5) ----
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#define ESP_NOW_NEW_API 1
#else
#define ESP_NOW_NEW_API 0
#endif

// ----- Callbacks ESP-NOW -----
#if ESP_NOW_NEW_API
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int data_len) {
  if (data_len > MAXMENSAJE) {
    ESP.restart();
    return;
  }
  memcpy(macServer, recv_info->src_addr, 6);
  lengthMensaje = (uint8_t)data_len;
  memcpy(&mensaje, data, lengthMensaje);
  mensajeRecibidoFromBridge = true;
}
void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  mensajeEnviadoCorrectamente = (status == ESP_NOW_SEND_SUCCESS);
  mensajeEnviado = true;
}
#else
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
  if (data_len > MAXMENSAJE) {
    ESP.restart();
    return;
  }
  memcpy(macServer, mac_addr, 6);
  lengthMensaje = (uint8_t)data_len;
  memcpy(&mensaje, data, lengthMensaje);
  mensajeRecibidoFromBridge = true;
}
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  mensajeEnviadoCorrectamente = (status == ESP_NOW_SEND_SUCCESS);
  mensajeEnviado = true;
}
#endif

void hard_reset(int punto_reset) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  esp_task_wdt_config_t cfg = {
    .timeout_ms = 1000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_task_wdt_deinit();
  ESP_ERROR_CHECK(esp_task_wdt_init(&cfg));
#else
  ESP_ERROR_CHECK(esp_task_wdt_init(1, true));
#endif
  ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
  while (true) {}
}


// ----- Encoder (polling) -----
void encInit() {
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  quad_last = (digitalRead(PIN_ENC_A) ? 1 : 0) | (digitalRead(PIN_ENC_B) ? 2 : 0);
}
void encPoll() {
  static const int8_t tbl[16] = {
    0, -1, +1, 0, +1, 0, 0, -1, -1, 0, 0, +1, 0, +1, -1, 0
  };
  int8_t nowAB = (digitalRead(PIN_ENC_A) ? 1 : 0) | (digitalRead(PIN_ENC_B) ? 2 : 0);
  enc_value += tbl[(quad_last << 2) | nowAB];
  quad_last = nowAB;


  /*Serial.println(enc_value);*/
  QR_SIZE = QR_SIZE_BASE + enc_value;
  //Serial.println(QR_SIZE);
}

// ----- UI helpers -----
static void printMac(const char *label, const uint8_t mac[6]) {
  Serial.printf("%s %02X:%02X:%02X:%02X:%02X:%02X\n",
                label, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
uint8_t mac[6];
void ShowInfo() {
  // MAC de la interfaz Station (cliente WiFi)
  esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, mac);
  if (err == ESP_OK) {
    printMac("STA MAC:", mac);
  } else {
    Serial.printf("esp_wifi_get_mac(WIFI_IF_STA) error=%d\n", (int)err);
  }

  // MAC de la interfaz SoftAP
  /*err = esp_wifi_get_mac(WIFI_IF_AP, mac);
  if (err == ESP_OK) {
    printMac("AP  MAC:", mac);
  } else {
    Serial.printf("esp_wifi_get_mac(WIFI_IF_AP) error=%d\n", (int)err);
  }*/

  // (Opcional) MAC base de eFuse, sin depender de esp_wifi_init
  /*uint8_t base[6];
  esp_efuse_mac_get_default(base);
  printMac("BASE MAC (eFuse):", base);*/
}

void LabelReactionTime(int fontColor, int bgColor) {

  //M5PrintCenterF(M5Dial.Display, 0, 195, 240, 16, 2, fontColor, bgColor, "Reaction time");
  M5PrintCenterF(M5Dial.Display, 0, 29, 240, 16, 2, fontColor, bgColor, "Reaction time");
}

void LabelCronometro(int fontColor, int bgColor) {

  // ---------- cálculos de tiempo comunes ----------
  uint32_t trans_llamada = 0;
  if (numMatriculasConfirmadas == 0) {
    trans_llamada = (millis() - marca_tiempo_cronometro) + tiempo_ultimo_llamada;
  } else {
    trans_llamada = tiempo_ultimo_llamada;
  }


  uint32_t h = trans_llamada / 3600000UL;
  uint32_t m = (trans_llamada % 3600000UL) / 60000UL;
  uint32_t s = (trans_llamada % 60000UL) / 1000UL;

  if (h == 0)
    M5PrintCenterF(M5Dial.Display, 0, 53, 240, 28, 4, fontColor, bgColor, "%02u:%02u", m, s);
  else
    M5PrintCenterF(M5Dial.Display, 0, 53, 240, 28, 4, fontColor, bgColor, "%01u:%02u:%02u", h, m, s);
}
// ====== DIBUJO POR ESTADO ======
void Draw_Reposo() {
  auto &d = M5Dial.Display;
  if (pantalla_actual != pantalla_requerida) {
    d.fillScreen(GREEN);
    //M5PrintCenter(M5Dial.Display, 0, 0, 240, 10, 4, BLACK, GREEN, "Hola");
    //M5PrintCenterF(M5Dial.Display, 0, 120, 240, 120, 4, BLACK, GREEN, "Hola %i",100);
    d.drawJpg(Llamada_blanco, sizeof(Llamada_blanco), 60, 60);
    M5PrintCenterF(M5Dial.Display, 40, 195, 160, 24, 2, BLACK, GREEN, nombreLinea);
    //botonLlamadaPulsado = false;
    pantalla_actual = pantalla_requerida;
    Serial.println("Activar pantalla Draw_Reposo");
  }


  if (botonLlamadaPulsado) {
    static uint32_t siguienteParpadeo2 = 0;
    static bool mostrar = false;
    if (now > siguienteParpadeo2) {
      //d.drawJpg(Llamada_blanco, sizeof(Llamada_blanco), 107, 213);
      if (mostrar) {
        Serial.println("NORMAL");
        M5Dial.Display.drawJpg(Llamada_blanco, sizeof(Llamada_blanco), 60, 60);
      } else {
        Serial.println("GIRO");
        M5Dial.Display.setRotation(1);  // 0–3, en pasos de 90°
        M5Dial.Display.drawJpg(Llamada_blanco, sizeof(Llamada_blanco), 60, 60);
        M5Dial.Display.setRotation(0);  // volver a normal
      }
      mostrar = !mostrar;
      siguienteParpadeo2 = now + 500;
    }
  }
}


static int ang = 0;
int r = 40;
int nBolos = 20;
uint8_t angPorIter = 12;
int cx = QR_X + (QR_SIZE_BASE/2)-5, cy = 102+ (r);

void CirculoCargando(int fColor, int bgColor){
  static uint32_t siguienteParpadeoCargando= 0;
  if (now > siguienteParpadeoCargando) {
    float a = ang  * DEG_TO_RAD;
    int x = cx + cos(a) * r;
    int y = cy + sin(a) * r;
    M5Dial.Display.fillCircle(x, y, 6, fColor);

    a = (ang - (nBolos*angPorIter) ) * DEG_TO_RAD;
    x = cx + cos(a) * (r+1);
    y = cy + sin(a) * (r+1);
    M5Dial.Display.fillCircle(x, y, 8, bgColor);

    ang = (ang + angPorIter) % 360;
    siguienteParpadeoCargando = now + 50;
  }
}


void Draw_EsperandoMant() {
  auto &d = M5Dial.Display;
  static int color = YELLOW;
  static int fontColor = BLACK;
  static uint32_t siguienteParpadeo = 0;
  static uint32_t siguienteParpadeoIcono = 0;
  static bool otMostrada = false;
  if (pantalla_actual != pantalla_requerida) {
    d.fillScreen(color);
    LabelReactionTime(fontColor, color);
    d.fillRect(0, 90, 240, 1, BLACK);
    d.fillRect(0, 192, 240, 1, BLACK);

    M5PrintCenterF(M5Dial.Display, 40, 195, 160, 24, 2, fontColor, color, nombreLinea);
    M5PrintCenterF(M5Dial.Display, 48, GENERANDO_OT_Y_2, 40, 40, 3, fontColor, color, "OT");

    otMostrada = false;

    siguienteParpadeo = 0;
    siguienteParpadeoIcono = 0;
    pantalla_actual = pantalla_requerida;
  }

  if (now > siguienteParpadeo) {
    LabelCronometro(fontColor, color);
    siguienteParpadeo = now + TIEMPO_PARPADEO;
  }

  if (now > siguienteParpadeoIcono) {
    static bool mostrarLogo=true;
    if (mostrarLogo)
      d.drawJpg(logo_mantenimiento_amarillo, sizeof(logo_mantenimiento_amarillo), QR_X+QR_SIZE+15, QR_Y);
    else
      d.fillRect(QR_X+QR_SIZE+15, QR_Y, 80, 66, color);
    
    siguienteParpadeoIcono = now + 500;
    mostrarLogo=!mostrarLogo;
  }


  if (ot > 0)
  {
    if (!otMostrada){
      char qr_dynamic_content[100];
      snprintf(qr_dynamic_content, 100, "%s%i", QR_BASE_URL, ot);
      M5.Display.qrcode(qr_dynamic_content, QR_X, QR_Y, QR_SIZE);
      otMostrada=true;
    }
  }
  else{
    CirculoCargando(fontColor, color);

  }


}


void Draw_EsperandoJustificarParo() {
  auto &d = M5Dial.Display;
  static int color = BLUE;
  static int fontColor = WHITE;
  static uint32_t siguienteParpadeo = 0;
  static bool mostrado = true;
  static bool otMostrada = false;
  if (pantalla_actual != pantalla_requerida) {
    color = BLUE;
    d.fillScreen(color);
    LabelReactionTime(fontColor, color);
    LabelCronometro(fontColor, color);
    d.fillRect(0, 90, 240, 1, BLACK);
    d.fillRect(0, 192, 240, 1, BLACK);


    /*char qr_dynamic_content[100];
    snprintf(qr_dynamic_content, 100, "%s%i", QR_BASE_URL, ot);
    M5.Display.qrcode(qr_dynamic_content, QR_X, QR_Y, QR_SIZE);*/

    d.drawJpg(logo_mantenimiento_azul, sizeof(logo_mantenimiento_azul), QR_X+QR_SIZE+15, QR_Y);


    M5PrintCenterF(M5Dial.Display, 40, 195, 160, 24, 2, fontColor, color, nombreLinea);
    M5PrintCenterF(M5Dial.Display, 48, GENERANDO_OT_Y_2, 40, 40, 3, fontColor, color, "OT");

    mostrado=true;
    otMostrada=false;

    siguienteParpadeo = 0;
    pantalla_actual = pantalla_requerida;
  }

  if (numMatriculasPendientesEnviar > 0){
    if (now > siguienteParpadeo){
      if (mostrado)
        M5PrintCenterF(M5Dial.Display, QR_X+QR_SIZE+15, QR_Y+66+5, 80, 24, 3, fontColor, color, "%u", numMatriculasConfirmadas);
      else
        d.fillRect(QR_X+QR_SIZE+15, QR_Y+66+5, 80, 24, color);

      mostrado = !mostrado;
      siguienteParpadeo=now+500;
    }
  }else{
    if (mostrado)
      M5PrintCenterF(M5Dial.Display, QR_X+QR_SIZE+15, QR_Y+66+5, 80, 24, 3, fontColor, color, "%u", numMatriculasConfirmadas);
    mostrado=false;
  }



  if (ot > 0)
  {
    if (!otMostrada){
      //M5PrintCenterF(M5Dial.Display, QR_X, QR_Y+QR_SIZE+5, QR_SIZE, 10, 1, fontColor, color, "%i", ot);
      char qr_dynamic_content[100];
      snprintf(qr_dynamic_content, 100, "%s%i", QR_BASE_URL, ot);
      M5.Display.qrcode(qr_dynamic_content, QR_X, QR_Y, QR_SIZE);
      otMostrada=true;
    }
  }
  else{
    CirculoCargando(fontColor, color);

  }



  //M5PrintCenterF(M5Dial.Display, 40, 195, 160, 24, 3, fontColor, color, nombreLinea);


  //M5PrintCenterF(M5Dial.Display, QR_X+QR_SIZE+15, QR_Y+66+5, 80, 24, 3, fontColor, color, "%u", numMatriculasConfirmadas);
}

void Draw_SinCobertura() {
  auto &d = M5Dial.Display;
  static int color = BLUE;
  static uint32_t siguienteParpadeo = 0;
  if (pantalla_actual != pantalla_requerida) {
    color = BLUE;
    siguienteParpadeo = 0;
    pantalla_actual = pantalla_requerida;
  }

  if (now > siguienteParpadeo) {
    color = (color == BLUE) ? BLACK : BLUE;

    d.fillScreen(color);
    //M5.Display.setFont(&fonts::FreeSans9pt7b);
    M5PrintCenter(M5Dial.Display, 0, 92, 240, 28, 4, WHITE, color, "Sin se\244al");
    //M5PrintCenter(M5Dial.Display, 0, 92, 240, 28, 4, WHITE, color, "Sin se\361al");
    //M5PrintCenter(M5Dial.Display, 0, 92, 240, 28, 4, WHITE, color, "Sin señal");
    //M5.Display.setFont(nullptr);
    M5PrintCenterF(M5Dial.Display, 0, 130, 240, 28, 4, WHITE, color, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    siguienteParpadeo += 1000;
  }
}



// ----- Logs y protocolo (tus funciones tal cual) -----
void MostrarMensajeRecibido() {
  if (!DEBUG) return;
  if (seq_esperada != mensaje.seq)
    Serial.printf("SEQ Expected: %u, received %u\n", seq_esperada, mensaje.seq);
  Serial.printf("From Bridge: SEQ: %03d | CMD: 0x%04X", mensaje.seq, mensaje.comando);
  Serial.printf(" <");
  uint8_t i = 0;
  while (i < (lengthMensaje - 4)) {
    Serial.printf("0x%02X", mensaje.data[i]);
    i++;
    if (i < lengthMensaje - 4) Serial.printf(" ");
  }
  Serial.printf(">\n");
}

uint8_t CalcularCRC() {
  uint8_t aux = 0;
  int i = 0;
  while (i < lengthMensaje - (LENGTH_SEQ + LENGTH_CRC + LENGTH_COMMAND)) {
    aux += mensaje.data[i];
    i++;
  }
  aux += mensaje.seq;
  aux += *((uint8_t *)&mensaje.comando);
  aux += *(((uint8_t *)&mensaje.comando) + 1);
  return aux;
}
uint8_t CalcularCRCRespuesta() {
  uint8_t aux = 0;
  int i = 0;
  while (i < lengthMensajeRespuesta - (LENGTH_SEQ + LENGTH_CRC + LENGTH_COMMAND)) {
    aux += mensajeRespuesta.data[i];
    i++;
  }
  aux += mensajeRespuesta.seq;
  aux += *((uint8_t *)&mensajeRespuesta.comando);
  aux += *(((uint8_t *)&mensajeRespuesta.comando) + 1);
  return aux;
}

void MostrarMensajeEnviado(bool esUnReenvio) {
  if (!DEBUG) return;
  if (esUnReenvio) Serial.printf("ToTo Bridge: SEQ: %03d | CMD: 0x%04X", mensajeRespuesta.seq, mensajeRespuesta.comando);
  else Serial.printf("  To Bridge: SEQ: %03d | CMD: 0x%04X", mensajeRespuesta.seq, mensajeRespuesta.comando);
  Serial.printf(" <");
  uint8_t i = 0;
  while (i < lengthMensajeRespuesta - 4) {
    Serial.printf("0x%02X", mensajeRespuesta.data[i]);
    i++;
    if (i < lengthMensajeRespuesta - 4) Serial.printf(" ");
  }
  Serial.printf(">");
}
bool Response(uint16_t comando, uint8_t *datos, uint8_t lenDatos, uint8_t intentos) {
  if (intentos == 0) { return false; }

  mensajeRespuesta.comando = comando;
  lengthMensajeRespuesta = lenDatos + LENGTH_SEQ + LENGTH_CRC + LENGTH_COMMAND;
  if (lenDatos && datos) memcpy(mensajeRespuesta.data, datos, lenDatos);
  mensajeRespuesta.seq = seq_esperada;
  mensajeRespuesta.crc = CalcularCRCRespuesta();

  MostrarMensajeEnviado(false);

  mensajeEnviado = false;
  esp_err_t r = esp_now_send(macServer, (uint8_t *)&mensajeRespuesta, lengthMensajeRespuesta);

  unsigned long limite = millis() + TIMEOUT_SEND;
  while (!mensajeEnviado && millis() < limite) { vTaskDelay(5); }
  if (!mensajeEnviado) {
    Serial.println("ERRRORRRRRRRRRRRRRR  Linea 510");
    hard_reset(1);
  }

  if (!mensajeEnviadoCorrectamente) {
    if (DEBUG) Serial.printf(" ERROR\n\n");
    return Response(comando, datos, lenDatos, intentos - 1);
  }

  if (r == ESP_OK) {
    if (DEBUG) Serial.printf(" OK\n\n");
    seq_esperada++;
    if (seq_esperada == 0) seq_esperada++;
    return true;
  }
  if (DEBUG) Serial.printf(" ERROR 2\n\n");
  return false;
}
bool Response(uint16_t comando, uint8_t *datos, uint8_t lenDatos) {
  return Response(comando, datos, lenDatos, INTENTOS_SEND);
}
bool Response(uint16_t comando, bool dato) {
  uint8_t datos[1] = { (uint8_t)dato };
  return Response(comando, datos, 1);
}
bool Response(uint16_t comando) {
  return Response(comando, NULL, 0);
}

bool ReenviarUltimoMensaje() {
  MostrarMensajeEnviado(true);
  mensajeEnviado = false;
  esp_err_t r = esp_now_send(macServer, (uint8_t *)&mensajeRespuesta, lengthMensajeRespuesta);
  unsigned long limite = millis() + TIMEOUT_SEND;
  while (!mensajeEnviado && millis() < limite) { vTaskDelay(5); }
  if (!mensajeEnviado) { hard_reset(2); }

  if (!mensajeEnviadoCorrectamente) {
    if (DEBUG) Serial.printf(" ERROR\n");
    return false;
  }
  if (r == ESP_OK) {
    if (DEBUG) Serial.printf(" OK\n\n");
    return true;
  }
  if (DEBUG) Serial.printf(" ERROR 2\n");
  return false;
}

bool GetBitValue(uint8_t byte, uint8_t indiceBit) {
  if (indiceBit > 7) return false;
  return (byte & (1 << indiceBit)) != 0;
}
void SetBitValue(uint8_t &byte, uint8_t indiceBit, bool value) {
  if (indiceBit > 7) return;
  if (value) byte |= (1 << indiceBit);
  else byte &= ~(1 << indiceBit);
}


void ComprobarGestionarmensajeRecibidoFromBridge() {
  static uint64_t ultimoMensajeRecibido = now;
  if (!mensajeRecibidoFromBridge) {
    if ((ultimoMensajeRecibido + REINICIO_TRAS_NO_RECIBIR_MENSAJES_EN) < now)
      hard_reset(4);
    return;
  }

  seHaRecibidoUnMensajeAlgunaVez = true;
  ultimoMensajeRecibido = now;
  mensajeRecibidoFromBridge = false;

  if (!esp_now_is_peer_exist(macServer)) {
    slave.channel = WIFI_CHANNEL;
    slave.encrypt = 0;
    slave.ifidx = WIFI_IF_STA;
    memcpy(&slave.peer_addr, macServer, 6);
    int32_t i = esp_now_add_peer(peer);
    if (i == ESP_OK) {
    } else if (DEBUG) {
      Serial.println("esp_now_add_peer != ESP_OK");
    }
  }

  MostrarMensajeRecibido();

  uint8_t seqaux2 = seq_esperada + 1;
  if (seqaux2 == 0) { seqaux2++; }
  if (mensaje.seq == seqaux2) {
    seq_esperada++;
    if (seq_esperada == 0) { seq_esperada++; }
    if (DEBUG) Serial.printf("Asumimos secuencia recibida como buena\n");
  }
  if (mensaje.seq != seq_esperada && mensaje.seq != 0) {
    if (seq_esperada == 0) {
      Response(BAD_SECUENCE, &seq_esperada, 1);
      seq_esperada = 0;
    } else {
      uint8_t seqaux = seq_esperada - 1;
      if (seqaux == 0) { seqaux--; }
      if (seqaux == mensaje.seq) {
        ReenviarUltimoMensaje();
      } else if (mensaje.seq == 0) {
        ReenviarUltimoMensaje();
      } else {
        if (DEBUG) { Serial.printf("\n\nCASO SIN CONTEMPLAR, CREO QUE SE HA REINICIADO EL BRIDGE\n\n"); }
      }
    }
  } else if (mensaje.crc != CalcularCRC()) {
    Response(BAD_CRC, &seq_esperada, 1);
  } else {
    if (mensaje.comando == ACK) {
      if (FirstACK) {
        Response(DISCONNECT);
        FirstACK = false;
      } else {
        Response(OK);
      }
    } else if (mensaje.comando == MENSAJE_COMPLETO_LLEGADA_MANTENIMIENTO) {
      marca_tiempo_cronometro = millis();
      hayLlamada = GetBitValue(mensaje.data[0], 0);
      tiempo_ultimo_llamada = (mensaje.data[4] << 24) | (mensaje.data[3] << 16) | (mensaje.data[2] << 8) | mensaje.data[1];
      ot = (mensaje.data[8] << 24) | (mensaje.data[7] << 16) | (mensaje.data[6] << 8) | mensaje.data[5];

      uint8_t matriculasRecibidas = mensaje.data[9];
      numMatriculasConfirmadas = 0;
      uint8_t i = 0;
      while (i < matriculasRecibidas) {
        if (!ExisteMatriculaConfirmadas((const char *)&mensaje.data[10 + (i * 8)])) {
          memcpy(matriculasConfirmadas[numMatriculasConfirmadas], &mensaje.data[10 + (i * 8)], 8);
          matriculasConfirmadas[numMatriculasConfirmadas][8] = '\0';
          numMatriculasConfirmadas++;
        }
        i++;
      }




      uint16_t cursor = 10 + matriculasRecibidas * 8;
      i = mensaje.data[cursor];
      if (i > 0){
        lengthLinea=i;
        memcpy(nombreLinea, &mensaje.data[cursor+1], lengthLinea);
        nombreLinea[lengthLinea] = '\0';   // ← AQUÍ CIERRAS LA CADENA
        //Serial.println(nombreLinea);
      }







      byte mix = 0;
      SetBitValue(mix, 0, botonLlamadaPulsado);
      SetBitValue(mix, 1, lengthLinea != 0);

      uint8_t buffer[1 + 1 + (MAX_MATRICULAS * 8)];
      uint16_t pos = 0;
      buffer[pos++] = mix;
      buffer[pos++] = (uint8_t)numMatriculasPendientesEnviar;
      for (uint16_t i = 0; i < numMatriculasPendientesEnviar; i++) {
        memcpy(&buffer[pos], matriculasPendientesEnviar[i], 8);
        pos += 8;
      }

      if (Response(OK, buffer, pos)) {
        botonLlamadaPulsado = false;

        /*i = 0;
        while (i < numMatriculasPendientesEnviar) {

          if (!ExisteMatriculaConfirmadas((const char *)&matriculasPendientesEnviar[i])) {
            memcpy(matriculasConfirmadas[numMatriculasConfirmadas], &matriculasPendientesEnviar[i], 8);
            matriculasConfirmadas[numMatriculasConfirmadas][8] = '\0';
            numMatriculasConfirmadas++;
          }
          i++;
        }*/
        numMatriculasPendientesEnviar = 0;
      }






      /***********   V2
      pantalla_requerida = static_cast<Estado>(mensaje.data[0]);
      //Serial.println(pantalla_requerida);
      incrementarContadorReaccion = pantalla_requerida == Estado::ESPERANDO_MANT;

      marca_tiempo_cronometro=millis();
      tiempo_ultimo_llamada = (mensaje.data[4] << 24) | (mensaje.data[3] << 16) | (mensaje.data[2] << 8) | mensaje.data[1];

      uint8_t recibidas = mensaje.data[5];
      Serial.printf("Matriculas confirmadas %u\n",numMatriculasConfirmadas);

      numMatriculasConfirmadas=0;
      uint8_t i = 0;
      while (i < recibidas){
        if (!ExisteMatriculaConfirmadas((const char*)&mensaje.data[10+(i*8)])) {
          memcpy(matriculasConfirmadas[numMatriculasConfirmadas], &mensaje.data[10+(i*8)], 8);
          matriculasConfirmadas[numMatriculasConfirmadas][8] = '\0';
          numMatriculasConfirmadas++;
        }
        i++;
      }

      uint8_t pos2 = 5 + (recibidas*8);

      ot = (mensaje.data[pos2+4] << 24) | (mensaje.data[pos2+3] << 16) | (mensaje.data[pos2+2] << 8) | mensaje.data[pos2+1];



      byte mix = 0;
      SetBitValue(mix, 0,botonLlamadaPulsado);

      uint8_t buffer[1 + 1 + (MAX_MATRICULAS * 8)];
      uint16_t pos = 0;
      buffer[pos++] = mix;
      buffer[pos++] = (uint8_t)numMatriculasPendientesEnviar;
      for (uint16_t i = 0; i < numMatriculasPendientesEnviar; i++) {
        memcpy(&buffer[pos], matriculasPendientesEnviar[i], 8);
        pos += 8;
      }

      if (Response(OK, buffer, pos)){
        botonLlamadaPulsado = false;

        i=0;
        while (i < numMatriculasPendientesEnviar){

          if (!ExisteMatriculaConfirmadas((const char*)&matriculasPendientesEnviar[i])) {
            memcpy(matriculasConfirmadas[numMatriculasConfirmadas], &matriculasPendientesEnviar[i], 8);
            matriculasConfirmadas[numMatriculasConfirmadas][8] = '\0';
            numMatriculasConfirmadas++;
          }
          i++;
        }
        numMatriculasPendientesEnviar=0;


      }*/
    } else if (mensaje.comando == ASK_VERSION) {
      uint8_t b[4] = { (uint8_t)(version >> 24), (uint8_t)(version >> 16), (uint8_t)(version >> 8), (uint8_t)version };
      Response(OK, b, 4);
    } else if (mensaje.comando == UPDATE) {
      static bool updateStarted = false;
      if (!updateStarted) {
        if (DEBUG) Serial.println("COMENZAR ACTUALIZACION");
        updateStarted = true;

        int lenDatos = lengthMensaje - (LENGTH_SEQ + LENGTH_CRC + LENGTH_COMMAND);
        if (lenDatos != 8) { hard_reset(5); }
        uint32_t indiceProgesoRecibido = (mensaje.data[3] << 24) | (mensaje.data[2] << 16) | (mensaje.data[1] << 8) | mensaje.data[0];
        if (indiceProgesoRecibido != progressBytesUpdate) {
          Response(OK, (uint8_t *)&progressBytesUpdate, 4);
        } else {
          if (indiceProgesoRecibido == 0) {
            totalBytesUpdate = (mensaje.data[7] << 24) | (mensaje.data[6] << 16) | (mensaje.data[5] << 8) | mensaje.data[4];
            if (!Update.begin(totalBytesUpdate)) { Update.printError(Serial); }
            Response(OK, (uint8_t *)&progressBytesUpdate, 4);
          }
        }
      } else {
        int lenDatos = lengthMensaje - (LENGTH_SEQ + LENGTH_CRC + LENGTH_COMMAND) - 4;

        uint32_t indiceProgesoRecibido = (mensaje.data[3] << 24) | (mensaje.data[2] << 16) | (mensaje.data[1] << 8) | mensaje.data[0];
        if (indiceProgesoRecibido == 0 && lenDatos == 8) {
          Response(OK, (uint8_t *)&progressBytesUpdate, 4);
        } else if (indiceProgesoRecibido != progressBytesUpdate) {
          Response(OK, (uint8_t *)&progressBytesUpdate, 4);
        } else {
          if (Update.write(&mensaje.data[4], lenDatos) != lenDatos) {
            Update.printError(Serial);
            hard_reset(6);
          }

          progressBytesUpdate += lenDatos;
          if (progressBytesUpdate == totalBytesUpdate) {
            if (DEBUG) Serial.println("FINALIZAR ACTUALIZACION");
            updateStarted = false;
            if (Update.end()) {
              Response(OK, (uint8_t *)&progressBytesUpdate, 4);
              hard_reset(7);
            } else {
              updateStarted = false;
              Update.printError(Serial);
              hard_reset(8);
            }
          } else {
            Response(OK, (uint8_t *)&progressBytesUpdate, 4);
          }
        }
      }
      horaApagarPantalla = now + PERIODO_APAGARPANTALLA;
      if (progressBytesUpdate > totalBytesUpdate) hard_reset(9);

    } else {
      Response(NOTFOUND);
    }
  }
}

bool ExisteMatriculaPendientesEnviar(const char *m) {
  for (uint16_t i = 0; i < numMatriculasPendientesEnviar; i++) {
    if (strncmp(matriculasPendientesEnviar[i], m, 8) == 0) return true;
  }
  return false;
}

bool ExisteMatriculaConfirmadas(const char *m) {
  for (uint16_t i = 0; i < numMatriculasConfirmadas; i++) {
    if (strncmp(matriculasConfirmadas[i], m, 8) == 0) return true;
  }
  return false;
}

void ComprobarTarjeta() {
  M5Dial.Rfid.PCD_AntennaOn();
  if (M5Dial.Rfid.PICC_IsNewCardPresent() && M5Dial.Rfid.PICC_ReadCardSerial()) {
    uint8_t piccType = M5Dial.Rfid.PICC_GetType(M5Dial.Rfid.uid.sak);
    if (piccType != MFRC522::PICC_TYPE_MIFARE_MINI && piccType != MFRC522::PICC_TYPE_MIFARE_1K && piccType != MFRC522::PICC_TYPE_MIFARE_4K) {
      M5Dial.Rfid.PICC_HaltA();
      M5Dial.Rfid.PCD_StopCrypto1();
      return;
    }

    MFRC522::MIFARE_Key key;
    for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

    byte block = 4;
    byte len = 18;
    byte buffer1[18];
    auto st = (MFRC522::StatusCode)
                M5Dial.Rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A,
                                             block, &key, &(M5Dial.Rfid.uid));
    if (st != MFRC522::STATUS_OK) {
      M5Dial.Rfid.PICC_HaltA();
      M5Dial.Rfid.PCD_StopCrypto1();
      return;
    }

    st = (MFRC522::StatusCode)M5Dial.Rfid.MIFARE_Read(block, buffer1, &len);
    if (st != MFRC522::STATUS_OK) {
      M5Dial.Rfid.PICC_HaltA();
      M5Dial.Rfid.PCD_StopCrypto1();
      return;
    }

    M5Dial.Rfid.PICC_HaltA();
    M5Dial.Rfid.PCD_StopCrypto1();
    M5Dial.Rfid.PCD_AntennaOff();

    if (numMatriculasPendientesEnviar < MAX_MATRICULAS && !ExisteMatriculaPendientesEnviar((const char *)&buffer1[8])) {
      memcpy(matriculasPendientesEnviar[numMatriculasPendientesEnviar], &buffer1[8], 8);
      matriculasPendientesEnviar[numMatriculasPendientesEnviar][8] = '\0';
      numMatriculasPendientesEnviar++;
    }
    M5.Speaker.tone(3600, 200);

    if (DEBUG) {
      Serial.printf("Hay %u matriculas guardadas:\n", numMatriculasPendientesEnviar);
      for (uint16_t i = 0; i < numMatriculasPendientesEnviar; i++) {
        Serial.printf("%2u: %s\n", i + 1, matriculasPendientesEnviar[i]);
      }
    }
  }
}

void ComprobarInteraccionUsuario() {
  static uint64_t siguienteLecturaTarjeta = now;
  if (now >= siguienteLecturaTarjeta) {
    ComprobarTarjeta();
    siguienteLecturaTarjeta = now + DELAY_BETWEEN_CARDS;
  }
}

void setup() {
  Serial.begin(115200);
  //delay(200);

  //auto cfg = M5.config();
  M5Dial.begin();
  for (int i = 0; i < 3; i++) {
    M5Dial.update();
    delay(5);
  }
  delay(100);
  M5.Speaker.begin();
  M5.Speaker.setVolume(255);



  /*qrSprite.setPsram(true);
  qrSprite.setColorDepth(1);       // menos memoria
  if (!qrSprite.createSprite(60, 60)) {
    Serial.println("Fallo al crear sprite (sin memoria)");
  } else {
    qrSprite.qrcode("https://bosch.com", 0, 0, 60, BLACK, WHITE);
    Serial.println("Sprite QR creado OK");
  }*/

  //M5Dial.Display.qrcode("https://bosch.com", 60, 60, 120, BLACK, WHITE);




  encInit();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  if (esp_now_init() != ESP_OK) ESP.restart();

  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnDataSent);

  ShowInfo();
}

void loop() {
  //static uint32_t siguienteParpadeo =0;

  now = millis();

  M5Dial.update();
  //encPoll();

  //Serial.println("Comprobar click");
  if (M5Dial.BtnA.wasPressed()) {
    Serial.print("PULSADO 1");
    botonLlamadaPulsado = true;
    M5.Speaker.tone(3600, 200);
  }
  auto t = M5Dial.Touch.getDetail();

  if (t.wasPressed()) {
    Serial.print("PULSADO 2");
    botonLlamadaPulsado = true;
    M5.Speaker.tone(3600, 200);
  }

  ComprobarGestionarmensajeRecibidoFromBridge();
  ComprobarInteraccionUsuario();

  // ---------- FSM: decidir estado ----------
  //Estado pantalla_requerida = estado;

  /*if (botonLlamadaPulsado) {
    pantalla_requerida = Estado::BOTON_PULSADO_2;
  } else */
  /*if (!seHaRecibidoUnMensajeAlgunaVez){
    pantalla_requerida = Estado::SIN_COBERTURA;
  }
  else if (hayUnaLlamadaPendiente || tiempo_ultimo_llamada > 0 || tiempo_paro_recibido > 0) {
    pantalla_requerida = (numMatriculasPendientesEnviar > 0) ? Estado::MANT_LLEGANDO_4 : */
  /*Estado::ESPERANDO_MANT_3;
  } else if (tiempo_paro_recibido > 0) {
    pantalla_requerida = Estado::CICLO_PERDIDO_1;
  }else {
    pantalla_requerida = Estado::OK_0;
  }*/

  /*if (next != estado) {
    estado_prev = estado;
    estado = next;
    need_full_redraw = true;
  }*/






  if (!seHaRecibidoUnMensajeAlgunaVez || lengthLinea == 0){
    pantalla_requerida = Estado::SIN_COBERTURA;
  }else if (botonLlamadaPulsado) {
    pantalla_requerida = Estado::REGISTRANDO_LLAMADA;
  } else if (!hayLlamada) {
    pantalla_requerida = Estado::REPOSO;
  } else if (numMatriculasConfirmadas == 0) {
    pantalla_requerida = Estado::ESPERANDO_MANT;
  } else {
    pantalla_requerida = Estado::ESPERANDO_FIN_PARO;
  }








  //bool tick_seg = (s_paro != last_segundos) ;
  //if (tick_seg) {
  switch (pantalla_requerida) {
    case Estado::REPOSO:
      Draw_Reposo();
      break;
    case Estado::REGISTRANDO_LLAMADA:
      Draw_Reposo();
      break;
    case Estado::ESPERANDO_MANT:
      Draw_EsperandoMant();
      break;
    case Estado::ESPERANDO_FIN_PARO:
      Draw_EsperandoJustificarParo();
      break;
    case Estado::SIN_COBERTURA:
      Draw_SinCobertura();
      break;
  }
  //last_segundos = s_paro;
  //}
}
