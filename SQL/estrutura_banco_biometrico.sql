-- SCRIPT AJUSTADO: estrutura_banco_biometrico_enhanced.sql
-- VERSÃO 3 (Híbrida: sensor_id + template_b64 para testes)

CREATE SCHEMA IF NOT EXISTS sistema_biometrico;

-- Tipos enumerados (Sem mudanças)
DO $$
BEGIN
  IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'tipo_usuario_enum') THEN
    CREATE TYPE sistema_biometrico.tipo_usuario_enum AS ENUM ('aluno', 'professor');
  END IF;
  IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'tipo_acesso_enum') THEN
    CREATE TYPE sistema_biometrico.tipo_acesso_enum AS ENUM ('entrada', 'saida');
  END IF;
  IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'tipo_dedo_enum') THEN
    CREATE TYPE sistema_biometrico.tipo_dedo_enum AS ENUM (
      'indicador_dir', 'polegar_dir', 'medio_dir', 'anelar_dir', 'minimo_dir',
      'indicador_esq', 'polegar_esq', 'medio_esq', 'anelar_esq', 'minimo_esq'
    );
  END IF;
END$$;

-- Tabela de usuários (Sem mudanças)
CREATE TABLE IF NOT EXISTS sistema_biometrico.usuarios (
    id SERIAL PRIMARY KEY,
    nome TEXT NOT NULL,
    codigo TEXT UNIQUE NOT NULL,
    tipo_usuario sistema_biometrico.tipo_usuario_enum NOT NULL,
    criado_em TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_usuarios_codigo ON sistema_biometrico.usuarios (codigo);

-- Tabela de salas (Sem mudanças)
CREATE TABLE IF NOT EXISTS sistema_biometrico.salas (
    id SERIAL PRIMARY KEY,
    nome TEXT NOT NULL,
    descricao TEXT,
    criado_em TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_salas_nome ON sistema_biometrico.salas (nome);

-- Associação entre usuários e salas (Sem mudanças)
CREATE TABLE IF NOT EXISTS sistema_biometrico.usuarios_salas (
    id SERIAL PRIMARY KEY,
    usuario_id INTEGER NOT NULL REFERENCES sistema_biometrico.usuarios(id) ON DELETE CASCADE ON UPDATE CASCADE,
    sala_id INTEGER NOT NULL REFERENCES sistema_biometrico.salas(id) ON DELETE CASCADE ON UPDATE CASCADE,
    criado_em TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE(usuario_id, sala_id)
);

-- ============================================================
-- Tabela de digitais (MODIFICADA)
-- ============================================================
CREATE TABLE IF NOT EXISTS sistema_biometrico.digitais (
    id SERIAL PRIMARY KEY,
    usuario_id INTEGER NOT NULL REFERENCES sistema_biometrico.usuarios(id)
        ON DELETE CASCADE ON UPDATE CASCADE,
    
    -- ID (slot) onde o sensor armazena (para o fluxo principal)
    sensor_id INTEGER NOT NULL UNIQUE, 

    -- ADICIONADO DE VOLTA: Template para o teste do orientador
    template_b64 TEXT NULL,

    dedo sistema_biometrico.tipo_dedo_enum,
    ativo BOOLEAN NOT NULL DEFAULT true,
    criado_em TIMESTAMPTZ NOT NULL DEFAULT now(),

    UNIQUE (usuario_id, dedo)
);

-- Índice para busca rápida pelo ID do sensor (que o Arduino envia)
CREATE UNIQUE INDEX IF NOT EXISTS idx_digitais_sensor_id ON sistema_biometrico.digitais (sensor_id);
-- ============================================================

-- Histórico de acessos (Sem mudanças)
CREATE TABLE IF NOT EXISTS sistema_biometrico.historico_acessos (
    id SERIAL PRIMARY KEY,
    usuario_id INTEGER REFERENCES sistema_biometrico.usuarios(id) ON DELETE SET NULL ON UPDATE CASCADE,
    sala_id INTEGER REFERENCES sistema_biometrico.salas(id) ON DELETE SET NULL ON UPDATE CASCADE,
    data_hora TIMESTAMPTZ NOT NULL DEFAULT now(),
    tipo_acesso sistema_biometrico.tipo_acesso_enum NOT NULL,
    motivo TEXT,
    metadata JSONB
);
CREATE INDEX IF NOT EXISTS idx_historico_datahora ON sistema_biometrico.historico_acessos (data_hora);