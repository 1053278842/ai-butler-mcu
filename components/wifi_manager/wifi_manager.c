#include "wifi_manager.h"

ESP_EVENT_DEFINE_BASE(WIFI_MANAGER_EVENT); // 定义 Event Base

static int s_retry_num = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    // 你的事件处理逻辑
    ESP_LOGI(WIFI_MANAGER_TAG, "WiFi event: base=%d, id=%ld", event_base, event_id);
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(WIFI_MANAGER_TAG, "WIFI就绪,等待连接...");
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            // 连接断开-尝试重试
            if (s_retry_num < 3)
            {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI(WIFI_MANAGER_TAG, "连接失败，正在重试 %d/3", s_retry_num);
            }
            else
            {
                s_retry_num = 0;
                // 设置事件组-WIFI断开
                esp_event_post(WIFI_MANAGER_EVENT, WIFI_MANAGER_CONNECTED_FAIL, NULL, 0, portMAX_DELAY);
            }
            break;
        case WIFI_EVENT_AP_START:
            ESP_LOGI(WIFI_MANAGER_TAG, "SoftAP transport: Started!");
            break;
        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI(WIFI_MANAGER_TAG, "SoftAP transport: Connected!");
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(WIFI_MANAGER_TAG, "SoftAP transport: Disconnected!");
            break;
        default:
            break;
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        s_retry_num = 0; // 重置重试次数
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(WIFI_MANAGER_TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
        esp_event_post(WIFI_MANAGER_EVENT, WIFI_MANAGER_CONNECTED_SUCCESS, NULL, 0, portMAX_DELAY);
    }
}

/**
 * 初始化网络环境、注册事项、设置连接失败重试
 */
esp_err_t wifi_manager_init(void)
{
    // 注册 HTTP 模块自定义事件

    ESP_LOGI(WIFI_MANAGER_TAG, "Initializing WiFi manager");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    // 注册事件处理程序
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,
                                               ESP_EVENT_ANY_ID,
                                               &wifi_event_handler,
                                               NULL));

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,
                                               IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler,
                                               NULL));

    return ESP_OK;
}

static void get_device_service_name(char *service_name, size_t max)
{
    uint8_t eth_mac[6];
    const char *ssid_prefix = "SoftAp_";
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    snprintf(service_name, max, "%s%02X%02X%02X",
             ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);
}

esp_err_t wifi_manager_start_apsta(const char *ssid, const char *password)
{
    char service_name[14];
    get_device_service_name(service_name, sizeof(service_name));
    wifi_config_t ap_config = {
        .ap = {
            .ssid_len = strlen(service_name),
            .channel = 1,
            .authmode = WIFI_AUTH_OPEN, // 无密码
            .max_connection = 4,        // 最大连接数
            .beacon_interval = 100,     // 信标间隔
        },
    };
    memcpy(ap_config.ap.ssid, service_name, strlen(service_name));

    // 配置 STA（从全局 target_ssid / target_pass）
    wifi_config_t sta_config = {0};

    strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
    strncpy((char *)sta_config.sta.password, password, sizeof(sta_config.sta.password));

    // 设置成 APSTA
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    if (ssid == NULL || strlen(ssid) == 0)
    {
        ESP_LOGW(WIFI_MANAGER_TAG, "STA SSID为空，触发连接失败事件");
        esp_event_post(WIFI_MANAGER_EVENT,
                       WIFI_MANAGER_CONNECTED_FAIL,
                       NULL, 0, portMAX_DELAY);
    }
    return ESP_OK;
}

esp_err_t wifi_manager_switch_sta(void)
{
    return esp_wifi_set_mode(WIFI_MODE_STA); // 切换到 STA
}

esp_err_t wifi_manager_connect_sta(const char *ssid, const char *password)
{
    nvs_wifi_info_t info = {0};
    wifi_manager_nvs_get(&info);

    wifi_config_t sta_config = {0};
    strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
    strncpy((char *)sta_config.sta.password, password, sizeof(sta_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config)); // 更新配置
    esp_wifi_connect();
    return ESP_OK;
}
