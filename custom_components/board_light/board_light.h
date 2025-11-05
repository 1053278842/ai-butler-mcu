#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

#define BOARD_LIGHT_TAG "board_light"
#define BLINK_GPIO 2

void board_light_init(void);
void board_light_on(void);
void board_light_off(void);
void board_light_blink(int blink_interval_ms);