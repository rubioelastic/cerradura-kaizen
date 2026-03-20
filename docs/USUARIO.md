# Cerradura Kaizen — Manual de Usuario

> Sistema de control de acceso por tarjeta RFID con pantalla táctil  
> Dispositivo: M5Dial (pantalla redonda 1,28")

---

## ¿Qué hace este dispositivo?

La cerradura Kaizen controla el acceso a un espacio mediante tarjetas RFID. Al acercar una tarjeta autorizada, la puerta se abre durante 3 segundos. La pantalla muestra en todo momento el estado del sistema.

---

## Pantallas que verás

### Pantalla de espera

```
  ┌─────────────┐
  │   [🔒]      │
  │   14:32     │
  │ 20/03/2026  │
  │Acerca tarjeta│
  └─────────────┘
```

Es la pantalla normal cuando nadie está usando la cerradura. Muestra la hora en tiempo real.

---

### Espacio LIBRE (solo si tu espacio tiene gestión de estado activa)

```
  ┌─────────────┐
  │   [🔓]      │
  │   LIBRE     │
  │  Sala test  │
  │   14:32     │
  └─────────────┘
Fondo verde
```

El espacio está disponible. Puedes entrar acercando tu tarjeta.

---

### Espacio OCUPADO (solo si tu espacio tiene gestión de estado activa)

```
  ┌─────────────┐
  │   [🔒]      │
  │  OCUPADO    │
  │ Espacio en  │
  │    uso      │
  └─────────────┘
Fondo rojo oscuro
```

Hay alguien trabajando en el espacio. Puedes entrar igualmente acercando tu tarjeta, pero el espacio seguirá marcado como ocupado por quien lo abrió primero.

---

### Acceso concedido

```
  ┌─────────────┐
  │   [🔓]      │
  │   ACCESO    │
  │ CONCEDIDO   │
  │  12345678   │
  └─────────────┘
Fondo verde, 3 segundos
```

Tu tarjeta ha sido reconocida y la puerta está abierta. Entra en los próximos 3 segundos.

---

### Acceso denegado

```
  ┌─────────────┐
  │     ✕       │
  │   ACCESO    │
  │  DENEGADO   │
  │  12345678   │
  └─────────────┘
Fondo rojo, 2 segundos
```

Puede ocurrir por dos motivos:
- La tarjeta no está autorizada para este espacio.
- Estás intentando acceder fuera del horario permitido.

---

### Pantalla de confirmación (¿liberar o entrar?)

```
  ┌─────────────┐
  │  ¿LIBERAR   │
  │  ESPACIO?   │
  │ Toca: liberar│
  │ Espera: entrar│
  │      3      │
  └─────────────┘
Fondo naranja, cuenta atrás
```

Esta pantalla aparece cuando **eres tú quien ocupa el espacio** y presentas tu tarjeta de nuevo. Tienes 3 segundos para decidir:

- **Toca la pantalla** → el espacio queda **LIBRE** y la puerta se abre.
- **No toques (espera)** → entras de nuevo y el espacio **sigue a tu nombre** (OCUPADO).

---

## Cómo usar la cerradura

### Entrar a un espacio libre

1. Acerca tu tarjeta al lector (zona frontal del M5Dial).
2. Si está autorizada, verás "ACCESO CONCEDIDO" en verde.
3. La puerta se abre. Entra en los 3 segundos siguientes.
4. Si el espacio tiene gestión de estado, quedará marcado como **OCUPADO a tu nombre**.

### Entrar a un espacio ocupado (persona diferente)

1. Acerca tu tarjeta.
2. Verás "ACCESO CONCEDIDO" → la puerta se abre.
3. El estado del espacio **no cambia**: sigue ocupado por quien entró primero.

### Liberar un espacio (la misma persona que lo ocupó)

1. Acerca tu tarjeta (la misma con la que entraste).
2. Aparece la pantalla naranja "¿LIBERAR ESPACIO?".
3. **Toca la pantalla** antes de que llegue a 0 → el espacio queda LIBRE y la puerta se abre.
4. Si no tocas, entras de nuevo sin cambiar el estado.

### Liberar un espacio con tarjeta maestra

La tarjeta maestra abre siempre. Si el espacio está OCUPADO, la tarjeta maestra lo libera automáticamente sin necesitar confirmación.

---

## Preguntas frecuentes

**¿Qué pasa si se va la luz y vuelve?**  
El dispositivo arranca solo. La lista de tarjetas autorizadas se guarda en memoria interna y no se pierde. El reloj puede desfasarse si la batería del RTC se agota.

**¿Puedo entrar aunque el espacio esté ocupado?**  
Sí. Si tienes una tarjeta autorizada, siempre puedes entrar. El estado en pantalla refleja quién ocupa el espacio, pero no bloquea el acceso a otras personas autorizadas.

**Mi tarjeta no funciona. ¿Qué hago?**  
Comprueba que estás acercando correctamente la tarjeta al frontal del dispositivo. Si el problema persiste, contacta con el responsable del espacio para que verifique que tu tarjeta está autorizada.

**La pantalla muestra solo el reloj, sin LIBRE/OCUPADO. ¿Es normal?**  
Sí. El estado LIBRE/OCUPADO solo se muestra cuando el dispositivo está conectado al servidor de gestión (Bridge). Si hay un problema de red, la cerradura sigue funcionando con acceso normal pero sin información de estado.

**¿Cuánto tiempo está abierta la puerta?**  
3 segundos desde que aparece "ACCESO CONCEDIDO".

---

## Señales sonoras

| Sonido | Significado |
|---|---|
| 2 pitidos cortos ascendentes | Acceso concedido |
| 1 pitido grave largo | Acceso denegado |
| 2 pitidos medios rápidos | Modo gestión: añadir tarjeta |
| 3 pitidos medios | Modo gestión: eliminar tarjeta |
| 5 pitidos descendentes (alarma) | Borrado de todas las tarjetas |
