// ============================================================
// test/test_02_comparacion/test_comparacion.cpp
//
// BDD Test Suite 2 — Comparación e identificación de matrículas
//
// Flujo crítico cubierto:
//   SonIguales() y Es0() son las funciones de guardia de TODO
//   el sistema de acceso. Un fallo en ellas abriría la cerradura
//   a cualquiera o no la abriría nunca. Son las pruebas de mayor
//   impacto en seguridad del proyecto.
//
// Estrategia de aislamiento:
//   Las funciones son pura lógica de cadenas (char[9]).
//   Se implementan inline aquí para no arrastrar dependencias
//   de hardware de main.cpp. Cuando se refactorice main.cpp
//   para extraerlas a include/acceso_helpers.h, estos tests
//   seguirán compilando sin cambios.
// ============================================================

#include <unity.h>
#include <cstdint>
#include <cstring>

// ── Funciones bajo prueba (inline, espejo de main.cpp) ───────

static bool SonIguales(const char *m1, const char *m2) {
    for (int i = 0; i < 8; i++)
        if (m1[i] != m2[i]) return false;
    return true;
}

static bool Es0(const char *matricula) {
    const char cero[9] = {'0','0','0','0','0','0','0','0','\0'};
    return SonIguales(matricula, cero);
}

// ── Helper ───────────────────────────────────────────────────
static void fill(char *buf, const char *src) {
    // Copia 8 chars + nul garantizando la longitud exacta del sistema
    for (int i = 0; i < 8; i++) buf[i] = src[i];
    buf[8] = '\0';
}

// ============================================================
//  ESCENARIO 1 — Dos matrículas idénticas se reconocen iguales
// ============================================================
void test_matriculas_identicas_son_iguales(void) {
    // DADO dos buffers con la misma matrícula
    char a[9], b[9];
    fill(a, "00405106");
    fill(b, "00405106");

    // CUANDO se comparan
    // ENTONCES son iguales
    TEST_ASSERT_TRUE(SonIguales(a, b));
}

// ============================================================
//  ESCENARIO 2 — Matrículas distintas no son iguales
// ============================================================
void test_matriculas_distintas_no_son_iguales(void) {
    // DADO dos matrículas diferentes
    char a[9], b[9];
    fill(a, "00405106");
    fill(b, "88040220");

    // CUANDO se comparan
    // ENTONCES no son iguales
    TEST_ASSERT_FALSE(SonIguales(a, b));
}

// ============================================================
//  ESCENARIO 3 — Diferencia en el primer byte se detecta
// ============================================================
void test_diferencia_en_primer_byte_detectada(void) {
    // DADO dos matrículas que solo difieren en el primer carácter
    // (caso límite: la comparación no debe comparar solo los últimos bytes)
    char a[9], b[9];
    fill(a, "00405106");
    fill(b, "10405106"); // solo el primer dígito cambia

    // CUANDO se comparan
    // ENTONCES se detecta la diferencia
    TEST_ASSERT_FALSE(SonIguales(a, b));
}

// ============================================================
//  ESCENARIO 4 — Diferencia en el último byte se detecta
// ============================================================
void test_diferencia_en_ultimo_byte_detectada(void) {
    // DADO dos matrículas que solo difieren en el último carácter
    char a[9], b[9];
    fill(a, "00405106");
    fill(b, "00405107"); // solo el último dígito cambia

    // CUANDO se comparan
    // ENTONCES se detecta la diferencia
    TEST_ASSERT_FALSE(SonIguales(a, b));
}

// ============================================================
//  ESCENARIO 5 — Buffer vacío (todos ceros) es reconocido como nulo
// ============================================================
void test_buffer_todo_ceros_es_reconocido_como_vacio(void) {
    // DADO un buffer inicializado a todo ceros (estado "sin tarjeta")
    char m[9] = {'0','0','0','0','0','0','0','0','\0'};

    // CUANDO se evalúa Es0()
    // ENTONCES devuelve true (no se detectó tarjeta)
    TEST_ASSERT_TRUE(Es0(m));
}

// ============================================================
//  ESCENARIO 6 — Una matrícula real no es confundida con vacía
// ============================================================
void test_matricula_real_no_es_confundida_con_vacia(void) {
    // DADO una matrícula real leída de una tarjeta
    char m[9];
    fill(m, "00405106");

    // CUANDO se evalúa Es0()
    // ENTONCES devuelve false (hay tarjeta presente)
    TEST_ASSERT_FALSE(Es0(m));
}

// ============================================================
//  ESCENARIO 7 — Matrículas maestras del sistema son reconocibles
// ============================================================
void test_matrículas_maestras_son_distintas_entre_si(void) {
    // DADO las dos matrículas maestras por defecto del sistema
    char m1[9], m2[9];
    fill(m1, "00405106"); // MAESTRAS_DEFECTO[0]
    fill(m2, "88040220"); // MAESTRAS_DEFECTO[1]

    // CUANDO se comparan entre sí
    // ENTONCES no son iguales (son dos tarjetas maestras distintas)
    TEST_ASSERT_FALSE(SonIguales(m1, m2));

    // Y tampoco son vacías
    TEST_ASSERT_FALSE(Es0(m1));
    TEST_ASSERT_FALSE(Es0(m2));
}

// ── setUp / tearDown (requeridos por Unity) ────────────────
void setUp(void) {}
void tearDown(void) {}

// ── Entry point ─────────────────────────────────────────────
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_matriculas_identicas_son_iguales);
    RUN_TEST(test_matriculas_distintas_no_son_iguales);
    RUN_TEST(test_diferencia_en_primer_byte_detectada);
    RUN_TEST(test_diferencia_en_ultimo_byte_detectada);
    RUN_TEST(test_buffer_todo_ceros_es_reconocido_como_vacio);
    RUN_TEST(test_matricula_real_no_es_confundida_con_vacia);
    RUN_TEST(test_matrículas_maestras_son_distintas_entre_si);
    return UNITY_END();
}
