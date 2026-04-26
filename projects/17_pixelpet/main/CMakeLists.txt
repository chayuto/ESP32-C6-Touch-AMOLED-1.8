idf_component_register(
    SRCS
        "main.c"
        "ui_screens.c"
        "pet_state.c"
        "pet_renderer.c"
    INCLUDE_DIRS "."
    REQUIRES
        amoled_driver
        nvs_flash
        esp_timer
        esp_system
        freertos
        driver
)
