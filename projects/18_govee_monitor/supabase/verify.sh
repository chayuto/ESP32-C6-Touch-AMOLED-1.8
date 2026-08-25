#!/bin/sh
# Verify the deployed schema behaves the way the design depends on.
#
# This checks the SECURITY posture, not just that tables exist. The publishable
# key is compiled into the firmware image and can be read back out of flash
# with esptool, so the checks that matter are the negative ones: what it
# CANNOT do. A passing run means a stolen firmware image cannot read or destroy
# anything.
#
#   ./supabase/verify.sh
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
[ -f "$DIR/../.env" ] && { set -a; . "$DIR/../.env"; set +a; }

fail=0
check() {
    if [ "$2" = "$3" ]; then printf 'ok    %-44s %s\n' "$1" "$3"
    else printf 'FAIL  %-44s got %s, want %s\n' "$1" "$3" "$2"; fail=$((fail+1)); fi
}
req() {
    _m=$1; _u=$2; _k=$3; shift 3
    curl -s -o /tmp/gv_verify.out -w '%{http_code}' -X "$_m" "$SUPABASE_URL$_u" \
        -H "apikey: $_k" -H "Authorization: Bearer $_k" "$@"
}
PUB="$SUPABASE_PUBLISHABLE_KEY"
SEC="$SUPABASE_SECRET_KEY"
JSON='-H Content-Type:application/json'

echo "== schema exists (secret key) =="
check "secret can select reading"       200 "$(req GET "/rest/v1/reading?select=id&limit=1" "$SEC")"
check "sensor dimension populated"      200 "$(req GET "/rest/v1/sensor?select=mac&limit=1" "$SEC")"
check "reading_5m view exists"          200 "$(req GET "/rest/v1/reading_5m?select=bucket&limit=1" "$SEC")"

echo "== firmware key cannot read or destroy anything =="
check "cannot select reading"           401 "$(req GET "/rest/v1/reading?select=id&limit=1" "$PUB")"
check "cannot delete reading"           401 "$(req DELETE "/rest/v1/reading?device_id=eq.verify" "$PUB")"
check "cannot read sensor labels"       401 "$(req GET "/rest/v1/sensor?select=mac&limit=1" "$PUB")"
check "cannot write sensor labels"      401 "$(req POST "/rest/v1/sensor" "$PUB" $JSON \
        -H 'Prefer: return=minimal' -d '[{"mac":"00:00:00:00:00:00","label":"pwned"}]')"

echo "== firmware key can insert readings, and replays are safe =="
ROW='[{"id":"00000000-0000-7000-8000-0000000000ff","ts":"2026-01-01T00:00:00Z","device_id":"verify","mac":"00:00:00:00:00:00","temp_c":1,"humid":2,"battery":50,"rssi":-70,"n_samples":3}]'
check "plain insert accepted"           201 "$(req POST "/rest/v1/reading" "$PUB" $JSON \
        -H 'Prefer: return=minimal' -d "$ROW")"
# Deterministic ids mean a duplicate is provably the same row, so the device
# treats 409/23505 as success. Upsert is NOT used: it would need SELECT.
check "replay rejected as duplicate"    409 "$(req POST "/rest/v1/reading" "$PUB" $JSON \
        -H 'Prefer: return=minimal' -d "$ROW")"
grep -q 23505 /tmp/gv_verify.out \
    && echo "ok    replay is a unique violation (23505), safe to ignore on device" \
    || { echo "FAIL  replay error was not 23505: $(cat /tmp/gv_verify.out)"; fail=$((fail+1)); }

echo "== cleanup (secret key) =="
check "verify row removed"              204 "$(req DELETE "/rest/v1/reading?device_id=eq.verify" "$SEC")"

[ "$fail" -eq 0 ] && echo "all schema checks passed" || { echo "$fail check(s) failed"; exit 1; }
