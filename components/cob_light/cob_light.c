#include "cob_light.h"
#include "driver/ledc.h"
#include <math.h>
#include <stdlib.h>

#define LEDC_MODE LEDC_HIGH_SPEED_MODE
#define LEDC_RESOLUTION LEDC_TIMER_12_BIT
#define LEDC_FREQ 5000
// 转换百分比到实际 duty 值
#define DUTY_PERCENT(res, percent) (((1 << (res)) - 1) * (percent) / 100)

// 内部函数：设置占空比
static void set_duty(cob_light_t *light, uint32_t duty)
{
    ledc_set_duty(LEDC_MODE, light->channel, duty);
    ledc_update_duty(LEDC_MODE, light->channel);
}

void reset_duty_range(cob_light_t *light, int min_percent, int max_percent)
{
    // 校验百分比输入
    if (min_percent < 0)
    {
        min_percent = 0;
    }
    if (max_percent > 100)
    {
        max_percent = 100;
    }
    if (min_percent > max_percent)
    {
        min_percent = max_percent;
    }

    light->min_duty = min_percent;
    light->max_duty = max_percent;
}

// 初始化 COB 灯带
void cob_light_init(cob_light_t *light, int gpio_num, int channel)
{
    light->gpio_num = gpio_num;
    light->channel = channel;
    light->timer = LEDC_TIMER_0; // 所有灯可共用一个 timer
    light->resolution = LEDC_RESOLUTION;
    light->freq_hz = LEDC_FREQ;
    light->task = NULL;
    light->stop_flag = false;
    light->phase = 0.0f;
    light->speed_multiplier = 1.0f; // 默认倍率 1

    reset_duty_range(light, 0, 100);

    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_MODE,
        .timer_num = light->timer,
        .duty_resolution = light->resolution,
        .freq_hz = light->freq_hz,
        .clk_cfg = LEDC_AUTO_CLK};
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t ch_conf = {
        .gpio_num = light->gpio_num,
        .speed_mode = LEDC_MODE,
        .channel = light->channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = light->timer,
        .duty = 0,
        .hpoint = 0};
    ledc_channel_config(&ch_conf);
}

// 关闭灯带（停止效果并熄灭）
void cob_light_off(cob_light_t *light)
{
    light->stop_flag = true;
    if (light->task)
    {
        vTaskDelete(light->task);
        light->task = NULL;
    }
    set_duty(light, 0);
}

// norm ∈ [0,1]，映射到 min_percent~max_percent 的 duty 值
static int duty_in_range(cob_light_t *light, float norm)
{
    int min_val = DUTY_PERCENT(light->resolution, light->min_duty);
    int max_val = DUTY_PERCENT(light->resolution, light->max_duty);
    return min_val + (int)((max_val - min_val) * norm);
}

// ----------------- 效果任务 -----------------

// 呼吸
static void breath_task(void *param)
{
    cob_light_t *light = (cob_light_t *)param;
    light->stop_flag = false;
    float t = 0;
    float speed = 0.005f; // 小 = 慢，大 = 快
    while (!light->stop_flag)
    {
        float norm = (sin(t + light->phase) + 1) / 2;

        float gamma = 2.2f;
        float corrected = powf(norm, gamma); // gamma校正后的亮度比例

        int duty = duty_in_range(light, corrected); // 归一化到 0~1
        set_duty(light, duty);
        float step = speed * light->speed_multiplier;
        // if (norm > 0.95f || norm < 0.05f)
        //     step *= 0.2f; // 峰值区域慢
        t += step;
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    set_duty(light, 0);
    vTaskDelete(NULL);
}

// 波纹
static void wave_task(void *param)
{
    cob_light_t *light = (cob_light_t *)param;
    light->stop_flag = false;
    float t = 0;
    while (!light->stop_flag)
    {
        float norm = (sin(t + light->phase) + sin((t + light->phase) * 1.5) + 2) / 4;

        float gamma = 2.2f;
        float corrected = powf(norm, gamma); // gamma校正后的亮度比例

        int duty = duty_in_range(light, corrected);
        set_duty(light, duty);
        float step = 0.01f * light->speed_multiplier;
        t += step;
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    set_duty(light, 0);
    vTaskDelete(NULL);
}

// 火焰闪烁
static void fire_task(void *param)
{
    cob_light_t *light = (cob_light_t *)param;
    light->stop_flag = false;
    while (!light->stop_flag)
    {
        float rand_norm = (rand() % 100) / 100.0f; // 0~1 随机数
        int duty = duty_in_range(light, rand_norm);

        set_duty(light, duty);
        vTaskDelay(pdMS_TO_TICKS(50 + rand() % 30));
    }
    set_duty(light, 0);
    vTaskDelete(NULL);
}

// 渐入渐出
static void fade_task(void *param)
{
    cob_light_t *light = (cob_light_t *)param;
    light->stop_flag = false;
    int steps = 5;
    while (!light->stop_flag)
    {
        for (int i = 0; i <= steps && !light->stop_flag; i++)
        {
            float norm = fmodf((float)i / steps + light->phase, 1.0f);
            int duty = duty_in_range(light, norm);
            set_duty(light, duty);

            vTaskDelay(pdMS_TO_TICKS((int)(20 / light->speed_multiplier)));
        }
        for (int i = steps; i >= 0 && !light->stop_flag; i--)
        {
            float norm = fmodf((float)i / steps + light->phase, 1.0f);
            int duty = duty_in_range(light, norm);
            set_duty(light, duty);

            vTaskDelay(pdMS_TO_TICKS((int)(20 / light->speed_multiplier)));
        }
    }
    set_duty(light, 0);
    vTaskDelete(NULL);
}

// ----------------- 启动接口 -----------------
void cob_light_breath(cob_light_t *light)
{
    cob_light_off(light);
    xTaskCreate(breath_task, "breath", 2048, light, 5, &light->task);
}

void cob_light_wave(cob_light_t *light)
{
    cob_light_off(light);
    xTaskCreate(wave_task, "wave", 2048, light, 5, &light->task);
}

void cob_light_fire(cob_light_t *light)
{
    cob_light_off(light);
    xTaskCreate(fire_task, "fire", 2048, light, 5, &light->task);
}

void cob_light_fade(cob_light_t *light)
{
    cob_light_off(light);
    xTaskCreate(fade_task, "fade", 2048, light, 5, &light->task);
}
