#!/bin/sh
# Sync Supabase -> Hugging Face. Creates a project-local venv on first run so
# nothing is installed into the system Python.
#
#   ./tools/sync.sh                 # archive whatever is new since last time
#   ./tools/sync.sh --days 30       # ignore the watermark, re-read 30 days
#   ./tools/sync.sh --all           # backfill everything Supabase still holds
#   ./tools/sync.sh --dry-run       # show what would happen, upload nothing
#   ./tools/sync.sh --prune-days 90 # delete rows older than 90d AFTER upload
#   ./tools/sync.sh --self-test     # test the partition-rewrite guard
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
VENV="$DIR/.venv"

if [ ! -x "$VENV/bin/python" ]; then
    echo "creating venv at $VENV ..."
    python3 -m venv "$VENV"
    "$VENV/bin/pip" install -q --upgrade pip
    "$VENV/bin/pip" install -q -r "$DIR/requirements.txt"
fi

if [ "$1" = "--self-test" ]; then
    exec "$VENV/bin/python" "$DIR/test_sync.py"
fi

exec "$VENV/bin/python" "$DIR/sync_hf.py" "$@"
