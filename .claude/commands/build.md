# Build current or specified project

Activate ESP-IDF and build. Usage: /build [project_name]

If a project name is given, build `projects/<project_name>`.
If no project name is given, ask which project to build or detect from context.

Steps:
1. Run `. ~/esp/esp-idf/export.sh 2>/dev/null` to activate IDF
2. Run `idf.py -C projects/<name> build 2>&1 | tail -25`
3. If build fails with "IDF_TARGET not set" or IRAM overflow → wrong target. Run `idf.py -C projects/<name> set-target esp32c6` then rebuild.
4. If build fails with path errors in dependencies.lock → delete `projects/<name>/dependencies.lock` and rebuild.
5. If build fails with `No COMPONENT found for esp_task_wdt` → change `esp_task_wdt` to `esp_system` in main/CMakeLists.txt REQUIRES. In IDF 5.5, `esp_task_wdt.h` lives inside `esp_system`, not a standalone component.
6. If build fails with "app partition is too small" → missing or wrong `partitions.csv`. Ensure custom partition with ≥2MB app:
   ```
   CONFIG_PARTITION_TABLE_CUSTOM=y
   CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
   CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
   ```
   Then delete `sdkconfig` and rebuild.
7. If `sdkconfig.defaults` was changed, always delete `sdkconfig` first to force regeneration.
8. ESP32-C6 is single-core — never use `xTaskCreatePinnedToCore()`. Use `xTaskCreate()`.
9. If build fails with SH8601 or touch component errors → check `main/idf_component.yml` includes:
   ```yaml
   dependencies:
     espressif/esp_lcd_sh8601: "^2.0.1"
     espressif/esp_lcd_touch_ft5x06: "*"
     espressif/esp_io_expander_tca9554: "^2.0.0"
   ```
10. Report success with binary size, or show the specific error lines.
