#include "stream_upload.h"
#define TAG "Stream_Upload"

void stream_upload_start(esp_http_client_handle_t *client)
{
    ESP_LOGI(TAG, "打开HTTP连接!");

    // 若上次残留连接未清理，强制清理
    if (*client != NULL)
    {
        esp_http_client_cleanup(*client);
        *client = NULL;
    }

    esp_http_client_config_t config = {
        .url = "http://121.36.251.16:7999/api/stream_upload/pcm",
        .method = HTTP_METHOD_POST,
        .timeout_ms = 30000,
    };
    *client = esp_http_client_init(&config);

    esp_http_client_set_header(*client, "Content-Type", "audio/pcm");
    esp_http_client_set_header(*client, "Transfer-Encoding", "chunked");

    esp_err_t err = esp_http_client_open(*client, -1);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "打开HTTP连接失败: %s", esp_err_to_name(err));
        return;
    }
}

void stream_upload_stop(esp_http_client_handle_t *client)
{
    ESP_LOGI(TAG, "关闭HTTP连接!");
    const char *end_chunk = "0\r\n\r\n";
    esp_http_client_write(*client, end_chunk, strlen(end_chunk));

    esp_http_client_close(*client);
    esp_http_client_cleanup(*client);
    *client = NULL;
}

void stream_upload_write(esp_http_client_handle_t *client, char *data, size_t len)
{
    ESP_LOGI(TAG, "数据输入中!");
    esp_err_t ret = esp_http_client_write(*client, data, len);
    if (ret < 0)
    {
        ESP_LOGE(TAG, "写入失败，重建连接");
        stream_upload_stop(client);
        stream_upload_start(client);
    }
}