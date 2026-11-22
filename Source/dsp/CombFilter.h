#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace sfx
{

//==============================================================================
// MIDI 追踪的梳状滤波器（共振器）。
//
//   y[n] = x[n] + g · LP(y[n − D]),    D = fs / (f × ratio)
//
// 作用：PM 拉满之后输出是宽频的非谐波噪波，人耳抓不到音高。
// 把它送进一个延迟锁在 MIDI 周期上的梳状滤波器，所有噪点会被"强迫对齐"
// 到基频的整数倍上 —— 这就是 Riddim 那种金属喉音的来源。
//
// 极性是音色的分水岭：
//   g > 0（正反馈）→ 全整数倍谐波，厚重 / 锯齿 / 金属管
//   g < 0（负反馈）→ 只留奇次谐波，空心 / 方波感（Square4 味）
//
// 分数延迟用线性插值。不做的话高音区会明显走音——延迟必须是小数。
// 反馈环里串了一级低通做阻尼，否则高反馈会在超高频自己啸叫。
//
// 跑在 4x 过采样域内（192 kHz），40 Hz 时需要 4800 个延迟样本，
// 精度极高，开销可以忽略。
//==============================================================================
class CombFilter
{
public:
    void prepare (double sampleRate, int /*maxBlockSize*/)
    {
        sr = sampleRate;

        // 最低按 20 Hz 留缓冲：192k / 20 = 9600，取 16384 有余量
        bufferSize = 16384;
        buffer[0].assign ((size_t) bufferSize, 0.0f);
        buffer[1].assign ((size_t) bufferSize, 0.0f);

        setDampingHz (14000.0f);
        reset();
    }

    void reset()
    {
        std::fill (buffer[0].begin(), buffer[0].end(), 0.0f);
        std::fill (buffer[1].begin(), buffer[1].end(), 0.0f);
        dampState[0] = dampState[1] = 0.0f;
        writePos = 0;
    }

    void setMix (float mix01)        { mix = juce::jlimit (0.0f, 1.0f, mix01); }
    void setFeedback (float g)       { feedback = juce::jlimit (-0.98f, 0.98f, g); }
    void setDampingHz (float hz)     { dampingCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * hz / (float) sr); }

    /** 按音高设定延迟长度。hz 不变时每块重复调用是无害的。 */
    void setFrequency (float hz, float ratio = 1.0f) noexcept
    {
        if (hz <= 0.0f || bufferSize <= 0)
            return;

        const auto samples = sr / ((double) hz * (double) juce::jmax (0.01f, ratio));
        delaySamples = (float) juce::jlimit (2.0, (double) bufferSize - 3.0, samples);
    }

    float processSample (int channel, float x) noexcept
    {
        if (bufferSize <= 0)
            return x;

        const auto ch = (size_t) (channel & 1);
        auto& buf = buffer[ch];

        // 分数延迟读：线性插值，缺了它高音区会走音
        auto readPos = (float) writePos - delaySamples;

        while (readPos < 0.0f)
            readPos += (float) bufferSize;

        const auto index0 = (int) readPos;
        const auto frac   = readPos - (float) index0;
        const auto index1 = (index0 + 1) % bufferSize;

        const auto delayed = buf[(size_t) index0] + (buf[(size_t) index1] - buf[(size_t) index0]) * frac;

        // 反馈环里的阻尼
        dampState[ch] += dampingCoef * (delayed - dampState[ch]);

        const auto resonant = x + dampState[ch] * feedback;
        buf[(size_t) writePos] = resonant;

        if (ch == 0)
        {
            if (++writePos >= bufferSize)
                writePos = 0;
        }

        return x * (1.0f - mix) + resonant * mix;
    }

private:
    double sr         = 192000.0;
    int    bufferSize = 16384;
    int    writePos   = 0;

    std::vector<float> buffer[2];      // 左右独立，和插件其余部分一致
    float dampState[2] { 0.0f, 0.0f };

    float delaySamples = 100.0f;
    float mix          = 0.0f;
    float feedback     = 0.0f;
    float dampingCoef  = 0.3f;
};

} // namespace sfx
