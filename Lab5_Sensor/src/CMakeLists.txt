idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_rainmaker rmaker_app_network nvs_flash driver esp_timer
)
