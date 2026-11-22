#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

namespace sfx
{

//==============================================================================
// 单声道音高跟踪：YIN 的累积均值归一化差分函数。
//
// 为什么不用过零率：噪声、泛音、几个周期的不稳定都会让它跳八度。
// YIN 的归一化把"第 k 个谷"压下去，八度错误明显少。
//
// 开销：每 hopSamples 个样本跑一帧，一帧 O(windowSize × tauMax) 次乘加。
// 48 kHz、W=1024、tauMax=960、hop=512 时约 9 千万次/秒 —— 单核几个百分点。
// 只在 Follow 打开时才喂它，关掉就完全不跑。
//
// 实时安全：所有缓冲在 prepare() 分配，push() 里没有 malloc。
//==============================================================================
class PitchTracker
{
public:
    static constexpr float minHz = 50.0f;
    static constexpr float maxHz = 1200.0f;

    void prepare (double sampleRate)
    {
        sr = sampleRate;

        windowSize = 1024;
        hopSamples = 512;

        tauMin = juce::jmax (2, (int) std::lround (sr / (double) maxHz));
        tauMax = juce::jmin (windowSize - 2, (int) std::lround (sr / (double) minHz));

        const auto ringSize = windowSize + tauMax + 8;

        ring    .assign ((size_t) ringSize, 0.0f);
        history .assign ((size_t) (windowSize + tauMax), 0.0f);
        diff    .assign ((size_t) tauMax + 2, 0.0f);
        normalised.assign ((size_t) tauMax + 2, 1.0f);

        reset();
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);

        writePos      = 0;
        filled        = 0;
        sinceAnalysis = 0;
        currentHz     = 0.0f;
        confidence    = 0.0f;
    }

    /** 喂基础采样率的干信号。内部会混合成单声道。 */
    void push (const juce::AudioBuffer<float>& buffer)
    {
        const auto numSamples  = buffer.getNumSamples();
        const auto numChannels = buffer.getNumChannels();

        if (numChannels <= 0 || ring.empty())
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            float mixed = 0.0f;

            for (int ch = 0; ch < numChannels; ++ch)
                mixed += buffer.getReadPointer (ch)[i];

            ring[(size_t) writePos] = mixed / (float) numChannels;

            if (++writePos >= (int) ring.size())
                writePos = 0;

            if (filled < (int) ring.size())
                ++filled;

            if (++sinceAnalysis >= hopSamples)
            {
                sinceAnalysis = 0;

                if (filled >= windowSize + tauMax)
                    analyse();
            }
        }
    }

    /** 0 表示这一帧没跟到周期（静音或噪声）。 */
    float getFrequencyHz() const noexcept   { return currentHz; }
    float getConfidence()  const noexcept   { return confidence; }

private:
    void analyse()
    {
        const int W    = windowSize;
        const int need = W + tauMax;

        // 环形缓冲 → 连续，最老的在前
        int start = writePos - need;

        while (start < 0)
            start += (int) ring.size();

        for (int i = 0; i < need; ++i)
            history[(size_t) i] = ring[(size_t) ((start + i) % (int) ring.size())];

        double energy = 0.0;

        for (int j = 0; j < W; ++j)
            energy += (double) history[(size_t) j] * (double) history[(size_t) j];

        // 太安静就别硬猜
        if (std::sqrt (energy / (double) W) < 1.0e-4)
        {
            currentHz  = 0.0f;
            confidence = 0.0f;
            return;
        }

        // 差分函数
        const auto* a = history.data();

        diff[0] = 0.0f;

        for (int tau = 1; tau <= tauMax; ++tau)
        {
            const auto* b = a + tau;
            double sum = 0.0;

            for (int j = 0; j < W; ++j)
            {
                const auto d = (double) a[j] - (double) b[j];
                sum += d * d;
            }

            diff[(size_t) tau] = (float) sum;
        }

        // 累积均值归一化：把靠前的高能量谷压下去，抑制八度错误
        double running = 0.0;
        int    tau     = -1;

        for (int t = 1; t <= tauMax; ++t)
        {
            running += (double) diff[(size_t) t];

            normalised[(size_t) t] = running > 0.0
                                       ? (double) diff[(size_t) t] * (double) t / running
                                       : 1.0;
        }

        constexpr double threshold = 0.20;

        for (int t = tauMin; t <= tauMax; ++t)
        {
            if (normalised[(size_t) t] < threshold)
            {
                tau = t;
                break;
            }
        }

        if (tau < 0)
        {
            // 没有明确周期：退而求其次取全局谷，置信度会很差
            double lowest = 1.0e9;

            for (int t = tauMin; t <= tauMax; ++t)
            {
                if (normalised[(size_t) t] < lowest)
                {
                    lowest = normalised[(size_t) t];
                    tau = t;
                }
            }
        }
        else
        {
            // 走到谷底
            while (tau + 1 <= tauMax && normalised[(size_t) (tau + 1)] < normalised[(size_t) tau])
                ++tau;
        }

        if (tau < 2 || tau >= tauMax)
        {
            currentHz  = 0.0f;
            confidence = 0.0f;
            return;
        }

        // 抛物线插值，把分辨率提到亚采样
        const auto y0 = normalised[(size_t) (tau - 1)];
        const auto y1 = normalised[(size_t) tau];
        const auto y2 = normalised[(size_t) (tau + 1)];

        const auto denominator = y0 - 2.0 * y1 + y2;
        const auto delta = std::abs (denominator) > 1.0e-12
                             ? 0.5 * (y0 - y2) / denominator
                             : 0.0;

        const auto refinedTau = (double) tau + juce::jlimit (-0.5, 0.5, delta);

        confidence = (float) juce::jlimit (0.0, 1.0, 1.0 - y1);

        if (confidence < 0.35f || refinedTau <= 0.0)
        {
            currentHz  = 0.0f;
            confidence = 0.0f;
            return;
        }

        currentHz = (float) juce::jlimit ((double) minHz, (double) maxHz, sr / refinedTau);
    }

    double sr = 48000.0;

    int windowSize = 1024;
    int hopSamples = 512;
    int tauMin     = 40;
    int tauMax     = 960;

    std::vector<float> ring, history, diff;
    std::vector<double> normalised;

    int   writePos = 0;
    int   filled = 0;
    int   sinceAnalysis = 0;
    float currentHz = 0.0f;
    float confidence = 0.0f;
};

} // namespace sfx
