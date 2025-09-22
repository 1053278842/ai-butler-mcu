idf_component_register(
    SRCS "http_wifi.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_server wifi_nvs
)
