#ifndef NVS_H
#define NVS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <esp_err.h>
#include "nvs_flash.h"
#include "esp_log.h"

#define WIFI_NAMESPACE "wifi_config"
#define WIFI_NVS_TAG "wifi_nvs"
typedef struct
{
    char ssid[32];
    char password[64];
} nvs_wifi_info_t;
esp_err_t wifi_manager_nvs_init(void);
esp_err_t wifi_manager_nvs_save(nvs_wifi_info_t *info);
esp_err_t wifi_manager_nvs_get(nvs_wifi_info_t *info);
/**
 * 清空存储的WiFi信息
 */
esp_err_t wifi_manager_nvs_clear(void);

#endif
