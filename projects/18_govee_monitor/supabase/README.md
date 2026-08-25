# Supabase backend

Cloud buffer for the monitor's readings. **Not the archive** — rows are pruned
after 90 days and the durable copy is pushed elsewhere. Sized for that role.

## Which credential does what

Four different secrets are involved and they are not interchangeable. Getting
this wrong is the most likely way to leak something that matters.

| Credential | Form | Lives in | Can do |
|---|---|---|---|
| Publishable key | `sb_publishable_…` | `sdkconfig.defaults` → **firmware** | INSERT readings, upsert sensors. Nothing else. |
| Secret key | `sb_secret_…` | `.env` (host only) | Full REST access — read, delete, prune. **Never** in firmware. |
| DB password | plain string | `.env` as `DATABASE_URL` | DDL via psql. Schema changes only. |
| Dashboard JWT | `eyJ…` | SPA build config | SELECT on `reading_5m` only. Public once shipped. |
| JWT signing secret | plain string | `.env` only | Signs credentials for **every** role incl. service_role. Never ships. |
| Personal access token | `sbp_…` | not used here | Account-wide. Avoided on purpose — see below. |

The publishable key **ships inside the firmware image and is recoverable with
`esptool read_flash`**. That is why `schema.sql` gives it an INSERT-only policy
on `reading`: a leaked key can add junk rows, but cannot read or destroy
measurements. `verify.sh` asserts exactly this, and its negative checks (key
*cannot* select, *cannot* delete) are the ones that matter.

A personal access token would also work for DDL via the Management API, but it
is **account-wide** — it can manage and delete every project on the account.
The DB password is scoped to one project, so it is the smaller blast radius for
the same job.

## Connecting with psql — read this before debugging

The **direct** connection string the dashboard shows first does not work from
every machine:

```
postgresql://postgres:PW@db.<ref>.supabase.co:5432/postgres     # often unusable
```

That host publishes **only an AAAA (IPv6) record**. On this machine `dig` found
it, but the system resolver returned nothing and the IPv6 literal was
unreachable even with working IPv6 egress elsewhere:

```
psql: could not translate host name "db.<ref>.supabase.co" to address
```

Use the **pooler** instead, which is dual-stack and reachable:

```
postgresql://postgres.<ref>:PW@aws-0-<region>.pooler.supabase.com:5432/postgres
```

Two things differ and both are mandatory:

- the username becomes `postgres.<ref>`, not `postgres` — this is how the
  pooler routes to the right tenant;
- the host carries the **region**, which the dashboard shows but the project
  ref does not encode. Guessing it is not practical: every
  `aws-N-<region>.pooler.supabase.com` name resolves, so a wrong region fails
  the same way a wrong password does. Copy it from the dashboard.

Port 5432 is session mode (use this for DDL); 6543 is the transaction pooler
and can be awkward with DDL.

### This project

- ref `cxjzerofzbjrwokbsxjc`, region `ap-northeast-1`
- pooler host `aws-0-ap-northeast-1.pooler.supabase.com`

### Reading the errors

| Error | Means |
|---|---|
| `could not translate host name` | IPv6-only direct host — switch to the pooler |
| `Connection refused` / timeout | wrong region, or 5432 blocked by the network |
| `password authentication failed for user "postgres"` | TCP and tenant routing are fine; the **password** is wrong. Note it reports the base user even when connecting as `postgres.<ref>` |
| `Tenant or user not found` | wrong ref in the username, or wrong region |

Supabase does not display the DB password after project creation. If you no
longer have it: **Project Settings → Database → Reset database password**.

## Why the dashboard does not reuse the publishable key

Every Supabase publishable key maps to the same `anon` role, so a dashboard
sharing that key would share a credential with the firmware. The decisive
problem is not privilege but **rotation**: cycling a leaked dashboard key would
mean reflashing the board. So the SPA gets its own role.

`dashboard_reader` can select `reading_5m` and nothing else. Because a Postgres
view executes with its owner's privileges, granting the view alone is enough —
the role has no privileges on `reading` or `sensor` and cannot read them.
PostgREST connects as `authenticator` and SET ROLEs to the JWT's `role` claim,
which is why `authenticator` must be a member of the role.

Mint the token with `mint_dashboard_jwt.py`; it needs the project's JWT secret,
which signs credentials for every role including `service_role` and therefore
never leaves `.env`. Only the minted token ships.

**Watch for Supabase's default privileges.** The project grants `anon` on every
new object in `public`, so `reading_5m` was readable by the firmware key the
moment it was created — silently undoing the separation. `schema.sql` revokes
it explicitly after the view is created, and `verify.sh` has a regression check
for it. Any new table or view needs the same treatment: absence of a grant is
not the same as a revoke here.

## Gaps you may see in the data

Two causes, both expected:

- **A sensor missed its bucket.** Weak sensors drop advertisements; a bucket
  with none is simply absent rather than zero.
- **The board was offline for more than 6 hours, or rebooted.** History is
  RAM-only and holds 120 x 3-minute buckets, so unsent readings older than that
  roll off. This limit is **accepted by design** — the board is effectively
  always in WiFi range, and NVS persistence was judged not worth its flash wear
  and restore complexity for a few hours of a multi-month record.

Neither is recoverable after the fact, so do not treat a gap as a bug to chase.

## Scripts

```zsh
./supabase/apply_schema.sh   # idempotent DDL — needs DATABASE_URL
./supabase/verify.sh         # asserts the security posture — needs both API keys
```

`apply_schema.sh` is safe to re-run: `create table if not exists`,
`drop policy if exists`, `create or replace view`.

`verify.sh` proves the idempotency contract end to end by inserting the same
row twice and asserting both calls succeed, which is what lets the device retry
uploads blindly.

## Schema shape

`sensor` (dimension) + `reading` (fact), with `reading_5m` as a view over both.
A MAC is immutable fact; a label like "Bed room" is mutable metadata, so labels
live on the dimension and are joined in — otherwise a rename would leave
history disagreeing with itself. There is deliberately **no foreign key** from
`reading.mac` to `sensor.mac`: it would make measurements depend on the sensor
upsert having landed first, and losing telemetry to protect referential
tidiness is the wrong trade. See the comments in `schema.sql`.
