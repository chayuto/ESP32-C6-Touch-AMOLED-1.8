# Flash firmware to connected ESP32-C6 board

Activate ESP-IDF and flash. Usage: /flash [project_name]

If no project name given, ask or detect from context.

Steps:
1. Run `. ~/esp/esp-idf/export.sh 2>/dev/null`
2. Detect port: `ls /dev/cu.usbmodem* /dev/tty.usbmodem* 2>/dev/null`
3. Use the first usbmodem port found (typically `/dev/cu.usbmodem1101`)
4. Run `idf.py -C projects/<name> -p <port> flash 2>&1`
5. A brief connection drop mid-flash (~14%) is normal for this board — it auto-recovers.
6. Confirm "Done" and "Hard resetting" at the end.
7. NEVER run `idf.py flash monitor` combined in a non-TTY shell (piped, subshell, agent).
   Monitor exits immediately in non-TTY → flash aborts at ~56% → corrupt firmware → "No bootable app" panic.
   Always flash separately first, confirm 100% write, then open serial separately:
   ```zsh
   ~/.espressif/python_env/idf5.5_py3.14_env/bin/python3 -m serial.tools.miniterm /dev/cu.usbmodem1101 115200
   ```
