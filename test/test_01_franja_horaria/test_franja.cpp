// ============================================================
// test/test_01_franja_horaria/test_franja.cpp
//
// BDD Test Suite 1 — Control de franja horaria de acceso
//
// Flujo crítico cubierto:
//   La lógica isAccessAllowed() decide si se concede o deniega
//   acceso según la hora actual y los días habilitados en
//   AccessSchedule. Esta es la ÚNICA defensa temporal del sistema.
//
// Estrategia de aislamiento:
//   Las estructuras DateTime y AccessSchedule + la función
//   isAccessAllowed() son pura lógica C++ sin dependencias de
//   hardware. Se copian aquí para evitar incluir M5Dial.h.
// ============================================================

#include <unity.h>
#include <cstdint>
#include <cstring>

// ── Estructuras y función copiadas de include/rtc_bm8563.h ──
// (pura lógica, sin dependencias de hardware)

struct DateTime {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  weekday; // 0=Dom, 1=Lun … 6=Sab
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    bool     valid;
};

struct AccessSchedule {
    uint8_t startHour;
    uint8_t startMinute;
    uint8_t endHour;
    uint8_t endMinute;
    bool    enabledDays[7];
    bool    enabled;
};

static bool isAccessAllowed(const DateTime &dt, const AccessSchedule &schedule) {
    if (!schedule.enabled) return true;
    if (!schedule.enabledDays[dt.weekday]) return false;
    uint16_t cur   = dt.hour * 60 + dt.minute;
    uint16_t start = schedule.startHour * 60 + schedule.startMinute;
    uint16_t end_  = schedule.endHour   * 60 + schedule.endMinute;
    if (start <= end_)
        return (cur >= start && cur <= end_);
    else
        return (cur >= start || cur <= end_);
}

// ── Franja de prueba reutilizable ────────────────────────────
// Lun-Vie, 08:00 – 20:00
static AccessSchedule make_schedule_lv_8_20() {
    AccessSchedule s;
    s.startHour    = 8;
    s.startMinute  = 0;
    s.endHour      = 20;
    s.endMinute    = 0;
    s.enabledDays[0] = false; // Dom
    s.enabledDays[1] = true;  // Lun
    s.enabledDays[2] = true;  // Mar
    s.enabledDays[3] = true;  // Mie
    s.enabledDays[4] = true;  // Jue
    s.enabledDays[5] = true;  // Vie
    s.enabledDays[6] = false; // Sab
    s.enabled = true;
    return s;
}

// ============================================================
//  ESCENARIO 1 — Restricción desactivada
// ============================================================
void test_acceso_siempre_permitido_si_franja_desactivada(void) {
    // DADO que la restricción horaria está desactivada
    AccessSchedule schedule = make_schedule_lv_8_20();
    schedule.enabled = false;

    // CUANDO se consulta el acceso en cualquier hora y día
    DateTime dt_domingo_madrugada = {2026, 7, 5, 0, 3, 0, 0, true}; // Dom 03:00

    // ENTONCES siempre se permite el acceso
    TEST_ASSERT_TRUE(isAccessAllowed(dt_domingo_madrugada, schedule));
}

// ============================================================
//  ESCENARIO 2 — Acceso dentro de franja y día habilitado
// ============================================================
void test_acceso_permitido_dentro_franja_y_dia_habilitado(void) {
    // DADO una franja Lun-Vie 08:00-20:00 activa
    AccessSchedule schedule = make_schedule_lv_8_20();

    // CUANDO es martes a las 10:30
    DateTime dt = {2026, 7, 7, 2, 10, 30, 0, true}; // Weekday 2=Martes

    // ENTONCES el acceso está permitido
    TEST_ASSERT_TRUE(isAccessAllowed(dt, schedule));
}

// ============================================================
//  ESCENARIO 3 — Acceso fuera de hora (tarde)
// ============================================================
void test_acceso_denegado_fuera_de_hora(void) {
    // DADO la misma franja activa
    AccessSchedule schedule = make_schedule_lv_8_20();

    // CUANDO es jueves a las 21:15 (fuera de la franja)
    DateTime dt = {2026, 7, 9, 4, 21, 15, 0, true}; // Weekday 4=Jueves

    // ENTONCES el acceso está denegado
    TEST_ASSERT_FALSE(isAccessAllowed(dt, schedule));
}

// ============================================================
//  ESCENARIO 4 — Acceso en día no habilitado
// ============================================================
void test_acceso_denegado_dia_no_habilitado(void) {
    // DADO la misma franja Lun-Vie activa
    AccessSchedule schedule = make_schedule_lv_8_20();

    // CUANDO es domingo a las 10:00 (dentro de hora pero día inválido)
    DateTime dt = {2026, 7, 5, 0, 10, 0, 0, true}; // Weekday 0=Domingo

    // ENTONCES el acceso está denegado
    TEST_ASSERT_FALSE(isAccessAllowed(dt, schedule));
}

// ============================================================
//  ESCENARIO 5 — Franja con cruce de medianoche (22:00 – 06:00)
// ============================================================
void test_acceso_franja_nocturna_cruce_medianoche(void) {
    // DADO una franja nocturna 22:00-06:00 todos los días
    AccessSchedule schedule;
    schedule.startHour    = 22;
    schedule.startMinute  = 0;
    schedule.endHour      = 6;
    schedule.endMinute    = 0;
    for (int i = 0; i < 7; i++) schedule.enabledDays[i] = true;
    schedule.enabled = true;

    // CUANDO es la 1:30 (dentro de la franja nocturna)
    DateTime dt_dentro  = {2026, 7, 7, 2, 1, 30, 0, true};
    // CUANDO es las 8:00 (fuera de la franja nocturna)
    DateTime dt_fuera   = {2026, 7, 7, 2, 8, 0,  0, true};

    // ENTONCES: 01:30 → permitido, 08:00 → denegado
    TEST_ASSERT_TRUE (isAccessAllowed(dt_dentro, schedule));
    TEST_ASSERT_FALSE(isAccessAllowed(dt_fuera,  schedule));
}

// ============================================================
//  ESCENARIO 6 — Exactamente en los límites de la franja
// ============================================================
void test_acceso_en_limite_exacto_de_franja(void) {
    // DADO franja 08:00-20:00 Lun-Vie
    AccessSchedule schedule = make_schedule_lv_8_20();

    // CUANDO son exactamente las 08:00 (inicio) y las 20:00 (fin)
    DateTime dt_inicio = {2026, 7, 7, 2, 8,  0, 0, true}; // Mar 08:00
    DateTime dt_fin    = {2026, 7, 7, 2, 20, 0, 0, true}; // Mar 20:00
    // CUANDO son las 07:59 (un minuto antes del inicio)
    DateTime dt_antes  = {2026, 7, 7, 2, 7, 59, 0, true};

    // ENTONCES: 08:00 y 20:00 inclusivos → permitido; 07:59 → denegado
    TEST_ASSERT_TRUE (isAccessAllowed(dt_inicio, schedule));
    TEST_ASSERT_TRUE (isAccessAllowed(dt_fin,    schedule));
    TEST_ASSERT_FALSE(isAccessAllowed(dt_antes,  schedule));
}

// ── Entry point ─────────────────────────────────────────────
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_acceso_siempre_permitido_si_franja_desactivada);
    RUN_TEST(test_acceso_permitido_dentro_franja_y_dia_habilitado);
    RUN_TEST(test_acceso_denegado_fuera_de_hora);
    RUN_TEST(test_acceso_denegado_dia_no_habilitado);
    RUN_TEST(test_acceso_franja_nocturna_cruce_medianoche);
    RUN_TEST(test_acceso_en_limite_exacto_de_franja);
    return UNITY_END();
}
