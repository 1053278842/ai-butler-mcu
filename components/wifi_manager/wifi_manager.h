#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "wifi_nvs.h"

#define WIFI_MANAGER_TAG "wifi_manager"
// 定义自定义事件 Base
ESP_EVENT_DECLARE_BASE(WIFI_MANAGER_EVENT);

// 定义事件 ID
typedef enum
{
    WIFI_MANAGER_CONNECTED_SUCCESS, // 即连接上wifi，获取到ip了
    WIFI_MANAGER_CONNECTED_FAIL     // 即连接失败/wifi断开，可能是密码错误等其他原因
} wifi_manager_event_id_t;

esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_start_apsta(const char *ssid, const char *password);

esp_err_t wifi_manager_switch_sta(void);
esp_err_t wifi_manager_connect_sta(const char *ssid, const char *password);

#endif // WIFI_MANAGER_H
