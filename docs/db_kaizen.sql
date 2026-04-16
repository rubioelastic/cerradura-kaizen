-- ============================================================
-- db_kaizen.sql — Esquema SQL Server para Cerradura Kaizen
-- Una cerradura = un espacio (relación 1:1)
-- Identificación del dispositivo: MAC WiFi STA del M5Dial
-- Timestamp: el Bridge reenvía el millis() crudo del firmware (uint32, ms desde boot).
--   millis_fw  → valor raw del dispositivo (referencia relativa, se reinicia con el ESP)
--   created_at → DATETIME2 del servidor cuando el Bridge inserta el registro (referencia real)
-- ============================================================

USE KaizenDB;   -- cambia por el nombre de tu base de datos
GO

-- ============================================================
-- TABLA: espacios
--   Cada fila representa un M5Dial / espacio físico.
--   El Bridge identifica el dispositivo por su MAC.
-- ============================================================
CREATE TABLE espacios (
    id               INT            IDENTITY(1,1)  NOT NULL,
    mac              CHAR(17)                       NOT NULL,   -- 'XX:XX:XX:XX:XX:XX'
    nombre           NVARCHAR(32)                   NOT NULL,   -- asignado por Bridge en KAIZEN_CONFIG
    estado           TINYINT        NOT NULL DEFAULT 0,          -- 0=LIBRE  1=OCUPADO
    ocupante         CHAR(8)                        NULL,        -- matrícula actual (NULL si LIBRE)
    modo_estado      BIT            NOT NULL DEFAULT 0,          -- 0=solo acceso  1=acceso+estado
    firmware_version INT            NOT NULL DEFAULT 1,
    ultimo_sync      DATETIME2(0)                   NULL,        -- último KAIZEN_SYNC recibido
    bridge_ok        BIT            NOT NULL DEFAULT 0,          -- 1 si Bridge responde en <2 min
    created_at       DATETIME2(0)   NOT NULL DEFAULT SYSUTCDATETIME(),

    CONSTRAINT PK_espacios        PRIMARY KEY (id),
    CONSTRAINT UQ_espacios_mac    UNIQUE      (mac),
    CONSTRAINT CK_espacios_estado CHECK       (estado IN (0, 1))
);
GO

-- ============================================================
-- TABLA: matriculas_autorizadas
--   Lista de códigos de tarjeta RFID habilitados por espacio.
--   Máximo 255 filas activas por espacio (límite del firmware).
-- ============================================================
CREATE TABLE matriculas_autorizadas (
    id           INT           IDENTITY(1,1)  NOT NULL,
    id_espacio   INT                          NOT NULL,
    matricula    CHAR(8)                      NOT NULL,   -- 8 chars ASCII, p.ej. '88040220'
    activa       BIT           NOT NULL DEFAULT 1,
    created_at   DATETIME2(0)  NOT NULL DEFAULT SYSUTCDATETIME(),

    CONSTRAINT PK_matriculas         PRIMARY KEY (id),
    CONSTRAINT FK_matriculas_espacio FOREIGN KEY (id_espacio)
        REFERENCES espacios (id) ON DELETE CASCADE,
    CONSTRAINT UQ_matriculas_espacio UNIQUE (id_espacio, matricula)
);
GO

-- ============================================================
-- TABLA: eventos_acceso
--   Registro histórico de cada evento reportado por el firmware
--   en la respuesta al KAIZEN_SYNC.
--   tipo: 1=ACCESO_OK  2=ACCESO_DENEGADO  3=APERTURA_MANUAL
--
--   millis_fw : uint32 que manda el firmware (ms desde el último arranque del ESP).
--               NO es una hora real. Se reinicia a 0 cada vez que el dispositivo
--               se apaga/reinicia (~49 días de rollover).
--   created_at: datetime del servidor en el momento en que el Bridge inserta la fila.
--               Esta es la referencia de tiempo real para consultas históricas.
-- ============================================================
CREATE TABLE eventos_acceso (
    id           BIGINT        IDENTITY(1,1)  NOT NULL,
    id_espacio   INT                          NOT NULL,
    tipo         TINYINT                      NOT NULL,
    matricula    CHAR(8)                      NULL,        -- NULL en APERTURA_MANUAL
    millis_fw    BIGINT                       NOT NULL,    -- millis() crudo del ESP (relativo, no es hora real)
    created_at   DATETIME2(0)  NOT NULL DEFAULT SYSUTCDATETIME(),  -- hora real del servidor

    CONSTRAINT PK_eventos          PRIMARY KEY (id),
    CONSTRAINT FK_eventos_espacio  FOREIGN KEY (id_espacio)
        REFERENCES espacios (id) ON DELETE CASCADE,
    CONSTRAINT CK_eventos_tipo     CHECK       (tipo IN (1, 2, 3))
);
GO

-- ============================================================
-- TABLA: ocupaciones
--   Historial de periodos de ocupación por espacio.
--   fin = NULL → la sala sigue ocupada en este momento.
--   inicio / fin : DATETIME2 del servidor — el Bridge los fija cuando procesa
--                 los eventos ACCESO_OK / KAIZEN_LIBERAR. No vienen del firmware.
-- ============================================================
CREATE TABLE ocupaciones (
    id           BIGINT        IDENTITY(1,1)  NOT NULL,
    id_espacio   INT                          NOT NULL,
    matricula    CHAR(8)                      NOT NULL,
    inicio       DATETIME2(0)                 NOT NULL,
    fin          DATETIME2(0)                 NULL,

    CONSTRAINT PK_ocupaciones         PRIMARY KEY (id),
    CONSTRAINT FK_ocupaciones_espacio FOREIGN KEY (id_espacio)
        REFERENCES espacios (id) ON DELETE CASCADE
);
GO

-- ============================================================
-- ÍNDICES
-- ============================================================

-- Consultas frecuentes por espacio + fecha real de inserción
CREATE INDEX IX_eventos_espacio_fecha
    ON eventos_acceso (id_espacio, created_at DESC);
GO

-- Buscar rápido qué matrícula está activa en un espacio
CREATE INDEX IX_matriculas_espacio_activa
    ON matriculas_autorizadas (id_espacio, activa);
GO

-- Ocupación vigente: buscar filas con fin IS NULL
CREATE INDEX IX_ocupaciones_vigente
    ON ocupaciones (id_espacio, fin);
GO

-- ============================================================
-- DATOS INICIALES DE EJEMPLO
--   Adapta la MAC y el nombre antes de ejecutar.
-- ============================================================
INSERT INTO espacios (mac, nombre, modo_estado)
VALUES ('3C:DC:75:EE:50:88', 'Sala Kaizen A', 1);
GO
