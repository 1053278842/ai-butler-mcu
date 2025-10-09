#include "i2s_voice.h"

#define I2S_NUM I2S_NUM_0
#define I2S_BCK_IO (16)
#define I2S_WS_IO (17)
#define I2S_DO_IO (18)
#define I2S_DI_IO (20)
#define TAG "AUDIO_PLAYER"

#define SAMPLE_RATE 16000
#define I2S_BUF_LEN 512
#define WWN_MODLE "hiesp"
#define VAD_THRESHOLD 5000

static i2s_chan_handle_t spk_chan = NULL; // 使用新的 I2S 通道句柄
static i2s_chan_handle_t mic_chan = NULL; // 使用新的 I2S 通道句柄
volatile bool stop_play_flag = false;

void i2s_spk_init(uint32_t sample_rate, uint16_t bits, uint16_t channels)
{
    // 如果通道已存在，先删除
    if (spk_chan)
    {
        i2s_channel_disable(spk_chan);
        i2s_del_channel(spk_chan);
        spk_chan = NULL;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
    esp_err_t ret = i2s_new_channel(&chan_cfg, &spk_chan, NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "I2S channel creation failed: %s", esp_err_to_name(ret));
        return;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits, channels),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCK_IO,
            .ws = I2S_WS_IO,
            .dout = I2S_DO_IO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(spk_chan, &std_cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "I2S channel init failed: %s", esp_err_to_name(ret));
        i2s_del_channel(spk_chan);
        spk_chan = NULL;
        return;
    }

    ret = i2s_channel_enable(spk_chan);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "I2S channel enable failed: %s", esp_err_to_name(ret));
        i2s_del_channel(spk_chan);
        spk_chan = NULL;
    }
}

i2s_chan_handle_t i2s_mic_init()
{
    // 初始化 inmp441 模块

    if (mic_chan)
    {
        i2s_channel_disable(mic_chan);
        i2s_del_channel(mic_chan);
        mic_chan = NULL;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_new_channel(&chan_cfg, NULL, &mic_chan);

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(16, 1),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCK_IO,
            .ws = I2S_WS_IO,
            .dout = I2S_GPIO_UNUSED,
            .din = I2S_DI_IO,
        }};
    i2s_channel_init_std_mode(mic_chan, &std_cfg);
    i2s_channel_enable(mic_chan);

    return mic_chan;
}

void send_audio_to_server(int16_t *buf, size_t len)
{
    ESP_LOGI(TAG, "录音长度: %d samples, 上传功能请自行实现", len);
}

void wake_callbak()
{
    ESP_LOGI(TAG, "开始录音...");
    // 确保使用麦克风通道
    i2s_mic_init();

    int16_t audio_buf[I2S_BUF_LEN];
    int16_t *record_buf = NULL;
    size_t record_len = 0;
    int silent_samples = 0;

    // esp_afe_sr_iface_t *afe = (esp_afe_sr_iface_t *)&ESP_AFE_SR_HANDLE;
    // void *afe_data = afe->create_from_config(&AFE_CONFIG_DEFAULT());

    // 设置阈值和静音超时（这里假设采样率 16kHz）
    const int vad_threshold = VAD_THRESHOLD;   // 能量阈值，需要根据环境调试
    const int silence_limit = SAMPLE_RATE * 1; // 1秒静音

    while (1)
    {
        size_t bytes_read = 0;
        i2s_channel_read(mic_chan, audio_buf, sizeof(audio_buf), &bytes_read, portMAX_DELAY);

        // === VAD 简单能量检测 ===
        long sum = 0;
        for (size_t i = 0; i < bytes_read / 2; i++)
        {
            sum += audio_buf[i] * audio_buf[i];
        }
        int energy = sum / (bytes_read / 2);
        static int dbg_cnt = 0;
        if ((dbg_cnt++ & 300) == 0)
        {
            ESP_LOGI(TAG, "energy=%u, rec_len=%zu", energy, record_len);
        }
        if (energy > vad_threshold)
        {
            silent_samples = 0; // 有声音，重置静音计数
        }
        else
        {
            silent_samples += bytes_read / 2; // 记录静音采样数
        }

        // afe_fetch_result_t *res = afe->fetch(audio_buf);
        // if (res->vad_state == VAD_SILENCE)
        //     silent_samples += bytes_read / 2;
        // else
        //     silent_samples = 0;

        record_buf = realloc(record_buf, (record_len + bytes_read / 2) * sizeof(int16_t));
        memcpy(record_buf + record_len, audio_buf, bytes_read);
        record_len += bytes_read / 2;

        // 如果静音超过1秒，结束录音
        if (silent_samples >= silence_limit)
        {
            break;
        }
    }

    send_audio_to_server(record_buf, record_len);
    free(record_buf);
    ESP_LOGI(TAG, "录音结束");
}
void wwd_task()
{
    ESP_LOGI(TAG, "唤醒词任务启动，栈剩余: %d", uxTaskGetStackHighWaterMark(NULL));

    // 先检查可用内存
    ESP_LOGI(TAG, "可用内存: %d", esp_get_free_heap_size());

    srmodel_list_t *models = esp_srmodel_init("model");
    if (!models)
    {
        ESP_LOGE(TAG, "模型初始化失败!");
        vTaskDelete(NULL);
    }

    ESP_LOGI(TAG, "模型加载后内存: %d", esp_get_free_heap_size());
    char *model_name = esp_srmodel_filter(models, ESP_WN_PREFIX, WWN_MODLE);
    esp_wn_iface_t *wakenet = (esp_wn_iface_t *)esp_wn_handle_from_name(model_name);
    model_iface_data_t *wn_data = wakenet->create(model_name, DET_MODE_95);

    int16_t buf[I2S_BUF_LEN];
    int stack_check_counter = 0;

    while (1)
    {
        // 定期检查栈使用情况
        if (stack_check_counter++ % 100 == 0)
        {
            UBaseType_t stack_remaining = uxTaskGetStackHighWaterMark(NULL);
            if (stack_remaining < 512)
            {
                ESP_LOGI(TAG, "唤醒词任务栈剩余: %d", stack_remaining);
                ESP_LOGE(TAG, "栈空间不足!");
            }
        }

        size_t bytes_read = 0;
        i2s_channel_read(mic_chan, buf, sizeof(buf), &bytes_read, portMAX_DELAY);

        if (wakenet->detect(wn_data, buf))
        {
            ESP_LOGI(TAG, "唤醒成功!");
            stop_play_flag = true;
            wake_callbak();
        }

        vTaskDelay(pdMS_TO_TICKS(1)); // 添加小延迟避免过度占用CPU
    }
}

// 或者在切换前完全关闭扬声器
void i2s_spk_deinit()
{
    if (spk_chan)
    {
        i2s_channel_disable(spk_chan);
        i2s_del_channel(spk_chan);
        spk_chan = NULL;
    }
}
// ---------------------- 解析 WAV header ----------------------
bool parse_wav_header(uint8_t *data, wav_info_t *info)
{
    if (memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0)
    {
        ESP_LOGE(TAG, "不是合法 WAV 文件");
        return false;
    }

    info->channels = *(uint16_t *)(data + 22);
    info->sample_rate = *(uint32_t *)(data + 24);
    info->bits_per_sample = *(uint16_t *)(data + 34);

    ESP_LOGI(TAG, "WAV info: %d Hz, %d bits, %d channels",
             info->sample_rate, info->bits_per_sample, info->channels);
    return true;
}
// ---------------------- HTTP 事件回调 ----------------------
esp_err_t voice_http_event_handler(esp_http_client_event_t *evt)
{
    stream_ctx_t *ctx = NULL;
    if (esp_http_client_get_user_data(evt->client, (void **)&ctx) != ESP_OK)
    {
        ESP_LOGE(TAG, "获取 user_data 失败");
        return ESP_FAIL;
    }

    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
    {
        uint8_t *data = (uint8_t *)evt->data;
        size_t len = evt->data_len;

        // 解析 WAV header
        if (!ctx->header_parsed)
        {
            if (len < 44)
            {
                ESP_LOGW(TAG, "WAV header incomplete, waiting for more data");
                return ESP_OK; // header 不完整
            }

            if (!parse_wav_header(data, &ctx->wav_info))
            {
                ESP_LOGE(TAG, "Failed to parse WAV header");
                return ESP_FAIL;
            }

            i2s_spk_init(ctx->wav_info.sample_rate, ctx->wav_info.bits_per_sample, ctx->wav_info.channels);

            data += 44;
            len -= 44;
            ctx->header_parsed = true;
        }

        // 写入 I2S 输出
        if (len > 0 && spk_chan)
        {
            size_t bytes_written = 0;
            esp_err_t ret = i2s_channel_write(spk_chan, data, len, &bytes_written, portMAX_DELAY);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "I2S write failed: %s", esp_err_to_name(ret));
            }
        }
        break;
    }
    case HTTP_EVENT_ON_FINISH:
        ctx->header_parsed = false;
        ESP_LOGI(TAG, "HTTP stream finished");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ctx->header_parsed = false;
        ESP_LOGI(TAG, "HTTP disconnected");

        // 先静音或关闭扬声器
        i2s_spk_deinit();

        // 添加短暂延迟，让噪声衰减
        vTaskDelay(pdMS_TO_TICKS(50));
        // 播放结束，切回麦克风通道
        i2s_mic_init();
        break;
    default:
        break;
    }

    return ESP_OK;
}
