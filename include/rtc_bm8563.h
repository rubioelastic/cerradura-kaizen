#pragma once
// ============================================================
// rtc_bm8563.h — Driver I2C para el RTC BM8563 del M5Dial (U5)
//
// CONTEXTO DE MIGRACIÓN:
//   Original:  El código NO usaba RTC ni timestamps
//   Nuevo:     - Mostrar hora/fecha en pantalla principal
//              - Registrar timestamp de cada intento de acceso
//              - Soporte para franjas horarias de acceso permitido
//
// El BM8563 es compatible con el popular PCF8563.
// Dirección I2C: 0x51
// Bus: Wire (compartido con WS1850S RFID)
//
// MAPA DE REGISTROS BM8563 (datasheet BM8563_V1.1_cn.pdf):
//   0x00 — Control/Status 1
//   0x01 — Control/Status 2
//   0x02 — Segundos    (BCD, bit7 = VL flag)
//   0x03 — Minutos     (BCD)
//   0x04 — Horas       (BCD, modo 24h)
//   0x05 — Día del mes (BCD, 1-31)
//   0x06 — Día semana  (BCD, 0=Dom...6=Sab)
//   0x07 — Mes         (BCD, bit7 = Century)
//   0x08 — Año         (BCD, 0-99; +1900 si Century=1, +2000 si Century=0)
// ============================================================

#include <Arduino.h>
#include <M5Dial.h>
#include "pins_config.h"

// ─────────────────────────────────────────────────────────────
// Registros del BM8563
// ─────────────────────────────────────────────────────────────
#define BM8563_ADDR         0x51  // Dirección I2C del BM8563

#define BM_REG_CTRL1        0x00  // Control/Status 1
#define BM_REG_CTRL2        0x01  // Control/Status 2
#define BM_REG_SECONDS      0x02  // Segundos (BCD) | bit7=VL (batería baja)
#define BM_REG_MINUTES      0x03  // Minutos (BCD)
#define BM_REG_HOURS        0x04  // Horas (BCD, 24h)
#define BM_REG_DAY          0x05  // Día del mes (BCD, 1-31)
#define BM_REG_WEEKDAY      0x06  // Día de la semana (0-6, 0=Domingo)
#define BM_REG_MONTH        0x07  // Mes (BCD) | bit7=Century
#define BM_REG_YEAR         0x08  // Año (BCD, 0-99)

// Máscaras BCD
#define BM_MASK_SECONDS     0x7F  // Bits válidos de segundos
#define BM_MASK_MINUTES     0x7F  // Bits válidos de minutos
#define BM_MASK_HOURS       0x3F  // Bits válidos de horas
#define BM_MASK_DAY         0x3F  // Bits válidos de día
#define BM_MASK_WEEKDAY     0x07  // Bits válidos de día de semana
#define BM_MASK_MONTH       0x1F  // Bits válidos de mes
#define BM_MASK_VL          0x80  // Bit de batería baja (Voltage Low)
#define BM_MASK_CENTURY     0x80  // Bit de siglo en registro mes

// ─────────────────────────────────────────────────────────────
// Estructura de fecha y hora
// ─────────────────────────────────────────────────────────────
struct DateTime {
    uint16_t year;    // Año completo (ej. 2026)
    uint8_t  month;   // Mes (1-12)
    uint8_t  day;     // Día del mes (1-31)
    uint8_t  weekday; // Día de la semana (0=Domingo, 1=Lunes... 6=Sábado)
    uint8_t  hour;    // Hora (0-23)
    uint8_t  minute;  // Minutos (0-59)
    uint8_t  second;  // Segundos (0-59)
    bool     valid;   // true si la batería del RTC mantiene la hora correcta
};

// ─────────────────────────────────────────────────────────────
// Nombres de los días de la semana (para mostrar en pantalla)
// ─────────────────────────────────────────────────────────────
static const char* WEEKDAY_NAMES[] = {
    "Dom", "Lun", "Mar", "Mie", "Jue", "Vie", "Sab"
};

// ─────────────────────────────────────────────────────────────
// Estructura para franja horaria de acceso permitido
// ─────────────────────────────────────────────────────────────
struct AccessSchedule {
    uint8_t startHour;    // Hora de inicio (0-23)
    uint8_t startMinute;  // Minuto de inicio (0-59)
    uint8_t endHour;      // Hora de fin (0-23)
    uint8_t endMinute;    // Minuto de fin (0-59)
    bool    enabledDays[7]; // Días habilitados: [0]=Dom, [1]=Lun... [6]=Sab
    bool    enabled;        // true = restricción activa
};

// ─────────────────────────────────────────────────────────────
// Conversión BCD ↔ decimal
// ─────────────────────────────────────────────────────────────
static inline uint8_t bcd2dec(uint8_t bcd) {
    return (bcd >> 4) * 10 + (bcd & 0x0F);
}
static inline uint8_t dec2bcd(uint8_t dec) {
    return ((dec / 10) << 4) | (dec % 10);
}

// ============================================================
// Clase KaizenRTC — wrapper sobre M5Dial.Rtc (RTC_Class de M5Unified)
//
// En lugar de acceder al BM8563 directamente por Wire (que usaría el
// bus Arduino público), delegamos en M5Dial.Rtc que ya fue inicializado
// por M5Dial.begin() con el bus interno correcto (m5::In_I2C, pines 11/12).
// ============================================================
class KaizenRTC {
public:

    // ─────────────────────────────────────────────────────────
    // Constructor — sin dependencia de Wire
    // ─────────────────────────────────────────────────────────
    KaizenRTC() {}

    // ─────────────────────────────────────────────────────────
    // Inicialización del RTC
    //   Devuelve true si M5Dial.Rtc está habilitado.
    //   Si VL=1 (batería baja), graba la hora de compilación
    //   extraída de los macros __DATE__ / __TIME__.
    // ─────────────────────────────────────────────────────────
    bool begin() {
        if (!M5Dial.Rtc.isEnabled()) {
            return false;
        }

        _voltLow = M5Dial.Rtc.getVoltLow();

        if (_voltLow) {
            // Batería baja o primer arranque: usar hora de compilación
            setBuildTime();
            _voltLow = false;
        }

        return true;
    }

    // ─────────────────────────────────────────────────────────
    // Forzar la hora del RTC a la hora de compilación del firmware.
    // Útil para poner en hora manualmente desde setup() si es necesario.
    // ─────────────────────────────────────────────────────────
    void setBuildTime() {
        // __DATE__ = "Mmm DD YYYY"   ej. "Mar 20 2026"
        // __TIME__ = "HH:MM:SS"      ej. "14:35:00"
        static const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
        char monStr[4] = {__DATE__[0], __DATE__[1], __DATE__[2], '\0'};
        int mon = (int)((strstr(months, monStr) - months) / 3) + 1;
        int day  = atoi(__DATE__ + 4);
        int year = atoi(__DATE__ + 7);
        int hour = atoi(__TIME__ + 0);
        int min  = atoi(__TIME__ + 3);
        int sec  = atoi(__TIME__ + 6);

        // Calcular día de la semana (algoritmo de Tomohiko Sakamoto)
        static const int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
        int y = year - (mon < 3 ? 1 : 0);
        int wday = (y + y/4 - y/100 + y/400 + t[mon-1] + day) % 7;

        m5::rtc_date_t d((int16_t)year, (int8_t)mon, (int8_t)day, (int8_t)wday);
        m5::rtc_time_t t2((int8_t)hour, (int8_t)min, (int8_t)sec);
        M5Dial.Rtc.setDateTime(&d, &t2);
    }

    // ─────────────────────────────────────────────────────────
    // Obtener la fecha y hora actual del RTC
    // ─────────────────────────────────────────────────────────
    DateTime getDateTime() {
        DateTime dt;
        m5::rtc_date_t d;
        m5::rtc_time_t t;

        if (M5Dial.Rtc.getDateTime(&d, &t)) {
            dt.year    = (uint16_t)d.year;
            dt.month   = (uint8_t)d.month;
            dt.day     = (uint8_t)d.date;
            dt.weekday = (uint8_t)d.weekDay;
            dt.hour    = (uint8_t)t.hours;
            dt.minute  = (uint8_t)t.minutes;
            dt.second  = (uint8_t)t.seconds;
            dt.valid   = !M5Dial.Rtc.getVoltLow();
        } else {
            dt = {2026, 3, 20, 5, 0, 0, 0, false};
        }

        return dt;
    }

    // ─────────────────────────────────────────────────────────
    // Establecer fecha y hora en el RTC
    // ─────────────────────────────────────────────────────────
    void setDateTime(const DateTime &dt) {
        m5::rtc_date_t d((int16_t)dt.year, (int8_t)dt.month,
                         (int8_t)dt.day,  (int8_t)dt.weekday);
        m5::rtc_time_t t((int8_t)dt.hour, (int8_t)dt.minute, (int8_t)dt.second);
        M5Dial.Rtc.setDateTime(&d, &t);
    }

    // ─────────────────────────────────────────────────────────
    // Formatear hora como cadena "HH:MM"
    // ─────────────────────────────────────────────────────────
    void formatTime(const DateTime &dt, char *buf, size_t bufSize) {
        snprintf(buf, bufSize, "%02d:%02d", dt.hour, dt.minute);
    }

    // ─────────────────────────────────────────────────────────
    // Formatear fecha como cadena "DD / MM / AAAA"
    // ─────────────────────────────────────────────────────────
    void formatDate(const DateTime &dt, char *buf, size_t bufSize) {
        snprintf(buf, bufSize, "%02d / %02d / %04d",
                 dt.day, dt.month, dt.year);
    }

    // ─────────────────────────────────────────────────────────
    // Formatear timestamp completo "DD/MM/AAAA HH:MM:SS"
    // Usado para registrar eventos de acceso
    // ─────────────────────────────────────────────────────────
    void formatTimestamp(const DateTime &dt, char *buf, size_t bufSize) {
        snprintf(buf, bufSize, "%02d/%02d/%04d %02d:%02d:%02d",
                 dt.day, dt.month, dt.year,
                 dt.hour, dt.minute, dt.second);
    }

    // ─────────────────────────────────────────────────────────
    // Verificar si el acceso está permitido según la franja horaria
    //
    // NUEVA FUNCIONALIDAD (no existía en el código original):
    // Permite restringir el acceso a franjas horarias y días
    // de la semana configurados en el struct AccessSchedule.
    //
    // Devuelve:
    //   true  — el acceso está permitido en este momento
    //   false — el acceso está fuera de la franja horaria
    // ─────────────────────────────────────────────────────────
    bool isAccessAllowed(const DateTime &dt, const AccessSchedule &schedule) {
        // Si la restricción está desactivada, siempre permitir
        if (!schedule.enabled) return true;

        // Verificar el día de la semana
        if (!schedule.enabledDays[dt.weekday]) return false;

        // Convertir hora actual y límites a minutos totales para comparar
        uint16_t currentMinutes = dt.hour * 60 + dt.minute;
        uint16_t startMinutes   = schedule.startHour * 60 + schedule.startMinute;
        uint16_t endMinutes     = schedule.endHour   * 60 + schedule.endMinute;

        if (startMinutes <= endMinutes) {
            // Franja sin cruce de medianoche (ej. 08:00 - 20:00)
            return (currentMinutes >= startMinutes && currentMinutes <= endMinutes);
        } else {
            // Franja con cruce de medianoche (ej. 22:00 - 06:00)
            return (currentMinutes >= startMinutes || currentMinutes <= endMinutes);
        }
    }

    // ─────────────────────────────────────────────────────────
    // Indica si la batería del RTC estaba baja en el arranque
    // (puede significar que la hora no es fiable)
    // ─────────────────────────────────────────────────────────
    bool isVoltLow() const { return _voltLow; }

private:
    bool _voltLow = false;
};
