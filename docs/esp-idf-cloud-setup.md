# Setting Up ESP-IDF Cross-Compiler in Cloud/Ephemeral Environments

Learnings from setting up ESP-IDF v5.5 for ESP32-C6 in a Claude Code web session
(Ubuntu 24.04, x86_64, no hardware attached).

## Why This Matters

Cloud dev environments (Claude Code web, GitHub Codespaces, Gitpod, etc.) are
ephemeral — they reset between sessions. ESP-IDF requires a ~1.5GB toolchain
install. This doc captures the exact steps and gotchas so future agents or
developers can reproduce it quickly.

## Prerequisites (Already Present in Most Cloud Envs)

```
python3 >= 3.8      # ESP-IDF 5.x requires Python 3.8+
cmake >= 3.16       # Build system
ninja >= 1.10       # Fast build backend
git                 # Clone IDF + submodules
pip3                # Python package installer
wget / curl         # Download toolchains
```

Verify: `python3 --version && cmake --version && ninja --version && git --version`

## System Dependencies

```bash
# libusb is required for OpenOCD (on-chip debugger tool)
# Without it, the ESP-IDF install script fails and export.sh refuses to activate
apt-get update -qq && apt-get install -y -qq libusb-1.0-0
```

**This is the #1 gotcha.** ESP-IDF's `install.sh` downloads OpenOCD, which
dynamically links against `libusb-1.0.so.0`. If missing:
- OpenOCD install silently fails
- `export.sh` then refuses to activate ("tool openocd-esp32 has no installed versions")
- Everything else works fine — it's just this one shared library

## Installation Steps (~10-15 minutes)

### 1. Clone ESP-IDF (depth=1 to save time/space)
```bash
mkdir -p ~/esp && cd ~/esp
git clone --depth 1 -b v5.5 https://github.com/espressif/esp-idf.git
```

### 2. Initialize Submodules
```bash
cd ~/esp/esp-idf
git submodule update --init --depth 1
```

### 3. Install Toolchain (target-specific saves ~500MB vs all targets)
```bash
./install.sh esp32c6
```

This downloads and installs:
- `riscv32-esp-elf` — RISC-V GCC cross-compiler (~400MB)
- `riscv32-esp-elf-gdb` — Debugger
- `openocd-esp32` — On-chip debugger (needs libusb!)
- `esp-rom-elfs` — ROM ELF files for debugging
- Python virtual environment with all ESP-IDF Python deps

### 4. Activate (Required Every Shell Session)
```bash
export IDF_PATH=~/esp/esp-idf
. $IDF_PATH/export.sh
```

After activation, `idf.py` should be in PATH:
```bash
idf.py --version   # Should print "ESP-IDF v5.5"
```

## Gotchas Encountered

### Gotcha 1: libusb Missing → OpenOCD Fails → export.sh Refuses to Work

**Symptom:**
```
ERROR: tool openocd-esp32 has no installed versions.
ERROR: Activation script failed
```

**Fix:** `apt-get install -y libusb-1.0-0` then re-run `install.sh`

### Gotcha 2: Manual Python Venv Doesn't Include All Dependencies

**Symptom:** Creating the venv manually and pip-installing `requirements.core.txt`
misses the constraints file and some IDF-specific package versions.

**Fix:** Always use `./install.sh` or `python3 tools/idf_tools.py install-python-env`.
Don't create the venv manually.

### Gotcha 3: install.sh Runs Tools After Download to Verify

The install script runs each tool after extracting to verify it works.
If a shared library is missing (like libusb), it:
1. Extracts the tool
2. Tries to run it — fails
3. **Deletes the extracted directory**
4. Reports error but continues

So you can't just install libusb after the fact — you need to re-run `install.sh`.

### Gotcha 4: source vs . in Bash Tool

In CI/automation contexts, `source` and `.` behave differently when chained:
```bash
# This may NOT work (subshell loses env):
source $IDF_PATH/export.sh 2>/dev/null && idf.py build

# This works (same shell):
export IDF_PATH=~/esp/esp-idf && . $IDF_PATH/export.sh 2>/dev/null && idf.py build
```

Always chain with `&&` in a single command, or `export.sh` modifications to PATH
won't persist.

### Gotcha 5: sdkconfig.defaults Is Gitignored

This project gitignores `sdkconfig.defaults` (credentials pattern). Use `git add -f`
to force-add it if it contains no secrets (just build config).

## Quick One-Liner for Future Sessions

```bash
apt-get update -qq && apt-get install -y -qq libusb-1.0-0 && \
mkdir -p ~/esp && cd ~/esp && \
git clone --depth 1 -b v5.5 https://github.com/espressif/esp-idf.git && \
cd esp-idf && git submodule update --init --depth 1 && \
./install.sh esp32c6
```

Then in every shell:
```bash
export IDF_PATH=~/esp/esp-idf && . $IDF_PATH/export.sh 2>/dev/null
```

## Disk and Time Budget

| Step | Time | Disk |
|------|------|------|
| Clone IDF (depth=1) | ~2 min | ~200MB |
| Submodule init | ~1 min | ~100MB |
| install.sh esp32c6 | ~5-8 min | ~800MB |
| Python venv + deps | ~2 min | ~200MB |
| **Total** | **~10-15 min** | **~1.3GB** |

First build of a project adds ~200MB for component downloads + build artifacts.

## What You CAN'T Do Without Hardware

- Flash firmware (`idf.py flash`)
- Serial monitor (`idf.py monitor`)
- On-chip debugging (OpenOCD/GDB)

**What you CAN do:**
- Full compilation and linking (catches all code errors)
- Binary size analysis (`idf.py size`)
- Component dependency resolution
- Static analysis
- Generate flashable .bin files for later download

## SessionStart Hook (Automate for Repeat Sessions)

For Claude Code web sessions, you can add a SessionStart hook in
`.claude/settings.json` that auto-installs the toolchain:

```json
{
  "hooks": {
    "SessionStart": [{
      "command": "bash -c 'if ! command -v idf.py &>/dev/null; then apt-get update -qq && apt-get install -y -qq libusb-1.0-0 && mkdir -p ~/esp && cd ~/esp && git clone --depth 1 -b v5.5 https://github.com/espressif/esp-idf.git && cd esp-idf && git submodule update --init --depth 1 && ./install.sh esp32c6; fi'",
      "timeout": 900
    }]
  }
}
```

This adds ~10-15 min to session startup but means every session can build.
