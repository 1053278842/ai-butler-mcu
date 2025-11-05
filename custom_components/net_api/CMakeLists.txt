idf_component_register(
    SRCS "stream_upload.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_client mqtt_ssl udp_manager
)
