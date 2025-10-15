
#include "pcm_util.h"

const float silence_threshold = 2000.0f; // 小于此值认为是环境静音
const float target_rms = 7000.0f;        // 理想RMS目标值
float gain = 10.0f;                      // 初始增益
const float adjust_rate = 0.05f;         // 增益调整速率（越小越平滑）
const int silence_frames = 60;           // 连续多少帧静音才衰减增益(大概2s)
int silence_count = 0;

float s_rms = 0;
const float smooth_alpha = 0.1f; // 平滑系数，越小越平滑（0.05~0.2）
// ==================== RMS计算函数 ====================
float pcm_calc_rms(int32_t *samples, size_t n)
{
    double sum = 0;
    for (size_t i = 0; i < n; i++)
    {
        // 取高24位有效值后再计算能量
        int32_t s = samples[i];
        int16_t s16 = pcm32_to_pcm16(s);
        sum += (double)s16 * s16;
    }
    return sqrt(sum / n);
}

// 放大pcm样本
void pcm_amplify(int32_t *sample, float gain)
{
    int64_t temp = (int64_t)(*sample * gain);
    if (temp > INT32_MAX)
        temp = INT32_MAX;
    else if (temp < INT32_MIN)
        temp = INT32_MIN;

    *sample = (int32_t)temp;
}

// 32位转16位
int16_t pcm32_to_pcm16(int32_t sample)
{
    sample = sample >> 8;                 // 舍弃掉低位，保留有效高位24位
    float f = (float)sample / 8388608.0f; // 归一化 2^24
    int16_t pcm16 = (int16_t)(f * 32768); // 放大到16位 2^16
    return pcm16;
}

float pcm_agc_get_gain(float rms)
{
    // 平滑RMS，避免抖动

    // 2️⃣ 动态调整增益（AGC逻辑）
    if (rms > silence_threshold) // 避免静音时除以0
    {
        // 根据目标值自动调整增益比例
        float ratio = target_rms / rms;
        // 使用平滑更新避免抖动（指数平均法）
        gain = gain * (1.0f - adjust_rate) + gain * ratio * adjust_rate;
        silence_count = 0; // 重置静音计数
    }
    else
    {
        silence_count++;
        if (silence_count > silence_frames)
        {
            // 慢慢衰减增益，避免放大底噪
            gain *= 0.98f; // 每帧降低2%
        }
    }

    // 限制增益范围，避免过大过小
    if (gain < 10.0f)
        gain = 10.0f;

    gain = 10.0f;
    return gain;
}

float pcm_smooth_rms(float rms)
{
    s_rms = s_rms * (1.0f - smooth_alpha) + rms * smooth_alpha;
    return s_rms;
}