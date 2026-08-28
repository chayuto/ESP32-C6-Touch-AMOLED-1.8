# Robustness in firmware that talks to a REST backend

Notes from building `18_govee_monitor`: a battery-powered ESP32-C6 that listens
to BLE sensors, buffers readings in RAM, and POSTs them to Supabase, which is
periodically drained into an append-only parquet archive.

Every rule below is here because something in *this* repo broke, or would have.
Generic advice is cheap; the value is in the specific failure that motivates it.
Where a rule cost real data, it says so.

---

## 1. The asymmetry that decides everything: which mistakes are reversible?

Before choosing any pattern, sort the failure modes by whether you can undo them.

| Failure | Cost | Recoverable? |
|---|---|---|
| Row uploaded twice | none — reader dedupes | yes, by construction |
| Row uploaded late | a gap that fills in | yes |
| Row never uploaded | permanent hole | **no** — RAM buffer rolls over |
| Wrong row uploaded | permanent bad data | **no** — archive is append-only |
| Device reboots | ~1 min of samples | mostly |

This table is the whole design. Duplicates are free, so make everything
idempotent and retry fearlessly. Losing or corrupting a row is unrecoverable, so
spend the complexity budget there and nowhere else.

The temptation is to optimise the common path — fewer requests, less RAM, tidier
code. But the common path already works. Every bug found in review was on a path
that runs during an *outage*, which is exactly when the data you cannot re-derive
is at stake.

**A concrete consequence.** Because the Hugging Face archive is append-only and
row ids are deterministic, a bad row cannot even be replaced by a corrected one
later — the correction would mint the same id and be rejected as a duplicate.
That single fact justifies validating values at the last gate before publishing,
even though every upstream path is "supposed to" be correct.

## 2. Idempotency belongs in the data, not the bookkeeping

Each reading carries a UUIDv7 derived deterministically from
`(bucket_ms, device_id, sensor_mac)`. The same measurement always mints the same
id, on the device, on a retry, on a replay after reboot, and in an export weeks
later.

This removes a whole category of problem. There is no "have I sent this?"
state to keep in sync, no sequence number to corrupt, no ack protocol. The
device can be careless and still be correct.

The archive uses the same trick at a different scale: a parquet part is named
after a hash of the ids it contains, so re-exporting the same rows produces a
byte-identical file with a name the repo already has, and the upload is a no-op.

**The rule:** derive identity from the measurement itself. Then "exactly once"
becomes a property you get for free instead of a protocol you have to run.

**The trap:** it makes the id inputs load-bearing forever. Changing how the id
is derived silently re-keys the entire archive and every historical row becomes
un-matchable. `uuid7.c` is treated as frozen, and `supabase/check_ids.sh`
re-derives ids from live rows to prove it.

## 3. Verify what the server actually does, not what you assume

We wrote "a 409 means the row was already stored, so treat it as success" into
the design *and* the project docs. It was wrong in a way that quietly destroyed
data.

PostgREST inserts a batch as a single statement. One duplicate id aborts the
whole thing — and any genuinely new rows in that batch are rejected too. The
sequence that loses data:

1. A POST lands server-side, but the response is lost (a 15 s timeout on WiFi).
2. The watermark is not advanced, so those rows are retried.
3. The retry batch now contains the already-stored rows *plus* newly closed ones.
4. Server returns 409. We counted it as success and advanced the watermark.
5. The new rows in that batch were never inserted, and are now unreachable.

The fix isn't clever: on an ambiguous 409, hold the watermark and drop to one
row per POST, where every answer is unambiguous — 201 means inserted, 409 means
genuinely already there. Then resume batching.

**The rule:** any assumption about backend semantics is a hypothesis until you
run it against the real deployment. Two lines of `curl` disproved this one:

```sh
# seed a row, then POST it again alongside a new one
POST [dup, new]  -> 409
GET  new.id      -> []          # the new row is NOT there
```

**Related:** `Prefer: resolution=ignore-duplicates` would fix this server-side,
but returns 401 for our INSERT-only role — `ON CONFLICT` needs `SELECT`, which
the device deliberately does not have. Worth testing before designing around it.

## 4. Never let a shared cursor represent independent progress

The uploader kept one watermark for all sensors and a global row cap per POST.
`build_batch` walked slots in order, filling the 40-row budget.

With a backlog, slot 0 consumed the entire budget, slots 1–4 contributed
nothing — and the shared watermark still advanced past *their* buckets. Those
readings were never uploaded and rolled off the ring buffer. Silent, permanent,
and only during an outage.

Two independent bugs in one line: a shared cursor over independent streams, and
a fixed iteration order that lets the first stream starve the rest.

**The rule:** if N things make progress independently, they need N cursors.
And if they compete for a shared budget, rotate the starting point so position
in an array does not decide who gets served.

## 5. Index-keyed state aliases the moment the index is reused

`history.c` keys its ring buffers by *slot index*. `slot_store.c` recycles a
slot when a sensor goes quiet, handing it to whichever device shows up next.
Nothing wiped the history.

So the new device's samples folded into the old device's open bucket — and the
uploader, which reads `history_since(slot)` and stamps rows with whatever MAC
now occupies that slot, would archive one sensor's readings under another
sensor's identity, permanently.

This was latent for months. It became reachable the day the slot count went from
4 to 5, because the fifth slot is the first unpinned one — a config change made a
dormant bug live.

**The rule:** when a container is keyed by position rather than identity, every
reassignment must reset *all* state keyed the same way. Keep a written list; the
compiler cannot help you. Here that was two call sites and two subsystems
(`history_reset()` and the per-slot watermark).

**Better where you can afford it:** key by identity (the MAC) instead of
position, and the class of bug disappears. We kept indices for RAM reasons on a
no-PSRAM part, which is a legitimate trade — but it is a trade, and it has to be
paid for with discipline elsewhere.

## 6. A partial reset is worse than no reset

The gap handler reset a ring buffer by setting `temp` and `humid` to
`NO_DATA` — but left `n`, `batt_pct` and `rssi` untouched.

`history_since` decides whether a bucket is real by testing `n != 0`. So every
stale slot came back as a *fresh* bucket carrying `NO_DATA`, which serialises as
`-327.68 °C`, with a new timestamp and a new permanent UUID, straight into the
append-only archive.

It reads as correct. It was reviewed as correct. It needed 6 hours of tick
starvation to fire, so it never did — I checked the 4,612 archived rows and
found none.

**The rule:** reset the whole record, not the fields you were thinking about.
`memset` the struct, or assign a named zero-value constant. Partial
initialisation is the same class of bug as a partial free.

**And:** the field that decides validity (`n`) was not the field the reset was
about (`temp`). Validity flags belong *with* the data they qualify, and should
be impossible to leave stale.

## 7. Intent and state are different variables

`ble_scanner_resume()` returned `ESP_FAIL` if the radio would not start, and
nothing ever retried. The BOOT button is live before NimBLE has synced, so one
early press ended data collection until the next reboot — with the display
happily on and the logs quiet.

Separately, the controller-reset callback did not clear `s_scanning`, so the
flag claimed the scan was running when it was not.

The fix is a reconcile loop, not a better error path:

```c
static bool s_scanning;       /* what the radio is doing  */
static bool s_want_scanning;  /* what power save asked for */

void ble_scanner_tick(void)   /* ~1 Hz */
{
    if (s_want_scanning && !s_scanning) start_scan();
}
```

**The rule:** store desired state separately from observed state and converge
them periodically. One-shot commands fail once and stay failed; a reconcile loop
heals from any state, including ones you did not anticipate. This is why
Kubernetes controllers work the way they do, and it applies just as well to a
radio on a microcontroller.

**Corollary:** the reconcile tick must run when the screen is off. Collection
does not stop because nobody is looking — putting `ble_scanner_tick()` behind
the power-save guard would have made the self-healing useless exactly when it
matters most.

## 8. Read-modify-write needs the read checked

Setting the RTC does a read-modify-write on `Control_1`. The read's return value
was discarded, so on an I2C NACK the code wrote a *guess* back to a control
register — potentially flipping 12/24-hour mode, which would silently corrupt
every timestamp conversion afterwards, or the software-reset bit.

Worse, if the "stop the clock" write succeeded and the "restart" write failed,
the RTC stayed stopped and the backup timekeeping was dead — with nothing logged.

**The rule:** never write back a value you did not successfully read. And for a
stop/modify/start sequence, the restart must be attempted on every path and its
failure must be loud, because leaving a peripheral in its transient state is
worse than never touching it.

## 9. Concurrency bugs on a 32-bit MCU are real, and they get archived

`s_epoch_offset_us` is a 64-bit clock offset, written by the SNTP callback and
read by the uploader task. A 64-bit store on a 32-bit RISC-V core is two
instructions. A read that lands between them gets a torn value.

On a desktop this would be a rare glitch. Here that value is multiplied into a
timestamp, hashed into a deterministic UUID, and written to an append-only
archive — so a microsecond-wide race becomes a permanent wrong row that can
never be corrected.

The fix is a two-instruction critical section around a handful of writes per
boot. The ordering also matters: `s_src` is written last and read first, so a
reader sees either the old consistent clock or the new one, never a mixture.

**The rule:** on a 32-bit core, anything wider than a word shared across tasks
needs a lock. Judge the severity by where the value ends up, not by how narrow
the window is.

## 10. Log what a debugger would have told you

There is no debugger attached in normal use, and the display shows only what the
UI chooses to show. The serial console is the only ground truth, which turns
logging from hygiene into the primary interface.

The project rule is "log values, not adjectives". `"WiFi down, retry in 8000 ms"`
beats `"WiFi problem"`. Every state transition gets a line: connected, synced,
paused, slot evicted, slot rotated, view changed.

**And a heartbeat every 30 s**, because silence must never be the normal steady
state:

```
status: wifi=up clock=NTP sync_age=20s slots=4/0/4(live/stale/max)
        adverts=68 screen=on up[sent=0 dup=0 fail=0 backlog=0] heap=86700 min=85348
```

**The trap that bit us:** `backlog` was assigned the size of the batch just
built, which is capped at 40. So during a long outage — the one moment a human
reads that number — it could never exceed 40 no matter how many hours were
actually pending. A metric that saturates exactly when it becomes interesting is
worse than no metric, because it reads as reassuring.

**The rule:** check that each number can still move at the extremes it exists to
describe.

## 11. Make the untestable testable by injecting the clock

`history.c` has 1 h / 6 h / 24 h tiers. Testing rollover on hardware means
waiting a day. So every entry point has an `_at()` twin taking an explicit
timestamp:

```c
void history_tick(void)                { history_tick_at(esp_timer_get_time()); }
void history_tick_at(int64_t now_us)   { /* real logic */ }
```

A 24-hour window is then exercised in microseconds on the host, with no ESP-IDF
and no board — `test/run.sh` is a `cc` invocation and a few stub headers.

That harness is what let a reviewer *prove* the phantom-bucket bug instead of
arguing about it. I was initially sceptical of the report and wrong; the test
settled it in seconds.

**The rule:** the ambient inputs — time, randomness, hardware state — should be
parameters at the seam, defaulting to the real thing. That single change moves
most logic from "testable only on hardware" to "testable in CI".

**And:** a regression test that has never failed is unproven. After fixing the
reset, I reverted the fix and confirmed the new test failed with five
`-327.68 °C` buckets before restoring it. A test written after the fix, never
seen red, is a comment with extra steps.

## 12. Negative security tests must exercise the path they claim to

`verify.sh` asserts the firmware key *cannot* read data — the checks that matter,
since the key is recoverable from flash with `esptool`.

The dashboard-role checks sent a JWT in both the `apikey` and `Authorization`
headers. Supabase's gateway authenticates `apikey` against a real project key
*before* PostgREST sees the request, so it returned 401 — and the assertions
expected 401. They passed. They proved nothing: the request never reached
Postgres, so the role's grants were never tested at all.

With the right headers the denials come back **403 / `42501 permission denied`**
— an actual grant check. The distinction between "rejected at the gateway" and
"rejected by the database" is the entire value of the test.

**The rule:** a negative test must fail for the reason you intend. Assert on the
specific error, not just "it didn't work" — and prove the positive case works
through the same path, or you cannot tell a working denial from a broken request.

## 13. Degrade toward collecting data

When something is unavailable, ask what preserves the irreplaceable thing.

- **No clock?** Keep recording. Samples are stamped with *uptime*, and wall-clock
  is applied at send time, so readings taken before the first NTP sync still
  resolve to the right instant once a clock arrives.
- **No WiFi?** Keep recording. RAM holds ~6 hours; uploads resume from the
  watermark.
- **No server?** Exponential backoff, 1 s → 5 min, and hold the rows.
- **Screen off?** Keep scanning. Power save blanks the display and nothing else,
  because a gap in the history is permanent and a lit screen is not.

The ordering principle: *collection* degrades last. Display, upload and sync are
all recoverable later; a sample not taken is gone.

**The limit is stated, not hidden.** History is RAM-only, so an outage longer
than ~6 hours loses data. That is written down as an accepted trade with a
reason, not left as an implied promise the code cannot keep.

## 14. Credentials sized to the blast radius

The publishable key is compiled into the firmware image and can be read back out
of flash. So it is scoped to `INSERT` on two tables and nothing else — it cannot
read a single row back, cannot delete, cannot see sensor labels. A stolen image
buys an attacker the ability to write junk rows, which deterministic ids and the
validity gate make tedious and detectable.

The dashboard gets a *different* credential — a separate `dashboard_reader` role
reading two aggregate views — so rotating a leaked dashboard token never means
reflashing hardware. The secret key never leaves `.env`.

**The trap:** Supabase's default privileges grant `anon` on every new object in
`public`. Creating a view silently made it readable by the firmware key. Least
privilege here needs an *explicit revoke after each create*, not an absence of
grants — and a regression check, since the next new view will do it again.

---

## The shortest version

1. Sort failures by reversibility; spend effort only on the irreversible ones.
2. Derive identity from data, so retries are free and "exactly once" is free.
3. Test your assumptions about the backend against the real backend.
4. N independent streams need N cursors.
5. Reassigning an index must reset everything keyed by that index.
6. Reset whole records, never some fields.
7. Separate desired state from actual state; reconcile on a timer.
8. Check the read in read-modify-write.
9. Lock anything wider than a word that crosses tasks.
10. Log values, heartbeat always, and check your metrics can't saturate.
11. Inject the clock; prove regression tests fail before they pass.
12. Make negative tests fail for the right reason.
13. Degrade toward collecting data.
14. Size each credential to what its leak would cost.
