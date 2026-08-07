#pragma once
// ============================================================
// ui_display.h — Interfaz gráfica para la pantalla TFT GC9A01
//                del M5Dial (1.28", 240×240 px, redonda)
//
// CONTEXTO DE MIGRACIÓN:
//   Original:  Serial.print() / Serial.println() para mensajes UI
//   Destino:   Pantalla TFT redonda con M5GFX / TFT_eSPI
//
// ESTADOS DE LA INTERFAZ:
//   UI_STATE_IDLE         — Pantalla de espera (hora + lock)
//   UI_STATE_ACCESS_OK    — Acceso concedido (verde)
//   UI_STATE_ACCESS_DENY  — Acceso denegado (rojo)
//   UI_STATE_ADD_MODE     — Modo añadir matrícula (azul)
//   UI_STATE_REMOVE_MODE  — Modo eliminar matrícula (naranja)
//   UI_STATE_REMOVE_ALL   — Todas las matrículas eliminadas (rojo)
//   UI_STATE_ERROR        — Error de sistema (amarillo)
// ============================================================

#include <Arduino.h>
#include <M5Dial.h>   // Incluye M5GFX, que gestiona el display GC9A01

// ─────────────────────────────────────────────────────────────
// Paleta de colores (RGB565 para GC9A01 / M5GFX)
// ─────────────────────────────────────────────────────────────
#define COL_BG_BLACK    0x0000  // Fondo negro
#define COL_BG_DARK     0x1082  // Fondo gris muy oscuro
#define COL_WHITE       0xFFFF  // Blanco
#define COL_GREEN       0x07E0  // Verde brillante → acceso OK
#define COL_RED         0xF800  // Rojo → acceso denegado / borrar todo
#define COL_BLUE        0x001F  // Azul → modo añadir
#define COL_ORANGE      0xFC00  // Naranja → modo eliminar
#define COL_YELLOW      0xFFE0  // Amarillo → error / advertencia
#define COL_GRAY        0x7BEF  // Gris → texto secundario
#define COL_CYAN        0x07FF  // Cian → información

// ─────────────────────────────────────────────────────────────
// Layout de las pantallas LIBRE / OCUPADO
//   Todas las coordenadas en píxeles; pantalla circular 240×240 px
// ─────────────────────────────────────────────────────────────
// ── Cabecera (icono + etiqueta LIBRE/OCUPADO + separador)
#define LO_LOCK_X        75    // X centro icono candado
#define LO_LOCK_Y        28    // Y base icono candado
#define LO_LABEL_X       100   // X texto estado (datum ML_DATUM)
#define LO_LABEL_Y       48    // Y texto estado
#define LO_SEP_X0        40    // X inicio línea separadora
#define LO_SEP_X1        200   // X fin línea separadora
#define LO_SEP_Y         62    // Y línea separadora
// ── Zona central (nombres)
#define LO_SALA_Y        78    // Y inicio nombre de sala
#define LO_NAMES_SIZE    2     // Tamaño de fuente para nombres
#define LO_NAMES_WRAP    190   // Ancho máximo antes de saltar de línea (px)
#define LO_LINE_H        22    // Alto de línea para LO_NAMES_SIZE (size*8+6)
#define LO_NAMES_GAP     6     // Separación vertical entre sala y persona (px)
// ── Separador encima del reloj
#define LO_SEP2_Y        188   // Y segunda línea separadora (encima del reloj)
// ── Reloj (parte inferior)
#define LO_CLOCK_Y       210   // Y centro del texto del reloj
#define LO_CLOCK_BG_Y    198   // Y inicio área de borrado del reloj
#define LO_CLOCK_BG_H    24    // Alto área de borrado del reloj

// ─────────────────────────────────────────────────────────────
// Definición de estados de la UI
// ─────────────────────────────────────────────────────────────
typedef enum {
    UI_STATE_IDLE        = 0,  // Espera — muestra reloj e icono de candado
    UI_STATE_ACCESS_OK   = 1,  // Acceso concedido
    UI_STATE_ACCESS_DENY = 2,  // Acceso denegado
    UI_STATE_ADD_MODE    = 3,  // Modo: añadir matrícula
    UI_STATE_REMOVE_MODE = 4,  // Modo: eliminar matrícula
    UI_STATE_REMOVE_ALL  = 5,  // Modo: eliminar todas las matrículas
    UI_STATE_ERROR       = 6,  // Error de sistema
    UI_STATE_LIBRE       = 7,  // Espacio LIBRE (Bridge conectado)
    UI_STATE_OCUPADO     = 8,  // Espacio OCUPADO (Bridge conectado)
    UI_STATE_ABIERTA     = 9
} UIState;

// ─────────────────────────────────────────────────────────────
// Clase UI — gestiona todos los elementos visuales
// ─────────────────────────────────────────────────────────────
class KaizenUI {
public:

    // ─────────────────────────────────────────────────────────
    // Constructor: recibe referencia al objeto display de M5Dial
    // ─────────────────────────────────────────────────────────
    KaizenUI(M5GFX &display) : _disp(display), _currentState(UI_STATE_IDLE) {}

    // ─────────────────────────────────────────────────────────
    // Inicialización de la pantalla
    // ─────────────────────────────────────────────────────────
    void begin() {
        _disp.setBrightness(180);       // Brillo moderado (0–255)
        _disp.fillScreen(COL_BG_BLACK); // Fondo negro inicial
        drawSplash();                   // Pantalla de bienvenida
        delay(5000);
    }

    // ─────────────────────────────────────────────────────────
    // Pantalla de espera principal:
    //   - Icono de candado cerrado
    //   - Hora y fecha del RTC
    //   - Texto "Acerca la tarjeta"
    //
    // Parámetros:
    //   timeStr — cadena de hora "HH:MM"
    //   dateStr — cadena de fecha "DD / MM / AAAA"
    // ─────────────────────────────────────────────────────────
    void drawIdle(const char *timeStr, const char *nombre = nullptr) {
        _currentState = UI_STATE_IDLE;
        _disp.fillScreen(COL_BG_BLACK);

        // Círculo decorativo exterior azul
        _disp.drawCircle(120, 120, 119, COL_CYAN);
        _disp.drawCircle(120, 120, 118, 0x000F);

        // Cabecera: candado cerrado + "ACCESO"
        drawHeaderEstado("ACCESO", COL_CYAN, true, COL_CYAN);

        // Nombre del espacio (zona central)
        if (nombre && nombre[0]) {
            drawWrapped(nombre, 120, LO_SALA_Y, LO_NAMES_SIZE, COL_CYAN, LO_NAMES_WRAP);
        }

        // Separador encima del reloj
        _disp.drawLine(LO_SEP_X0, LO_SEP2_Y, LO_SEP_X1, LO_SEP2_Y, COL_CYAN);

        // Reloj inferior
        _disp.setTextSize(2);
        _disp.setTextColor(COL_WHITE);
        _disp.setTextDatum(MC_DATUM);
        _disp.drawString(timeStr, 120, LO_CLOCK_Y);
    }

    // ─────────────────────────────────────────────────────────
    // Pantalla de ACCESO CONCEDIDO
    //   - Fondo verde
    //   - Icono candado abierto
    //   - Matrícula leída
    //   - Hora del evento
    // ─────────────────────────────────────────────────────────
    void drawAccessOK(const char *matricula, const char *timeStr) {
        _currentState = UI_STATE_ACCESS_OK;
        _disp.fillScreen(COL_BG_BLACK);

        // Círculo verde de fondo
        _disp.fillCircle(120, 120, 117, 0x0320); // verde oscuro de fondo

        // Icono candado abierto
        drawLockIcon(120, 70, COL_GREEN, false); // false = abierto

        // Texto principal
        _disp.setTextSize(2);
        _disp.setTextColor(COL_GREEN);
        _disp.setTextDatum(MC_DATUM);
        _disp.drawString("ACCESO", 120, 115);
        _disp.drawString("CONCEDIDO", 120, 138);

        // Matrícula
        _disp.setTextSize(1);
        _disp.setTextColor(COL_WHITE);
        _disp.drawString(matricula, 120, 165);

        // Hora del evento
        _disp.setTextColor(COL_GRAY);
        _disp.drawString(timeStr, 120, 182);
    }

    // ─────────────────────────────────────────────────────────
    // Pantalla de ACCESO DENEGADO
    //   - Fondo rojo oscuro
    //   - Icono X grande
    //   - Matrícula leída
    // ─────────────────────────────────────────────────────────
    void drawAccessDeny(const char *matricula, const char *timeStr) {
        _currentState = UI_STATE_ACCESS_DENY;
        _disp.fillScreen(COL_BG_BLACK);

        // Círculo rojo de fondo
        _disp.fillCircle(120, 120, 117, 0x3000); // rojo muy oscuro

        // Icono X (dos líneas diagonales)
        _disp.drawLine(90, 80, 150, 140, COL_RED);
        _disp.drawLine(91, 80, 151, 140, COL_RED);
        _disp.drawLine(150, 80, 90,  140, COL_RED);
        _disp.drawLine(151, 80, 91,  140, COL_RED);

        // Círculo alrededor de la X
        _disp.drawCircle(120, 110, 36, COL_RED);
        _disp.drawCircle(120, 110, 35, COL_RED);

        // Texto principal
        _disp.setTextSize(2);
        _disp.setTextColor(COL_RED);
        _disp.setTextDatum(MC_DATUM);
        _disp.drawString("ACCESO", 120, 158);
        _disp.drawString("DENEGADO", 120, 180);

        // Matrícula
        _disp.setTextSize(1);
        _disp.setTextColor(COL_GRAY);
        _disp.drawString(matricula, 120, 200);
    }

    // ─────────────────────────────────────────────────────────
    // Pantalla MODO AÑADIR MATRÍCULAS
    //   Equivale al mensaje del Serial del código original:
    //   "Comienza tiempo de añadir matriculas."
    // ─────────────────────────────────────────────────────────
    void drawAddMode(int countdown) {
        _currentState = UI_STATE_ADD_MODE;
        _disp.fillScreen(COL_BG_BLACK);

        // Borde azul
        _disp.drawCircle(120, 120, 119, COL_BLUE);
        _disp.drawCircle(120, 120, 118, COL_BLUE);

        // Icono "+"
        _disp.fillRect(112, 80, 16, 80, COL_BLUE);  // vertical
        _disp.fillRect(80,  112, 80, 16, COL_BLUE); // horizontal

        // Texto
        _disp.setTextSize(2);
        _disp.setTextColor(COL_BLUE);
        _disp.setTextDatum(MC_DATUM);
        _disp.drawString("AÑADIR", 120, 175);

        // Cuenta atrás
        _disp.setTextSize(2);
        _disp.setTextColor(COL_WHITE);
        char buf[8];
        snprintf(buf, sizeof(buf), "%ds", countdown);
        _disp.drawString(buf, 120, 205);
    }

    // ─────────────────────────────────────────────────────────
    // Pantalla MODO ELIMINAR MATRÍCULAS (individuales)
    //   Equivale al mensaje del Serial:
    //   "Comienza tiempo de eliminar matriculas."
    // ─────────────────────────────────────────────────────────
    void drawRemoveMode(int countdown) {
        _currentState = UI_STATE_REMOVE_MODE;
        _disp.fillScreen(COL_BG_BLACK);

        // Borde naranja
        _disp.drawCircle(120, 120, 119, COL_ORANGE);
        _disp.drawCircle(120, 120, 118, COL_ORANGE);

        // Icono "-"
        _disp.fillRect(80, 112, 80, 16, COL_ORANGE);

        // Texto
        _disp.setTextSize(2);
        _disp.setTextColor(COL_ORANGE);
        _disp.setTextDatum(MC_DATUM);
        _disp.drawString("ELIMINAR", 120, 163);
        _disp.drawString("TARJETA", 120, 186);

        // Cuenta atrás
        _disp.setTextSize(2);
        _disp.setTextColor(COL_WHITE);
        char buf[8];
        snprintf(buf, sizeof(buf), "%ds", countdown);
        _disp.drawString(buf, 120, 210);
    }

    // ─────────────────────────────────────────────────────────
    // Pantalla MODO ELIMINAR TODAS las matrículas
    //   Equivale a Pulsos_RemoveAllMode() del código original
    // ─────────────────────────────────────────────────────────
    void drawRemoveAll() {
        _currentState = UI_STATE_REMOVE_ALL;
        _disp.fillScreen(COL_BG_BLACK);

        // Círculo rojo de advertencia
        _disp.drawCircle(120, 120, 119, COL_RED);
        _disp.drawCircle(120, 120, 118, COL_RED);

        // Icono de papelera (simplificado)
        _disp.fillRect(100, 85,  40, 5,  COL_RED); // tapa
        _disp.fillRect(107, 78,  10, 7,  COL_RED); // asa izquierda
        _disp.fillRect(123, 78,  10, 7,  COL_RED); // asa derecha
        _disp.fillRect(102, 92,  36, 45, COL_RED); // cuerpo

        // Texto
        _disp.setTextSize(2);
        _disp.setTextColor(COL_RED);
        _disp.setTextDatum(MC_DATUM);
        _disp.drawString("BORRAR", 120, 155);
        _disp.drawString("TODO", 120, 178);

        _disp.setTextSize(1);
        _disp.setTextColor(COL_YELLOW);
        _disp.drawString("Todas las tarjetas", 120, 204);
        _disp.drawString("eliminadas", 120, 218);
    }

    // ─────────────────────────────────────────────────────────
    // Pantalla de ERROR del sistema
    //   Ej: fallo al inicializar RFID, RTC o EEPROM
    // ─────────────────────────────────────────────────────────
    void drawError(const char *errorMsg) {
        _currentState = UI_STATE_ERROR;
        _disp.fillScreen(COL_BG_BLACK);

        // Triángulo de advertencia (dibujado con líneas)
        _disp.drawTriangle(120, 55, 60, 165, 180, 165, COL_YELLOW);
        _disp.drawTriangle(120, 57, 62, 163, 178, 163, COL_YELLOW);

        // Signo de exclamación dentro del triángulo
        _disp.fillRect(117, 90, 6, 45, COL_YELLOW);
        _disp.fillCircle(120, 148, 4, COL_YELLOW);

        // Mensaje de error
        _disp.setTextSize(1);
        _disp.setTextColor(COL_YELLOW);
        _disp.setTextDatum(MC_DATUM);
        _disp.drawString("ERROR", 120, 178);
        _disp.setTextColor(COL_GRAY);
        _disp.drawString(errorMsg, 120, 196);
    }

    // ─────────────────────────────────────────────────────────
    // Actualiza SOLO la hora y la fecha en la pantalla Idle
    // (sin redibujar toda la pantalla para evitar parpadeos)
    // ─────────────────────────────────────────────────────────
    void updateIdleTime(const char *timeStr) {
        if (_currentState != UI_STATE_IDLE) return;
        _disp.fillRect(60, LO_CLOCK_BG_Y, 120, LO_CLOCK_BG_H, COL_BG_BLACK);
        _disp.setTextSize(2);
        _disp.setTextColor(COL_WHITE);
        _disp.setTextDatum(MC_DATUM);
        _disp.drawString(timeStr, 120, LO_CLOCK_Y);
    }

    // ─────────────────────────────────────────────────────────
    // Muestra la matrícula leída durante el proceso de lectura
    // (mientras se decide si hay acceso o no)
    // ─────────────────────────────────────────────────────────
    void drawReadingCard(const char *matricula) {
        if (_currentState != UI_STATE_IDLE) return;
        _disp.fillRect(40, 145, 160, 20, COL_BG_BLACK); // Borra línea anterior
        _disp.setTextSize(1);
        _disp.setTextColor(COL_CYAN);
        _disp.setTextDatum(MC_DATUM);
        _disp.drawString(matricula, 120, 155);
    }

    // ─────────────────────────────────────────────────────────
    // Pantalla de CONFIRMACIÓN DE LIBERACIÓN
    //   Se muestra cuando la misma persona que ocupó presenta
    //   su tarjeta de nuevo. Pregunta si quiere liberar el espacio
    //   o simplemente entrar de nuevo.
    //   countdown: segundos restantes (0–3) para actualizar sin redibujar.
    // ─────────────────────────────────────────────────────────
    void drawConfirmRelease(int countdown, bool fullDraw = true) {
        _currentState = UI_STATE_OCUPADO; // reutiliza el estado
        if (fullDraw) {
            _disp.fillScreen(0x2800); // naranja muy oscuro
            _disp.drawCircle(120, 120, 119, COL_ORANGE);
            _disp.drawCircle(120, 120, 118, COL_ORANGE);

            // Icono candado entreabierto
            drawLockIcon(120, 50, COL_ORANGE, false);

            // Pregunta
            _disp.setTextSize(2);
            _disp.setTextColor(COL_ORANGE);
            _disp.setTextDatum(MC_DATUM);
            _disp.drawString("¿LIBERAR", 120, 108);
            _disp.drawString("ESPACIO?", 120, 130);

            // Instrucción
            _disp.setTextSize(1);
            _disp.setTextColor(COL_WHITE);
            _disp.drawString("Toca para liberar", 120, 165);
            _disp.drawString("Espera para entrar", 120, 180);
        }
        // Actualizar solo la cuenta atrás (llamado cada segundo)
        _disp.fillRect(95, 198, 50, 20, 0x2800);
        _disp.setTextSize(2);
        _disp.setTextColor(COL_YELLOW);
        _disp.setTextDatum(MC_DATUM);
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", countdown);
        _disp.drawString(buf, 120, 208);
    }

    // ─────────────────────────────────────────────────────────
    // Pantalla MANTENER — cuenta atrás para ocupar o liberar.
    //   accion: "OCUPAR" o "LIBERAR"
    //   fullDraw=false actualiza solo el número (sin redibujar todo)
    // ─────────────────────────────────────────────────────────
    void drawMantener(const char *accion, int countdown, bool fullDraw = true) {
        if (fullDraw) {
            _currentState = UI_STATE_OCUPADO;
            _disp.fillScreen(0x2800);
            _disp.drawCircle(120, 120, 119, COL_ORANGE);
            _disp.drawCircle(120, 120, 118, COL_ORANGE);
            drawLockIcon(120, 50, COL_ORANGE, false);
            _disp.setTextSize(1);
            _disp.setTextColor(COL_WHITE);
            _disp.setTextDatum(MC_DATUM);
            _disp.drawString("Manten para", 120, 108);
            _disp.setTextSize(2);
            _disp.setTextColor(COL_ORANGE);
            _disp.drawString(accion, 120, 128);
        }
        _disp.fillRect(95, 160, 50, 45, 0x2800);
        _disp.setTextSize(3);
        _disp.setTextColor(COL_YELLOW);
        _disp.setTextDatum(MC_DATUM);
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", countdown);
        _disp.drawString(buf, 120, 183);
    }

    // ─────────────────────────────────────────────────────────
    // Pantalla PUERTA ABIERTA — se muestra mientras el relé está activo.
    //   Fondo verde oscuro, candado abierto, texto "ABIERTA".
    // ─────────────────────────────────────────────────────────────
    void drawAbierta(const char *texto = "ABIERTA") {
        _currentState = UI_STATE_ABIERTA;
        _disp.fillScreen(COL_BG_BLACK);
        _disp.fillCircle(120, 120, 118, 0x0320);         // verde oscuro
        _disp.drawCircle(120, 120, 119, COL_GREEN);
        _disp.drawCircle(120, 120, 118, COL_GREEN);
        drawLockIcon(120, 65, COL_GREEN, false);          // candado abierto
        _disp.setTextSize(2);
        _disp.setTextColor(COL_GREEN);
        _disp.setTextDatum(MC_DATUM);
        _disp.drawString(texto, 120, 165);
    }

    // ─────────────────────────────────────────────────────────
    // drawHeaderEstado — cabecera horizontal común a LIBRE/OCUPADO:
    //   icono de candado a la izquierda + palabra de estado a su
    //   derecha, y una línea separadora debajo. Libera espacio
    //   inferior para mostrar los nombres en grande.
    // ─────────────────────────────────────────────────────────
    void drawHeaderEstado(const char *estado, uint16_t color, bool candadoCerrado, uint16_t sepColor) {
        drawLockIcon(LO_LOCK_X, LO_LOCK_Y, color, candadoCerrado);
        // Versión de firmware a la izquierda del candado
        char verBuf[8];
        snprintf(verBuf, sizeof(verBuf), "v%u", _fwVersion);
        _disp.setTextSize(1);
        _disp.setTextColor(COL_GRAY);
        _disp.setTextDatum(MC_DATUM);
        _disp.drawString(verBuf, 195, 55);
        _disp.setTextSize(2);
        _disp.setTextColor(color);
        _disp.setTextDatum(ML_DATUM);
        _disp.drawString(estado, LO_LABEL_X, LO_LABEL_Y);
        _disp.drawLine(LO_SEP_X0, LO_SEP_Y, LO_SEP_X1, LO_SEP_Y, sepColor);
    }

    // ─────────────────────────────────────────────────────────
    // drawWrapped — dibuja texto centrado con ajuste por palabras.
    //   Si una palabra/línea no cabe en maxWidth, salta de línea.
    //   Devuelve el número de líneas dibujadas.
    // ─────────────────────────────────────────────────────────
    int drawWrapped(const char *text, int cx, int topY, uint8_t size, uint16_t color, int maxWidth) {
        _disp.setTextSize(size);
        _disp.setTextColor(color);
        _disp.setTextDatum(TC_DATUM);
        const int lineH = size * 8 + 6;

        char buf[64];
        strncpy(buf, text, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        char linea[64] = "";
        int y = topY;
        int nLineas = 0;

        char *palabra = strtok(buf, " ");
        while (palabra) {
            char prueba[64];
            if (linea[0]) snprintf(prueba, sizeof(prueba), "%s %s", linea, palabra);
            else          snprintf(prueba, sizeof(prueba), "%s", palabra);

            if (linea[0] == '\0' || _disp.textWidth(prueba) <= maxWidth) {
                strncpy(linea, prueba, sizeof(linea) - 1);
                linea[sizeof(linea) - 1] = '\0';
            } else {
                _disp.drawString(linea, cx, y);
                y += lineH; nLineas++;
                strncpy(linea, palabra, sizeof(linea) - 1);
                linea[sizeof(linea) - 1] = '\0';
            }
            palabra = strtok(NULL, " ");
        }
        if (linea[0]) { _disp.drawString(linea, cx, y); nLineas++; }
        return nLineas;
    }

    void drawLibre(const char *timeStr, const char *nombre) {
        _currentState = UI_STATE_LIBRE;
        _disp.fillScreen(0x0240);
        _disp.drawCircle(120, 120, 119, COL_GREEN);
        _disp.drawCircle(120, 120, 118, 0x0320);

        // Cabecera: candado abierto + "LIBRE"
        drawHeaderEstado("LIBRE", COL_GREEN, false, 0x0320);

        // Nombres (zona central)
        drawWrapped(nombre, 120, LO_SALA_Y, LO_NAMES_SIZE, COL_CYAN, LO_NAMES_WRAP);

        // Separador encima del reloj
        _disp.drawLine(LO_SEP_X0, LO_SEP2_Y, LO_SEP_X1, LO_SEP2_Y, 0x0320);

        // Reloj inferior
        _disp.setTextSize(2);
        _disp.setTextColor(COL_WHITE);
        _disp.setTextDatum(MC_DATUM);
        _disp.drawString(timeStr, 120, LO_CLOCK_Y);
    }

    // Actualiza solo la hora en pantalla LIBRE (sin redibujar todo)
    void updateLibreTime(const char *timeStr) {
        if (_currentState != UI_STATE_LIBRE) return;
        _disp.fillRect(60, LO_CLOCK_BG_Y, 120, LO_CLOCK_BG_H, 0x0240);
        _disp.setTextSize(2);
        _disp.setTextColor(COL_WHITE);
        _disp.setTextDatum(MC_DATUM);
        _disp.drawString(timeStr, 120, LO_CLOCK_Y);
    }

    // ─────────────────────────────────────────────────────────
    // Pantalla ESPACIO OCUPADO (Bridge conectado)
    //   Fondo rojo oscuro, candado cerrado, texto grande "OCUPADO",
    //   nombre del espacio (no se muestra quién está por privacidad).
    // ─────────────────────────────────────────────────────────
    void drawOcupado(const char *nombre, const char *nombreOcupante = nullptr, const char *timeStr = nullptr) {
        _currentState = UI_STATE_OCUPADO;
        _disp.fillScreen(0x2000);
        _disp.drawCircle(120, 120, 119, COL_RED);
        _disp.drawCircle(120, 120, 118, COL_RED);

        // Cabecera: candado cerrado + "OCUPADO"
        drawHeaderEstado("OCUPADO", COL_RED, true, 0x4000);

        // Nombre de sala (zona central)
        int lineasSala = drawWrapped(nombre, 120, LO_SALA_Y, LO_NAMES_SIZE, COL_CYAN, LO_NAMES_WRAP);

        // Nombre del ocupante debajo del nombre de sala
        int yOcupante = LO_SALA_Y + lineasSala * LO_LINE_H + LO_NAMES_GAP;
        if (nombreOcupante && nombreOcupante[0] != '\0') {
            drawWrapped(nombreOcupante, 120, yOcupante, LO_NAMES_SIZE, COL_WHITE, LO_NAMES_WRAP);
        } else {
            _disp.setTextSize(LO_NAMES_SIZE);
            _disp.setTextColor(0x7BEF);
            _disp.setTextDatum(TC_DATUM);
            _disp.drawString("Espacio en uso", 120, yOcupante);
        }

        // Separador encima del reloj
        _disp.drawLine(LO_SEP_X0, LO_SEP2_Y, LO_SEP_X1, LO_SEP2_Y, 0x4000);

        // Reloj inferior
        if (timeStr && timeStr[0]) {
            _disp.setTextSize(2);
            _disp.setTextColor(COL_WHITE);
            _disp.setTextDatum(MC_DATUM);
            _disp.drawString(timeStr, 120, LO_CLOCK_Y);
        }
    }

    // Actualiza solo la hora en pantalla OCUPADO (sin redibujar todo)
    void updateOcupadoTime(const char *timeStr) {
        if (_currentState != UI_STATE_OCUPADO) return;
        _disp.fillRect(60, LO_CLOCK_BG_Y, 120, LO_CLOCK_BG_H, 0x2000);
        _disp.setTextSize(2);
        _disp.setTextColor(COL_WHITE);
        _disp.setTextDatum(MC_DATUM);
        _disp.drawString(timeStr, 120, LO_CLOCK_Y);
    }

    // ─────────────────────────────────────────────────────────
    // drawBridgeIndicator — indicador LED de conexión con el Bridge
    //   Dibuja un pequeño punto de color en la esquina superior
    //   derecha de la pantalla (dentro del arco visible GC9A01).
    //
    //   connected=true  → punto verde  (Bridge ok)
    //   connected=false → punto gris   (Sin cobertura / timeout)
    //
    //   Llamar DESPUÉS de dibujar la pantalla base para que
    //   el indicador quede siempre en primer plano.
    // ─────────────────────────────────────────────────────────
    void drawBridgeIndicator(bool connected) {
        // Posición: esquina superior derecha, dentro de la circunferencia
        // (182, 30) → ~111 px del centro, dentro del radio visible de 115 px
        const int cx = 182, cy = 30, r = 5;

        // Limpiar área previa con el color de fondo correspondiente al estado
        uint16_t bg = COL_BG_BLACK;
        if (_currentState == UI_STATE_LIBRE)    bg = 0x0240; // verde oscuro
        if (_currentState == UI_STATE_OCUPADO)  bg = 0x2000; // rojo oscuro
        if (_currentState == UI_STATE_ABIERTA)  bg = 0x0320; // verde oscuro (fill de drawAbierta)
        _disp.fillRect(cx - r - 3, cy - r - 3, (r + 3) * 2, (r + 3) * 2, bg);

        // Punto principal: verde=conectado, gris=sin Bridge
        uint16_t col = connected ? COL_GREEN : COL_GRAY;
        _disp.fillCircle(cx, cy, r, col);

        // Destello interior más claro para efecto de profundidad
        _disp.fillCircle(cx - 1, cy - 1, r / 2, connected ? 0x87F0 : 0xBDF7);
    }

    // ─────────────────────────────────────────────────────────
    // Devuelve el estado actual de la UI
    // ─────────────────────────────────────────────────────────
    UIState getState() const { return _currentState; }

    void setFwVersion(uint8_t v) { _fwVersion = v; }

private:
    M5GFX   &_disp;         // Referencia al objeto de pantalla del M5Dial
    UIState  _currentState; // Estado visual actual
    uint8_t  _fwVersion = 0; // Versión de firmware (se muestra en cabecera)

    // ─────────────────────────────────────────────────────────
    // Dibuja la pantalla de splash inicial (bienvenida)
    // ─────────────────────────────────────────────────────────
    void drawSplash() {
        _disp.fillScreen(COL_BG_BLACK);
        _disp.drawCircle(120, 120, 119, COL_CYAN);
        _disp.setTextSize(2);
        _disp.setTextColor(COL_CYAN);
        _disp.setTextDatum(MC_DATUM);
        _disp.drawString("CERRADURA", 120, 90);
        _disp.drawString("KAIZEN", 120, 115);
        _disp.setTextSize(1);
        _disp.setTextColor(COL_GRAY);
        _disp.drawString("Sistema RFID de Acceso", 120, 148);
        _disp.drawString("Adaptativo", 120, 162);
        String macAddr = WiFi.macAddress();
        _disp.drawString(macAddr, 120, 190);
    }

    // ─────────────────────────────────────────────────────────
    // Dibuja un icono de candado simplificado
    //   x, y   — centro del icono
    //   color  — color del candado
    //   closed — true=cerrado, false=abierto
    // ─────────────────────────────────────────────────────────
    void drawLockIcon(int x, int y, uint16_t color, bool closed) {
        // Cuerpo del candado (rectángulo redondeado)
        _disp.fillRoundRect(x - 14, y + 6, 28, 22, 4, color);

        if (closed) {
            // Arco cerrado (arco superior completo)
            _disp.drawArc(x, y + 6, 14, 9, 210, 330, color);
            _disp.drawArc(x, y + 6, 13, 8, 210, 330, color);
        } else {
            // Arco abierto (sólo lado izquierdo)
            _disp.drawArc(x - 5, y + 4, 14, 9, 200, 320, color);
            _disp.drawArc(x - 5, y + 4, 13, 8, 200, 320, color);
        }

        // Ojo del candado (círculo interior)
        _disp.fillCircle(x, y + 15, 4, COL_BG_BLACK);
    }
};
