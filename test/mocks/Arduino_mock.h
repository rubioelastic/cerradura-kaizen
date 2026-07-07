#pragma once
// ============================================================
// test/mocks/Arduino_mock.h — Stubs mínimos del runtime Arduino
//
// Solo define lo necesario para compilar los módulos bajo prueba
// en un entorno nativo (PC). No simula comportamiento real.
// ============================================================

#ifndef UNIT_TEST
#error "Arduino_mock.h solo debe incluirse en builds de prueba"
#endif

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

// ── Tipos básicos Arduino ──────────────────────────────────
using byte    = uint8_t;
using boolean = bool;

// ── String Arduino (alias a std::string para tests) ────────
using String = std::string;

// ── Serial stub (imprime a stdout en tests) ────────────────
struct SerialStub {
    template<typename T>
    void print(T val)   { /* silencio en pruebas */ }
    template<typename T>
    void println(T val) { /* silencio en pruebas */ }
    template<typename T, typename U>
    void printf(T, U)   { /* silencio en pruebas */ }
    void begin(int)     {}
} Serial;

// ── delay / millis stubs ───────────────────────────────────
inline void     delay(unsigned long) {}
inline uint32_t millis() { return 0; }

// ── GPIO stubs ─────────────────────────────────────────────
inline void     pinMode(uint8_t, uint8_t)         {}
inline void     digitalWrite(uint8_t, uint8_t)    {}
inline int      digitalRead(uint8_t)               { return 1; }

// ── LEDC (buzzer PWM) stubs ────────────────────────────────
inline void     ledcSetup(uint8_t, uint32_t, uint8_t) {}
inline void     ledcAttachPin(uint8_t, uint8_t)        {}
inline void     ledcWriteTone(uint8_t, uint32_t)       {}

// ── Constantes digitales ───────────────────────────────────
#define HIGH    1
#define LOW     0
#define INPUT_PULLUP 2
#define OUTPUT  1
