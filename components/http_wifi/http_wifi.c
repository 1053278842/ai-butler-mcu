#include <http_wifi.h>
#include <wifi_nvs.h>

ESP_EVENT_DEFINE_BASE(HTTP_RECIVE_EVENT); // 定义 Event Base

// 处理配网请求的回调函数
static esp_err_t config_handler(httpd_req_t *req)
{
    char query[128] = {0};

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK)
    {
        ESP_LOGE(HTTP_WIFI_TAG, "Failed to get query string");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Request");
        return ESP_FAIL;
    }

    nvs_wifi_info_t info = {0};

    if (httpd_query_key_value(query, "ssid", info.ssid, sizeof(info.ssid)) == ESP_OK &&
        httpd_query_key_value(query, "password", info.password, sizeof(info.password)) == ESP_OK)
    {
        ESP_LOGI(HTTP_WIFI_TAG, "收到WiFi凭证: SSID=%s, 密码=%s", info.ssid, info.password);

        // 堆上分配一个副本，保证事件处理期间不会悬空
        nvs_wifi_info_t *info_copy = malloc(sizeof(nvs_wifi_info_t));
        if (!info_copy)
        {
            ESP_LOGE(HTTP_WIFI_TAG, "Failed to allocate memory for wifi info");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory Error");
            return ESP_FAIL;
        }

        memcpy(info_copy, &info, sizeof(nvs_wifi_info_t));

        // 发送事件，异步通知 wifi_manager
        esp_event_post(HTTP_RECIVE_EVENT, HTTP_RECIVE_SSID, info_copy, sizeof(nvs_wifi_info_t), portMAX_DELAY);
    }
    else
    {
        ESP_LOGE(HTTP_WIFI_TAG, "Failed to parse ssid/password");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Request");
        return ESP_FAIL;
    }

    httpd_resp_send(req, "Received WiFi credentials, trying to connect...", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t http_wifi_web_init(httpd_handle_t *server)
{
    if (server == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (*server != NULL)
    {
        ESP_LOGW(HTTP_WIFI_TAG, "Web server already running");
        return ESP_OK;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(server, &config) == ESP_OK)
    {
        // 注册POST /config接口
        httpd_uri_t config_uri = {
            .uri = "/config",
            .method = HTTP_GET,
            .handler = config_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(*server, &config_uri);
    }
    return ESP_OK;
}

esp_err_t http_wifi_stop(httpd_handle_t *server)
{
    if (server && *server)
    {
        ESP_LOGI(HTTP_WIFI_TAG, "Stopping HTTP Server");
        return httpd_stop(*server);
    }
    return ESP_OK;
}