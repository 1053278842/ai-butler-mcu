idf_component_register(
    SRCS "i2s_voice.c"
    INCLUDE_DIRS "."
    REQUIRES driver json esp_http_client esp-sr
)
