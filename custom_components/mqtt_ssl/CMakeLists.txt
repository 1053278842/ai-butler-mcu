idf_component_register(
    SRCS "mqtt_ssl.c"
    INCLUDE_DIRS "."
    REQUIRES  mqtt esp_partition nvs_flash esp_netif app_update json board_light driver light_manager i2s_voice
)
