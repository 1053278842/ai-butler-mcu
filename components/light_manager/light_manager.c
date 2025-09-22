#include "light_manager.h"
#include <string.h>

static cob_light_t cob1;
static cob_light_t cob2;

void light_manager_init(void)
{
    cob_light_init(&cob1, 15, LEDC_CHANNEL_0);
    cob_light_init(&cob2, 16, LEDC_CHANNEL_1);
}

void light_manager_control(int id, const char *cmd)
{
    printf("light_manager_control.cmd = %s\n", cmd);
    cob_light_t *target = (id == 1) ? &cob1 : &cob2;

    if (strcmp(cmd, "off") == 0)
        cob_light_off(target);
    else if (strcmp(cmd, "breath") == 0)
    {
        printf("strcmp.breath \n");
        cob_light_breath(target);
    }
    else if (strcmp(cmd, "wave") == 0)
        cob_light_wave(target);
    else if (strcmp(cmd, "fire") == 0)
        cob_light_fire(target);
    else if (strcmp(cmd, "fade") == 0)
        cob_light_fade(target);
}

void light_manager_set_duty_range(int id, int min_duty, int max_duty)
{
    reset_duty_range((id == 1) ? &cob1 : &cob2, min_duty, max_duty);
}

void light_manager_set_phase_range(int id, float phase)
{
    if (id == 1)
        cob1.phase = phase;
    else
        cob2.phase = phase;
}

void light_manager_set_speed_multiplier(int id, float speed_multiplier)
{
    if (id == 1)
        cob1.speed_multiplier = speed_multiplier;
    else
        cob2.speed_multiplier = speed_multiplier;
}