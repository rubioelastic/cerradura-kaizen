// ============================================================
// test/test_03_eeprom/test_eeprom.cpp
//
// BDD Test Suite 3 — Persistencia de matrículas en EEPROM
//
// Flujo crítico cubierto:
//   AddMatricula / RemoveMatricula / Habilitado / GetNMatriculas
//   son el núcleo de la persistencia de acceso. Un bug aquí puede
//   dejar matrículas huérfanas (E2: "chapuza primer arranque") o
//   corromper el contador del byte 0.
//
// Estrategia de aislamiento:
//   - EEPROM_mock.h sustituye la librería real por un array en RAM
//   - Las funciones EEPROM se reimplementan aquí (espejo de main.cpp)
//     para no arrastrar M5Dial ni espnow_kaizen al build nativo.
//   - EEPROM.reset_mock() se llama en setUp() para garantizar
//     aislamiento total entre casos de prueba.
// ============================================================

// Los mocks deben incluirse antes que cualquier cabecera del proyecto
#define UNIT_TEST
#include "EEPROM_mock.h"  // sustituye EEPROM real (está en test/mocks/ vía -I)
#include <unity.h>
#include <cstring>
#include <cstdint>

// ── Constante ─────────────────────────────────────────────
#define MAX_MATRICULAS 255

// ── Funciones bajo prueba (espejo exacto de main.cpp) ─────
// Cuando se extraigan a include/acceso_helpers.h, sustituir
// estas implementaciones por un #include de ese header.

static unsigned char GetNMatriculasHabilitadas() {
    return (unsigned char)EEPROM.read(0);
}

static void GetMatricula(char *m, unsigned char i) {
    for (int j = 0; j < 8; j++)
        m[j] = (char)EEPROM.read((i * 8) + j + 1);
    m[8] = '\0';
}

static bool SonIguales(const char *m1, const char *m2) {
    for (int i = 0; i < 8; i++)
        if (m1[i] != m2[i]) return false;
    return true;
}

static int Indice(const char *m) {
    unsigned char n = GetNMatriculasHabilitadas();
    for (int i = 0; i < (int)n; i++) {
        char tmp[9];
        GetMatricula(tmp, i);
        if (SonIguales(m, tmp)) return i;
    }
    return -1;
}

static bool Habilitado(const char *m) { return Indice(m) != -1; }

static void AddMatricula(const char *m) {
    if (Habilitado(m)) return;
    unsigned char n = GetNMatriculasHabilitadas();
    for (int i = 0; i < 8; i++) {
        EEPROM.write((n * 8) + i + 1, (uint8_t)m[i]);
        EEPROM.commit();
    }
    EEPROM.write(0, n + 1);
    EEPROM.commit();
}

static void RemoveMatricula(const char *m) {
    int idx = Indice(m);
    while (idx != -1) {
        unsigned char n = GetNMatriculasHabilitadas();
        for (int i = idx; i < (int)n; i++) {
            for (int z = 0; z < 8; z++) {
                EEPROM.write((i * 8) + z + 1, EEPROM.read(((i + 1) * 8) + z + 1));
                EEPROM.commit();
            }
        }
        EEPROM.write(0, n - 1);
        EEPROM.commit();
        idx = Indice(m);
    }
}

static void RemoveAllMatriculas() {
    EEPROM.write(0, 0);
    EEPROM.commit();
}

// ── setUp / tearDown ─────────────────────────────────────
void setUp(void) {
    EEPROM.reset_mock();         // tabla vacía antes de cada test
    EEPROM.begin(2297);
}
void tearDown(void) {}

// ============================================================
//  ESCENARIO 1 — Añadir una matrícula la persiste en EEPROM
// ============================================================
void test_add_matricula_incrementa_contador(void) {
    // DADO una EEPROM vacía (0 matrículas)
    TEST_ASSERT_EQUAL(0, GetNMatriculasHabilitadas());

    // CUANDO se añade una matrícula válida
    AddMatricula("AABBCCDD");

    // ENTONCES el contador sube a 1 y la matrícula es habilitada
    TEST_ASSERT_EQUAL(1, GetNMatriculasHabilitadas());
    TEST_ASSERT_TRUE(Habilitado("AABBCCDD"));
}

// ============================================================
//  ESCENARIO 2 — AddMatricula no añade duplicados
// ============================================================
void test_add_matricula_no_duplica(void) {
    // DADO una matrícula ya registrada
    AddMatricula("AABBCCDD");
    TEST_ASSERT_EQUAL(1, GetNMatriculasHabilitadas());

    // CUANDO se intenta añadir la misma matrícula de nuevo
    AddMatricula("AABBCCDD");

    // ENTONCES el contador no cambia
    TEST_ASSERT_EQUAL(1, GetNMatriculasHabilitadas());
}

// ============================================================
//  ESCENARIO 3 — Eliminar una matrícula existente
// ============================================================
void test_remove_matricula_existente(void) {
    // DADO dos matrículas registradas
    AddMatricula("AABBCCDD");
    AddMatricula("11223344");
    TEST_ASSERT_EQUAL(2, GetNMatriculasHabilitadas());

    // CUANDO se elimina la primera
    RemoveMatricula("AABBCCDD");

    // ENTONCES queda solo la segunda y el contador es 1
    TEST_ASSERT_EQUAL(1, GetNMatriculasHabilitadas());
    TEST_ASSERT_FALSE(Habilitado("AABBCCDD"));
    TEST_ASSERT_TRUE (Habilitado("11223344"));
}

// ============================================================
//  ESCENARIO 4 — Eliminar una matrícula compacta el array
// ============================================================
void test_remove_matricula_del_medio_compacta(void) {
    // DADO tres matrículas: A, B, C
    AddMatricula("AAAAAAAA");
    AddMatricula("BBBBBBBB");
    AddMatricula("CCCCCCCC");

    // CUANDO se elimina la del medio (B)
    RemoveMatricula("BBBBBBBB");

    // ENTONCES A y C siguen habilitadas y el contador es 2
    TEST_ASSERT_EQUAL(2, GetNMatriculasHabilitadas());
    TEST_ASSERT_TRUE (Habilitado("AAAAAAAA"));
    TEST_ASSERT_FALSE(Habilitado("BBBBBBBB"));
    TEST_ASSERT_TRUE (Habilitado("CCCCCCCC"));

    // Y la posición 1 ahora contiene C (compactación correcta)
    char buf[9];
    GetMatricula(buf, 1);
    TEST_ASSERT_EQUAL_STRING("CCCCCCCC", buf);
}

// ============================================================
//  ESCENARIO 5 — RemoveAll borra todas las matrículas
// ============================================================
void test_remove_all_vacia_la_eeprom(void) {
    // DADO 3 matrículas registradas
    AddMatricula("AAA00001");
    AddMatricula("AAA00002");
    AddMatricula("AAA00003");
    TEST_ASSERT_EQUAL(3, GetNMatriculasHabilitadas());

    // CUANDO se ejecuta RemoveAll
    RemoveAllMatriculas();

    // ENTONCES el contador vuelve a 0
    TEST_ASSERT_EQUAL(0, GetNMatriculasHabilitadas());
    TEST_ASSERT_FALSE(Habilitado("AAA00001"));
}

// ============================================================
//  ESCENARIO 6 — E2: "Chapuza primer arranque" no corrompe EEPROM
//   Reproduce exactamente el bloque de setup() de main.cpp
// ============================================================
void test_chapuza_arranque_idempotente(void) {
    // DADO el estado de la EEPROM en el primer arranque (vacía)
    // CUANDO se ejecuta la secuencia de inicialización del setup()
    //   habilitarMatricula + Remove + habilitarMatricula
    AddMatricula("88040220");
    RemoveMatricula("88040220");
    AddMatricula("88040220");

    // ENTONCES solo hay 1 matrícula registrada (no hay duplicados)
    TEST_ASSERT_EQUAL(1, GetNMatriculasHabilitadas());
    TEST_ASSERT_TRUE(Habilitado("88040220"));

    // Y si el arranque se repite (reset del dispositivo)
    AddMatricula("88040220"); // no añade duplicado
    RemoveMatricula("88040220");
    AddMatricula("88040220");

    // ENTONCES sigue habiendo solo 1 matrícula
    TEST_ASSERT_EQUAL(1, GetNMatriculasHabilitadas());
}

// ── Entry point ─────────────────────────────────────────────
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_add_matricula_incrementa_contador);
    RUN_TEST(test_add_matricula_no_duplica);
    RUN_TEST(test_remove_matricula_existente);
    RUN_TEST(test_remove_matricula_del_medio_compacta);
    RUN_TEST(test_remove_all_vacia_la_eeprom);
    RUN_TEST(test_chapuza_arranque_idempotente);
    return UNITY_END();
}
