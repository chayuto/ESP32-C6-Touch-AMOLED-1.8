#!/usr/bin/env python3
"""
Agent-friendly serial monitor for the ESP32-C6 board.

Why this exists: `idf.py monitor` requires a TTY and exits immediately when
run from a non-interactive shell (Claude Code, scripts). This script gives
the agent a deterministic way to capture serial output:

    python3 .claude/bin/agent_monitor.py [--port PORT] [--seconds N] [--reset]

The reset path uses the proper esptool DTR/RTS sequence so the chip is
guaranteed to come up fresh. Output is line-buffered so you see logs as
they arrive, not at exit.

Use `--reset` for a fresh boot capture, or omit it to passively eavesdrop on
a running device.
"""
import argparse
import sys
import time
import glob

try:
    import serial
except ImportError:
    sys.stderr.write("pyserial not found. Run with the IDF venv python:\n")
    sys.stderr.write("  ~/.espressif/python_env/idf5.5_py3.14_env/bin/python "
                     ".claude/bin/agent_monitor.py [...]\n")
    sys.exit(2)


def autodetect_port() -> str:
    candidates = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not candidates:
        sys.stderr.write("No /dev/cu.usbmodem* device found.\n")
        sys.exit(1)
    return candidates[0]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", default=None,
                    help="Serial device. Default: first /dev/cu.usbmodem*.")
    ap.add_argument("--seconds", type=float, default=10.0,
                    help="How long to capture before exiting (default 10).")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--reset", action="store_true",
                    help="Reset the chip via esptool usb_reset before capture. "
                         "DTR/RTS are virtual on the C6's USB Serial JTAG, so "
                         "we shell out to esptool which uses the proper "
                         "USB-level reset command.")
    ap.add_argument("--quiet-bootloader", action="store_true",
                    help="Skip lines that start with the ROM bootloader prefix.")
    args = ap.parse_args()

    port = args.port or autodetect_port()
    sys.stderr.write(f"[agent_monitor] {port} @ {args.baud}, "
                     f"{args.seconds:.1f}s{', reset' if args.reset else ''}\n")
    sys.stderr.flush()

    if args.reset:
        import subprocess, os
        # esptool.py needs IDF on PATH. Fall back to `python -m esptool` from
        # the IDF venv so this works without sourcing export.sh first.
        idf_py = os.path.expanduser(
            "~/.espressif/python_env/idf5.5_py3.14_env/bin/python3")
        candidates = [
            ["esptool.py"],
            [idf_py, "-m", "esptool"],
        ]
        ok = False
        for prefix in candidates:
            try:
                rc = subprocess.run(
                    prefix + ["--chip", "esp32c6", "--port", port,
                              "--before", "usb_reset", "--after", "hard_reset",
                              "chip_id"],
                    capture_output=True, text=True, timeout=10,
                )
                if rc.returncode == 0:
                    ok = True
                    break
                sys.stderr.write(f"[agent_monitor] {prefix[0]} failed: "
                                 f"{rc.stderr[-200:]}\n")
            except FileNotFoundError:
                continue
        if not ok:
            sys.stderr.write("[agent_monitor] could not run esptool — "
                             "skipping reset\n")

    try:
        ser = serial.Serial(port, baudrate=args.baud, timeout=0.2)
    except serial.SerialException as e:
        sys.stderr.write(f"open failed: {e}\n")
        return 1

    # Always release DTR/RTS so a stale assertion doesn't hold the chip
    # in reset or download mode.
    ser.setDTR(False)
    ser.setRTS(False)

    deadline = time.monotonic() + args.seconds
    buf = bytearray()
    skip_bootloader = args.quiet_bootloader
    bytes_seen = 0
    try:
        while time.monotonic() < deadline:
            chunk = ser.read(1024)
            if not chunk:
                continue
            bytes_seen += len(chunk)
            buf.extend(chunk)
            while b"\n" in buf:
                line, _, rest = buf.partition(b"\n")
                buf = bytearray(rest)
                text = line.decode("utf-8", errors="replace").rstrip("\r")
                if skip_bootloader and (
                    text.startswith(("ESP-ROM:", "rst:", "load:",
                                     "entry ", "Build:", "SPIWP:", "mode:",
                                     "Saved PC:"))):
                    continue
                print(text, flush=True)
    finally:
        if buf:
            print(buf.decode("utf-8", errors="replace"), flush=True)
        ser.close()
        sys.stderr.write(f"[agent_monitor] {bytes_seen} bytes captured\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
