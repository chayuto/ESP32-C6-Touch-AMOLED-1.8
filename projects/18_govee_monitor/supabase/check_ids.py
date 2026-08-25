"""Compare stored row ids against ids re-derived from their own inputs.

Reads rows as JSON on stdin. Called by check_ids.sh, which builds the CLI from
the same uuid7.c the firmware runs.
"""
import json
import subprocess
import sys
from datetime import datetime

cli, device = sys.argv[1], sys.argv[2]
rows = json.load(sys.stdin)
if not rows:
    print("no rows to check")
    sys.exit(0)

bad = 0
for r in rows:
    ts = r["ts"].replace("Z", "+00:00")
    ms = int(datetime.fromisoformat(ts).timestamp() * 1000 + 0.5)
    got = subprocess.run([cli, str(ms), device, r["mac"]],
                         capture_output=True, text=True).stdout.strip()
    if got != r["id"]:
        print(f'MISMATCH {r["mac"]} {r["ts"]}')
        print(f'  stored  {r["id"]}')
        print(f'  derived {got}')
        bad += 1

print(f"{len(rows) - bad}/{len(rows)} ids reproduce exactly")
sys.exit(1 if bad else 0)
