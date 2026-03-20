#pragma once
// ============================================================
// pins_config.h — Mapa de pines del M5Dial (ESP32-S3 / M5StampS3)
// Referencia: Esquemático "Sch_M5Dial.pdf"
//
// ARQUITECTURA HARDWARE DEL M5DIAL:
// ┌─────────────────────────────────────────────────────────┐
// │  M5StampS3 (ESP32-S3)                                   │
// │  ┌─────────────┐   I2C interno   ┌──────────────────┐   │
// │  │  ESP32-S3   │◄───────────────►│ WS1850S (RFID)   │   │
// │  │             │                 │ Addr: 0x28        │   │
// │  │             │   I2C interno   ┌──────────────────┐   │
// │  │             │◄───────────────►│ BM8563 (RTC)     │   │
// │  │             │                 │ Addr: 0x51        │   │
// │  │             │   SPI           ┌──────────────────┐   │
// │  │             │◄───────────────►│ GC9A01 (TFT)     │   │
// │  │             │                 └──────────────────┘   │
// │  │             │   GPIO          ┌──────────────────┐   │
// │  │             │◄───────────────►│ Encoder rotatorio│   │
// │  │             │   PWM/GPIO      ┌──────────────────┐   │
// │  │             │────────────────►│ Buzzer (LS1)     │   │
// │  └─────────────┘                 └──────────────────┘   │
// └─────────────────────────────────────────────────────────┘
// ============================================================

// ─────────────────────────────────────────────────────────────
// BUS I2C INTERNO (Wire)
// Compartido por WS1850S (RFID) y BM8563 (RTC)
// ─────────────────────────────────────────────────────────────
#define I2C_SDA_PIN     11   // SDA del bus I2C interno del M5Dial
#define I2C_SCL_PIN     12   // SCL del bus I2C interno del M5Dial
#define I2C_FREQ_HZ     400000UL  // 400 kHz Fast-mode

// ─────────────────────────────────────────────────────────────
// MÓDULO RFID — WS1850S (U1) — I2C
// En el esquemático aparece como RC522_SDA / RC522_SCL,
// pero el WS1850S soporta I2C en esos pines.
// Dirección I2C por defecto del WS1850S: 0x28
// ─────────────────────────────────────────────────────────────
#define RFID_I2C_ADDR   0x28   // Dirección I2C del WS1850S
// Los pines SDA/SCL son los mismos que I2C_SDA_PIN / I2C_SCL_PIN

// ─────────────────────────────────────────────────────────────
// RTC — BM8563 (U5) — I2C
// Dirección I2C del BM8563: 0x51
// ─────────────────────────────────────────────────────────────
#define RTC_I2C_ADDR    0x51   // Dirección I2C del BM8563

// ─────────────────────────────────────────────────────────────
// PANTALLA TFT — GC9A01 (U7) — SPI
// La librería M5Dial gestiona estos pines automáticamente,
// se documentan aquí para referencia.
// ─────────────────────────────────────────────────────────────
// #define TFT_MOSI  6    // Gestionado por M5Dial BSP
// #define TFT_SCLK  5    // Gestionado por M5Dial BSP
// #define TFT_CS    7    // Gestionado por M5Dial BSP
// #define TFT_DC    4    // Gestionado por M5Dial BSP
// #define TFT_RST   8    // Gestionado por M5Dial BSP
// #define TFT_BL    9    // Backlight, gestionado por M5Dial BSP

// ─────────────────────────────────────────────────────────────
// ENCODER ROTATORIO — Gestionado por M5Dial BSP
// Se documenta para referencia.
// ─────────────────────────────────────────────────────────────
// #define ENCODER_A  40  // Gestionado por M5Dial BSP
// #define ENCODER_B  41  // Gestionado por M5Dial BSP
// #define ENCODER_BTN 42 // Botón central del encoder (touch del dial)

// ─────────────────────────────────────────────────────────────
// BUZZER — LS1 — GPIO de PWM
// Conectado al pin "beep" del M5StampS3 según esquemático
// ─────────────────────────────────────────────────────────────
#define BUZZER_PIN      3    // GPIO3 → señal BEEP del M5StampS3

// ─────────────────────────────────────────────────────────────
// GPIO EXTERNOS — Pines disponibles en headers P3 / conector J2
// Usados para RELÉ externo y PULSADOR DE PUERTA
//
//  ┌──────────────────────────────────────────────────────┐
//  │  Conector J2 (HY-2.0 IO, 2 pines):                  │
//  │   Pin 1 → +5VOUT (alimenta bobina del relé)          │
//  │   Pin 2 → GND                                        │
//  ├──────────────────────────────────────────────────────┤
//  │  Header P3 (2.54mm, pines externos disponibles):     │
//  │   GPIO 1  → RELE (salida digital para activar relé)  │
//  │   GPIO 2  → PULSADOR_PUERTA (entrada con pull-up)    │
//  └──────────────────────────────────────────────────────┘
//
//  NOTA DE CONEXIÓN FÍSICA:
//  - Fuente 220V→12V DC alimenta directamente el M5Dial (Vin)
//  - +5VOUT del M5Dial alimenta bobina del relé de 5V
//  - Contacto NO del relé controla la cerradura de 12V
//  - NO se necesita convertidor 12V→5V externo
// ─────────────────────────────────────────────────────────────
#define RELE            1    // GPIO1 del header P3 → control del relé
#define PULSADOR_PUERTA 2    // GPIO2 del header P3 → pulsador de apertura manual

// ─────────────────────────────────────────────────────────────
// CANAL PWM para el buzzer (ESP32 LEDC)
// ─────────────────────────────────────────────────────────────
#define BUZZER_CHANNEL  0    // Canal LEDC 0
#define BUZZER_RESOLUTION 8  // 8 bits de resolución PWM
