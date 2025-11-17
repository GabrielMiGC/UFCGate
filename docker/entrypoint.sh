#!/usr/bin/env sh
set -eu

# Defaults (can be overridden by .env or compose)
: "${DB_HOST:=db}"
: "${DB_PORT:=5432}"
: "${POSTGRES_USER:=postgres}"
: "${POSTGRES_DB:=postgres}"

SCHEMA_SQL_PATH="/app/SQL/estrutura_banco_biometrico.sql"

echo "[entrypoint] Waiting for Postgres ${DB_HOST}:${DB_PORT}..."
# Wait for DB to accept connections
for i in $(seq 1 30); do
  if pg_isready -h "$DB_HOST" -p "$DB_PORT" -U "$POSTGRES_USER" >/dev/null 2>&1; then
    echo "[entrypoint] Postgres is ready."
    break
  fi
  sleep 1
done

# Apply schema if file exists (idempotent script, safe to re-run)
if [ -f "$SCHEMA_SQL_PATH" ]; then
  echo "[entrypoint] Applying schema from $SCHEMA_SQL_PATH ..."
  # Use PGPASSWORD for non-interactive auth if provided
  if [ -n "${POSTGRES_PASSWORD:-}" ]; then
    export PGPASSWORD="$POSTGRES_PASSWORD"
  fi
  psql -h "$DB_HOST" -p "$DB_PORT" -U "$POSTGRES_USER" -d "$POSTGRES_DB" -v ON_ERROR_STOP=1 -f "$SCHEMA_SQL_PATH" || {
    echo "[entrypoint] WARN: psql returned non-zero exit code. Continuing because script is idempotent." >&2
  }
else
  echo "[entrypoint] Schema file not found at $SCHEMA_SQL_PATH. Skipping."
fi

echo "[entrypoint] Starting app: $*"
exec "$@"
