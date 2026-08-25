#!/bin/sh
# Apply schema.sql to the Supabase project. Idempotent — safe to re-run.
#
# PostgREST (the sb_secret_/sb_publishable_ keys) cannot run DDL; it only
# exposes tables, views and functions. Schema changes need a real Postgres
# connection, which is a separate credential: Supabase dashboard ->
# Project Settings -> Database -> Connection string (URI).
#
#   DATABASE_URL=postgresql://postgres.<ref>:<pw>@<host>:5432/postgres
#
# Put it in .env (gitignored) next to the API keys, then:
#   ./supabase/apply_schema.sh
set -e
DIR=$(cd "$(dirname "$0")" && pwd)

if [ -f "$DIR/../.env" ]; then
    set -a; . "$DIR/../.env"; set +a
fi

if [ -z "$DATABASE_URL" ]; then
    echo "DATABASE_URL is not set — add it to projects/18_govee_monitor/.env" >&2
    echo "(Project Settings -> Database -> Connection string -> URI)" >&2
    exit 1
fi

PSQL=$(command -v psql || echo /Applications/Postgres.app/Contents/Versions/18/bin/psql)
if [ ! -x "$PSQL" ]; then echo "psql not found" >&2; exit 1; fi

echo "applying $DIR/schema.sql ..."
"$PSQL" "$DATABASE_URL" -v ON_ERROR_STOP=1 -f "$DIR/schema.sql"
echo "schema applied"
