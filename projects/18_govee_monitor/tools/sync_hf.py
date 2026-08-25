#!/usr/bin/env python3
"""Sync readings from Supabase to a Hugging Face dataset repo.

Supabase is a buffer with short retention; Hugging Face is the durable archive.
This script moves rows between them and is safe to run at any cadence, twice in
a row, or after being skipped for a month.

Idempotency comes from the data, not from bookkeeping. Every row carries a
deterministic UUIDv7 minted from (bucket ms, device_id, sensor MAC), so the
same reading always has the same id no matter how many times it is uploaded or
re-exported. The sync therefore:

  * always re-reads an overlapping window rather than trusting a watermark,
  * merges into the existing month file and drops duplicates by id,
  * rewrites only months whose contents actually changed.

Skipping runs is safe. The only true deadline is Supabase's retention: as long
as the gap is shorter than that, nothing is lost.

Layout in the dataset repo:

    data/readings/YYYY-MM.parquet   monthly fact partitions
    sensors.parquet                 the dimension, rewritten each run

The dimension is kept separate rather than denormalised into every row, for
the same reason the database keeps it separate: a label is mutable metadata and
must not be frozen into millions of immutable measurements.
"""
import argparse
import os
import tempfile
import sys
from collections import defaultdict
from datetime import datetime, timedelta, timezone

import pyarrow as pa
import pyarrow.parquet as pq
import requests
from huggingface_hub import HfApi
from huggingface_hub.utils import EntryNotFoundError, RepositoryNotFoundError

READINGS_DIR = "data/readings"
SENSORS_PATH = "sensors.parquet"
PAGE = 1000                     # PostgREST's default ceiling

SCHEMA = pa.schema([
    ("id", pa.string()),
    ("ts", pa.timestamp("us", tz="UTC")),
    ("device_id", pa.string()),
    ("mac", pa.string()),
    ("temp_c", pa.float32()),
    ("humid", pa.float32()),
    ("battery", pa.int16()),
    ("rssi", pa.int16()),
    ("n_samples", pa.int16()),
    ("inserted_at", pa.timestamp("us", tz="UTC")),
])


def load_env(path):
    if not os.path.exists(path):
        return
    for line in open(path):
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, v = line.split("=", 1)
        os.environ.setdefault(k.strip(), v.strip())


def need(name):
    v = os.environ.get(name)
    if not v:
        sys.exit(f"{name} is not set — add it to projects/18_govee_monitor/.env")
    return v


def fetch_rows(url, key, table, since_iso, order="ts"):
    """Page through PostgREST. Returns a list of dicts."""
    out, offset = [], 0
    headers = {"apikey": key, "Authorization": f"Bearer {key}"}
    while True:
        params = {"select": "*", "order": order, "limit": PAGE, "offset": offset}
        if since_iso:
            params["ts"] = f"gte.{since_iso}"
        r = requests.get(f"{url}/rest/v1/{table}", headers=headers,
                         params=params, timeout=60)
        r.raise_for_status()
        batch = r.json()
        out.extend(batch)
        if len(batch) < PAGE:
            return out
        offset += PAGE


def parse_ts(v):
    """PostgREST returns ISO8601 strings; pyarrow wants datetimes."""
    if v is None or isinstance(v, datetime):
        return v
    v = v.replace("Z", "+00:00")
    dt = datetime.fromisoformat(v)
    return dt if dt.tzinfo else dt.replace(tzinfo=timezone.utc)


def to_table(rows):
    cols = {f.name: [] for f in SCHEMA}
    for r in rows:
        for f in SCHEMA:
            v = r.get(f.name)
            cols[f.name].append(parse_ts(v) if pa.types.is_timestamp(f.type) else v)
    return pa.table(cols, schema=SCHEMA)


def month_of(ts_str):
    return ts_str[:7]           # ISO8601 'YYYY-MM-...'


def download_parquet(api, repo, path):
    try:
        local = api.hf_hub_download(repo_id=repo, filename=path,
                                    repo_type="dataset")
        return pq.read_table(local)
    except (EntryNotFoundError, RepositoryNotFoundError):
        return None
    except Exception as e:                       # noqa: BLE001 - network etc.
        if "404" in str(e) or "Entry Not Found" in str(e):
            return None
        raise


def merge_dedupe(existing, new):
    """Union by id, newest-wins on ties, sorted by ts.

    Returns (table, changed). `changed` is False when the merge added nothing,
    which is what keeps a re-run from producing a pointless commit.
    """
    table = pa.concat_tables([existing, new]) if existing is not None else new
    seen, keep = set(), []
    ids = table.column("id").to_pylist()
    for i, rid in enumerate(ids):
        if rid in seen:
            continue
        seen.add(rid)
        keep.append(i)
    deduped = table.take(sorted(keep, key=lambda i: (table.column("ts")[i].as_py(),)))
    changed = existing is None or deduped.num_rows != existing.num_rows
    return deduped, changed


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    load_env(os.path.join(here, "..", ".env"))

    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--days", type=int, default=7,
                    help="overlap window to re-read from Supabase (default 7)")
    ap.add_argument("--all", action="store_true",
                    help="export everything Supabase still holds")
    ap.add_argument("--dry-run", action="store_true",
                    help="report what would change; upload and prune nothing")
    ap.add_argument("--prune-days", type=int, default=0,
                    help="after a successful upload, delete Supabase rows older "
                         "than this many days (0 = never prune)")
    args = ap.parse_args()

    url = need("SUPABASE_URL")
    secret = need("SUPABASE_SECRET_KEY")
    # A dry run never touches Hugging Face, so it must not demand credentials
    # for it — that is exactly the state you are in while still wiring this up.
    repo = os.environ.get("HF_DATASET") if args.dry_run else need("HF_DATASET")
    # HF_TOKEN is optional: huggingface_hub falls back to the machine's cached
    # login (~/.cache/huggingface/token) when it is None, so a `hf auth login`
    # is enough and the token need not be copied into .env.
    token = os.environ.get("HF_TOKEN") or None

    since = None
    if not args.all:
        since = (datetime.now(timezone.utc)
                 - timedelta(days=args.days)).isoformat()

    print(f"pulling readings since {since or 'the beginning'} ...")
    rows = fetch_rows(url, secret, "reading", since)
    sensors = fetch_rows(url, secret, "sensor", None, order="mac")
    print(f"  {len(rows)} readings, {len(sensors)} sensors")

    if not rows and not sensors:
        print("nothing to sync")
        return

    api = HfApi(token=token)
    if not args.dry_run:
        who = api.whoami()
        print(f"hugging face: {who.get('name')} "
              f"({who.get('auth', {}).get('accessToken', {}).get('role', 'unknown')} token)")
        api.create_repo(repo_id=repo, repo_type="dataset",
                        private=True, exist_ok=True)

    # --- fact partitions -----------------------------------------------------
    by_month = defaultdict(list)
    for r in rows:
        by_month[month_of(r["ts"])].append(r)

    uploaded = skipped = 0
    for month, rs in sorted(by_month.items()):
        path = f"{READINGS_DIR}/{month}.parquet"
        new = to_table(rs)
        existing = None if args.dry_run else download_parquet(api, repo, path)
        merged, changed = merge_dedupe(existing, new)

        if not changed:
            print(f"  {month}: {merged.num_rows} rows, unchanged")
            skipped += 1
            continue

        added = merged.num_rows - (existing.num_rows if existing is not None else 0)
        print(f"  {month}: {merged.num_rows} rows (+{added})"
              f"{' [dry-run]' if args.dry_run else ''}")
        if args.dry_run:
            continue

        # A real file rather than BytesIO: Xet storage cannot dedupe an
        # in-memory buffer and falls back to plain HTTP with a warning.
        with tempfile.NamedTemporaryFile(suffix=".parquet") as tmp:
            pq.write_table(merged, tmp.name, compression="zstd")
            api.upload_file(path_or_fileobj=tmp.name, path_in_repo=path,
                            repo_id=repo, repo_type="dataset",
                            commit_message=f"readings {month}: {merged.num_rows} rows")
        uploaded += 1

    # --- dimension -----------------------------------------------------------
    if sensors and not args.dry_run:
        sschema = pa.schema([
            ("mac", pa.string()), ("label", pa.string()),
            ("device_id", pa.string()),
            ("first_seen", pa.timestamp("us", tz="UTC")),
            ("last_seen", pa.timestamp("us", tz="UTC")),
        ])
        stab = pa.table({f.name: [parse_ts(s.get(f.name))
                                  if pa.types.is_timestamp(f.type) else s.get(f.name)
                                  for s in sensors]
                         for f in sschema}, schema=sschema)
        with tempfile.NamedTemporaryFile(suffix=".parquet") as tmp:
            pq.write_table(stab, tmp.name, compression="zstd")
            api.upload_file(path_or_fileobj=tmp.name, path_in_repo=SENSORS_PATH,
                            repo_id=repo, repo_type="dataset",
                            commit_message=f"sensors: {len(sensors)} rows")
        print(f"  {SENSORS_PATH}: {len(sensors)} rows")

    print(f"{uploaded} partition(s) written, {skipped} unchanged")

    # --- prune, only after everything above succeeded ------------------------
    if args.prune_days and not args.dry_run:
        cutoff = (datetime.now(timezone.utc)
                  - timedelta(days=args.prune_days)).isoformat()
        r = requests.delete(f"{url}/rest/v1/reading",
                            headers={"apikey": secret,
                                     "Authorization": f"Bearer {secret}",
                                     "Prefer": "return=minimal"},
                            params={"ts": f"lt.{cutoff}"}, timeout=60)
        r.raise_for_status()
        print(f"pruned Supabase rows older than {cutoff}")


if __name__ == "__main__":
    main()
