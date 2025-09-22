#include "board_light.h"
static TaskHandle_t s_blink_task_handle = NULL;
static uint8_t s_led_state = 0;
static int s_blink_interval = 0;

void board_light_init(void)
{
    // 配置 GPIO 作为输出
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    board_light_off();
}

void board_light_off(void)
{
    if (s_blink_task_handle != NULL)
    {
        vTaskDelete(s_blink_task_handle); // 停止闪烁任务
        s_blink_task_handle = NULL;
    }

    gpio_set_level(BLINK_GPIO, 0);
    s_led_state = 0;
}

void board_light_on(void)
{
    if (s_blink_task_handle != NULL)
    {
        vTaskDelete(s_blink_task_handle); // 停止闪烁任务
        s_blink_task_handle = NULL;
    }

    gpio_set_level(BLINK_GPIO, 1);
    s_led_state = 1;
}

// 闪烁任务
static void board_light_blink_task(void *param)
{
    int delay_ms = s_blink_interval;

    while (1)
    {
        gpio_set_level(BLINK_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        gpio_set_level(BLINK_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

void board_light_blink(int blink_interval_ms)
{
    // 如果已有闪烁任务，先删除
    if (s_blink_task_handle != NULL)
    {
        vTaskDelete(s_blink_task_handle);
        s_blink_task_handle = NULL;
    }

    s_blink_interval = blink_interval_ms;
    xTaskCreate(board_light_blink_task, "led_blink", 2048, NULL, 5, &s_blink_task_handle);
}