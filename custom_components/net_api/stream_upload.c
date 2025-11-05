#include "stream_upload.h"
#define TAG "Stream_Upload"
#define UPLOAD_BUFFER_SIZE 16 * 1024
#define MQTT_UPLOAD_FRAME 2

#define MQTT_UPLOAD_TOPIC "ll/bedroom/voice/mic01/up/pcmfile"

// 缓冲区，单位必须是 帧样本数*位深 ，这里单位是1024
static uint8_t tx_buf[UPLOAD_BUFFER_SIZE];
static size_t tx_len = 0;

static uint8_t mqtt_upload_buf[1024 * MQTT_UPLOAD_FRAME];
static int mqtt_upload_len = 0;

// 臭计数的
// static int dots = 0;

// qos 1,retain 0 ,分片发送。最后一包可以为空作为结束标识。
void mqtt_upload_write(mqtt_upload_chunk_t data)
{
    esp_mqtt_client_handle_t mqtt_client = esp_mqtt_client_get_global_handle();
    if (mqtt_client == NULL)
    {
        ESP_LOGI(TAG, "MQTT客户端未初始化,丢弃本次数据包！");
        return;
    }

    int qos = 0;
    if (data.len == 0 || data.len < 1024)
    {
        ESP_LOGI(TAG, "发送最后一包数据，len=%d", data.len);
        qos = 1; // 最后一包，确保到达
        esp_mqtt_client_publish(mqtt_client, MQTT_UPLOAD_TOPIC, (const char *)mqtt_upload_buf, mqtt_upload_len, qos, 0);
        esp_mqtt_client_publish(mqtt_client, MQTT_UPLOAD_TOPIC, NULL, 0, qos, 0);
    }

    if (mqtt_upload_len + data.len > sizeof(mqtt_upload_buf))
    {
        // 缓冲区满，发送数据
        esp_mqtt_client_publish(mqtt_client, MQTT_UPLOAD_TOPIC, (const char *)mqtt_upload_buf, mqtt_upload_len, qos, 0);
        mqtt_upload_len = 0;
    }
    memcpy(mqtt_upload_buf + mqtt_upload_len, data.data, data.len);
    mqtt_upload_len += data.len;
    // esp_mqtt_client_publish(mqtt_client, MQTT_UPLOAD_TOPIC, (const char *)data.data, data.len, qos, 0);
}

// 负责 chunked 数据的格式包装、http 的数据块发送。
static void flush_tx_buffer(esp_http_client_handle_t *client, bool force)
{
    if (client == NULL || *client == NULL)
    {
        tx_len = 0;
        return;
    }

    if (tx_len == 0)
    {
        return;
    }

    if (force)
    {
        ESP_LOGI(TAG, "发送剩余缓冲数据！");
    }
    else
    {
        ESP_LOGI(TAG, "帧缓冲已满，发送数据！");
    }

    size_t data_bytes = tx_len;
    int8_t *up_buff = malloc(data_bytes + 32);
    char chunk_header[16];
    snprintf(chunk_header, sizeof(chunk_header), "%X\r\n", data_bytes);
    size_t header_len = strlen(chunk_header);
    memcpy(up_buff, chunk_header, header_len);
    memcpy(up_buff + header_len, tx_buf, data_bytes);
    memcpy(up_buff + header_len + data_bytes, "\r\n", 2);
    size_t total_len = header_len + data_bytes + 2; // "\r\n" = 2 bytes
    esp_err_t ret = esp_http_client_write(*client, (char *)up_buff, total_len);
    if (ret < 0)
    {
        tx_len = 0;
        free(up_buff);
        ESP_LOGI(TAG, "写入失败，重建连接");
        stream_upload_stop(client);
        stream_upload_start(client);
        return;
    }
    tx_len = 0;
    free(up_buff);
}

void stream_upload_start(esp_http_client_handle_t *client)
{

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
    ESP_LOGI(TAG, "已打开HTTP连接!");
}

void stream_upload_stop(esp_http_client_handle_t *client)
{
    if (*client == NULL)
    {
        return;
    }

    // 把缓冲区数据一口气都发掉，防止缓冲数据滞留。
    flush_tx_buffer(client, true);
    // 拼接发送结束标志
    const char *end_chunk = "0\r\n\r\n";
    esp_http_client_write(*client, end_chunk, strlen(end_chunk));

    esp_http_client_close(*client);
    esp_http_client_cleanup(*client);
    *client = NULL;
    ESP_LOGI(TAG, "已关闭HTTP连接!");
}

void stream_upload_write(esp_http_client_handle_t *client, char *data, size_t len)
{
    if (client == NULL || *client == NULL)
    {
        ESP_LOGI(TAG, "客户端被提前终止,丢弃本次数据包！");
        return;
    }

    // 环形缓冲区减少write请求次数
    if (tx_len + len > sizeof(tx_buf))
    {
        // 缓冲区已满，直接发送数据
        flush_tx_buffer(client, false);
    }

    memcpy(tx_buf + tx_len, data, len);
    tx_len += len;
}

void udp_upload_write(const void *data, size_t len)
{
    udp_mgr_t udp_mgr = udp_mgr_get();
    if (!udp_mgr.ready)
    {
        udp_mgr_init("121.36.251.16", 6543);
        // udp_mgr_init("192.168.88.250", 6543);
        udp_mgr = udp_mgr_get();
    }

    udp_mgr_send(data, len);
}
