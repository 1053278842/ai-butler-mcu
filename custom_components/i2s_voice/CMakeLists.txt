idf_component_register(
    SRCS "pcm_util.c" "i2s_voice.c"
    INCLUDE_DIRS "."
    REQUIRES driver json esp_http_client esp-sr esp_timer lwip net_api
)
