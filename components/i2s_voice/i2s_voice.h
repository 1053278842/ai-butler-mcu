#ifndef I2S_VOICE_H
#define I2S_VOICE_H
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdint.h>
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "model_path.h"
#include <stdbool.h>
#include <math.h>
#include "esp_timer.h"

#include "esp_vadn_models.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
// #include "esp_board_init.h"

// #include "driver/i2s.h"
// #include "driver/i2s_common.h"

// #include "socket.h"
// #include "lwip/inet.h"
// #include "lwip/err.h"
// #include "lwip/sys.h"

#include "pcm_util.h"

// WAV header 信息
typedef struct
{
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint16_t channels;
} wav_info_t;

typedef struct
{
    wav_info_t wav_info;
    bool header_parsed;
} stream_ctx_t;

typedef struct
{
    char riff_id[4]; // "RIFF"
    uint32_t riff_size;
    char wave_id[4]; // "WAVE"
    char fmt_id[4];  // "fmt "
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data_id[4]; // "data"
    uint32_t data_size;
} wav_header_t;

typedef struct
{
    uint8_t *data;
    size_t length;
} wav_mem_t;

// 初始化
void i2s_spk_init(uint32_t sample_rate, uint16_t bits, uint16_t channels);
i2s_chan_handle_t i2s_mic_init();
// 解析wav
bool parse_wav_header(uint8_t *data, wav_info_t *info);

// http请求回调
esp_err_t voice_http_event_handler(esp_http_client_event_t *evt);

// wake word net 唤醒词任务
void wwd_task();
#endif // I2S_VOICE_H
