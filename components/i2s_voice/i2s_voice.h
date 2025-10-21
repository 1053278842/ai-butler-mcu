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

#include "stream_upload.h"
// #include "esp_board_init.h"

// #include "driver/i2s.h"
// #include "driver/i2s_common.h"

// #include "socket.h"
// #include "lwip/inet.h"
// #include "lwip/err.h"
// #include "lwip/sys.h"

#include "pcm_util.h"
typedef struct
{
    esp_afe_sr_iface_t *handle;
    esp_afe_sr_data_t *data;
} vad_ctx_t;
typedef enum
{
    UPLOAD_MSG_START,
    UPLOAD_MSG_STOP,
    UPLOAD_MSG_DATA,
} upload_msg_type_t;

typedef struct
{
    upload_msg_type_t type;
    size_t len;
    uint8_t data[2048]; // or pointer
} upload_msg_t;

typedef struct
{
    bool uploading;
    int silence_ms;
} stream_upload_ctx_t;
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
void i2s_spk_init(uint32_t sample_rate, uint16_t bits, uint16_t channels);
i2s_chan_handle_t i2s_mic_init();

// http请求回调
esp_err_t voice_http_event_handler(esp_http_client_event_t *evt);

// wake word net 唤醒词任务
void wwd_task();
#endif // I2S_VOICE_H
