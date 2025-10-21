#include "i2s_voice.h"

#define I2S_NUM I2S_NUM_0
#define I2S_BCK_IO (16)
#define I2S_WS_IO (17)
#define I2S_DO_IO (18)
#define I2S_DI_IO (20)
#define TAG "AUDIO_PLAYER"

#define SAMPLE_RATE 16000
#define WWN_MODLE "hiesp"
#define EXAMPLE_BUFF_SIZE 1 * 1024 // 接收BUFF
#define MAX_RECORD_SEC 10

int16_t *ad_buffer_16 = NULL;
static size_t buffer_pos = 0;

volatile bool need_record = false;

static i2s_chan_handle_t spk_chan = NULL; // 使用新的 I2S 通道句柄
static i2s_chan_handle_t mic_chan = NULL; // 使用新的 I2S 通道句柄
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
/**
 * 创建 WAV 文件（支持 16位 / 32位 深度）
 * @param buf      音频数据指针（int16_t* 或 int32_t*）
 * @param samples  样本数量
 * @param sample_rate  采样率
 * @param bits_per_sample  每样本位深（16 或 32）
 */
static wav_mem_t create_wav_in_memory(const void *buf, size_t samples, int sample_rate, int bits_per_sample)
{
    wav_mem_t wav = {0};

    if (!buf || samples == 0 || (bits_per_sample != 16 && bits_per_sample != 32))
        return wav;

    const uint8_t *pcm_data = (const uint8_t *)buf;
    uint32_t bytes_per_sample = bits_per_sample / 8;
    uint32_t data_size = samples * bytes_per_sample;

    wav_header_t header;
    memcpy(header.riff_id, "RIFF", 4);
    memcpy(header.wave_id, "WAVE", 4);
    memcpy(header.fmt_id, "fmt ", 4);
    memcpy(header.data_id, "data", 4);

    header.fmt_size = 16;
    header.audio_format = 1; // PCM
    header.num_channels = 1;
    header.sample_rate = sample_rate;
    header.bits_per_sample = bits_per_sample;
    header.block_align = header.num_channels * bytes_per_sample;
    header.byte_rate = header.sample_rate * header.block_align;
    header.data_size = data_size;
    header.riff_size = 36 + data_size;

    wav.length = sizeof(wav_header_t) + data_size;
    wav.data = malloc(wav.length);
    if (!wav.data)
        return wav;

    memcpy(wav.data, &header, sizeof(wav_header_t));
    memcpy(wav.data + sizeof(wav_header_t), pcm_data, data_size);

    return wav;
}

void upload_wav_memory(const char *url, const uint8_t *data, size_t length)
{
    ESP_LOGI(TAG, "开始上传 WAV 数据, 大小: %d bytes", (int)length);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    // 手动构造 multipart/form-data 请求体
    const char *boundary = "----ESP32Boundary";
    char header[128];
    snprintf(header, sizeof(header),
             "multipart/form-data; boundary=%s", boundary);
    esp_http_client_set_header(client, "Content-Type", header);

    // ==== 构造 multipart 数据 ====
    char start[256];
    int start_len = snprintf(start, sizeof(start),
                             "--%s\r\n"
                             "Content-Disposition: form-data; name=\"file\"; filename=\"voice.wav\"\r\n"
                             "Content-Type: audio/wav\r\n\r\n",
                             boundary);

    const char *end_fmt = "\r\n--%s--\r\n";
    char end[64];
    int end_len = snprintf(end, sizeof(end), end_fmt, boundary);

    // 总长度
    size_t total_len = start_len + length + end_len;
    uint8_t *post_data = malloc(total_len);
    if (!post_data)
    {
        ESP_LOGE(TAG, "内存不足，无法构造上传包");
        esp_http_client_cleanup(client);
        return;
    }

    memcpy(post_data, start, start_len);
    memcpy(post_data + start_len, data, length);
    memcpy(post_data + start_len + length, end, end_len);

    esp_http_client_set_post_field(client, (const char *)post_data, total_len);

    // 执行上传
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "上传成功, 状态码 = %d",
                 esp_http_client_get_status_code(client));
    }
    else
    {
        ESP_LOGE(TAG, "上传失败: %s", esp_err_to_name(err));
    }

    free(post_data);
    esp_http_client_cleanup(client);
}

void send_audio_to_server(const void *buf, size_t samples, int bits_per_sample)
{
    wav_mem_t wav = create_wav_in_memory(buf, samples, SAMPLE_RATE, bits_per_sample);
    if (wav.data)
    {
        ESP_LOGI(TAG, "WAV内存数据大小: %zu bytes", wav.length);
        // upload_wav_memory("http://192.168.1.100:8080/upload", wav.data, wav.length);
        upload_wav_memory("http://121.36.251.16:7999/api/upload", wav.data, wav.length);

        free(wav.data);
    }
    else
    {
        ESP_LOGE(TAG, "内存分配失败，无法创建 WAV 数据");
    }
}

void wake_callbak()
{
    ESP_LOGI(TAG, "开始录音...");
    // ad_buffer = malloc(SAMPLE_RATE * MAX_RECORD_SEC * sizeof(int32_t));
    ad_buffer_16 = malloc(SAMPLE_RATE * MAX_RECORD_SEC * sizeof(int16_t));

    uint8_t *r_buf = (uint8_t *)calloc(1, EXAMPLE_BUFF_SIZE);
    assert(r_buf);
    size_t r_bytes = 0;

    size_t slient_samples_count = 0;
    size_t slient_max_samples = SAMPLE_RATE / (EXAMPLE_BUFF_SIZE / sizeof(int32_t)) * 3; // 3s 静音结束录音
    while (1)
    {
        if (i2s_channel_read(mic_chan, r_buf, EXAMPLE_BUFF_SIZE, &r_bytes, portMAX_DELAY) == ESP_OK)
        {
            // 样本指针：每个元素标识一个样本 r_buf 是 uint8_t*，这里要按 32 位整型解析
            int32_t *samples = (int32_t *)r_buf;
            // 样本数： 1024/4 = 256 个样本，每个样子占用4字节，其中只有24位有效
            int sample_count = r_bytes / sizeof(int32_t);

            // 1️⃣ 计算当前帧音量（RMS）
            float rms = pcm_calc_rms(samples, sample_count);
            // 平滑RMS，避免抖动
            rms = pcm_smooth_rms(rms);
            // 自动增益控制
            float gain = pcm_agc_get_gain(rms);
            // 麻痹哦太难控制增益了，不搞了日
            // float gain = 1.0f;

            double sum = 0;
            for (int i = 0; i < sample_count; i++)
            {
                int32_t sample = samples[i];
                pcm_amplify(&sample, gain);
                int16_t pcm16 = pcm32_to_pcm16(sample);

                float f = (float)pcm16 / 32768.0f; // 归一化到 -1.0 ~ 1.0
                sum += f * f;

                ad_buffer_16[buffer_pos++] = pcm16;
                if (buffer_pos >= SAMPLE_RATE * MAX_RECORD_SEC)
                {
                    ESP_LOGI(TAG, "缓冲区满，强制结束录音，共 %d 样本 (%.2f秒)", (int)buffer_pos, buffer_pos / (float)SAMPLE_RATE);
                    buffer_pos = 0;
                }
            }

            float rms_16 = sqrt(sum / sample_count) * 32768.0f;
            ESP_LOGI("TAG", "rms:%.2f,rms16:%.2f", rms, rms_16);
            if (rms < 800)
            {
                // ESP_LOGI(TAG, "环境音, 样本数: %d, 音频值：%.2f, 静音样本数：%zu", buffer_pos, rms_16, slient_samples_count);

                slient_samples_count += 1;
                if (slient_samples_count >= slient_max_samples && need_record == true) // 静音超过3秒
                {
                    // ESP_LOGI(TAG, "静音超过3秒，录音结束，共 %d 样本 (%.2f秒)", (int)buffer_pos, buffer_pos / (float)SAMPLE_RATE);

                    if (buffer_pos > 0)
                    {
                        // 发送给服务器（内部会生成 wav 并上传）
                        // send_audio_to_server(ad_buffer, buffer_pos, 32);
                        send_audio_to_server(ad_buffer_16, buffer_pos, 16);
                    }
                    // 清空录音缓冲，准备下一次
                    buffer_pos = 0;
                    slient_samples_count = 0;
                    need_record = false;
                    // free(ad_buffer_16);
                    // ad_buffer_16 = NULL;
                    // ESP_LOGI(TAG, "录音已完成，等待下一次唤醒");
                    // return; // 结束录音任务
                }
            }
            else
            {
                // ESP_LOGI(TAG, "检测到声音, 样本数: %d, 音频值：%.2f,静音样本数：%zu", buffer_pos, rms_16, slient_samples_count);
                slient_samples_count = 0; // 有声音，重置静音计数
                need_record = true;
            }
        }
        else
        {
            ESP_LOGE(TAG, "Read Task: i2s read failed\n");
        }
    }
    free(r_buf);
    vTaskDelete(NULL);
}

vad_ctx_t *init_vad_mod()
{
    srmodel_list_t *models = esp_srmodel_init("model");
    afe_config_t *afe_config = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    afe_config->vad_min_noise_ms = 1000; // The minimum duration of noise or silence in ms.
    afe_config->vad_min_speech_ms = 256; // The minimum duration of speech in ms.
    afe_config->vad_mode = VAD_MODE_1;   // The larger the mode, the higher the speech trigger probability.
    // afe_config->agc_mode = 2;            // 启用更强的自动增益控制（AGC）

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

void start_stream_upload(esp_http_client_handle_t *client)
{
    ESP_LOGI("UPLOAD", "打开HTTP连接!");
    if (*client == NULL)
    {
        esp_http_client_config_t config = {
            .url = "http://121.36.251.16:7999/api/stream_upload",
            .method = HTTP_METHOD_POST,
            .timeout_ms = 30000,
        };
        *client = esp_http_client_init(&config);
    }

    esp_http_client_set_header(*client, "Content-Type", "audio/pcm");
    esp_http_client_set_header(*client, "Transfer-Encoding", "chunked");
    esp_http_client_set_header(*client, "Connection", "keep-alive");

    esp_err_t err = esp_http_client_open(*client, -1);
    if (err != ESP_OK)
    {
        ESP_LOGE("UPLOAD", "打开HTTP连接失败: %s", esp_err_to_name(err));
        return;
    }
}

void stop_stream_upload(esp_http_client_handle_t *client)
{
    ESP_LOGI("UPLOAD", "关闭HTTP连接!");
    const char *end_chunk = "0\r\n\r\n";
    esp_http_client_write(*client, end_chunk, strlen(end_chunk));

    esp_http_client_close(*client);
    esp_http_client_cleanup(*client);
    *client = NULL;
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

                // start_stream_upload(&ctx, afe_chunksize);
            }
            ctx.silence_ms = 0;
        }
        else
        {
            if (ctx.uploading)
            {
                ctx.silence_ms += 32;
                if (ctx.silence_ms > 300)
                {
                    ESP_LOGI(TAG, "检测到静音，结束流式上传");
                    // stop_stream_upload(&ctx);
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
                start_stream_upload(&client);
                break;

            case UPLOAD_MSG_DATA:
                ESP_LOGI("UPLOAD", "数据输入中!");
                esp_http_client_write(client, (const char *)msg.data, msg.len);
                break;

            case UPLOAD_MSG_STOP:
                stop_stream_upload(&client);
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
