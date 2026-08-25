#!/usr/bin/env python3
"""Sync readings from Supabase to a Hugging Face dataset repo.

Supabase is a buffer with short retention; Hugging Face is the durable archive.
This script moves rows between them and is safe to run at any cadence, twice in
a row, or after being skipped for a month.

The archive is APPEND-ONLY. A published file is never rewritten, re-merged or
deleted, because parquet cannot be appended in place — "adding" rows to a month
would mean downloading it, merging, and uploading a replacement, and a bug or a
truncated download anywhere in that path silently replaces an archived month
with a subset of itself. No amount of guarding makes that a good shape for data
you cannot re-derive. Each run instead writes NEW immutable part files:

    data/readings/month=YYYY-MM/part-<from>-<to>-<hash>.parquet
    sensors.parquet                 dimension snapshot; current state, so
                                    overwriting it is the intent

Idempotency comes from the data rather than from bookkeeping. Every row carries
a deterministic UUIDv7 minted from (bucket ms, device_id, sensor MAC), and each
part is named after a hash of the ids it contains — so re-running a sync
produces a byte-identical file with an identical name, which the repo already
has and therefore skips. Nothing is uploaded twice and nothing is overwritten.

Because runs overlap deliberately, the same reading may appear in more than one
part. That is correct for an append-only log: readers deduplicate by id, which
is exact precisely because ids are deterministic. See the README for the
one-liner.

Skipping runs is safe. The only real deadline is Supabase retention: as long as
the gap is shorter than that, nothing is lost.

The dimension is kept separate rather than denormalised into every row, for the
same reason the database keeps it separate: a label is mutable metadata and
must not be frozen into millions of immutable measurements.
"""
import argparse
import hashlib
import os
import tempfile
import sys
from collections import defaultdict
from datetime import datetime, timedelta, timezone

import pyarrow as pa
import pyarrow.parquet as pq
import requests
from huggingface_hub import HfApi

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


def part_name(table):
    """Content-addressed file name for a batch of rows.

    Deterministic in the rows it contains, so re-exporting the same window
    yields the same name and the repo simply already has it. This is what makes
    an append-only archive idempotent without any read-modify-write.
    """
    ids = sorted(table.column("id").to_pylist())
    digest = hashlib.sha256("\n".join(ids).encode()).hexdigest()[:12]
    ts = table.column("ts").to_pylist()
    span = f"{min(ts):%Y%m%dT%H%M%S}-{max(ts):%Y%m%dT%H%M%S}"
    return f"part-{span}-{digest}.parquet"


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

    # --- fact parts: write new files, never touch existing ones ------------
    by_month = defaultdict(list)
    for r in rows:
        by_month[month_of(r["ts"])].append(r)

    existing_files = set()
    if not args.dry_run:
        existing_files = set(api.list_repo_files(repo_id=repo,
                                                 repo_type="dataset"))

    written = skipped = 0
    for month, rs in sorted(by_month.items()):
        table = to_table(rs)
        path = f"{READINGS_DIR}/month={month}/{part_name(table)}"

        if path in existing_files:
            print(f"  {month}: {table.num_rows} rows already archived "
                  f"({os.path.basename(path)})")
            skipped += 1
            continue

        print(f"  {month}: {table.num_rows} rows -> {os.path.basename(path)}"
              f"{' [dry-run]' if args.dry_run else ''}")
        if args.dry_run:
            continue

        # A real file rather than BytesIO: Xet storage cannot dedupe an
        # in-memory buffer and falls back to plain HTTP with a warning.
        with tempfile.NamedTemporaryFile(suffix=".parquet") as tmp:
            pq.write_table(table, tmp.name, compression="zstd")
            api.upload_file(path_or_fileobj=tmp.name, path_in_repo=path,
                            repo_id=repo, repo_type="dataset",
                            commit_message=f"readings {month}: +{table.num_rows} rows")
        written += 1

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

    print(f"{written} part(s) appended, {skipped} already archived")

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
