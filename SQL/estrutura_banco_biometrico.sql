-- SCRIPT AJUSTADO: estrutura_banco_biometrico_enhanced.sql

CREATE SCHEMA IF NOT EXISTS sistema_biometrico;

-- Tipos enumerados (mais seguro que CHECK com TEXT)
DO $$
BEGIN
  IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'tipo_usuario_enum') THEN
    CREATE TYPE sistema_biometrico.tipo_usuario_enum AS ENUM ('aluno', 'professor');
  END IF;
  IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'tipo_acesso_enum') THEN
    CREATE TYPE sistema_biometrico.tipo_acesso_enum AS ENUM ('entrada', 'saida');
  END IF;
END$$;

-- Tabela de usuários
CREATE TABLE IF NOT EXISTS sistema_biometrico.usuarios (
    id SERIAL PRIMARY KEY,
    nome TEXT NOT NULL,
    codigo TEXT UNIQUE NOT NULL,           -- identificador do usuário (badge, matrícula, etc.)
    tipo_usuario sistema_biometrico.tipo_usuario_enum NOT NULL,
    criado_em TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Índice para buscas rápidas pelo código
CREATE UNIQUE INDEX IF NOT EXISTS idx_usuarios_codigo ON sistema_biometrico.usuarios (codigo);

-- Tabela de salas
CREATE TABLE IF NOT EXISTS sistema_biometrico.salas (
    id SERIAL PRIMARY KEY,
    nome TEXT NOT NULL,
    descricao TEXT,
    criado_em TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Evitar duplicates de nome de sala 
CREATE UNIQUE INDEX IF NOT EXISTS idx_salas_nome ON sistema_biometrico.salas (nome);

-- Associação entre usuários e salas (evita duplicação com UNIQUE composto)
CREATE TABLE IF NOT EXISTS sistema_biometrico.usuarios_salas (
    id SERIAL PRIMARY KEY,
    usuario_id INTEGER NOT NULL REFERENCES sistema_biometrico.usuarios(id) ON DELETE CASCADE ON UPDATE CASCADE,
    sala_id INTEGER NOT NULL REFERENCES sistema_biometrico.salas(id) ON DELETE CASCADE ON UPDATE CASCADE,
    criado_em TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE(usuario_id, sala_id)
);

-- Tabela de digitais (templates)
CREATE TABLE IF NOT EXISTS sistema_biometrico.digitais (
    id SERIAL PRIMARY KEY,
    usuario_id INTEGER NOT NULL REFERENCES sistema_biometrico.usuarios(id)
        ON DELETE CASCADE ON UPDATE CASCADE,
    template_b64 TEXT NOT NULL,
    hash_sha256 CHAR(64) NOT NULL,              -- calculado pelo backend em SHA256(template_b64)
    dedo SMALLINT,
    ativo BOOLEAN NOT NULL DEFAULT true,
    criado_em TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE (usuario_id, hash_sha256)
);



-- Índice para busca por hash (comparação rápida)
CREATE INDEX IF NOT EXISTS idx_digitais_hash ON sistema_biometrico.digitais (hash_sha256);

-- Histórico de acessos
CREATE TABLE IF NOT EXISTS sistema_biometrico.historico_acessos (
    id SERIAL PRIMARY KEY,
    usuario_id INTEGER REFERENCES sistema_biometrico.usuarios(id) ON DELETE SET NULL ON UPDATE CASCADE,
    sala_id INTEGER REFERENCES sistema_biometrico.salas(id) ON DELETE SET NULL ON UPDATE CASCADE,
    data_hora TIMESTAMPTZ NOT NULL DEFAULT now(),
    tipo_acesso sistema_biometrico.tipo_acesso_enum NOT NULL,
    motivo TEXT,               -- opcional: "porta aberta manualmente", "falha leitura", etc.
    metadata JSONB             -- opcional: armazenar resposta do leitor, confidence, ip, etc.
);

-- Índice para consultas por período
CREATE INDEX IF NOT EXISTS idx_historico_datahora ON sistema_biometrico.historico_acessos (data_hora);

-- Permissões mínimas (exemplo) - ajustar conforme seu usuário/role
-- GRANT SELECT, INSERT, UPDATE, DELETE ON ALL TABLES IN SCHEMA sistema_biometrico TO your_app_role;

