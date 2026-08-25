#!/bin/sh
# Cross-check live row ids against the firmware's id derivation.
#
# The entire retry/idempotency story rests on one claim: a given reading always
# gets the same id. This proves it against real data by re-deriving every id on
# the host, from the same uuid7.c the firmware runs, and comparing.
#
# A mismatch means replays would create duplicate rows instead of being
# rejected — so this is worth running after any change to uuid7.c, the device
# id, or the upload payload.
#
#   ./supabase/check_ids.sh [limit]
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
[ -f "$DIR/../.env" ] && { set -a; . "$DIR/../.env"; set +a; }
LIMIT=${1:-50}

DEVICE_ID=$(sed -nE 's/^CONFIG_GOVEE_DEVICE_ID="(.*)"/\1/p' "$DIR/../sdkconfig.defaults" | tail -1)
[ -n "$DEVICE_ID" ] || DEVICE_ID="device 1"

OUT=$(mktemp -d)
cc -std=c11 -Wall -Wextra -O1 -I"$DIR/../test/stub" -I"$DIR/../main" \
   "$DIR/../test/uuid7_cli.c" "$DIR/../main/uuid7.c" -o "$OUT/uuid7_cli"

ROWS=$(curl -s "$SUPABASE_URL/rest/v1/reading?select=id,ts,mac&order=ts.desc&limit=$LIMIT" \
        -H "apikey: $SUPABASE_SECRET_KEY" -H "Authorization: Bearer $SUPABASE_SECRET_KEY")

echo "$ROWS" | python3 "$DIR/check_ids.py" "$OUT/uuid7_cli" "$DEVICE_ID"
