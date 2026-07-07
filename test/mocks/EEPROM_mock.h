#pragma once
// ============================================================
// test/mocks/EEPROM_mock.h — Simulación de EEPROM en RAM
//
// Sustituye a la librería EEPROM del Arduino/ESP32 en las
// pruebas nativas (env:native, -DUNIT_TEST).
//
// Características:
//   - Respaldo en un array estático de 2300 bytes (cubre 2297)
//   - begin(), read(), write(), commit() con la misma firma
//   - reset_mock() para limpiar entre pruebas (solo en tests)
// ============================================================

#ifndef UNIT_TEST
#error "EEPROM_mock.h solo debe incluirse en builds de prueba (-DUNIT_TEST)"
#endif

#include <cstdint>
#include <cstring>

static constexpr uint16_t EEPROM_MOCK_SIZE = 2300;

class EEPROMClass_Mock {
public:
    bool begin(size_t size) {
        _size = (size > EEPROM_MOCK_SIZE) ? EEPROM_MOCK_SIZE : size;
        return true;
    }

    uint8_t read(int addr) const {
        if (addr < 0 || addr >= (int)_size) return 0xFF;
        return _data[addr];
    }

    void write(int addr, uint8_t val) {
        if (addr >= 0 && addr < (int)_size)
            _data[addr] = val;
    }

    void commit() { /* no-op en RAM */ }

    // Limpia toda la memoria a 0x00 (útil entre casos de prueba)
    void reset_mock() {
        std::memset(_data, 0x00, sizeof(_data));
        _size = EEPROM_MOCK_SIZE;
    }

    // Expone el buffer para inspección directa en las pruebas
    const uint8_t* raw() const { return _data; }

private:
    uint8_t  _data[EEPROM_MOCK_SIZE] = {};
    size_t   _size = EEPROM_MOCK_SIZE;
};

// Instancia global que sustituye al objeto EEPROM del framework
static EEPROMClass_Mock EEPROM;
