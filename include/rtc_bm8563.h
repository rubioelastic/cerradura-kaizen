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
#include <Wire.h>
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
// Clase KaizenRTC — driver BM8563 para el M5Dial
// ============================================================
class KaizenRTC {
public:

    // ─────────────────────────────────────────────────────────
    // Constructor
    // ─────────────────────────────────────────────────────────
    KaizenRTC(TwoWire &wirePort = Wire) : _wire(wirePort) {}

    // ─────────────────────────────────────────────────────────
    // Inicialización del RTC
    //   Devuelve true si el chip responde correctamente.
    //   Si VL=1 (batería baja o primera puesta en marcha),
    //   advierte de que hay que ajustar la hora.
    // ─────────────────────────────────────────────────────────
    bool begin() {
        // Verificar que el BM8563 responde en I2C
        _wire.beginTransmission(BM8563_ADDR);
        if (_wire.endTransmission() != 0) {
            return false; // El chip no responde
        }

        // Limpiar interrupciones y alarmas del registro de control 2
        writeReg(BM_REG_CTRL2, 0x00);

        // Leer el flag VL para saber si la hora es válida
        uint8_t secReg = readReg(BM_REG_SECONDS);
        _voltLow = (secReg & BM_MASK_VL) != 0;

        if (_voltLow) {
            // Primera puesta en marcha o batería descargada:
            // Establecer una fecha/hora por defecto (20/03/2026 00:00:00)
            DateTime defaultDT = {2026, 3, 20, 5, 0, 0, 0, false};
            setDateTime(defaultDT);
            _voltLow = false;
        }

        return true;
    }

    // ─────────────────────────────────────────────────────────
    // Obtener la fecha y hora actual del RTC
    // ─────────────────────────────────────────────────────────
    DateTime getDateTime() {
        DateTime dt;

        // Leer los 7 registros de tiempo de una vez (burst read)
        _wire.beginTransmission(BM8563_ADDR);
        _wire.write(BM_REG_SECONDS);
        _wire.endTransmission(false); // Repeated START
        _wire.requestFrom((uint8_t)BM8563_ADDR, (uint8_t)7);

        if (_wire.available() >= 7) {
            uint8_t rawSec  = _wire.read();
            uint8_t rawMin  = _wire.read();
            uint8_t rawHour = _wire.read();
            uint8_t rawDay  = _wire.read();
            uint8_t rawWday = _wire.read();
            uint8_t rawMon  = _wire.read();
            uint8_t rawYear = _wire.read();

            dt.valid   = !(rawSec & BM_MASK_VL);
            dt.second  = bcd2dec(rawSec  & BM_MASK_SECONDS);
            dt.minute  = bcd2dec(rawMin  & BM_MASK_MINUTES);
            dt.hour    = bcd2dec(rawHour & BM_MASK_HOURS);
            dt.day     = bcd2dec(rawDay  & BM_MASK_DAY);
            dt.weekday = bcd2dec(rawWday & BM_MASK_WEEKDAY);
            dt.month   = bcd2dec(rawMon  & BM_MASK_MONTH);

            // Calcular año completo (BM8563 almacena 0-99)
            // Si el bit Century=1, el año está en el rango 1900-1999
            // Si Century=0, está en el rango 2000-2099
            bool century = (rawMon & BM_MASK_CENTURY) != 0;
            uint16_t baseYear = century ? 1900 : 2000;
            dt.year = baseYear + bcd2dec(rawYear);
        } else {
            // Error de lectura → valores por defecto
            dt = {2026, 3, 20, 5, 0, 0, 0, false};
        }

        return dt;
    }

    // ─────────────────────────────────────────────────────────
    // Establecer fecha y hora en el RTC
    // ─────────────────────────────────────────────────────────
    void setDateTime(const DateTime &dt) {
        _wire.beginTransmission(BM8563_ADDR);
        _wire.write(BM_REG_SECONDS);
        _wire.write(dec2bcd(dt.second)  & BM_MASK_SECONDS); // Limpia VL
        _wire.write(dec2bcd(dt.minute)  & BM_MASK_MINUTES);
        _wire.write(dec2bcd(dt.hour)    & BM_MASK_HOURS);
        _wire.write(dec2bcd(dt.day)     & BM_MASK_DAY);
        _wire.write(dec2bcd(dt.weekday) & BM_MASK_WEEKDAY);

        // Mes: determinar bit de century según el año
        uint8_t monthByte = dec2bcd(dt.month) & BM_MASK_MONTH;
        if (dt.year < 2000) monthByte |= BM_MASK_CENTURY;
        _wire.write(monthByte);

        // Año: guardar solo los dos últimos dígitos en BCD
        _wire.write(dec2bcd(dt.year % 100));
        _wire.endTransmission();
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
    TwoWire &_wire;
    bool     _voltLow = false;

    // ─────────────────────────────────────────────────────────
    // Escritura de un registro I2C
    // ─────────────────────────────────────────────────────────
    void writeReg(uint8_t reg, uint8_t value) {
        _wire.beginTransmission(BM8563_ADDR);
        _wire.write(reg);
        _wire.write(value);
        _wire.endTransmission();
    }

    // ─────────────────────────────────────────────────────────
    // Lectura de un registro I2C
    // ─────────────────────────────────────────────────────────
    uint8_t readReg(uint8_t reg) {
        _wire.beginTransmission(BM8563_ADDR);
        _wire.write(reg);
        _wire.endTransmission(false);
        _wire.requestFrom((uint8_t)BM8563_ADDR, (uint8_t)1);
        if (_wire.available()) return _wire.read();
        return 0x00;
    }
};
