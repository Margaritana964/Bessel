#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace sfx
{

//==============================================================================
// 输入包络跟随 → AM 增益。
//
// 这不是二值开关（门），也不是"干湿混合"，而是把外部音频的音量包络
// 直接乘到输出上，**永远 100% 生效**：
//
//   · 没输入   → 包络 0 → 输出静音（"没输入就不出声"是结构决定的，不是参数决定的）
//   · 输入响亮 → 载波满幅；输入轻 → 载波也跟着轻
//   · Gain 旋钮只是把这个包络整体放大 / 缩小，不会放干信号漏出来
//
// 检测点必须在干信号上：输出里永远有载波，拿它当判据会自己撑住自己。
//
// 攻击 / 释放决定性格：
//   快起快落 = 打击感，会听到每个正峰
//   慢起慢落 = 呼吸感，接近 pad / 环境
//
// 释放时间不能太短：110 Hz 的正弦周期约 9 ms，释放短于它包络就会带 220 Hz 的纹波，
// 那是音频速率的 AM，会多出边带（有时是想要的，但不该是默认值）。
//==============================================================================
class InputEnvelope
{
public:
    void prepare (double sampleRate, int maxBlockSize)
    {
        sr = sampleRate;

        gains.assign ((size_t) juce::jmax (1, maxBlockSize), 0.0f);
        envs .assign ((size_t) juce::jmax (1, maxBlockSize), 0.0f);

        setGainDb (0.0f);
        setAttackMs (5.0f);
        setReleaseMs (250.0f);

        reset();
    }

    void reset()
    {
        envelope = 0.0f;
        std::fill (gains.begin(), gains.end(), 0.0f);
        std::fill (envs .begin(), envs .end(), 0.0f);
    }

    /** 包络的整体增益：+ 用来把安静素材的纹理提起来，- 用来压。 */
    void setGainDb (float db)       { gainLinear = juce::Decibels::decibelsToGain (db); }
    void setAttackMs (float ms)     { attackCoef  = coefForSeconds (juce::jmax (0.5,  (double) ms) * 0.001); }
    void setReleaseMs (float ms)    { releaseCoef = coefForSeconds (juce::jmax (20.0, (double) ms) * 0.001); }

    /** 用输入块算出逐采样的 AM 增益，之后再乘到**湿信号**上。 */
    void process (const juce::AudioBuffer<float>& input) noexcept
    {
        if (gains.empty())
            return;

        const auto numChannels = input.getNumChannels();
        const auto numSamples  = juce::jmin (input.getNumSamples(), (int) gains.size());

        for (int i = 0; i < numSamples; ++i)
        {
            auto peak = 0.0f;

            for (int ch = 0; ch < numChannels; ++ch)
                peak = juce::jmax (peak, std::abs (input.getReadPointer (ch)[i]));

            // 峰值检波 + 单极点平滑
            envelope = peak > envelope
                         ? attackCoef  * envelope + (1.0f - attackCoef)  * peak
                         : releaseCoef * envelope + (1.0f - releaseCoef) * peak;

            // 没有 (1 - depth) 这种"漏一点干载波"的项，这就是恒满的 AM
            gains[(size_t) i] = envelope * gainLinear;

            // 归一化后的包络（0–1，不带 Gain）：给调制矩阵当源用，
            // 这样 Gain 旋钮只影响音量，不会顺手改掉调制深度。
            envs[(size_t) i] = juce::jlimit (0.0f, 1.0f, envelope);
        }

        for (int i = numSamples; i < (int) gains.size(); ++i)
        {
            gains[(size_t) i] = gains[(size_t) juce::jmax (0, numSamples - 1)];
            envs [(size_t) i] = envs [(size_t) juce::jmax (0, numSamples - 1)];
        }
    }

    const std::vector<float>& getGains() const noexcept { return gains; }

    /** 归一化包络（0–1），给调制用。 */
    const std::vector<float>& getEnvelope() const noexcept { return envs; }

    /** 给界面显示用：当前归一化包络（0–1）。 */
    float getCurrentEnvelope() const noexcept
    {
        return envs.empty() ? 0.0f : envs.back();
    }

    /** 给界面显示用：当前 AM 增益（0 = 静音，>1 = 包络被提起）。 */
    float getCurrentGain() const noexcept
    {
        return gains.empty() ? 0.0f : gains.back();
    }

private:
    float coefForSeconds (double seconds) const noexcept
    {
        const auto samples = juce::jmax (1.0, sr * seconds);
        return (float) std::exp (-1.0 / samples);
    }

    double sr = 48000.0;

    std::vector<float> gains;
    std::vector<float> envs;      // 归一化包络，调制源的输入

    float envelope    = 0.0f;
    float gainLinear  = 1.0f;
    float attackCoef  = 0.9f;
    float releaseCoef = 0.9f;
};

} // namespace sfx
