
#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// 计算样本平均方差rms
float pcm_calc_rms(int32_t *samples, size_t n);

// 自动增益
float pcm_agc_get_gain(float rms);

// 平滑RMS，避免抖动
float pcm_smooth_rms(float rms);

// pcm放大
void pcm_amplify(int32_t *sample, float gain);

// 32位转16位
int16_t pcm32_to_pcm16(int32_t sample);