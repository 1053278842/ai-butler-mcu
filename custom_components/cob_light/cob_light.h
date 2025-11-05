#ifndef COB_LIGHT_H
#define COB_LIGHT_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// COB 灯带对象
typedef struct
{
    int gpio_num;            // GPIO
    int channel;             // LEDC 通道
    int timer;               // 使用的 timer
    int resolution;          // 分辨率
    int freq_hz;             // PWM 频率
    TaskHandle_t task;       // 当前效果任务
    volatile bool stop_flag; // 停止标志
    int min_duty;            // 最小占空比
    int max_duty;            // 最大占空比
    float phase;             // 相位（错开曲线的时钟）
    float speed_multiplier;  // 播放速度倍率
} cob_light_t;

// 初始化 COB 灯带
void cob_light_init(cob_light_t *light, int gpio_num, int channel);

// 关闭灯带（停止效果并熄灭）
void cob_light_off(cob_light_t *light);

// 启动不同效果
void cob_light_breath(cob_light_t *light);
void cob_light_wave(cob_light_t *light);
void cob_light_fire(cob_light_t *light);
void cob_light_fade(cob_light_t *light);

void reset_duty_range(cob_light_t *light, int min_percent, int max_percent);
#endif // COB_LIGHT_H
