#!/bin/sh
# Upsert the sensor dimension from main/device_config.h.
#
# The firmware deliberately has NO write access to `sensor` — granting it would
# have cost the INSERT-only property on `reading` (see schema.sql). Labels are
# a human concern that changes a handful of times ever, so they are published
# from here instead, with device_config.h as the single source of truth.
#
# Re-runnable: updates labels in place and leaves first_seen alone.
#
#   ./supabase/seed_sensors.sh
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
[ -f "$DIR/../.env" ] && { set -a; . "$DIR/../.env"; set +a; }

CFG="$DIR/../main/device_config.h"
[ -f "$CFG" ] || { echo "missing $CFG (copy device_config.h.template)" >&2; exit 1; }
[ -n "$DATABASE_URL" ] || { echo "DATABASE_URL not set — see supabase/README.md" >&2; exit 1; }

DEVICE_ID=$(sed -nE 's/^CONFIG_GOVEE_DEVICE_ID="(.*)"/\1/p' "$DIR/../sdkconfig.defaults" | tail -1)
[ -n "$DEVICE_ID" ] || DEVICE_ID="device 1"

SQL=$(python3 - "$CFG" "$DEVICE_ID" <<'PY'
import re, sys
cfg, device = sys.argv[1], sys.argv[2]
# Match the MAC braces exactly; [^}]* would swallow the opening inner brace,
# and commented-out template rows must not be picked up.
text = "\n".join(l for l in open(cfg)
                 if not l.lstrip().startswith(("//", "/*", "*")))
rows = re.findall(
    r'\{\s*(0x[0-9a-fA-F]{2}(?:\s*,\s*0x[0-9a-fA-F]{2}){5})\s*\}\s*,\s*"([^"]*)"',
    text)
if not rows:
    sys.exit("no sensors found in device_config.h")
def q(s): return "'" + s.replace("'", "''") + "'"
for octets, label in rows:
    mac = ":".join("%02X" % int(b.strip(), 16) for b in octets.split(","))
    print(f"insert into sensor (mac, label, device_id, last_seen) "
          f"values ({q(mac)}, {q(label)}, {q(device)}, now()) "
          f"on conflict (mac) do update set label = excluded.label, "
          f"device_id = excluded.device_id, last_seen = now();")
print("select mac, label, device_id from sensor order by mac;")
PY
)

PSQL=$(command -v psql || echo /Applications/Postgres.app/Contents/Versions/18/bin/psql)
printf '%s\n' "$SQL" | "$PSQL" "$DATABASE_URL" -v ON_ERROR_STOP=1
