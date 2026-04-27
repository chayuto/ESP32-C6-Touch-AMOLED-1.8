# Flash firmware to connected ESP32-C6 board

Activate ESP-IDF, flash, then capture serial logs agentically. Usage: `/flash [project_name]`

If no project name given, ask or detect from context.

## Standard flow (works when the project follows the conventions below)

```bash
. ~/esp/esp-idf/export.sh 2>/dev/null
PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
idf.py -C projects/<name> -p $PORT flash
~/.espressif/python_env/idf5.5_py3.14_env/bin/python3 \
    .claude/bin/agent_monitor.py --port $PORT --seconds 12 --reset
```

A brief connection drop mid-flash (~14%) is normal — it auto-recovers.
Confirm "Done" and "Hard resetting" before moving on.

## Critical: this board needs USB Serial JTAG console, not UART0

The board's USB-C connector goes straight to the C6's native USB pins
(GPIO 12/13). UART0 (GPIO 16/17) is **not connected to USB**. So the IDF
default `CONFIG_ESP_CONSOLE_UART_DEFAULT=y` makes the bootloader logs
appear (via the secondary USB JTAG path) but the running app's
`ESP_LOGI` lines vanish — there's nothing on UART0 to read.

Every project's `sdkconfig.defaults` must include:

```
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
```

If you're flashing a project for the first time, check `sdkconfig.defaults`
before flashing. If the line is missing, add it, then `rm
projects/<name>/sdkconfig` (force regen) and rebuild before flashing — saves
a confusing "why is the device silent" debug loop.

## When esptool can't auto-reset

A running app that owns USB CDC will block esptool's `--before=default_reset`.
Symptoms: `A fatal error occurred: Failed to connect to ESP32-C6: No serial data received.`

Try in order:

1. `esptool.py --chip esp32c6 --port $PORT --before=usb_reset ...` — issues
   a USB-level reset that often wakes the bootloader without touching pins.
2. **Manual download mode**: ask the user to hold **BOOT** (GPIO 9) and
   briefly press **RESET**, then release BOOT. Then retry the flash.
3. After one successful flash with USB Serial JTAG console enabled,
   subsequent re-flashes auto-reset reliably.

### When `agent_monitor.py --reset` reports "could not run esptool"

Happens right after a flash when the port is still held by the previous
esptool invocation. Three options:

- Wait ~1 s and retry — usually sufficient.
- Fall back to a passive listen (no `--reset`); the app is already running
  so you just need to watch.
- Force a clean reset via `python -m esptool --chip esp32c6 --port $PORT
  run`, then passive-listen.

A passive listen with no recent activity will return 0 bytes — the app
only emits during boot or when something happens (touch, stage change,
etc.). For a fresh boot capture you need either `--reset` or an esptool
`run` first.

## Asset / data partitions and `erase-flash`

`idf.py erase-flash` wipes **the entire flash**, including any custom
data partitions defined in `partitions.csv` (e.g. `assets`). The
standard `idf.py flash` then only re-writes app + bootloader + partition
table, leaving data partitions empty. The app boots, mmap'd asset
lookups return null, and you see things like:

```
asset_loader: assets partition: addr=0x00210000 size=12582912
asset_loader: loaded 0/N assets   ← bad
```

If a project uses a separate asset/data partition, follow `erase-flash`
with the project's data-flash target. For pixelpet that's:

```bash
idf.py -C projects/<name> -p $PORT flash-assets
```

Or, if you need the firmware AND data both fresh, chain them:
`erase-flash → flash → flash-assets`. NVS is the exception — it's
auto-formatted by `nvs_flash_init` on a blank read, so no separate
flash step is needed.

## Agent-friendly serial monitoring

Never run `idf.py monitor` from a non-TTY shell — it silently exits, and if
you piped it after `flash`, the flash is aborted at ~56% leaving a corrupt
firmware ("No bootable app" panic on next boot). Always flash and monitor in
**two separate** invocations.

For monitoring, use the helper at `.claude/bin/agent_monitor.py`:

```bash
~/.espressif/python_env/idf5.5_py3.14_env/bin/python3 \
    .claude/bin/agent_monitor.py --seconds 15 --reset
```

Flags:
- `--reset` — toggle DTR to start from a fresh boot (proper esptool sequence).
- `--seconds N` — capture window. 10–15 s is enough for boot + a couple of
  decay ticks; bump for longer-running tests.
- `--port PATH` — override autodetect.
- `--quiet-bootloader` — drop the `ESP-ROM:` / `load:` / `entry` noise so
  app logs are easier to scan.

The script line-buffers stdout, so logs stream as they arrive — you can
watch a `Bash` tool call's output in near real time instead of waiting for
the whole capture window.

## Verbose logging for debugging

When you need to see what's happening inside drivers, add this to
`sdkconfig.defaults` for the project (or set it temporarily via
menuconfig):

```
CONFIG_LOG_MAXIMUM_LEVEL_DEBUG=y
CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y
```

Then suppress noisy components in `app_main`:

```c
esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_NONE);
esp_log_level_set("FT5x06", ESP_LOG_NONE);
esp_log_level_set("I2C_If", ESP_LOG_WARN);
```

Set specific tags to DEBUG to drill into one subsystem:

```c
esp_log_level_set("rtc", ESP_LOG_DEBUG);
esp_log_level_set("pet_save", ESP_LOG_DEBUG);
```

## Troubleshooting checklist

| Symptom | First thing to check |
|---|---|
| Bootloader logs visible, then silence | `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` set? |
| `No serial data received` during flash | Manual BOOT+RESET, then retry |
| Bash returns 0 bytes from monitor | Did you `--reset`? Is the device actually plugged in (`ls /dev/cu.usb*`)? |
| Boot loops forever | Read with `--seconds 30 --reset` to catch the panic backtrace |
| Wrong target chip | `idf.py set-target esp32c6` (default is xtensa esp32 — fails with IRAM overflow) |
| App boots but display blank / "0 assets loaded" | After `erase-flash`, also run `flash-assets` (or whatever data-partition target the project defines) |
| `agent_monitor --reset` returns 0 bytes | Port still held by esptool — wait 1 s, retry, or `python -m esptool ... run` then passive listen |
