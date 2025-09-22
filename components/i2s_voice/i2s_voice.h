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

// 初始化
void i2s_init(uint32_t sample_rate, uint16_t bits, uint16_t channels);

// 解析wav
bool parse_wav_header(uint8_t *data, wav_info_t *info);

// http请求回调
esp_err_t voice_http_event_handler(esp_http_client_event_t *evt);

#endif // I2S_VOICE_H
