-- Govee monitor — Supabase schema.
-- Run once in the Supabase dashboard SQL editor. Contains no secrets.
--
-- Supabase is a BUFFER here, not the archive: rows are pruned after 90 days
-- and the durable copy lives elsewhere (Hugging Face, later). Everything below
-- is sized for that role.

-- ---------------------------------------------------------------------------
-- Detailed datapoints, one row per sensor per closed history bucket.
-- ---------------------------------------------------------------------------
create table if not exists reading (
    -- Deterministic UUIDv7 from (bucket ms, device_id, sensor MAC). NOT random:
    -- re-uploading a bucket mints the same id, which is what makes every retry
    -- and every overlapping export idempotent. Do not switch this to a serial.
    id          uuid        primary key,
    ts          timestamptz not null,        -- bucket end, from the device clock
    device_id   text        not null,        -- which board reported it
    mac         text        not null,        -- sensor identity; survives reordering
    label       text,                        -- friendly name at time of upload
    temp_c      real,
    humid       real,
    battery     smallint,
    rssi        smallint,
    n_samples   smallint,                    -- adverts averaged into this bucket
    inserted_at timestamptz not null default now()   -- server clock, for lag checks
);

create index if not exists reading_ts_idx      on reading (ts desc);
create index if not exists reading_mac_ts_idx  on reading (mac, ts desc);

-- ---------------------------------------------------------------------------
-- Row Level Security.
--
-- The publishable key is compiled into the firmware image and can be read back
-- out of flash, so it must be able to do exactly one thing: insert. There is
-- deliberately NO select/update/delete policy, which denies those to `anon`.
-- Reads and prunes use the secret key from host-side tooling instead.
--
-- Consequence for the device: it must send `Prefer: return=minimal`, because
-- returning the inserted row would require select.
-- ---------------------------------------------------------------------------
alter table reading enable row level security;

drop policy if exists reading_device_insert on reading;
create policy reading_device_insert on reading
    for insert to anon
    with check (true);

grant usage on schema public to anon;
grant insert on table reading to anon;
revoke select, update, delete on table reading from anon;

-- ---------------------------------------------------------------------------
-- Dashboard rollup.
--
-- A view, not a second table the device writes: one write path means one set
-- of retries and one idempotency story. At this data volume (~700k rows/year)
-- a plain view is fast. Promote it to a materialized view + pg_cron only if a
-- dashboard actually drags.
-- ---------------------------------------------------------------------------
create or replace view reading_5m as
select to_timestamp(floor(extract(epoch from ts) / 300) * 300) as bucket,
       device_id,
       mac,
       max(label)          as label,
       avg(temp_c)::real   as temp_c,
       avg(humid)::real    as humid,
       min(battery)        as battery,
       avg(rssi)::smallint as rssi,
       count(*)            as n
from reading
group by 1, 2, 3;

-- ---------------------------------------------------------------------------
-- Retention. Run from the export job after a successful archive push, so rows
-- are only dropped once they are durably stored somewhere else.
-- ---------------------------------------------------------------------------
-- delete from reading where ts < now() - interval '90 days';
