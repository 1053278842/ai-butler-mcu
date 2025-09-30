/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_log.h>
#include <esp_event.h>
#include "http_wifi.h"
#include "wifi_manager.h"
#include "wifi_nvs.h"
#include "board_light.h"
#include "mqtt_ssl.h"
#include "light_manager.h"
#include "i2s_voice.h"

static const char *TAG = "wifi_connect";

// 全局变量存储接收的WiFi凭证
httpd_handle_t service = NULL;
nvs_wifi_info_t info = {0};

// 事件回调函数
static void event_handler(void *handler_arg,
                          esp_event_base_t event_base,
                          int32_t event_id,
                          void *event_data)
{
    if (event_base == WIFI_MANAGER_EVENT)
    {
        switch (event_id)
        {
        case WIFI_MANAGER_CONNECTED_SUCCESS:
            ESP_LOGI(TAG, "WiFi 连接成功事件收到");
            ESP_ERROR_CHECK(wifi_manager_switch_sta()); // 切换到 STA
            http_wifi_stop(&service);                   // 停止 Web 服务器
            wifi_manager_nvs_save(&info);               // 保存成功凭证
            board_light_off();                          // 连接成功，熄灭
            ESP_LOGI(TAG, "WiFi 连接成功!存储本次凭证,关闭WEB服务,关闭AP WIFI模式,关闭指示灯");
            ESP_LOGI(TAG, "开启 MQTT 任务");
            start_mqtt_ssl(); // 启动 MQTT 任务
            break;

        case WIFI_MANAGER_CONNECTED_FAIL:
            ESP_LOGI(TAG, "WiFi 连接失败事件收到");
            stop_mqtt_ssl(); // 停止MQTT客户端，释放资源
            ESP_LOGI(TAG, "启动 Web 服务器，等待新的 WiFi 凭证");
            http_wifi_web_init(&service); // 启动http,准备接受参数重新配网
            // wifi_manager_nvs_clear();     // 清除已接收凭证
            info = (nvs_wifi_info_t){0}; // 清空全局变量
            board_light_on();            // 连接失败，点亮指示灯
            break;
        }
    }

    if (event_base == HTTP_RECIVE_EVENT)
    {
        switch (event_id)
        {
        case HTTP_RECIVE_SSID:
            nvs_wifi_info_t *data = (nvs_wifi_info_t *)event_data;
            ESP_LOGI(TAG, "前端请求收到,尝试连接SSID: %s,PASS: %s", data->ssid, data->password);
            board_light_blink(500);                               // 快速闪烁，表示正在连接
            wifi_manager_connect_sta(data->ssid, data->password); // 尝试连接新的密码
            info = *data;                                         // 保存到全局变量
                                                                  // 释放事件数据内存
            break;
        }
    }
}
void app_main(void)
{
    // 1. 初始化 NVS
    wifi_manager_nvs_init();
    // wifi_manager_nvs_clear(); // 测试时，清除NVS中存储的WiFi凭证

    board_light_init();

    // 初始化麦克风、启动唤醒词任务
    i2s_mic_init();
    xTaskCreate(wwd_task, "wwd_task", 4096, NULL, 5, NULL);

    // 2. 初始化WIFI
    wifi_manager_init();

    if (wifi_manager_nvs_get(&info) == ESP_OK)
    {
        ESP_LOGI(TAG, "NVS中获取WiFi凭证: SSID=%s, 密码=%s", info.ssid, info.password);
    }
    else
    {
        ESP_LOGI(TAG, "NVS中没有WiFi凭证");
    };

    ESP_ERROR_CHECK(esp_event_handler_register(
        HTTP_RECIVE_EVENT,
        ESP_EVENT_ANY_ID,
        &event_handler,
        NULL));

    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_MANAGER_EVENT,
        ESP_EVENT_ANY_ID,
        &event_handler,
        NULL));

    // 3. 启动 WiFi 和 Web 服务器
    wifi_manager_start_apsta(info.ssid, info.password);
    // ESP_LOGI(TAG, "WiFi 配网完成，进入主程序逻辑");
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}