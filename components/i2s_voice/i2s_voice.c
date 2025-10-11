#include "i2s_voice.h"

#define I2S_NUM I2S_NUM_0
#define I2S_BCK_IO (16)
#define I2S_WS_IO (17)
#define I2S_DO_IO (18)
#define I2S_DI_IO (20)
#define TAG "AUDIO_PLAYER"

#define SAMPLE_RATE 16000
#define I2S_BUF_LEN 64
#define WWN_MODLE "hiesp"
#define VAD_THRESHOLD 5000

#define MAX_RECORD_SEC 5
#define SILENCE_THRESHOLD 0.65f // 声音阈值，降低以提高灵敏度
#define BIT_DEPTH 24

#define CONFIG_EXAMPLE_BIT_SAMPLE 32
#define CONFIG_EXAMPLE_SAMPLE_RATE 44100
#define NUM_CHANNELS (1) // For mono recording only!
#define SAMPLE_SIZE (CONFIG_EXAMPLE_BIT_SAMPLE * 32)
#define BYTE_RATE (CONFIG_EXAMPLE_SAMPLE_RATE * (CONFIG_EXAMPLE_BIT_SAMPLE / 8)) * NUM_CHANNELS

static int64_t last_loud_time = 0;
static float *audio_buffer = NULL;
static size_t buffer_pos = 0;
static int active = 0;
static int silence_samples = 0;
static int64_t recording_start_time = 0; // 录音开始时间
static bool recording_enabled = true;    // 录音使能标志
int32_t r_buf[SAMPLE_SIZE + 32];

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

    // i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    // chan_cfg.dma_frame_num = 1024;
    // i2s_new_channel(&chan_cfg, NULL, &mic_chan);

    // 初始化或更新声道和时钟配置
    i2s_std_config_t std_cfg = {
        .clk_cfg =
            {
                .clk_src = I2S_CLK_SRC_DEFAULT,
                .ext_clk_freq_hz = 0,
                .mclk_multiple = I2S_MCLK_MULTIPLE_512,
                .sample_rate_hz = 44100,
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
    // chan_cfg.dma_frame_num = 1024;

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &mic_chan));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(mic_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(mic_chan));

    return mic_chan;
}

wav_mem_t create_wav_in_memory(const int16_t *pcm_data, size_t samples, int sample_rate)
{
    const int num_channels = 1;
    const int bits_per_sample = 16;
    const int byte_rate = sample_rate * num_channels * bits_per_sample / 8;
    const int block_align = num_channels * bits_per_sample / 8;
    const int data_size = samples * block_align;
    const int total_size = 44 + data_size;

    uint8_t *wav = malloc(total_size);
    if (!wav)
    {
        wav_mem_t empty = {NULL, 0};
        return empty;
    }

    // === 填写 WAV Header ===
    memcpy(wav + 0, "RIFF", 4);
    *(uint32_t *)(wav + 4) = total_size - 8; // 文件大小-8字节
    memcpy(wav + 8, "WAVE", 4);
    memcpy(wav + 12, "fmt ", 4);
    *(uint32_t *)(wav + 16) = 16; // fmt 块大小
    *(uint16_t *)(wav + 20) = 1;  // PCM 格式
    *(uint16_t *)(wav + 22) = num_channels;
    *(uint32_t *)(wav + 24) = sample_rate;
    *(uint32_t *)(wav + 28) = byte_rate;
    *(uint16_t *)(wav + 32) = block_align;
    *(uint16_t *)(wav + 34) = bits_per_sample;
    memcpy(wav + 36, "data", 4);
    *(uint32_t *)(wav + 40) = data_size;

    // === 拷贝音频数据 ===
    memcpy(wav + 44, pcm_data, data_size);

    wav_mem_t result = {
        .data = wav,
        .length = total_size};
    return result;
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
                             "Content-Disposition: form-data; name=\"file\"; filename=\"voice_%ld.wav\"\r\n"
                             "Content-Type: audio/wav\r\n\r\n",
                             boundary, (long)esp_log_timestamp());

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

void send_audio_to_server(int16_t *buf, size_t len)
{
    wav_mem_t wav = create_wav_in_memory(buf, len, SAMPLE_RATE);
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

// 初始化音频缓冲区
void init_audio_buffer()
{
    size_t buffer_size = SAMPLE_RATE * MAX_RECORD_SEC * sizeof(float);
    audio_buffer = heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (!audio_buffer)
    {
        ESP_LOGE(TAG, "音频缓冲区分配失败! 需要 %d 字节", buffer_size);
    }
    else
    {
        memset(audio_buffer, 0, buffer_size);
        ESP_LOGI(TAG, "音频缓冲区初始化完成: %.2f KB", buffer_size / 1024.0);
    }
}
// 把归一化 float[-1.0,1.0] 转为 int16 PCM
static void float_to_int16(const float *in, int16_t *out, size_t samples)
{
    for (size_t i = 0; i < samples; ++i)
    {
        float v = in[i];

        // 简单的边界检查
        if (v > 1.0f)
            v = 1.0f;
        if (v < -1.0f)
            v = -1.0f;

        // 简单的转换
        out[i] = (int16_t)(v * 32767.0f);
    }
}

// 处理采样
void process_sample(int32_t raw)
{
    // 检查录音是否使能
    if (!recording_enabled)
    {
        return;
    }

    // 简化的24bit转float
    float norm = (float)raw / 8388608.0f;

    // 调试信息：每1000个样本检查一次数据范围
    if (buffer_pos % 1000 == 0)
    {
        ESP_LOGI(TAG, "样本%d: 原始=0x%08X (%d), 归一化=%.6f", buffer_pos, raw, raw, norm);
    }

    // 临时：打印前100个样本的详细信息用于调试
    if (buffer_pos < 100)
    {
        ESP_LOGI(TAG, "调试样本%d: 原始=0x%08X (%d), 归一化=%.6f", buffer_pos, raw, raw, norm);
    }

    // 保存数据 - 修复缓冲区溢出问题
    if (buffer_pos < SAMPLE_RATE * MAX_RECORD_SEC)
    {
        audio_buffer[buffer_pos] = norm;
        buffer_pos++;
    }
    else
    {
        ESP_LOGW(TAG, "音频缓冲区已满，停止录音！当前样本数: %d", buffer_pos);
        // 缓冲区满了，直接结束录音并上传
        ESP_LOGI(TAG, "缓冲区满，强制结束录音，共 %d 样本 (%.2f秒)", (int)buffer_pos, buffer_pos / (float)SAMPLE_RATE);

        if (buffer_pos > 0)
        {
            // 分配 int16_t 缓冲并把 float -> int16
            int16_t *pcm16 = malloc(buffer_pos * sizeof(int16_t));
            if (!pcm16)
            {
                ESP_LOGE(TAG, "无法分配 pcm16 缓冲 (%zu bytes)", buffer_pos * sizeof(int16_t));
            }
            else
            {
                float_to_int16(audio_buffer, pcm16, buffer_pos);

                // 发送给服务器（内部会生成 wav 并上传）
                send_audio_to_server(pcm16, buffer_pos);

                free(pcm16);
            }
        }

        // 清空录音缓冲，准备下一次
        buffer_pos = 0;
        active = 0;
        silence_samples = 0;
        recording_start_time = 0; // 重置录音开始时间

        // 暂时禁用录音，避免死循环
        recording_enabled = false;
        ESP_LOGI(TAG, "录音已禁用，等待重新启动");

        return; // 缓冲区满了，停止处理
    }

    int64_t now = esp_timer_get_time(); // 获取当前时间 (微秒)
    // 检测声音
    if (fabsf(norm) > SILENCE_THRESHOLD)
    {
        if (!active) // 第一次检测到声音
        {
            ESP_LOGI(TAG, "开始检测到声音, 样本数: %d, 音频值：%.6f", buffer_pos, fabsf(norm));
        }
        active = 1;
        last_loud_time = now; // 有声音时更新时间戳
    }
    else if (buffer_pos % 1000 == 0) // 每1000个样本打印一次调试信息
    {
        ESP_LOGI(TAG, "样本数: %d, 当前音频值: %.6f, 阈值: %.6f, active: %d", buffer_pos, fabsf(norm), SILENCE_THRESHOLD, active);
    }

    // 初始化录音开始时间
    if (buffer_pos == 1 && recording_start_time == 0)
    {
        recording_start_time = now;
        ESP_LOGI(TAG, "开始录音，时间戳: %lld", recording_start_time);
    }

    // 修改逻辑：录音至少持续2秒，然后检测静音
    if (active || (now - recording_start_time) < 2 * 1000000) // 录音至少2秒
    {
        // 检查是否超过3秒静音（只有在录音超过2秒后才检查）
        if (active && (now - last_loud_time) > 3 * 1000000) // 超过3秒没声音
        {
            ESP_LOGI(TAG, "静音超过3秒，录音结束，共 %d 样本 (%.2f秒)", (int)buffer_pos, buffer_pos / (float)SAMPLE_RATE);

            // 检查是否有足够的录音数据（至少1秒）
            if (buffer_pos < SAMPLE_RATE * 1) // 至少1秒的录音
            {
                ESP_LOGW(TAG, "录音时长太短（%.2f秒），丢弃录音数据", buffer_pos / (float)SAMPLE_RATE);
                buffer_pos = 0;
                active = 0;
                silence_samples = 0;
                return;
            }

            if (buffer_pos > 0)
            {
                // 分配 int16_t 缓冲并把 float -> int16
                int16_t *pcm16 = malloc(buffer_pos * sizeof(int16_t));
                if (!pcm16)
                {
                    ESP_LOGE(TAG, "无法分配 pcm16 缓冲 (%zu bytes)", buffer_pos * sizeof(int16_t));
                }
                else
                {
                    float_to_int16(audio_buffer, pcm16, buffer_pos);

                    // 发送给服务器（内部会生成 wav 并上传）
                    send_audio_to_server(pcm16, buffer_pos);

                    free(pcm16);
                }
            }
            // 清空录音缓冲，准备下一次
            buffer_pos = 0;
            active = 0;
            silence_samples = 0;
            recording_start_time = 0; // 重置录音开始时间

            // 禁用录音，等待重新启动
            recording_enabled = false;
            ESP_LOGI(TAG, "录音已完成，已禁用等待重新启动");
        }
    }
}

void wake_callbak()
{
#define EXAMPLE_BUFF_SIZE 1 * 1024 // 接收BUFF
    ESP_LOGI(TAG, "开始录音...");
    i2s_mic_init();

    // 初始化UDP主机地址参数
    int soock = -1;
    struct sockaddr_in client_addr;
    // client_addr.sin_addr.s_addr = inet_addr("192.168.88.250");
    client_addr.sin_addr.s_addr = inet_addr("192.168.100.6");
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(7456);

    // 创建UDP套接字
    soock = socket(AF_INET, SOCK_DGRAM, 0);
    if (soock < 0)
    {
        ESP_LOGE("UDP_CLIENT", "UDP_CLIENT套接字创建失败！\r\n");
    }
    ESP_LOGI("UDP_CLIENT", "UDP_CLIENT套接字创建成功！\r\n");

    uint8_t *r_buf = (uint8_t *)calloc(1, EXAMPLE_BUFF_SIZE);
    assert(r_buf); // Check if r_buf allocation success
    size_t r_bytes = 0;

    // udp_do_init();
    // uint16_t cnt = 0;
    while (1)
    {
        if (i2s_channel_read(mic_chan, r_buf, EXAMPLE_BUFF_SIZE, &r_bytes, portMAX_DELAY) == ESP_OK)
        {
            // r_buf 是 uint8_t*，这里要按 32 位整型解析
            int32_t *samples = (int32_t *)r_buf;
            // 2048/32 = 512 个样本，每个样子占用4字节，其中只有24位有效
            int sample_count = r_bytes / sizeof(int32_t);

            // int64_t sum = 0;
            // for (int i = 0; i < sample_count; i++)
            // {
            //     sum += llabs(samples[i]); // 用 llabs 防止溢出
            // }

            // int64_t avg = sum / sample_count;
            // ESP_LOGI(TAG, "Average amplitude: %lld", avg);

            const float gain = 4.0f; // 📢 调节这个倍数来放大音量，建议 8~16
            // 🎚️ 软件放大处理
            for (int i = 0; i < sample_count; i++)
            {
                int64_t v = (int64_t)(samples[i] * gain);
                if (v > INT32_MAX)
                    v = INT32_MAX;
                else if (v < INT32_MIN)
                    v = INT32_MIN;
                samples[i] = (int32_t)v;
            }

            esp_err_t ret = sendto(soock, (uint8_t *)r_buf, r_bytes, 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
            if (ret < 0)
            {
                ESP_LOGE(TAG, "UDP发送失败!, %d", ret);
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

    int num = 0;
    while (num < 5)
    {
        size_t bytes_read = 0;
        i2s_channel_read(mic_chan, buf, sizeof(buf), &bytes_read, portMAX_DELAY);

        if (!wakenet->detect(wn_data, buf))
        {
            ESP_LOGI(TAG, "唤醒成功!");
            stop_play_flag = true;
            num++;
            ESP_LOGD(TAG, "第 %d 次唤醒!", num);
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
