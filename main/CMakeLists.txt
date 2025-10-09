idf_component_register(SRCS "app_main.c"
                       PRIV_REQUIRES esp_wifi nvs_flash 
                       REQUIRES wifi_manager wifi_nvs http_wifi board_light mqtt_ssl light_manager esp_psram
                       INCLUDE_DIRS "")
