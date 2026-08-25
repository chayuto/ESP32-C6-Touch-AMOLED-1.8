"""Tests for the append-only archive layout.

The archive never rewrites a published file, so the property that has to hold
is narrower than a merge would need: a given set of rows must always produce
the same part name, and different rows must not collide. That is what lets a
re-run be a no-op without any read-modify-write.

    ./tools/sync.sh --self-test
"""
import sys
from datetime import datetime, timedelta, timezone

import pyarrow as pa

from sync_hf import SCHEMA, part_name

T0 = datetime(2026, 8, 25, 22, 0, tzinfo=timezone.utc)
fails = 0


def check(cond, msg):
    global fails
    if cond:
        print(f"ok    {msg}")
    else:
        print(f"FAIL  {msg}")
        fails += 1


def rows(ids, temp=20.0, start=T0):
    cols = {f.name: [] for f in SCHEMA}
    for n, i in enumerate(ids):
        cols["id"].append(i)
        cols["ts"].append(start + timedelta(minutes=3 * n))
        cols["device_id"].append("device 1")
        cols["mac"].append("A4:C1:38:DE:38:9B")
        cols["temp_c"].append(temp)
        cols["humid"].append(70.0)
        cols["battery"].append(80)
        cols["rssi"].append(-70)
        cols["n_samples"].append(60)
        cols["inserted_at"].append(T0)
    return pa.table(cols, schema=SCHEMA)


def test_deterministic():
    """A re-run must produce the same name, or it would upload a duplicate."""
    check(part_name(rows(["a", "b", "c"])) == part_name(rows(["a", "b", "c"])),
          "same rows produce the same part name")


def test_order_independent():
    """Row order out of PostgREST must not change the file name."""
    check(part_name(rows(["a", "b", "c"])) == part_name(rows(["c", "b", "a"])),
          "row order does not change the part name")


def test_different_rows_differ():
    a = part_name(rows(["a", "b"]))
    b = part_name(rows(["a", "b", "c"]))
    c = part_name(rows(["a", "z"]))
    check(a != b, "an extra row changes the name")
    check(a != c, "a different id changes the name")


def test_name_is_readable_and_sortable():
    n = part_name(rows(["a", "b"]))
    check(n.startswith("part-") and n.endswith(".parquet"), f"shape: {n}")
    check(n.count("-") >= 3, f"carries a time span: {n}")
    later = part_name(rows(["c"], start=T0 + timedelta(days=1)))
    check(n < later, "names sort chronologically")


def test_values_do_not_affect_name():
    """Names address the id set, so a corrected value would need a new id.

    This is deliberate: ids are deterministic, so the same reading re-exported
    with a different value is a bug upstream, not a new part.
    """
    check(part_name(rows(["a"], temp=19.0)) == part_name(rows(["a"], temp=99.0)),
          "part name addresses ids, not values")


if __name__ == "__main__":
    test_deterministic()
    test_order_independent()
    test_different_rows_differ()
    test_name_is_readable_and_sortable()
    test_values_do_not_affect_name()
    print("all sync tests passed" if not fails else f"{fails} check(s) failed")
    sys.exit(1 if fails else 0)
