idf_component_register(
    SRCS
        "main.c"
        "ui_screens.c"
        "pet_state.c"
        "pet_renderer.c"
        "stat_engine.c"
        "audio_jingles.c"
        "audio_output.c"
        "rtc_manager.c"
        "pet_save.c"
        "imu_manager.c"
        "minigame_catch.c"
        "power_manager.c"
        "asset_loader.c"
        "species.c"
        "fishbowl.c"
        "idle_scheduler.c"
        "intro_screens.c"
        "story_card.c"
    INCLUDE_DIRS "."
    REQUIRES
        amoled_driver
        nvs_flash
        esp_timer
        esp_system
        freertos
        driver
        esp_driver_i2s
        esp_driver_i2c
        esp_partition
        spi_flash
)

# ── Asset bundle build hook ─────────────────────────────────────────────
# Run the sprite_factory Python tool whenever any .yml under
# tools/sprite_factory/ changes; output to build/assets.bin and a
# generated header next to main.c so app code can refer to assets by
# compile-time enum constants.
set(SPRITE_FACTORY_DIR "${CMAKE_SOURCE_DIR}/../../tools/sprite_factory")
set(ASSETS_BIN  "${CMAKE_BINARY_DIR}/assets.bin")
set(ASSET_INDEX "${CMAKE_CURRENT_SOURCE_DIR}/asset_index.h")

file(GLOB_RECURSE SPRITE_YML
     "${SPRITE_FACTORY_DIR}/sprites/*.yml"
     "${SPRITE_FACTORY_DIR}/palettes/*.yml")

set(SPRITE_PY_VENV
    "$ENV{HOME}/.espressif/python_env/idf5.5_py3.14_env/bin/python3")

add_custom_command(
    OUTPUT ${ASSETS_BIN} ${ASSET_INDEX}
    COMMAND ${SPRITE_PY_VENV}
            ${SPRITE_FACTORY_DIR}/build_assets.py
            --src ${SPRITE_FACTORY_DIR}
            --out-bin ${ASSETS_BIN}
            --out-header ${ASSET_INDEX}
    DEPENDS ${SPRITE_YML}
            ${SPRITE_FACTORY_DIR}/build_assets.py
            ${SPRITE_FACTORY_DIR}/primitives.py
    COMMENT "Building sprite asset bundle"
    VERBATIM
)
add_custom_target(sprite_assets DEPENDS ${ASSETS_BIN} ${ASSET_INDEX})
add_dependencies(${COMPONENT_LIB} sprite_assets)

# ── flash-assets target: write only the assets partition ────────────────
# Run with: idf.py flash-assets [-p PORT]
# Avoids re-flashing firmware when only sprite art changed.
set(IDF_PATH_VAR "$ENV{IDF_PATH}")
add_custom_target(flash-assets
    COMMAND ${SPRITE_PY_VENV}
            ${IDF_PATH_VAR}/components/partition_table/parttool.py
            --port=${ESPPORT}
            --partition-table-file=${CMAKE_BINARY_DIR}/partition_table/partition-table.bin
            write_partition
            --partition-name=assets
            --input=${ASSETS_BIN}
    DEPENDS ${ASSETS_BIN}
    USES_TERMINAL
    COMMENT "Flashing assets partition only"
)
