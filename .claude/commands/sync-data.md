# Sync sensor readings from Supabase to Hugging Face

Move Govee monitor readings out of the Supabase buffer into the durable
Hugging Face parquet archive. Usage: `/sync-data [--days N | --all] [--dry-run] [--prune-days N]`

Supabase holds a rolling window; Hugging Face is the archive. This is the job
that bridges them, and it is safe to run at any cadence — on demand, from cron,
or from a scheduled agent.

## Why it is safe to re-run

Every reading carries a deterministic UUIDv7 minted on the device from
(bucket ms, device_id, sensor MAC), so a given measurement always has the same
id. The sync re-reads an overlapping window rather than trusting a watermark,
merges into the existing month partition, and drops duplicates by id. Running
it twice changes nothing; skipping it for a week costs nothing. The only real
deadline is Supabase retention — as long as the gap is shorter than that,
nothing is lost.

## Steps

1. Run `./projects/18_govee_monitor/tools/sync.sh` with whatever flags the user
   asked for. Defaults to a 7-day overlap window.
   - First run creates a project-local venv under `tools/.venv` and installs
     pyarrow + huggingface_hub. Nothing is installed system-wide.
2. Report what changed: rows pulled, which month partitions were written, and
   which were unchanged. "unchanged" is the expected result for a re-run and
   is not a failure.
3. If it reports a missing variable, the fix is `projects/18_govee_monitor/.env`
   (gitignored). Required: `SUPABASE_URL`, `SUPABASE_SECRET_KEY`, `HF_DATASET`,
   `HF_TOKEN`. `--dry-run` needs only the Supabase pair.
4. Never put credentials anywhere but `.env`. The publishable key in
   `sdkconfig.defaults` is a different key and must not be used here — it has
   no read access by design.

## Flags

| Flag | Effect |
|---|---|
| *(none)* | pull from the archive's own watermark, rewound 24 h |
| `--days N` | ignore the watermark, re-read a fixed N-day window |
| `--lookback-hours N` | how far to rewind behind the watermark (default 24) |
| `--all` | export everything Supabase still holds (backfill) |
| `--dry-run` | report what would change; upload and prune nothing |
| `--prune-days N` | after a **successful** upload, delete Supabase rows older than N days |
| `--self-test` | test the part-naming invariants, no network |

## Cautions

- **Do not prune.** Retention is deliberately not enforced on this project —
  the user wants the full history kept in Supabase. `--prune-days` defaults to
  0; never pass it, never schedule it, and do not suggest a retention policy.
  (If that decision is ever reversed: it deletes from Supabase, runs only after
  uploads succeed, and must never use a window shorter than the sync cadence.)
- Do not schedule this through an agent when cron would do. The transformation
  is fixed and mechanical; an agent adds cost and a failure mode without adding
  judgement. Prefer a cron entry or GitHub Action calling `tools/sync.sh`.

## Layout in the dataset repo

```
data/readings/month=YYYY-MM/part-<from>-<to>-<hash>.parquet
sensors.parquet                 dimension snapshot, overwritten each run
```

**The archive is append-only — never rewrite or delete a published part.** Each
run writes new immutable files; part names hash the ids they contain, so a
re-run produces a name the repo already has and is skipped. Do not "tidy up" by
merging parts into one file per month: parquet cannot be appended in place, so
merging means download → rewrite → upload, and a bug there replaces archived
data with a subset of itself.

Overlapping runs mean a reading can appear in several parts. Readers dedupe by
id; that is exact because ids are deterministic.

Labels live only in `sensors.parquet`, never denormalised into readings — a
label is mutable metadata and must not be frozen into immutable measurements.
