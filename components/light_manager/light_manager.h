#ifndef LIGHT_MANAGER_H
#define LIGHT_MANAGER_H

#include "cob_light.h"
#include "driver/ledc.h"

// 初始化所有 COB 灯带
void light_manager_init(void);

// 控制接口（字符串命令方式）
void light_manager_control(int id, const char *cmd);

// 设置灯带占空比范围（百分比）
void light_manager_set_duty_range(int id, int min_duty, int max_duty);

// 设置灯带相位（错开曲线的时钟）
void light_manager_set_phase_range(int id, float phase);

// 设置灯带速度倍率（大于0，1为正常速度）
void light_manager_set_speed_multiplier(int id, float speed_multiplier);
#endif
