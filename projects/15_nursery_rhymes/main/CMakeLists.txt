idf_component_register(
    SRCS
        "main.c"
        "audio_player.c"
        "song_data.c"
        "ui.c"
    INCLUDE_DIRS "."
    REQUIRES
        amoled_driver nvs_flash esp_timer esp_system freertos driver
)
