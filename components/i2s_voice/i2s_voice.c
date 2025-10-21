#include "i2s_voice.h"

#define I2S_NUM I2S_NUM_0
#define I2S_BCK_IO (16)
#define I2S_WS_IO (17)
#define I2S_DO_IO (18)
#define I2S_DI_IO (20)
#define TAG "AUDIO_PLAYER"

#define SAMPLE_RATE 16000

static i2s_chan_handle_t spk_chan = NULL; // 使用新的 I2S 通道句柄
static i2s_chan_handle_t mic_chan = NULL; // 使用新的 I2S 通道句柄fv
QueueHandle_t frame_queue;
// vad

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
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
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
    // 初始化或更新声道和时钟配置
    i2s_std_config_t std_cfg = {
        .clk_cfg =
            {
                .clk_src = I2S_CLK_SRC_DEFAULT,
                .ext_clk_freq_hz = 0,
                .mclk_multiple = I2S_MCLK_MULTIPLE_512,
                .sample_rate_hz = SAMPLE_RATE,
            },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED, // 不需要
            .dout = I2S_GPIO_UNUSED, // 不需要
            .bclk = I2S_BCK_IO,
            .ws = I2S_WS_IO,
            .din = I2S_DI_IO,
            .invert_flags = {
                // 都不需要
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT; // 修改为左声道
    std_cfg.slot_cfg.bit_order_lsb = false;         // 大端模式,高位在前，低位补零。默认值(如果是true则高位补零，补码需要处理)

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    // chan_cfg.dma_frame_num = 4 * 1024;

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &mic_chan));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(mic_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(mic_chan));

    return mic_chan;
}

vad_ctx_t *init_vad_mod()
{
    srmodel_list_t *models = esp_srmodel_init("model");
    afe_config_t *afe_config = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    afe_config->vad_min_noise_ms = 800;  // The minimum duration of noise or silence in ms.
    afe_config->vad_min_speech_ms = 512; // The minimum duration of speech in ms.
    afe_config->vad_mode = VAD_MODE_0;   // 这傻逼玩意，别信他的注释（“So If you want trigger more speech, please select lower mode.”）。明明是越大越灵敏
    afe_config->agc_mode = 2;            // 启用更强的自动增益控制（AGC）

    // 噪声抑制
    char *ns_model_name = esp_srmodel_filter(models, ESP_NSNET_PREFIX, NULL);
    if (ns_model_name != NULL)
    {
        afe_config->ns_init = true;
        afe_config->ns_model_name = ns_model_name;
        afe_config->afe_ns_mode = AFE_NS_MODE_NET;
        ESP_LOGI(TAG, "找到噪声抑制模型：%s，启用噪声抑制", ns_model_name);
    }
    else
    {
        ESP_LOGI(TAG, "未找到噪声抑制模型，禁用噪声抑制");
        afe_config->ns_init = false;
    }

    vad_ctx_t *ctx = malloc(sizeof(vad_ctx_t));
    ctx->handle = esp_afe_handle_from_config(afe_config);
    ctx->data = ctx->handle->create_from_config(afe_config);
    afe_config_free(afe_config);
    return ctx;
}

void feed_Task(void *arg)
{
    vad_ctx_t *vad = (vad_ctx_t *)arg;
    esp_afe_sr_data_t *afe_data = vad->data;
    esp_afe_sr_iface_t *afe_handle = vad->handle;
    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data);
    int nch = afe_handle->get_feed_channel_num(afe_data);

    int16_t *i2s_buff = malloc(audio_chunksize * sizeof(int16_t) * nch);                // 512 * 2 = 1024bytes ,512个元素
    uint8_t *r_buf = (uint8_t *)calloc(1, audio_chunksize * sizeof(int16_t) * nch * 2); // 2028bytes,512个元素，每个元素32-16bits
    size_t r_bytes = 0;
    size_t chunks = 0;
    assert(i2s_buff);
    ESP_LOGI(TAG, "feed chunksize=%d, nch=%d", audio_chunksize, nch);

    while (1)
    {
        i2s_channel_read(mic_chan, r_buf, audio_chunksize * sizeof(int16_t) * nch * 2, &r_bytes, portMAX_DELAY);
        int32_t *samples = (int32_t *)r_buf;
        int sample_count = r_bytes / sizeof(int32_t);
        for (int i = 0; i < sample_count; i++)
        {
            int32_t sample = samples[i];
            int16_t pcm16 = pcm32_to_pcm16(sample);
            i2s_buff[chunks++] = pcm16;
        }
        chunks = 0;
        afe_handle->feed(afe_data, i2s_buff);
    }
    if (i2s_buff)
    {
        free(i2s_buff);
        i2s_buff = NULL;
    }
    vTaskDelete(NULL);
}

void detect_Task(void *arg)
{
    vad_ctx_t *vad = (vad_ctx_t *)arg;
    esp_afe_sr_data_t *afe_data = vad->data;
    esp_afe_sr_iface_t *afe_handle = vad->handle;

    int afe_chunksize = afe_handle->get_fetch_chunksize(afe_data);
    int8_t *up_buff = malloc(afe_chunksize * sizeof(int16_t) + 32);
    stream_upload_ctx_t ctx = {0};

    while (1)
    {
        afe_fetch_result_t *res = afe_handle->fetch(afe_data);
        if (!res || res->ret_value == ESP_FAIL)
        {
            printf("fetch error!\n");
            break;
        }

        if (res->vad_state == VAD_SPEECH)
        {
            ESP_LOGI(TAG, "检测到人声");
            if (!ctx.uploading)
            {
                ESP_LOGI(TAG, "检测到人声，开始流式上传...");
                ctx.uploading = true;
                ctx.silence_ms = 0;
                upload_msg_t msg = {.type = UPLOAD_MSG_START};
                xQueueSend(frame_queue, &msg, 0);

                // stream_upload_start(&ctx, afe_chunksize);
            }
            ctx.silence_ms = 0;
        }
        else
        {
            if (ctx.uploading)
            {
                ctx.silence_ms += 32;
                if (ctx.silence_ms > 2000)
                {
                    ESP_LOGI(TAG, "检测到静音，结束流式上传");
                    // stream_upload_stop(&ctx);
                    ctx.uploading = false;
                    ctx.silence_ms = 0;
                    upload_msg_t msg = {.type = UPLOAD_MSG_STOP};
                    xQueueSend(frame_queue, &msg, 0);
                }
            }
        }

        if (ctx.uploading)
        {
            size_t payload_len = res->data_size;
            if (payload_len > 0)
            {

                char chunk_header[16];
                snprintf(chunk_header, sizeof(chunk_header), "%X\r\n", (unsigned int)payload_len);
                size_t header_len = strlen(chunk_header);

                // ESP_LOGI(TAG, "chunk_header:%s,chunk_size:%zu", chunk_header, header_len);
                memcpy(up_buff, chunk_header, header_len);

                // ESP_LOGI(TAG, "buff:%s,buff_size:%zu", up_buff, payload_len + 32);
                memcpy(up_buff + header_len, res->data, payload_len);
                memcpy(up_buff + header_len + payload_len, "\r\n", 2);

                size_t total_len = header_len + payload_len + 2; // "\r\n" = 2 bytes

                upload_msg_t msg = {.type = UPLOAD_MSG_DATA, .len = total_len};
                memcpy(msg.data, up_buff, msg.len);
                xQueueSend(frame_queue, &msg, 0);
            }
        }

        if (res->wakeup_state == WAKENET_DETECTED)
        {
            ESP_LOGI(TAG, "触发唤醒词！");
            // printf("model index:%d, word index:%d\n", res->wakenet_model_index, res->wake_word_index);
            ESP_LOGI(TAG, "唤醒词模型索引:%d, 唤醒词索引:%d", res->wakenet_model_index, res->wake_word_index);
        }
    }

    vTaskDelete(NULL);
}

void upload_Task(void *arg)
{
    upload_msg_t msg;
    esp_http_client_handle_t client = NULL;

    while (1)
    {
        if (xQueueReceive(frame_queue, &msg, portMAX_DELAY) == pdPASS)
        {
            switch (msg.type)
            {
            case UPLOAD_MSG_START:
                stream_upload_start(&client);
                break;

            case UPLOAD_MSG_DATA:
                stream_upload_write(&client, (char *)msg.data, msg.len);
                break;

            case UPLOAD_MSG_STOP:
                stream_upload_stop(&client);
                break;
            }
        }
    }
}

void wwd_task()
{
    i2s_mic_init();
    vad_ctx_t *vad = init_vad_mod();
    frame_queue = xQueueCreate(8, sizeof(upload_msg_t));
    xTaskCreatePinnedToCore(&feed_Task, "feed", 8 * 1024, (void *)vad, 5, NULL, 0);
    xTaskCreatePinnedToCore(&detect_Task, "detect", 8 * 1024, (void *)vad, 5, NULL, 1);

    xTaskCreatePinnedToCore(&upload_Task, "upload", 8 * 1024, NULL, 5, NULL, 0);
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
