#ifndef HTTP_WIFI_H
#define HTTP_WIFI_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include <esp_http_server.h>
#include "esp_event.h"
#include <esp_log.h>

#define HTTP_WIFI_TAG "http_wifi"
// 定义自定义事件 Base
ESP_EVENT_DECLARE_BASE(HTTP_RECIVE_EVENT);

// 定义事件 ID
typedef enum
{
    HTTP_RECIVE_SSID, // 接收到前端请求的ssid参数
} http_wifi_event_id_t;

esp_err_t http_wifi_web_init(httpd_handle_t *server);

esp_err_t http_wifi_stop(httpd_handle_t *server);

#endif