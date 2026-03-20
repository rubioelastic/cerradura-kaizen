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
// Definición de estados de la UI
// ─────────────────────────────────────────────────────────────
typedef enum {
    UI_STATE_IDLE        = 0,  // Espera — muestra reloj e icono de candado
    UI_STATE_ACCESS_OK   = 1,  // Acceso concedido
    UI_STATE_ACCESS_DENY = 2,  // Acceso denegado
    UI_STATE_ADD_MODE    = 3,  // Modo: añadir matrícula
    UI_STATE_REMOVE_MODE = 4,  // Modo: eliminar matrícula
    UI_STATE_REMOVE_ALL  = 5,  // Modo: eliminar todas las matrículas
    UI_STATE_ERROR       = 6   // Error de sistema
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
        delay(2000);
        drawIdle("--:--", "-- / -- / ----"); // Estado inicial sin hora
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
    void drawIdle(const char *timeStr, const char *dateStr) {
        _currentState = UI_STATE_IDLE;
        _disp.fillScreen(COL_BG_BLACK);

        // ── Círculo decorativo exterior
        _disp.drawCircle(120, 120, 115, COL_GRAY);
        _disp.drawCircle(120, 120, 113, COL_BG_DARK);

        // ── Icono de candado simplificado (centro superior)
        drawLockIcon(120, 62, COL_WHITE, true); // true = cerrado

        // ── Hora grande en el centro
        _disp.setTextSize(3);
        _disp.setTextColor(COL_WHITE);
        _disp.setTextDatum(MC_DATUM);            // Centrado horizontal y vertical
        _disp.drawString(timeStr, 120, 120);

        // ── Fecha pequeña debajo de la hora
        _disp.setTextSize(1);
        _disp.setTextColor(COL_GRAY);
        _disp.drawString(dateStr, 120, 150);

        // ── Instrucción al usuario
        _disp.setTextSize(1);
        _disp.setTextColor(COL_CYAN);
        _disp.drawString("Acerca la tarjeta", 120, 185);
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
        _disp.fillCircle(120, 120, 112, 0x0320); // verde oscuro de fondo

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
        _disp.fillCircle(120, 120, 112, 0x3000); // rojo muy oscuro

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
        _disp.drawCircle(120, 120, 115, COL_BLUE);
        _disp.drawCircle(120, 120, 114, COL_BLUE);

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
        _disp.drawCircle(120, 120, 115, COL_ORANGE);
        _disp.drawCircle(120, 120, 114, COL_ORANGE);

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
        _disp.drawCircle(120, 120, 115, COL_RED);
        _disp.drawCircle(120, 120, 114, COL_RED);

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
    void updateIdleTime(const char *timeStr, const char *dateStr) {
        if (_currentState != UI_STATE_IDLE) return;

        // Borra el área de hora anterior con fondo negro
        _disp.fillRect(40, 103, 160, 35, COL_BG_BLACK);
        _disp.fillRect(40, 140, 160, 18, COL_BG_BLACK);

        // Redibuja hora
        _disp.setTextSize(3);
        _disp.setTextColor(COL_WHITE);
        _disp.setTextDatum(MC_DATUM);
        _disp.drawString(timeStr, 120, 120);

        // Redibuja fecha
        _disp.setTextSize(1);
        _disp.setTextColor(COL_GRAY);
        _disp.drawString(dateStr, 120, 150);
    }

    // ─────────────────────────────────────────────────────────
    // Muestra la matrícula leída durante el proceso de lectura
    // (mientras se decide si hay acceso o no)
    // ─────────────────────────────────────────────────────────
    void drawReadingCard(const char *matricula) {
        if (_currentState != UI_STATE_IDLE) return;
        _disp.fillRect(40, 175, 160, 20, COL_BG_BLACK); // Borra línea anterior
        _disp.setTextSize(1);
        _disp.setTextColor(COL_CYAN);
        _disp.setTextDatum(MC_DATUM);
        _disp.drawString(matricula, 120, 185);
    }

    // ─────────────────────────────────────────────────────────
    // Devuelve el estado actual de la UI
    // ─────────────────────────────────────────────────────────
    UIState getState() const { return _currentState; }

private:
    M5GFX  &_disp;         // Referencia al objeto de pantalla del M5Dial
    UIState _currentState; // Estado visual actual

    // ─────────────────────────────────────────────────────────
    // Dibuja la pantalla de splash inicial (bienvenida)
    // ─────────────────────────────────────────────────────────
    void drawSplash() {
        _disp.fillScreen(COL_BG_BLACK);
        _disp.drawCircle(120, 120, 115, COL_CYAN);
        _disp.setTextSize(2);
        _disp.setTextColor(COL_CYAN);
        _disp.setTextDatum(MC_DATUM);
        _disp.drawString("CERRADURA", 120, 90);
        _disp.drawString("KAIZEN", 120, 115);
        _disp.setTextSize(1);
        _disp.setTextColor(COL_GRAY);
        _disp.drawString("Sistema RFID de Acceso", 120, 148);
        _disp.drawString("Adaptativo", 120, 162);
        _disp.setTextColor(COL_WHITE);
        _disp.drawString("M5Dial v1.0", 120, 190);
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
