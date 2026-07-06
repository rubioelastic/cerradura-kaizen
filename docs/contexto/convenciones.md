# Convenciones — Cerradura Kaizen

## Estilo de código

- **Lenguaje:** C++17 (Arduino framework)
- **Indentación:** 4 espacios (visible en `main.cpp`) / mezcla con tabs en `AccesoKaizen.ino` (legado)
- **Llaves:** estilo Allman (llave en línea propia) en el código nuevo; K&R en el legado
- **Máximo de línea:** sin límite fijo definido

---

## Naming

| Elemento | Convención | Ejemplos |
|---|---|---|
| Clases propias | `PascalCase` con prefijo `Kaizen` | `KaizenUI`, `KaizenRTC` |
| Funciones de negocio | `PascalCase` | `AbrirCerradura()`, `ReadEmpleado()`, `SonIguales()` |
| Funciones de buzzer | `Buzzer_` + acción | `Buzzer_AccessOK()`, `Buzzer_AddMode()` |
| Funciones ESP-NOW | `kaizen_` + verbo | `kaizen_begin()`, `kaizen_tick()`, `kaizen_marcarOcupado()` |
| Constantes / `#define` | `SCREAMING_SNAKE_CASE` | `MAX_MATRICULAS`, `TIEMPO_ABIERTA_MS`, `BUZZER_PIN` |
| Variables globales | `camelCase` | `ultimaActualizacionHora`, `franjaAcceso` |
| Variables locales | `camelCase` corto | `matricula`, `tiempo`, `nMatriculas` |
| Structs | `PascalCase` | `DateTime`, `AccessSchedule`, `KaizenEventoPendiente` |
| Enums | `PascalCase` / `SCREAMING` para valores | `UIState`, `UI_STATE_IDLE`, `KaizenEvento::ACCESO_OK` |
| `typedef` de callback | `PascalCase` + `Cb` | `KaizenConfigCb` |
| Pines en `pins_config.h` | `SCREAMING_SNAKE_CASE` + sufijo `_PIN` (opcional) | `RELE`, `PULSADOR_PUERTA`, `BUZZER_PIN`, `I2C_SDA_PIN` |
| Colores UI | `COL_` + nombre | `COL_GREEN`, `COL_BG_BLACK` |

---

## Patrones usados

- **Clase con referencia al hardware externo:** `KaizenUI(M5GFX &display)`, `KaizenRTC(TwoWire &wire)` — se pasan por referencia, no se instancian internamente.
- **Estado de UI como enum:** `UIState` guarda el estado actual; los métodos `draw*` actualizan `_currentState`.
- **Burst read I2C:** `rtc_bm8563.h` lee los 7 registros de tiempo en una sola transacción.
- **Guard de compilación:** `#pragma once` en todos los headers.
- **Comentarios de migración:** bloques extensos con `MANTENIDO`, `CAMBIO`, `NUEVO`, `ELIMINADO` explican qué vino del código original.
- **EEPROM compactada manualmente:** `RemoveMatricula()` desplaza registros hacia atrás en lugar de usar listas enlazadas.

---

## Patrones prohibidos / evitados

- **No usar MFRC522 como librería externa:** el acceso RFID se hace siempre vía `M5Dial.Rfid` (WS1850S I2C), nunca instanciando `MFRC522` directamente.
- **No usar SPI para RFID:** el `.ino` legado lo hacía; el firmware nuevo solo usa I2C.
- **No usar `Wire` directo para el RTC o el RFID:** el M5Dial expone estos periféricos en un bus I2C interno gestionado por el BSP. Solo `M5Dial.Rtc` y `M5Dial.Rfid`.
- **No usar `Serial` como interfaz de usuario:** solo para depuración (`[RFID]`, `[ACCESO]`, `[EEPROM]`).
- **No incluir `rfid_ws1850s.h`:** comentado explícitamente en `platformio.ini`.
- **No incluir `espnow_kaizen.h` en más de una unidad de compilación:** es header-only con variables `static`; incluirlo en varios `.cpp` las duplicaría.

---

## Tests

**No existen tests.** No hay carpeta `test/`, no hay framework de testing configurado en `platformio.ini`.

La verificación funcional se hace manualmente con tarjeta física y monitor serie.

---

## Commits

El proyecto usa **Conventional Commits**:

```
feat: descripción de nueva funcionalidad
fix:  descripción de corrección
chore: tareas de mantenimiento / metadatos
```

Ejemplos del historial:
- `feat: migración Cerradura Kaizen de ESP32+MFRC522 a M5Dial`
- `fix: corregir EEPROM size y lógica de maestras por defecto`
- `feat: ESP-NOW Bridge integration + docs`
- `chore: fix gitignore macOS ._* + add ReactionTime doc`

No se han visto `BREAKING CHANGE`, `refactor:` ni `test:` en el historial.

**Agente Copilot:** el repo incluye `.github/agents/kaizen-firmware.agent.md` con instrucciones específicas para el proyecto. Se invoca automáticamente al hacer preguntas sobre el firmware.
