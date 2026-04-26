idf_component_register(
    SRCS
        "main.c"
        "ui_screens.c"
    INCLUDE_DIRS "."
    REQUIRES
        amoled_driver
        nvs_flash
        esp_timer
        esp_system
        freertos
        driver
)
