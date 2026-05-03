idf_component_register(
    SRCS
        "main.c"
        "audio_player.c"
        "noise_player.c"
        "song_data.c"
        "ui.c"
        "ui_noise.c"
    INCLUDE_DIRS "."
    REQUIRES
        amoled_driver nvs_flash esp_timer esp_system freertos driver
)
