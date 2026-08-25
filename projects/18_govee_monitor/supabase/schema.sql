-- Govee monitor — Supabase schema.
-- Idempotent; applied by ./supabase/apply_schema.sh. Contains no secrets.
--
-- Supabase is a BUFFER here, not the archive: rows are pruned after 90 days
-- and the durable copy lives elsewhere. Everything below is sized for that.
--
-- Modelled as fact + dimension, because the two kinds of data have genuinely
-- different lifetimes:
--
--   sensor   IDENTITY. A MAC is immutable fact. Its label ("Bed room") is
--            mutable human metadata that changes when a sensor moves house or
--            gets renamed. Stamping the label onto every reading would freeze
--            a naming decision into millions of rows and leave history
--            disagreeing with itself after a rename.
--
--   reading  MEASUREMENT. What was true at an instant, keyed by MAC. Never
--            rewritten, only inserted.

-- ---------------------------------------------------------------------------
-- Dimension: who the sensors are.
-- ---------------------------------------------------------------------------
create table if not exists sensor (
    mac         text primary key,           -- immutable identity, as on the sticker
    label       text,                       -- current human name; may change
    device_id   text,                       -- board that last reported it
    first_seen  timestamptz not null default now(),
    last_seen   timestamptz not null default now()
);

-- Optional rename history. Not written by the device — insert a row here if
-- you ever need "what was this called when that reading was taken".
create table if not exists sensor_label (
    mac        text        not null,
    label      text        not null,
    valid_from timestamptz not null default now(),
    primary key (mac, valid_from)
);

-- ---------------------------------------------------------------------------
-- Fact: one row per sensor per closed history bucket.
--
-- Deliberately NO foreign key to sensor(mac). A FK would impose an ordering
-- dependency between two independent uploads — if the sensor upsert failed or
-- arrived second, readings would be rejected. Losing measurements to protect
-- referential tidiness is the wrong trade for telemetry; the join is
-- best-effort and a missing dimension row costs a label, not data.
-- ---------------------------------------------------------------------------
create table if not exists reading (
    -- Deterministic UUIDv7 from (bucket ms, device_id, sensor MAC). NOT random:
    -- re-uploading a bucket mints the same id, which is what makes every retry
    -- and overlapping export idempotent. Do not switch this to a serial.
    id          uuid        primary key,
    ts          timestamptz not null,       -- bucket end, from the device clock
    device_id   text        not null,
    mac         text        not null,       -- joins to sensor, softly
    temp_c      real,
    humid       real,
    battery     smallint,                   -- %, broadcast by the H5075
    rssi        smallint,                   -- dBm, mean over the bucket
    n_samples   smallint,                   -- adverts averaged; 1 == thin bucket
    inserted_at timestamptz not null default now()   -- server clock, for lag
);

create index if not exists reading_ts_idx     on reading (ts desc);
create index if not exists reading_mac_ts_idx on reading (mac, ts desc);

-- ---------------------------------------------------------------------------
-- Row Level Security.
--
-- The publishable key is compiled into the firmware image and can be read back
-- out of flash, so it must do as little as possible.
--
--   reading  INSERT only. No select/update/delete policy exists, which denies
--            those to `anon`. Consequence for the device: it must send
--            `Prefer: return=minimal`, since returning the row needs select.
--   sensor   INSERT + UPDATE, so the board can upsert its own labels. Worst
--            case a leaked key renames rooms; it still cannot read or destroy
--            measurements.
-- ---------------------------------------------------------------------------
alter table reading enable row level security;
alter table sensor  enable row level security;

drop policy if exists reading_device_insert on reading;
create policy reading_device_insert on reading
    for insert to anon with check (true);

drop policy if exists sensor_device_insert on sensor;
create policy sensor_device_insert on sensor
    for insert to anon with check (true);

drop policy if exists sensor_device_update on sensor;
create policy sensor_device_update on sensor
    for update to anon using (true) with check (true);

grant usage on schema public to anon;
grant insert on table reading to anon;
grant insert, update, select on table sensor to anon;   -- upsert needs select
revoke select, update, delete on table reading from anon;

-- ---------------------------------------------------------------------------
-- Dashboard rollup: a view, not a second table the device writes. One write
-- path means one set of retries and one idempotency story. At ~700k rows/year
-- a plain view is fast; promote to materialized + pg_cron only if it drags.
-- ---------------------------------------------------------------------------
create or replace view reading_5m as
select to_timestamp(floor(extract(epoch from r.ts) / 300) * 300) as bucket,
       r.device_id,
       r.mac,
       s.label,                              -- joined, never stored on the fact
       avg(r.temp_c)::real   as temp_c,
       avg(r.humid)::real    as humid,
       min(r.battery)        as battery,
       avg(r.rssi)::smallint as rssi,
       sum(r.n_samples)      as n_samples,
       count(*)              as n_buckets
from reading r
left join sensor s on s.mac = r.mac
group by 1, 2, 3, 4;

-- Retention: run from the export job only after a successful archive push, so
-- rows are dropped only once they are durably stored somewhere else.
-- delete from reading where ts < now() - interval '90 days';
