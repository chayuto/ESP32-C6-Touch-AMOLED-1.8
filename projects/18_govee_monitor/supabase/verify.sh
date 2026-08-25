#!/bin/sh
# Verify the deployed schema actually behaves the way the design depends on.
#
# This checks the SECURITY posture, not just that the table exists — the
# publishable key ships inside the firmware image and is recoverable from
# flash, so "can it only insert?" is the question that matters.
#
#   ./supabase/verify.sh
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
if [ -f "$DIR/../.env" ]; then set -a; . "$DIR/../.env"; set +a; fi

fail=0
check() { # name expected actual
    if [ "$2" = "$3" ]; then printf 'ok    %-42s %s\n' "$1" "$3"
    else printf 'FAIL  %-42s got %s, want %s\n' "$1" "$3" "$2"; fail=$((fail+1)); fi
}

req() { # method url key extra... -> http code
    _m=$1; _u=$2; _k=$3; shift 3
    curl -s -o /tmp/gv_verify.out -w '%{http_code}' -X "$_m" "$SUPABASE_URL$_u" \
        -H "apikey: $_k" -H "Authorization: Bearer $_k" "$@"
}

echo "== table and view exist (secret key) =="
check "secret can select reading"  200 "$(req GET /rest/v1/reading?select=id\&limit=1 "$SUPABASE_SECRET_KEY")"
check "reading_5m view exists"     200 "$(req GET /rest/v1/reading_5m?select=bucket\&limit=1 "$SUPABASE_SECRET_KEY")"

echo "== device key is INSERT-only (this is the one that matters) =="
check "publishable CANNOT select"  401 "$(req GET /rest/v1/reading?select=id\&limit=1 "$SUPABASE_PUBLISHABLE_KEY")"
check "publishable CANNOT delete"  401 "$(req DELETE "/rest/v1/reading?id=eq.00000000-0000-7000-8000-000000000000" "$SUPABASE_PUBLISHABLE_KEY")"

echo "== sensor dimension is upsertable by the device =="
SROW='[{"mac":"00:00:00:00:00:00","label":"verify","device_id":"verify"}]'
check "publishable CAN upsert sensor" 201 "$(req POST "/rest/v1/sensor?on_conflict=mac" "$SUPABASE_PUBLISHABLE_KEY" \
        -H 'Content-Type: application/json' -H 'Prefer: return=minimal,resolution=merge-duplicates' -d "$SROW")"
check "re-upsert sensor is fine"      201 "$(req POST "/rest/v1/sensor?on_conflict=mac" "$SUPABASE_PUBLISHABLE_KEY" \
        -H 'Content-Type: application/json' -H 'Prefer: return=minimal,resolution=merge-duplicates' -d "$SROW")"

echo "== device key can insert, and re-inserting is a no-op =="
ROW='[{"id":"00000000-0000-7000-8000-0000000000ff","ts":"2026-01-01T00:00:00Z","device_id":"verify","mac":"00:00:00:00:00:00","temp_c":1,"humid":2}]'
check "publishable CAN insert"     201 "$(req POST "/rest/v1/reading?on_conflict=id" "$SUPABASE_PUBLISHABLE_KEY" \
        -H 'Content-Type: application/json' -H 'Prefer: return=minimal,resolution=ignore-duplicates' -d "$ROW")"
check "duplicate insert ignored"   201 "$(req POST "/rest/v1/reading?on_conflict=id" "$SUPABASE_PUBLISHABLE_KEY" \
        -H 'Content-Type: application/json' -H 'Prefer: return=minimal,resolution=ignore-duplicates' -d "$ROW")"

echo "== cleanup (secret key) =="
check "verify reading removed"     204 "$(req DELETE "/rest/v1/reading?device_id=eq.verify" "$SUPABASE_SECRET_KEY")"
check "verify sensor removed"      204 "$(req DELETE "/rest/v1/sensor?device_id=eq.verify" "$SUPABASE_SECRET_KEY")"

[ "$fail" -eq 0 ] && echo "all schema checks passed" || { echo "$fail check(s) failed"; exit 1; }
