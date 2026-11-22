//==============================================================================
// PM 效果器的验证。
//
//   PmEffectTest.exe      返回 0 = 全部通过
//
// 这个文件把三条设计要求逐条变成可执行的断言：
//
//   · PM 而不是 FM  → 喂直流偏移，载波音高必须纹丝不动（T2）
//   · 真立体声      → 只调制左声道，右声道必须还是干干净净的载波（T3）
//   · 过采样        → 插件层的延迟必须如实上报，说明 4x 路径真的接上了（T6）
//
// 另外还有频率精度、干湿混合、锯齿波形的基本检查。
//==============================================================================

#include "../Source/PluginProcessor.h"

#include <cmath>
#include <iostream>
#include <functional>
#include <vector>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 512;

int failures = 0;

void check (bool ok, const juce::String& what, const juce::String& detail = {})
{
    std::cout << (ok ? "  [PASS] " : "  [FAIL] ") << what;

    if (detail.isNotEmpty())
        std::cout << "  (" << detail << ")";

    std::cout << "\n";

    if (! ok)
        ++failures;
}

juce::String num (double v, int places = 2)
{
    return juce::String (v, places);
}

//------------------------------------------------------------------------------
struct Metrics
{
    double zcHz[2] { 0.0, 0.0 };     // 由过零率反推的频率
    double rms[2]  { 0.0, 0.0 };
    double peak    = 0.0;
    bool   finite  = true;

    std::vector<float> tailOut[2];   // 最后一块的输出，用来算左右相关性
    std::vector<float> tailIn[2];    // 最后一块的输入，用来查干信号是否原样穿过
    std::vector<float> ch0;          // 左声道全程，用来做频谱分析
};

/** Goertzel：只算单个频点的能量，比整段 FFT 便宜，够用来盯混叠。 */
double goertzel (const std::vector<float>& x, double sampleRate, double freq)
{
    const auto n = x.size();

    if (n < 4)
        return 0.0;

    const auto w     = 2.0 * juce::MathConstants<double>::pi * freq / sampleRate;
    const auto coeff = 2.0 * std::cos (w);

    double s1 = 0.0, s2 = 0.0;

    for (size_t i = 0; i < n; ++i)
    {
        // Hann 窗，压住旁瓣泄漏 —— 否则邻近的强分量会污染这个频点
        const auto win = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi
                                               * (double) i / (double) (n - 1));
        const auto s0 = (double) x[i] * win + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    return std::sqrt (juce::jmax (0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2)) / (double) n;
}

/** 直接跑 PmEffect（不走插件的过采样），用来精确测频率和波形。 */
Metrics render (const sfx::PmEffect::Params& params,
                const std::function<float (int ch, long long n)>& input,
                int numBlocks = 120)
{
    sfx::PmEffect fx;
    fx.prepare (kSampleRate, kBlockSize);
    fx.setParams (params);

    juce::AudioBuffer<float> buffer (2, kBlockSize);
    buffer.clear();
    juce::dsp::AudioBlock<float> block (buffer);

    Metrics m;

    double sumSquares[2] { 0.0, 0.0 };
    long long crossings[2] { 0, 0 };
    long long counted = 0;
    float previous[2] { 0.0f, 0.0f };
    bool  havePrevious[2] { false, false };

    for (int b = 0; b < numBlocks; ++b)
    {
        for (int i = 0; i < kBlockSize; ++i)
            for (int ch = 0; ch < 2; ++ch)
                buffer.getWritePointer (ch)[i] = input (ch, (long long) b * kBlockSize + i);

        fx.process (block);

        if (b < 4)          // 让平滑器和滤波器先走完开头那段
            continue;

        if (b == numBlocks - 1)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                m.tailOut[ch].assign (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + kBlockSize);
                m.tailIn[ch].assign (kBlockSize, 0.0f);

                for (int i = 0; i < kBlockSize; ++i)
                    m.tailIn[ch][(size_t) i] = input (ch, (long long) b * kBlockSize + i);
            }
        }

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* data = buffer.getReadPointer (ch);

            for (int i = 0; i < kBlockSize; ++i)
            {
                const auto v = data[i];

                if (! std::isfinite (v))
                    m.finite = false;

                sumSquares[ch] += (double) v * (double) v;
                m.peak = juce::jmax (m.peak, std::abs ((double) v));

                if (havePrevious[ch]
                    && ((previous[ch] < 0.0f && v >= 0.0f) || (previous[ch] >= 0.0f && v < 0.0f)))
                    ++crossings[ch];

                previous[ch] = v;
                havePrevious[ch] = true;
            }
        }

        m.ch0.insert (m.ch0.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + kBlockSize);

        counted += kBlockSize;
    }

    const auto seconds = (double) counted / kSampleRate;

    for (int ch = 0; ch < 2; ++ch)
    {
        m.rms[ch]  = std::sqrt (sumSquares[ch] / (double) juce::jmax ((long long) 1, counted));
        m.zcHz[ch] = seconds > 0.0 ? (double) crossings[ch] / (2.0 * seconds) : 0.0;
    }

    return m;
}

//------------------------------------------------------------------------------
void setFloat (juce::AudioProcessorValueTreeState& apvts, const char* id, float v)
{
    if (auto* p = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (id)))
        p->setValueNotifyingHost (p->convertTo0to1 (v));
}

void setChoice (juce::AudioProcessorValueTreeState& apvts, const char* id, int v)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id)))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) v));
}

void setBool (juce::AudioProcessorValueTreeState& apvts, const char* id, bool v)
{
    if (auto* p = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (id)))
        p->setValueNotifyingHost (v ? 1.0f : 0.0f);
}

/** 跑完整插件，用过零率反推输出载波的频率。 */
double runPluginAndMeasureHz (BesselAudioProcessor& processor,
                              const std::function<float (int, long long)>& input,
                              int numBlocks, int skipBlocks = 20)
{
    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;

    long long crossings = 0;
    long long counted   = 0;
    float previous = 0.0f;
    bool  havePrevious = false;

    for (int b = 0; b < numBlocks; ++b)
    {
        for (int i = 0; i < kBlockSize; ++i)
        {
            const auto v = input (0, (long long) b * kBlockSize + i);
            buffer.getWritePointer (0)[i] = v;
            buffer.getWritePointer (1)[i] = v;
        }

        processor.processBlock (buffer, midi);

        if (b < skipBlocks)
            continue;

        const auto* data = buffer.getReadPointer (0);

        for (int i = 0; i < kBlockSize; ++i)
        {
            const auto v = data[i];

            if (havePrevious && ((previous < 0.0f && v >= 0.0f) || (previous >= 0.0f && v < 0.0f)))
                ++crossings;

            previous = v;
            havePrevious = true;
        }

        counted += kBlockSize;
    }

    const auto seconds = (double) counted / kSampleRate;
    return seconds > 0.0 ? (double) crossings / (2.0 * seconds) : 0.0;
}

//==============================================================================
// T1 · 不调制时就是干净载波（频率精度）
void testPureCarrier()
{
    std::cout << "\nT1  index=0 时是纯载波\n";

    sfx::PmEffect::Params p;
    p.carrierHz = 440.0f;
    p.shape     = 0.0f;
    p.pmAmount = 0.0f;
    p.mix = 1.0f;

    const auto m = render (p, [] (int, long long) { return 0.0f; });

    check (m.finite, "no NaN / Inf");
    check (std::abs (m.zcHz[0] - 440.0) < 4.4,
           "carrier frequency is 440 Hz", num (m.zcHz[0]) + " Hz");
    check (m.rms[0] > 0.6, "carrier has full level", "rms=" + num (m.rms[0], 3));
}

//==============================================================================
// T2 · 直流偏移不能让音高跑 —— 这就是 PM 和 FM 的分界线
void testDcOffsetDoesNotDetune()
{
    std::cout << "\nT2  外部音频带直流偏移（PM vs FM 的分界线）\n";

    sfx::PmEffect::Params p;
    p.carrierHz   = 440.0f;
    p.shape       = 0.0f;
    p.pmAmount = 4.0f;      // 深度拉满
    p.modCutoffHz = 2000.0f;
    p.modFilterType = (int) sfx::Svf::Type::lowpass;
    p.mix = 1.0f;

    // 纯直流 0.3：FM 会把它当成持续的频率偏移，音高直接跑掉；
    // PM 只会得到一个静态相位偏移，过零率必须还是 440 Hz。
    const auto m = render (p, [] (int, long long) { return 0.3f; });

    check (m.finite, "no NaN / Inf");
    check (std::abs (m.zcHz[0] - 440.0) < 4.4,
           "DC offset does not detune the carrier",
           num (m.zcHz[0]) + " Hz (expect 440)");
    check (m.rms[0] > 0.6, "still a full-level carrier", "rms=" + num (m.rms[0], 3));
}

//==============================================================================
// T3 · 真立体声：只调制一边，另一边必须干净
void testTrueStereo()
{
    std::cout << "\nT3  左右声道独立\n";

    sfx::PmEffect::Params p;
    p.carrierHz   = 440.0f;
    p.shape       = 0.0f;
    p.pmAmount = 4.0f;
    p.modCutoffHz = 2000.0f;
    p.mix = 1.0f;

    // 左：直流 0.3 → 相位偏移 1.2 圈（等效 0.2 圈）
    // 右：零输入   → 相位偏移 0
    const auto m = render (p, [] (int ch, long long) { return ch == 0 ? 0.3f : 0.0f; });

    check (m.finite, "no NaN / Inf");
    check (std::abs (m.zcHz[0] - 440.0) < 4.4 && std::abs (m.zcHz[1] - 440.0) < 4.4,
           "both channels stay at carrier pitch",
           "L=" + num (m.zcHz[0]) + " R=" + num (m.zcHz[1]));

    // 相关系数：0.2 圈偏移 → cos(72°) ≈ 0.31
    double ab = 0.0, aa = 0.0, bb = 0.0;

    for (size_t i = 0; i < m.tailOut[0].size(); ++i)
    {
        const double a = m.tailOut[0][i];
        const double b = m.tailOut[1][i];
        ab += a * b;
        aa += a * a;
        bb += b * b;
    }

    const auto corr = ab / std::sqrt (juce::jmax (1.0e-12, aa * bb));

    check (corr < 0.9, "channels are not phase-locked to each other",
           "corr=" + num (corr, 3));

    check (m.rms[1] > 0.6, "unmodulated channel is still audible",
           "rms=" + num (m.rms[1], 3));
}

//==============================================================================
// T4 · 锯齿载波
void testSaw()
{
    std::cout << "\nT4  锯齿载波\n";

    sfx::PmEffect::Params p;
    p.carrierHz   = 220.0f;
    p.shape       = 1.0f;
    p.pmAmount = 1.5f;
    p.mix = 1.0f;

    const auto m = render (p, [] (int ch, long long n) {
        return 0.4f * std::sin (juce::MathConstants<float>::twoPi * 300.0f * (float) n / (float) kSampleRate);
    });

    check (m.finite, "no NaN / Inf");
    check (m.rms[0] > 0.3, "saw carrier passes audio", "rms=" + num (m.rms[0], 3));
    check (m.peak < 1.6, "saw stays bounded (polyBLEP)", "peak=" + num (m.peak, 3));
}

//==============================================================================
// T5 · 干湿混合
void testMix()
{
    std::cout << "\nT5  mix=0 时原样放行\n";

    sfx::PmEffect::Params p;
    p.carrierHz = 440.0f;
    p.mix = 0.0f;

    const auto m = render (p, [] (int ch, long long n) {
        return 0.5f * std::sin (juce::MathConstants<float>::twoPi * 1000.0f
                                * (float) n / (float) kSampleRate + (ch == 0 ? 0.0f : 1.0f));
    });

    double maxDiff = 0.0;

    for (size_t i = 0; i < m.tailOut[0].size(); ++i)
        maxDiff = juce::jmax (maxDiff, std::abs ((double) m.tailOut[0][i] - (double) m.tailIn[0][i]));

    check (maxDiff < 1.0e-5, "dry path is bit-transparent", "maxDiff=" + num (maxDiff, 8));
}

//==============================================================================
// T6 · 插件层：过采样接上了，参数通到 DSP
void testPluginLevel()
{
    std::cout << "\nT6  插件层（过采样 + 参数通路）\n";

    BesselAudioProcessor processor;
    processor.setPlayConfigDetails (2, 2, kSampleRate, kBlockSize);
    processor.prepareToPlay (kSampleRate, kBlockSize);

    check (processor.getLatencySamples() > 0,
           "reports oversampling latency",
           juce::String (processor.getLatencySamples()) + " samples");

    check (processor.getParameters().size() == 32,
           "exposes 32 parameters (31 + bypass)",
           "count=" + juce::String (processor.getParameters().size()));

    setFloat  (processor.apvts, BesselAudioProcessor::paramCarrier, 1000.0f);
    setFloat  (processor.apvts, BesselAudioProcessor::paramPmAmount,   0.0f);
    setFloat  (processor.apvts, BesselAudioProcessor::paramMix,     1.0f);
    setFloat (processor.apvts, BesselAudioProcessor::paramShape, 0.0f);   // 正弦

    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;

    double sumSquares = 0.0;
    long long counted = 0;
    long long crossings = 0;
    float previous = 0.0f;
    bool  havePrevious = false;
    bool  finite = true;

    for (int b = 0; b < 120; ++b)
    {
        // 输入包络 AM 是恒开的，所以这里必须喂一个满幅信号；
        // 幅度 1.0 → 包络 ≈ 1 → 输出就是满幅载波。
        for (int i = 0; i < kBlockSize; ++i)
        {
            const auto n = (long long) b * kBlockSize + i;
            const auto v = (float) std::sin (juce::MathConstants<double>::twoPi * 110.0
                                             * (double) n / kSampleRate);
            buffer.getWritePointer (0)[i] = v;
            buffer.getWritePointer (1)[i] = v;
        }

        processor.processBlock (buffer, midi);

        if (b < 8)                            // 等过采样滤波器的起始瞬态过去
            continue;

        for (int i = 0; i < kBlockSize; ++i)
        {
            const auto v = buffer.getReadPointer (0)[i];

            if (! std::isfinite (v))
                finite = false;

            sumSquares += (double) v * (double) v;

            if (havePrevious && ((previous < 0.0f && v >= 0.0f) || (previous >= 0.0f && v < 0.0f)))
                ++crossings;

            previous = v;
            havePrevious = true;
        }

        counted += kBlockSize;
    }

    const auto seconds = (double) counted / kSampleRate;
    const auto hz      = seconds > 0.0 ? (double) crossings / (2.0 * seconds) : 0.0;
    const auto rms     = std::sqrt (sumSquares / (double) juce::jmax ((long long) 1, counted));

    check (finite, "no NaN / Inf through the oversampled path");
    check (rms > 0.4, "generates a carrier through the plugin", "rms=" + num (rms, 3));
    check (std::abs (hz - 1000.0) < 20.0,
           "carrier frequency survives the 4x round trip",
           num (hz) + " Hz (expect 1000)");
}

//==============================================================================
// T7 · 过采样到底压掉了多少混叠
//
// 手法：调制器给 15 kHz 正弦，载波 227 Hz，观察 2773 Hz 这个频点。
//   227 + 3×15000 = 45227 Hz，在 48 kHz 下折回 2773 Hz。
//
// 注意一个陷阱：192000 − |227 − 13×15000| = 2773 —— 4x 内部 k=13 的边带
// 也会折回同一个频点。所以这里测两个深度：
//   · 温和（index 1.0）：13 阶边带可以忽略，测到的基本是抽取滤波器的真实阻带
//   · 极端（index 4.0）：25 弧度，贝塞尔级数拖到 13 阶，4x 自己也会折叠。
//     这是 PM 的固有上限，不是滤波器没做好 —— 越深越需要更高倍数。
double measureAliasingSuppression (float pmAmount)
{
    constexpr double modulatorHz = 15000.0;
    constexpr double foldedHz    = 2773.0;

    auto modulator = [modulatorHz] (int, long long n) {
        return (float) std::sin (juce::MathConstants<double>::twoPi * modulatorHz
                                 * (double) n / kSampleRate);
    };

    sfx::PmEffect::Params p;
    p.carrierHz     = 227.0f;
    p.shape         = 0.0f;
    p.pmAmount   = pmAmount;
    p.modCutoffHz   = 20000.0f;
    p.modFilterType = (int) sfx::Svf::Type::lowpass;
    p.modQ          = 0.707f;
    p.mix           = 1.0f;

    // 1x 参考：完全没有过采样
    const auto raw = render (p, modulator);

    // 4x：完整插件
    BesselAudioProcessor processor;
    processor.setPlayConfigDetails (2, 2, kSampleRate, kBlockSize);
    processor.prepareToPlay (kSampleRate, kBlockSize);

    setFloat  (processor.apvts, BesselAudioProcessor::paramCarrier, p.carrierHz);
    setFloat  (processor.apvts, BesselAudioProcessor::paramPmAmount,   p.pmAmount);
    setFloat  (processor.apvts, BesselAudioProcessor::paramModFreq, p.modCutoffHz);
    setFloat  (processor.apvts, BesselAudioProcessor::paramModQ,    p.modQ);
    setFloat  (processor.apvts, BesselAudioProcessor::paramMix,     1.0f);
    setFloat (processor.apvts, BesselAudioProcessor::paramShape, 0.0f);   // 正弦
    setChoice (processor.apvts, BesselAudioProcessor::paramModType, 0);

    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;
    std::vector<float> fast;

    for (int b = 0; b < 120; ++b)
    {
        for (int i = 0; i < kBlockSize; ++i)
        {
            const auto v = modulator (0, (long long) b * kBlockSize + i);
            buffer.getWritePointer (0)[i] = v;
            buffer.getWritePointer (1)[i] = v;
        }

        processor.processBlock (buffer, midi);

        if (b >= 4)
            fast.insert (fast.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + kBlockSize);
    }

    const auto rawAlias  = goertzel (raw.ch0, kSampleRate, foldedHz);
    const auto fastAlias = goertzel (fast,    kSampleRate, foldedHz);

    return 20.0 * std::log10 (juce::jmax (1.0e-12, rawAlias) / juce::jmax (1.0e-12, fastAlias));
}

void testAliasing()
{
    std::cout << "\nT7  4x 过采样压掉混叠（2773 Hz 折叠点）\n";

    const auto moderate = measureAliasingSuppression (1.0f);
    const auto extreme  = measureAliasingSuppression (4.0f);

    check (moderate > 30.0,
           "moderate depth: decimation filter does the work",
           num (moderate, 1) + " dB below 1x");

    check (extreme > 10.0,
           "extreme depth: still better, but 4x's own high orders fold back",
           num (extreme, 1) + " dB below 1x");
}

/** 跑完整插件，返回左声道的 RMS。 */
double runPluginAndMeasureRms (BesselAudioProcessor& processor,
                               const std::function<float (int, long long)>& input,
                               int numBlocks, int skipBlocks = 20)
{
    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;

    double sumSquares = 0.0;
    long long counted = 0;

    for (int b = 0; b < numBlocks; ++b)
    {
        for (int i = 0; i < kBlockSize; ++i)
        {
            const auto v = input (0, (long long) b * kBlockSize + i);
            buffer.getWritePointer (0)[i] = v;
            buffer.getWritePointer (1)[i] = v;
        }

        processor.processBlock (buffer, midi);

        if (b < skipBlocks)
            continue;

        const auto* data = buffer.getReadPointer (0);

        for (int i = 0; i < kBlockSize; ++i)
            sumSquares += (double) data[i] * (double) data[i];

        counted += kBlockSize;
    }

    return std::sqrt (sumSquares / (double) juce::jmax ((long long) 1, counted));
}

//==============================================================================
// T9 · 输入包络 → AM（恒满）
//
// 目的：把原素材的纹理提取出来做 sub 层。所以：
//   · AM 永远是满的，没有"漏一点干载波"的深度旋钮
//   · Env Gain 只缩放包络本身，用来提安静素材 / 压响素材
//   · 干信号不经过 AM（Mix < 100% 时不会被自己的包络泵动）
void testInputAm()
{
    std::cout << "\nT9  输入包络 AM（恒满，Gain 只缩放包络）\n";

    BesselAudioProcessor processor;
    processor.setPlayConfigDetails (2, 2, kSampleRate, kBlockSize);
    processor.prepareToPlay (kSampleRate, kBlockSize);

    setFloat (processor.apvts, BesselAudioProcessor::paramCarrier,   440.0f);
    setFloat (processor.apvts, BesselAudioProcessor::paramPmAmount,     0.0f);   // 输出 = 纯载波
    setFloat (processor.apvts, BesselAudioProcessor::paramMix,       1.0f);
    setFloat (processor.apvts, BesselAudioProcessor::paramAmGain,    0.0f);
    setFloat (processor.apvts, BesselAudioProcessor::paramAmAttack,  5.0f);
    setFloat (processor.apvts, BesselAudioProcessor::paramAmRelease, 250.0f);

    auto silent = [] (int, long long) { return 0.0f; };

    auto tone = [] (float amplitude)
    {
        return [amplitude] (int, long long n)
        {
            return amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * 110.0
                                                 * (double) n / kSampleRate);
        };
    };

    // 1) 没有输入 → 一点载波都不能漏。这是"恒满"的直接后果，不是靠参数关掉的
    const auto rmsSilent = runPluginAndMeasureRms (processor, silent, 120);
    check (rmsSilent < 1.0e-3, "no input -> no output, no leakage",
           "rms=" + num (rmsSilent, 6));

    // 2) 输入 0.5 → 输出 = 载波(0.707) × 包络(0.5)
    const auto rmsHalf = runPluginAndMeasureRms (processor, tone (0.5f), 200);
    check (std::abs (rmsHalf - 0.3535) < 0.06,
           "output level follows input level", "rms=" + num (rmsHalf, 3));

    // 3) Env Gain 就是包络的增益：+6 dB 大约翻倍
    setFloat (processor.apvts, BesselAudioProcessor::paramAmGain, 6.0f);
    const auto rmsPlus6 = runPluginAndMeasureRms (processor, tone (0.5f), 200);
    check (std::abs (rmsPlus6 / juce::jmax (1.0e-9, rmsHalf) - 2.0) < 0.25,
           "env gain +6 dB doubles the envelope",
           "ratio=" + num (rmsPlus6 / juce::jmax (1.0e-9, rmsHalf), 3));

    // 4) -12 dB 大约四分之一
    setFloat (processor.apvts, BesselAudioProcessor::paramAmGain, -12.0f);
    const auto rmsMinus12 = runPluginAndMeasureRms (processor, tone (0.5f), 200);
    check (std::abs (rmsMinus12 / juce::jmax (1.0e-9, rmsHalf) - 0.25) < 0.06,
           "env gain -12 dB quarters the envelope",
           "ratio=" + num (rmsMinus12 / juce::jmax (1.0e-9, rmsHalf), 3));

    // 5) 增益开多大，静音输入都必须是静音（没有"漏出来"的路径）
    //    先放一段静音把上一段的释放尾巴放干净——+24 dB 会把这个尾巴放大 16 倍，
    //    那不是漏音，是物理上应有的衰减。
    setFloat (processor.apvts, BesselAudioProcessor::paramAmGain, 24.0f);
    runPluginAndMeasureRms (processor, silent, 400);
    const auto rmsSilentHot = runPluginAndMeasureRms (processor, silent, 120);
    check (rmsSilentHot < 1.0e-3, "no leakage even at +24 dB",
           "rms=" + num (rmsSilentHot, 6));

    // 6) 干信号不被 AM 调制：Mix=0 时输出应该就是输入本身，和 Env Gain 无关
    setFloat (processor.apvts, BesselAudioProcessor::paramAmGain, 12.0f);
    setFloat (processor.apvts, BesselAudioProcessor::paramMix,    0.0f);
    const auto rmsDry = runPluginAndMeasureRms (processor, tone (0.5f), 200);
    check (std::abs (rmsDry - 0.3535) < 0.03,
           "dry signal is not AM'd by its own envelope", "rms=" + num (rmsDry, 3));

    // 7) 释放：停音后是衰减，不是瞬间切断
    setFloat (processor.apvts, BesselAudioProcessor::paramMix,       1.0f);
    setFloat (processor.apvts, BesselAudioProcessor::paramAmGain,    0.0f);
    setFloat (processor.apvts, BesselAudioProcessor::paramAmRelease, 800.0f);

    runPluginAndMeasureRms (processor, tone (0.5f), 120);              // 把包络推到 0.5
    const auto early    = runPluginAndMeasureRms (processor, silent, 20, 0);
    const auto veryLate = runPluginAndMeasureRms (processor, silent, 300, 250);

    check (early > 0.05, "release lets it fade, not cut", "rms=" + num (early, 4));
    check (veryLate < 1.0e-2, "eventually fully silent", "rms=" + num (veryLate, 6));
}

//==============================================================================
// T10 · PM 与 FM 共存
//
// 两个量各自独立加进同一个相位。关键性质：
//   · PM 只改读相位 → 无论 FM 开多大，PM 都不影响音高
//   · FM 改相位步长 → 直流偏移会变成持续频偏
//
// 所以「PM + FM 同时开」的音高应该只由 FM 决定，PM 只改音色。
void testModulationModes()
{
    std::cout << "\nT10  PM / FM 共存\n";

    sfx::PmEffect::Params p;
    p.carrierHz   = 440.0f;
    p.shape       = 0.0f;
    p.modCutoffHz = 20000.0f;
    p.mix         = 1.0f;

    auto dc = [] (int, long long) { return 0.3f; };

    // 1) 两个量都为 0：纯载波
    p.pmAmount = 0.0f;
    p.fmAmount = 0.0f;
    const auto bothZero = render (p, dc);
    check (std::abs (bothZero.zcHz[0] - 440.0) < 4.4,
           "both amounts at 0 is a pure carrier", num (bothZero.zcHz[0]) + " Hz");

    // 2) 只有 PM：直流偏移不影响音高
    p.pmAmount = 4.0f;
    p.fmAmount = 0.0f;
    const auto pmOnly = render (p, dc);
    check (std::abs (pmOnly.zcHz[0] - 440.0) < 4.4,
           "PM alone: DC offset does not detune", num (pmOnly.zcHz[0]) + " Hz");

    // 3) 只有 FM：4 × 0.3 × 500 Hz = +600 Hz
    p.pmAmount = 0.0f;
    p.fmAmount = 4.0f;
    const auto fmOnly = render (p, dc);
    check (std::abs (fmOnly.zcHz[0] - 1040.0) < 25.0,
           "FM alone: DC shifts the carrier",
           num (fmOnly.zcHz[0]) + " Hz (expect 1040)");

    // 4) 两个同时开：音高必须和「只有 FM」完全一样 —— PM 不动音高
    p.pmAmount = 4.0f;
    p.fmAmount = 4.0f;
    const auto bothOn = render (p, dc);
    check (std::abs (bothOn.zcHz[0] - 1040.0) < 25.0,
           "PM + FM: pitch still only follows the FM amount",
           num (bothOn.zcHz[0]) + " Hz (expect 1040)");

    // 5) FM 偏移量随 fmAmount 线性走：2 × 0.3 × 500 = +300 Hz
    p.pmAmount = 4.0f;
    p.fmAmount = 2.0f;
    const auto fmHalf = render (p, dc);
    check (std::abs (fmHalf.zcHz[0] - 740.0) < 20.0,
           "FM deviation scales with its own amount",
           num (fmHalf.zcHz[0]) + " Hz (expect 740)");
}

//==============================================================================
// T8 · 载波跟随输入音高（开关）
//
// 测的是控制通路的正确性，所以把 PM Index 关掉：输出就是纯载波，
// 用它的过零率反推"载波现在到底在什么频率"。
void testPitchFollow()
{
    std::cout << "\nT8  载波跟随输入音高\n";

    BesselAudioProcessor processor;
    processor.setPlayConfigDetails (2, 2, kSampleRate, kBlockSize);
    processor.prepareToPlay (kSampleRate, kBlockSize);

    setFloat (processor.apvts, BesselAudioProcessor::paramCarrier, 500.0f);
    setFloat (processor.apvts, BesselAudioProcessor::paramPmAmount,   0.0f);
    setFloat (processor.apvts, BesselAudioProcessor::paramMix,     1.0f);
    setChoice (processor.apvts, BesselAudioProcessor::paramPitchMode, 0);   // Free

    // AM 恒开，所以这里也必须有输入才听得到载波（这正是设计意图）
    auto silence = [] (int, long long) { return 0.0f; };

    auto tone = [] (float hz)
    {
        return [hz] (int, long long n)
        {
            return 0.6f * (float) std::sin (juce::MathConstants<double>::twoPi * (double) hz
                                            * (double) n / kSampleRate);
        };
    };

    // 关：载波锁在 Carrier 旋钮
    const auto hzOff = runPluginAndMeasureHz (processor, tone (110.0f), 120);
    check (std::abs (hzOff - 500.0) < 5.0,
           "follow off keeps the Carrier knob", num (hzOff) + " Hz (expect 500)");

    // 开 + 喂 220 Hz
    setChoice (processor.apvts, BesselAudioProcessor::paramPitchMode, 1);   // Follow

    const auto hz220 = runPluginAndMeasureHz (processor, tone (220.0f), 240);
    check (std::abs (hz220 - 220.0) < 6.0,
           "locks onto a 220 Hz source", num (hz220) + " Hz");

    // 不重置，直接换 330 Hz
    const auto hz330 = runPluginAndMeasureHz (processor, tone (330.0f), 240);
    check (std::abs (hz330 - 330.0) < 8.0,
           "tracks a pitch change to 330 Hz", num (hz330) + " Hz");

    // 富泛音信号：基频 220 + 前 6 次谐波。
    // 过零率法在这种信号上会直接数泛音，跳到 440 甚至 660；YIN 该给 220。
    auto harmonic = [] (int, long long n)
    {
        const auto t = (double) n / kSampleRate;
        double v = 0.0;

        for (int h = 1; h <= 6; ++h)
            v += std::sin (juce::MathConstants<double>::twoPi * 220.0 * (double) h * t) / (double) h;

        return (float) (0.25 * v);
    };

    const auto hzHarmonic = runPluginAndMeasureHz (processor, harmonic, 240);
    check (std::abs (hzHarmonic - 220.0) < 6.0,
           "no octave jump on a harmonically rich source", num (hzHarmonic) + " Hz (expect 220)");

    // 断音 → 跟丢 → 滑回 Carrier 旋钮。
    // 这里不能靠测输出频率：AM 恒开，静音输入时输出也是静音，没有过零点可数。
    // 改成读插件实际送去振荡器的载波频率。
    runPluginAndMeasureHz (processor, silence, 300);
    const auto lostHz = processor.currentCarrierHz.load();
    check (std::abs (lostHz - 500.0f) < 8.0f,
           "falls back to Carrier when tracking is lost",
           num (lostHz) + " Hz (expect 500)");
}
//==============================================================================
// T11 · MIDI 接管载波音高
//
// 效果器身份（能挂音频轨吃素材）+ MIDI 输入（改音高）。
// 这里只验控制通路：音符 → 载波频率、弯音 → 频率倍数、松键 → 回落旋钮。
void testMidiPitch()
{
    std::cout << "\nT11  MIDI 接管音高\n";

    BesselAudioProcessor processor;
    processor.setPlayConfigDetails (2, 2, kSampleRate, kBlockSize);
    processor.prepareToPlay (kSampleRate, kBlockSize);

    setFloat (processor.apvts, BesselAudioProcessor::paramCarrier,  500.0f);
    setFloat (processor.apvts, BesselAudioProcessor::paramPmAmount,   0.0f);
    setFloat (processor.apvts, BesselAudioProcessor::paramFmAmount,   0.0f);
    setFloat (processor.apvts, BesselAudioProcessor::paramMix,        1.0f);
    setFloat (processor.apvts, BesselAudioProcessor::paramAmGain,     0.0f);
    setFloat (processor.apvts, BesselAudioProcessor::paramGlide,      5.0f);
    setChoice (processor.apvts, BesselAudioProcessor::paramPitchMode, 2);   // MIDI

    juce::AudioBuffer<float> buffer (2, kBlockSize);
    long long sampleCounter = 0;

    // 跑一段，返回这段输出的平均频率（用上升过零点反推）
    auto run = [&] (int blocks, const juce::MidiBuffer* firstBlockMessage)
    {
        long long crossings = 0;
        long long counted   = 0;
        float previous = 0.0f;
        bool  havePrevious = false;

        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < kBlockSize; ++i)
            {
                const auto v = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 110.0
                                                        * (double) (sampleCounter + i) / kSampleRate);
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
            }

            sampleCounter += kBlockSize;

            juce::MidiBuffer message;

            if (b == 0 && firstBlockMessage != nullptr)
                message = *firstBlockMessage;

            processor.processBlock (buffer, message);

            if (b < 6)                       // 等滑音和过采样瞬态过去
                continue;

            for (int i = 0; i < kBlockSize; ++i)
            {
                const auto v = buffer.getReadPointer (0)[i];

                if (havePrevious && ((previous < 0.0f && v >= 0.0f) || (previous >= 0.0f && v < 0.0f)))
                    ++crossings;

                previous = v;
                havePrevious = true;
            }

            counted += kBlockSize;
        }

        const auto seconds = (double) counted / kSampleRate;
        return seconds > 0.0 ? (double) crossings / (2.0 * seconds) : 0.0;
    };

    auto noteOnMessage = [] (int note)
    {
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
        return m;
    };

    // MIDI 40 = E2 = 82.41 Hz
    {
        auto on = noteOnMessage (40);
        const auto hz = run (40, &on);
        check (std::abs (hz - 82.41) < 1.5,
               "MIDI note 40 makes the carrier 82.4 Hz", num (hz) + " Hz");
    }

    // MIDI 52 = E3 = 164.81 Hz
    {
        auto on = noteOnMessage (52);
        const auto hz = run (40, &on);
        check (std::abs (hz - 164.81) < 2.5,
               "MIDI note 52 makes the carrier 164.8 Hz", num (hz) + " Hz");
    }

    // 弯音推到顶 = +12 st = 一个八度
    {
        juce::MidiBuffer bend;
        bend.addEvent (juce::MidiMessage::pitchWheel (1, 16383), 0);
        const auto hz = run (40, &bend);
        check (std::abs (hz - 329.63) < 4.0,
               "pitch bend +12 st doubles the carrier", num (hz) + " Hz");
    }

    // 松键 → 回落 Carrier 旋钮（它仍是效果器，不是非按不可的合成器）
    {
        juce::MidiBuffer off;
        off.addEvent (juce::MidiMessage::noteOff (1, 52), 0);
        const auto hz = run (40, &off);
        check (std::abs (hz - 500.0) < 6.0,
               "note off falls back to the Carrier knob", num (hz) + " Hz");
    }
}

//==============================================================================
// T12 · MIDI 追踪梳状滤波
//
// 极性是音色的分水岭：正反馈强化基频，负反馈把它抵掉（只留奇次谐波）。
void testComb()
{
    std::cout << "\nT12  MIDI 追踪梳状滤波\n";

    auto measure = [] (float feedback)
    {
        sfx::PmEffect::Params p;
        p.carrierHz    = 100.0f;
        p.shape        = 0.0f;
        p.pmAmount     = 0.0f;
        p.fmAmount     = 0.0f;
        p.mix          = 1.0f;
        p.combMix      = 1.0f;
        p.combFeedback = feedback;
        p.subLevel     = 0.0f;
        p.driveDb      = 0.0f;

        return render (p, [] (int, long long) { return 0.0f; }, 200).rms[0];
    };

    const auto flat = measure (0.0f);    // 梳状滤波不介入
    const auto pos  = measure (0.8f);    // 正反馈：基频被放大
    const auto neg  = measure (-0.8f);   // 负反馈：基频被抵消

    check (pos > flat * 2.0,
           "positive feedback boosts the fundamental",
           num (pos / juce::jmax (1.0e-9, flat), 2) + "x");

    check (neg < flat * 0.8,
           "negative feedback cancels the fundamental",
           num (neg / juce::jmax (1.0e-9, flat), 2) + "x");
}

//==============================================================================
// T13 · Clean Sub 与 Drive
//
// 大深度 PM 会因为 J0 把基频抽空（看起来凶、底盘发飘）。
// Clean Sub 补一条不受调制的同相正弦；Drive 把上下层焊在一起。
void testSubAndDrive()
{
    std::cout << "\nT13  Clean Sub 与 Drive\n";

    auto renderVoice = [] (float subLevel, float driveDb)
    {
        sfx::PmEffect::Params p;
        p.carrierHz = 60.0f;
        p.shape     = 0.0f;
        p.pmAmount  = 4.0f;                 // 25 弧度：J0 已经很弱
        p.fmAmount  = 0.0f;
        p.mix       = 1.0f;
        p.subLevel  = subLevel;
        p.driveDb   = driveDb;

        return render (p, [] (int, long long n)
        {
            return 0.8f * (float) std::sin (juce::MathConstants<double>::twoPi * 300.0
                                            * (double) n / kSampleRate);
        }, 200);
    };

    const auto withoutSub = renderVoice (0.0f, 0.0f);
    const auto withSub    = renderVoice (1.0f, 0.0f);

    const auto magWithout = goertzel (withoutSub.ch0, kSampleRate, 60.0);
    const auto magWith    = goertzel (withSub.ch0,    kSampleRate, 60.0);

    check (magWith > magWithout * 3.0,
           "clean sub restores the fundamental",
           "60 Hz energy x" + num (magWith / juce::jmax (1.0e-12, magWithout), 1));

    const auto clean = renderVoice (1.0f, 0.0f);
    const auto hot   = renderVoice (1.0f, 12.0f);

    check (hot.peak <= 1.02 && clean.peak > hot.peak * 1.05,
           "drive clamps the peaks",
           "peak " + num (clean.peak, 3) + " -> " + num (hot.peak, 3));
}

//==============================================================================
// T14 · LFO 调制载波音高 / 调制滤波截止
//
// 之前插件没有任何自动调制源：想动音高只能外部画自动化。
// LFO 让"颤音 / 警报 / riser / 哇音"能在插件内部自己跑起来。
void testLfo()
{
    std::cout << "\nT14  LFO 调制\n";

    auto windowHz = [] (const std::vector<float>& v, int from, int to)
    {
        int crossings = 0;
        float previous = v[(size_t) from];
        bool first = true;

        for (int i = from; i < to; ++i)
        {
            const auto x = v[(size_t) i];

            if (! first && ((previous < 0.0f && x >= 0.0f) || (previous >= 0.0f && x < 0.0f)))
                ++crossings;

            previous = x;
            first = false;
        }

        const auto seconds = (double) (to - from) / kSampleRate;
        return seconds > 0.0 ? (double) crossings / (2.0 * seconds) : 0.0;
    };

    auto windowRms = [] (const std::vector<float>& v, int from, int to)
    {
        double sum = 0.0;

        for (int i = from; i < to; ++i)
            sum += (double) v[(size_t) i] * (double) v[(size_t) i];

        return std::sqrt (sum / juce::jmax (1, to - from));
    };

    // --- LFO → 音高：0.25 Hz（4 秒一拍），深度 +12 st -------------------
    // t≈0 时 LFO=0 → 200 Hz；t≈1 s 时 LFO=+1 → 400 Hz
    {
        sfx::PmEffect::Params p;
        p.carrierHz  = 200.0f;
        p.shape      = 0.0f;
        p.pmAmount   = 0.0f;
        p.mix        = 1.0f;
        p.lfoRateHz  = 0.25f;
        p.lfoShape   = 0;
        p.lfoToPitch = 12.0f;

        const auto m = render (p, [] (int, long long) { return 0.0f; }, 360);

        const auto atStart = windowHz (m.ch0, (int) (0.05 * kSampleRate), (int) (0.15 * kSampleRate));
        const auto atPeak  = windowHz (m.ch0, (int) (0.95 * kSampleRate), (int) (1.05 * kSampleRate));

        // 起点时 LFO 已经在往上爬（sin 在 0.05 s 处 ≈ 0.08），所以不会正好 200
        check (atStart > 195.0 && atStart < 245.0,
               "LFO starts near the base pitch", num (atStart) + " Hz (expect ~200-240)");

        check (std::abs (atPeak - 400.0) < 40.0,
               "LFO +12 st doubles the carrier at its peak", num (atPeak) + " Hz (expect 400)");
    }

    // --- LFO → 截止：8 kHz 的调制源在截止扫开之前进不来 -----------------
    //
    // 不能用 RMS 判断：PM 是非幅度调制，调制度再大 RMS 也不变。
    // 要看载波基频本身——调制度上去之后 J0 衰减，基频被抽走。
    {
        auto windowMag = [] (const std::vector<float>& v, int from, int to, double freq)
        {
            const std::vector<float> slice (v.begin() + from, v.begin() + to);
            return goertzel (slice, kSampleRate, freq);
        };

        auto measure = [&] (float toCutoff)
        {
            sfx::PmEffect::Params p;
            p.carrierHz     = 200.0f;
            p.pmAmount      = 4.0f;
            p.modCutoffHz   = 500.0f;     // 基准很低：8 kHz 调制源被挡住
            p.modFilterType = 0;          // LP
            p.mix           = 1.0f;
            p.lfoRateHz     = 0.25f;
            p.lfoShape      = 0;
            p.lfoToCutoff   = toCutoff;

            const auto m = render (p, [] (int, long long n)
            {
                return 0.7f * (float) std::sin (juce::MathConstants<double>::twoPi * 8000.0
                                                * (double) n / kSampleRate);
            }, 360);

            const auto early = windowMag (m.ch0, (int) (0.05 * kSampleRate),
                                          (int) (0.15 * kSampleRate), 200.0);
            const auto late  = windowMag (m.ch0, (int) (0.95 * kSampleRate),
                                          (int) (1.05 * kSampleRate), 200.0);
            return std::make_pair (early, late);
        };

        const auto flat = measure (0.0f);
        check (std::abs (flat.second - flat.first) < flat.first * 0.15,
               "LFO to cutoff at 0 changes nothing",
               num (flat.first, 2) + " -> " + num (flat.second, 2));

        const auto swept = measure (5.0f);
        check (swept.second < swept.first * 0.5,
               "LFO to cutoff lets the 8 kHz modulator through",
               "200 Hz energy " + num (swept.first, 2) + " -> " + num (swept.second, 2));
    }
}

//==============================================================================
// T15 · 输入包络进入调制矩阵
//
// 包络原本只做 AM（音量）。现在它还是一个调制源，能推音高 / 截止 / LFO 速率。
// 这里直接注入一个恒定的归一化包络，验证三个目的地都真的被推到。
void testEnvModulation()
{
    std::cout << "\nT15  包络 → 调制矩阵\n";

    auto renderWithEnv = [] (float envToPitch, float envValue, int numBlocks)
    {
        sfx::PmEffect fx;
        fx.prepare (kSampleRate, kBlockSize);

        sfx::PmEffect::Params p;
        p.carrierHz  = 200.0f;
        p.shape      = 0.0f;
        p.pmAmount   = 0.0f;
        p.mix        = 1.0f;
        p.envToPitch = envToPitch;
        fx.setParams (p);

        // 恒定包络，AM 增益给 1（不影响音高）
        std::vector<float> am  ((size_t) kBlockSize, 1.0f);
        std::vector<float> env ((size_t) kBlockSize, envValue);
        fx.setEnvelopeGains (am.data(), env.data(), kBlockSize);

        juce::AudioBuffer<float> buffer (2, kBlockSize);
        juce::dsp::AudioBlock<float> block (buffer);

        long long crossings = 0;
        long long counted   = 0;
        float previous = 0.0f;
        bool havePrevious = false;

        for (int b = 0; b < numBlocks; ++b)
        {
            buffer.clear();
            fx.process (block);

            if (b < 4)
                continue;

            const auto* data = buffer.getReadPointer (0);

            for (int i = 0; i < kBlockSize; ++i)
            {
                const auto x = data[i];

                if (havePrevious && ((previous < 0.0f && x >= 0.0f) || (previous >= 0.0f && x < 0.0f)))
                    ++crossings;

                previous = x;
                havePrevious = true;
            }

            counted += kBlockSize;
        }

        const auto seconds = (double) counted / kSampleRate;
        return seconds > 0.0 ? (double) crossings / (2.0 * seconds) : 0.0;
    };

    // 包络 0：不动
    const auto flat = renderWithEnv (24.0f, 0.0f, 40);
    check (std::abs (flat - 200.0) < 8.0,
           "zero envelope leaves the carrier alone", num (flat) + " Hz (expect 200)");

    // 包络 0.5、深度 24 st：源先开方（√0.5 = 0.707），所以是 +16.97 st
    //   → 200 × 2^(16.97/12) ≈ 533 Hz
    const auto half = renderWithEnv (24.0f, 0.5f, 40);
    check (std::abs (half - 533.0) < 25.0,
           "envelope 0.5 at 24 st uses the square-root curve", num (half) + " Hz (expect 533)");

    // 包络满、深度 12 st → 又是一个八度（再 x2 = 800）
    const auto full = renderWithEnv (12.0f, 1.0f, 40);
    check (std::abs (full - 400.0) < 25.0,
           "envelope 1.0 at 12 st doubles the carrier", num (full) + " Hz (expect 400)");
}

//==============================================================================
// T16 · 包络调制走整条插件链（不是直接注入 PmEffect）
//
// T15 验证了 DSP，但如果插件层没把数组接上，用户那边仍然是"没反应"。
// 这一条从 AudioProcessor 进去，走完 AM / 过采样 / 调制，再量输出频率。
void testEnvModulationThroughPlugin()
{
    std::cout << "\nT16  包络调制（整条插件链）\n";

    auto measure = [] (float envToPitch, float inputAmplitude)
    {
        BesselAudioProcessor processor;
        processor.setPlayConfigDetails (2, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay (kSampleRate, kBlockSize);

        setChoice (processor.apvts, BesselAudioProcessor::paramPitchMode, 0);   // Free
        setFloat  (processor.apvts, BesselAudioProcessor::paramCarrier,  200.0f);
        setFloat  (processor.apvts, BesselAudioProcessor::paramPmAmount,   0.0f);
        setFloat  (processor.apvts, BesselAudioProcessor::paramFmAmount,   0.0f);
        setFloat  (processor.apvts, BesselAudioProcessor::paramMix,        1.0f);
        setFloat  (processor.apvts, BesselAudioProcessor::paramAmGain,     0.0f);
        setFloat  (processor.apvts, BesselAudioProcessor::paramAmAttack,   1.0f);
        setFloat  (processor.apvts, BesselAudioProcessor::paramAmRelease, 50.0f);
        setFloat  (processor.apvts, BesselAudioProcessor::paramEnvPitch, envToPitch);

        juce::AudioBuffer<float> buffer (2, kBlockSize);
        juce::MidiBuffer midi;
        long long sampleCounter = 0;

        long long crossings = 0;
        long long counted   = 0;
        float previous = 0.0f;
        bool havePrevious = false;

        for (int b = 0; b < 80; ++b)
        {
            for (int i = 0; i < kBlockSize; ++i)
            {
                const auto v = inputAmplitude * (float) std::sin (
                    juce::MathConstants<double>::twoPi * 110.0
                    * (double) (sampleCounter + i) / kSampleRate);

                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
            }

            sampleCounter += kBlockSize;
            processor.processBlock (buffer, midi);

            if (b < 20)                  // 等包络爬到稳态
                continue;

            const auto* data = buffer.getReadPointer (0);

            for (int i = 0; i < kBlockSize; ++i)
            {
                const auto x = data[i];

                if (havePrevious && ((previous < 0.0f && x >= 0.0f) || (previous >= 0.0f && x < 0.0f)))
                    ++crossings;

                previous = x;
                havePrevious = true;
            }

            counted += kBlockSize;
        }

        const auto seconds = (double) counted / kSampleRate;
        return seconds > 0.0 ? (double) crossings / (2.0 * seconds) : 0.0;
    };

    // 没有包络调制时，输入幅度不该影响载波
    const auto quietBase = measure (0.0f, 0.1f);
    const auto loudBase  = measure (0.0f, 0.8f);
    check (std::abs (quietBase - 200.0) < 8.0 && std::abs (loudBase - 200.0) < 8.0,
           "without env depth the carrier stays at 200 Hz",
           num (quietBase) + " / " + num (loudBase) + " Hz");

    // 深度 24 st：包络 0.1 → +2.4 st；包络 0.8 → +19.2 st
    const auto quiet = measure (24.0f, 0.1f);
    const auto loud  = measure (24.0f, 0.8f);

    check (loud > quiet * 1.5,
           "louder input pushes the carrier higher",
           num (quiet) + " Hz -> " + num (loud) + " Hz");

    check (loud > 400.0,
           "env 0.8 at 24 st is clearly audible",
           num (loud) + " Hz (expect >400)");
}

//==============================================================================
// T17 · ENV → 滤波截止（MOD FREQ）到底动没动
//
// 这条单独拎出来验：调制支路上的 SVF 截止被包络推着走时，
// 宽带素材的 PM 边带应该明显变化。截止设 300 Hz、包络深度 +5 八度，
// 只有当截止真的被推开，8 kHz 附近的噪声才能进到调制里去。
void testEnvToCutoff()
{
    std::cout << "\nT17  ENV → 滤波截止\n";

    auto measure = [] (float envToCutoff)
    {
        sfx::PmEffect fx;
        fx.prepare (kSampleRate, kBlockSize);

        sfx::PmEffect::Params p;
        p.carrierHz     = 200.0f;
        p.pmAmount      = 3.0f;
        p.modGainDb     = 0.0f;
        p.modCutoffHz   = 300.0f;     // 很低：把噪声的高频挡住
        p.modFilterType = 0;          // LP
        p.modQ          = 0.707f;
        p.mix           = 1.0f;
        p.envToCutoff   = envToCutoff;
        fx.setParams (p);

        juce::Random noise (0x5eed);
        std::vector<float> am  ((size_t) kBlockSize, 1.0f);
        std::vector<float> env ((size_t) kBlockSize, 0.9f);   // 包络接近满
        fx.setEnvelopeGains (am.data(), env.data(), kBlockSize);

        juce::AudioBuffer<float> buffer (2, kBlockSize);
        juce::dsp::AudioBlock<float> block (buffer);

        std::vector<float> collected;
        collected.reserve ((size_t) (kBlockSize * 60));

        for (int b = 0; b < 60; ++b)
        {
            for (int i = 0; i < kBlockSize; ++i)
            {
                const auto v = 0.5f * (noise.nextFloat() * 2.0f - 1.0f);
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
            }

            fx.process (block);

            if (b >= 20)
            {
                const auto* data = buffer.getReadPointer (0);
                collected.insert (collected.end(), data, data + kBlockSize);
            }
        }

        // 高频边带：截止被推开之后 8 kHz 附近的噪声会进到调制里
        const auto high = goertzel (collected, kSampleRate, 8000.0)
                        + goertzel (collected, kSampleRate, 6000.0);

        // 基频能量：PM 变深时 J0 会掉
        const auto fundamental = goertzel (collected, kSampleRate, 200.0);

        return std::make_pair (high, fundamental);
    };

    const auto without = measure (0.0f);
    const auto with    = measure (5.0f);

    check (with.first > without.first * 2.0,
           "ENV to cutoff lets high content into the modulator",
           "high-band " + num (without.first, 4) + " -> " + num (with.first, 4));

    check (with.second < without.second * 0.8,
           "ENV to cutoff deepens PM (fundamental drops)",
           "200 Hz " + num (without.second, 4) + " -> " + num (with.second, 4));
}

//==============================================================================
// T18 · ENV → LFO 速率
//
// 用相位反推：LFO 0.25 Hz、深度 +12 st 时，
//   t=0.75 s 的 LFO 值是 sin(2π·0.25·0.75) = +0.92 → 载波约 379 Hz
// 如果包络把速率推了 2 个八度（×4 → 1 Hz），
//   t=0.75 s 的 LFO 值是 sin(2π·1·0.75) = −1 → 载波约 100 Hz
// 两个结果方向相反，是最好的判别器。
void testEnvToLfoRate()
{
    std::cout << "\nT18  ENV → LFO 速率\n";

    auto measureAt = [] (float envToRate)
    {
        sfx::PmEffect fx;
        fx.prepare (kSampleRate, kBlockSize);

        sfx::PmEffect::Params p;
        p.carrierHz   = 200.0f;
        p.shape       = 0.0f;
        p.pmAmount    = 0.0f;
        p.mix         = 1.0f;
        p.lfoRateHz   = 0.25f;
        p.lfoShape    = 0;
        p.lfoToPitch  = 12.0f;
        p.envToRate   = envToRate;
        fx.setParams (p);

        std::vector<float> am  ((size_t) kBlockSize, 1.0f);
        std::vector<float> env ((size_t) kBlockSize, 1.0f);   // 包络满：×4
        fx.setEnvelopeGains (am.data(), env.data(), kBlockSize);

        juce::AudioBuffer<float> buffer (2, kBlockSize);
        juce::dsp::AudioBlock<float> block (buffer);

        // 跑到 t = 0.75 s，量最后一小段的频率
        const auto targetSamples = (long long) (0.75 * kSampleRate);
        long long total = 0;

        while (total < targetSamples)
        {
            buffer.clear();
            fx.process (block);
            total += kBlockSize;
        }

        // 再跑 0.1 s 采样窗口
        long long crossings = 0;
        long long counted = 0;
        float previous = 0.0f;
        bool havePrevious = false;

        for (int b = 0; b < (int) (0.1 * kSampleRate / kBlockSize); ++b)
        {
            buffer.clear();
            fx.process (block);

            const auto* data = buffer.getReadPointer (0);

            for (int i = 0; i < kBlockSize; ++i)
            {
                const auto x = data[i];

                if (havePrevious && ((previous < 0.0f && x >= 0.0f) || (previous >= 0.0f && x < 0.0f)))
                    ++crossings;

                previous = x;
                havePrevious = true;
            }

            counted += kBlockSize;
        }

        const auto seconds = (double) counted / kSampleRate;
        return seconds > 0.0 ? (double) crossings / (2.0 * seconds) : 0.0;
    };

    const auto noMod  = measureAt (0.0f);    // LFO 仍 0.25 Hz → t=0.75 s 在上升段顶
    const auto pushed = measureAt (0.75f);   // 0.25 + 0.75 = 1 Hz → t=0.75 s 在谷底

    check (noMod > 300.0,
           "without env depth the LFO stays slow at t=0.75 s",
           num (noMod) + " Hz (expect ~379)");

    check (pushed < 200.0,
           "env pushes the LFO rate up (1 Hz vs 0.25 Hz)",
           num (pushed) + " Hz (expect ~100)");
}

//==============================================================================
// T19 · ENV → LFO 速率，走整条插件链
//
// 用户在 DAW 里反馈"包络推 LFO 速率听不出来"。DSP 层的 T18 是绿的，
// 所以这一条专测插件层：参数 → 调制包络 → PmEffect 的接线。
void testEnvToLfoRateThroughPlugin()
{
    std::cout << "\nT19  ENV → LFO 速率（整条插件链）\n";

    auto measureAt = [] (float envToRate)
    {
        BesselAudioProcessor processor;
        processor.setPlayConfigDetails (2, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay (kSampleRate, kBlockSize);

        setChoice (processor.apvts, BesselAudioProcessor::paramPitchMode, 0);   // Free
        setFloat  (processor.apvts, BesselAudioProcessor::paramCarrier,  200.0f);
        setFloat  (processor.apvts, BesselAudioProcessor::paramPmAmount,   0.0f);
        setFloat  (processor.apvts, BesselAudioProcessor::paramMix,        1.0f);
        setFloat  (processor.apvts, BesselAudioProcessor::paramAmGain,     0.0f);
        setFloat  (processor.apvts, BesselAudioProcessor::paramLfoRate,    0.25f);
        setFloat  (processor.apvts, BesselAudioProcessor::paramLfoPitch,  12.0f);
        setFloat  (processor.apvts, BesselAudioProcessor::paramEnvRate, envToRate);

        // 调制包络要跟得快，才能在这 1 秒内到达满值
        setFloat  (processor.apvts, BesselAudioProcessor::paramEnv2Attack,   1.0f);
        setFloat  (processor.apvts, BesselAudioProcessor::paramEnv2Release, 50.0f);

        juce::AudioBuffer<float> buffer (2, kBlockSize);
        juce::MidiBuffer midi;
        long long sampleCounter = 0;

        // 输入一个稳定的大信号，让包络推到接近满
        auto feed = [&] (float amplitude)
        {
            for (int i = 0; i < kBlockSize; ++i)
            {
                const auto v = amplitude * (float) std::sin (
                    juce::MathConstants<double>::twoPi * 110.0
                    * (double) (sampleCounter + i) / kSampleRate);

                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
            }

            sampleCounter += kBlockSize;
            processor.processBlock (buffer, midi);
        };

        const auto blocksToTarget = (int) ((0.75 * kSampleRate) / kBlockSize);

        for (int b = 0; b < blocksToTarget; ++b)
            feed (0.9f);

        long long crossings = 0;
        long long counted = 0;
        float previous = 0.0f;
        bool havePrevious = false;

        const auto windowBlocks = juce::jmax (1, (int) ((0.1 * kSampleRate) / kBlockSize));

        for (int b = 0; b < windowBlocks; ++b)
        {
            feed (0.9f);

            const auto* data = buffer.getReadPointer (0);

            for (int i = 0; i < kBlockSize; ++i)
            {
                const auto x = data[i];

                if (havePrevious && ((previous < 0.0f && x >= 0.0f) || (previous >= 0.0f && x < 0.0f)))
                    ++crossings;

                previous = x;
                havePrevious = true;
            }

            counted += kBlockSize;
        }

        const auto seconds = (double) counted / kSampleRate;
        return seconds > 0.0 ? (double) crossings / (2.0 * seconds) : 0.0;
    };

    const auto noMod  = measureAt (0.0f);
    const auto pushed = measureAt (0.75f);

    std::cout << "   t=0.75 s 时的载波：深度 0 → " << num (noMod)
              << " Hz，深度 +2 oct → " << num (pushed) << " Hz\n";

    check (std::abs (noMod - pushed) > 100.0,
           "env depth changes the LFO phase through the plugin",
           num (noMod) + " -> " + num (pushed) + " Hz");
}

//==============================================================================
// T20 · 波形形变（正弦 → 三角 → 带限锯齿）
//
// 判据用**偶次谐波**：三角只有奇次，锯齿有全部。
// 所以 shape 从 0 推到 1，2 次谐波应该从无到有。
void testShapeMorph()
{
    std::cout << "\nT20  波形形变\n";

    auto harmonicAt = [] (float shape, double freq)
    {
        sfx::PmEffect::Params p;
        p.carrierHz = 200.0f;
        p.shape     = shape;
        p.pmAmount  = 0.0f;
        p.mix       = 1.0f;

        const auto m = render (p, [] (int, long long) { return 0.0f; }, 200);
        return goertzel (m.ch0, kSampleRate, freq);
    };

    const auto sine2nd   = harmonicAt (0.0f, 400.0);
    const auto tri2nd    = harmonicAt (0.5f, 400.0);
    const auto saw2nd    = harmonicAt (1.0f, 400.0);
    const auto sawFund   = harmonicAt (1.0f, 200.0);

    check (sine2nd < 0.01 && tri2nd < 0.01,
           "sine and triangle have no even harmonics",
           num (sine2nd, 4) + " / " + num (tri2nd, 4));

    check (saw2nd > sawFund * 0.3,
           "saw has a strong 2nd harmonic",
           "2nd/fund = " + num (saw2nd / juce::jmax (1.0e-9, sawFund), 2));

    // 三角的 3 次谐波是 1/9，锯齿的是 1/3 —— 可以据此确认中间那一档真的是三角
    const auto tri3rd = harmonicAt (0.5f, 600.0);
    const auto saw3rd = harmonicAt (1.0f, 600.0);

    check (tri3rd < saw3rd * 0.6,
           "triangle rolls off faster than saw",
           "3rd: tri " + num (tri3rd, 3) + " vs saw " + num (saw3rd, 3));
}

//==============================================================================
// T21 · 0 Hz = Wavefolder
//
// 载波 0 Hz 时相位不再自己走，输出变成 wave(PM × 调制信号)。
// 拿一个 200 Hz 正弦去推，输出应该长出 200 Hz 的整数倍谐波（波形整形特征）。
void testZeroCarrierFolder()
{
    std::cout << "\nT21  0 Hz = Wavefolder\n";

    auto measure = [] (float carrierHz)
    {
        sfx::PmEffect::Params p;
        p.carrierHz   = carrierHz;
        p.shape       = 0.0f;          // 正弦形变，只看整形
        p.pmAmount    = 3.0f;          // 深推：正弦进、折出谐波
        p.mix         = 1.0f;
        p.modCutoffHz = 20000.0f;      // 调制滤波全开

        return render (p, [] (int, long long n)
        {
            return 0.8f * (float) std::sin (juce::MathConstants<double>::twoPi * 200.0
                                            * (double) n / kSampleRate);
        }, 200);
    };

    const auto folded = measure (0.0f);

    // 折叠之后 200 Hz 的 2 次、3 次谐波应该明显存在
    const auto h1 = goertzel (folded.ch0, kSampleRate, 200.0);
    const auto h2 = goertzel (folded.ch0, kSampleRate, 400.0);
    const auto h3 = goertzel (folded.ch0, kSampleRate, 600.0);

    check (h2 > h1 * 0.1 || h3 > h1 * 0.1,
           "0 Hz carrier folds the modulator into harmonics",
           "h1 " + num (h1, 3) + " / h2 " + num (h2, 3) + " / h3 " + num (h3, 3));

    check (folded.rms[0] > 0.1,
           "folder mode still passes audio",
           "rms=" + num (folded.rms[0], 3));
}

//==============================================================================
// T22 · DC / 亚声波高通
//
// PM 和 Folder 都会在 20 Hz 以下堆能量：听不见、占动态余量、还会顶扬声器。
// 12 Hz 一阶高通应该把 5 Hz 的载波压下去，而 60 Hz 的低音几乎不受影响。
void testSubsonicFilter()
{
    std::cout << "\nT22  DC / 亚声波高通\n";

    auto rmsAtCarrier = [] (float carrierHz)
    {
        sfx::PmEffect::Params p;
        p.carrierHz = carrierHz;
        p.shape     = 0.0f;
        p.pmAmount  = 0.0f;
        p.mix       = 1.0f;

        return render (p, [] (int, long long) { return 0.0f; }, 300).rms[0];
    };

    const auto sub  = rmsAtCarrier (5.0f);     // 亚声波：应该被压得很低
    const auto bass = rmsAtCarrier (60.0f);    // 可用低频：几乎不动

    check (sub < bass * 0.35,
           "5 Hz is filtered out by the DC blocker",
           "5 Hz " + num (sub, 4) + " vs 60 Hz " + num (bass, 4));

    check (bass > 0.5,
           "60 Hz survives the DC blocker",
           "rms=" + num (bass, 3));
}

//==============================================================================
// T23 · 预设：Factory / User 分离 + 版本戳 + 缺参数检测
void testPresets()
{
    std::cout << "\nT23  预设系统\n";

    // 把预设目录指到构建目录下，避免污染真实的 APPDATA
    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile ("test-presets-t23");
    dir.deleteRecursively();
    dir.createDirectory();

#if JUCE_WINDOWS
    _putenv_s ("BESSEL_PRESET_DIR", dir.getFullPathName().toRawUTF8());
#endif

    // 先放一个 v1 的平铺预设，验证升级迁移
    {
        juce::XmlElement legacy ("PARAMETERS");
        legacy.createNewChildElement ("PARAM")->setAttribute ("id", "carrier");
        legacy.getChildElement (0)->setAttribute ("value", "77");

        auto legacyFile = dir.getChildFile ("Legacy.bessel");

        {
            juce::FileOutputStream out (legacyFile);
            legacy.writeTo (out);
            out.flush();
        }
    }

    {
        BesselAudioProcessor processor;

        check (! dir.getChildFile ("Legacy.bessel").existsAsFile()
                 && sfx::PresetManager::getUserDirectory().getChildFile ("Legacy.bessel").existsAsFile(),
               "legacy flat preset migrated into User/",
               sfx::PresetManager::getUserDirectory().getFullPathName());

        // 工厂预设应该被写进 Factory/
        const auto factoryDir = sfx::PresetManager::getFactoryDirectory();
        const auto userDir    = sfx::PresetManager::getUserDirectory();

        int factoryCount = 0;

        for (const auto& entry : juce::RangedDirectoryIterator (factoryDir, false, "*.bessel",
                                                                juce::File::findFiles))
        {
            juce::ignoreUnused (entry);
            ++factoryCount;
        }

        check (factoryCount >= 3,
               "factory presets land in Presets/Factory",
               "count=" + juce::String (factoryCount));

        check (userDir.isDirectory(),
               "Presets/User exists and is separate",
               userDir.getFullPathName());

        // 版本戳
        check (factoryDir.getChildFile (".version").existsAsFile(),
               "version stamp written", "ok");

        // 存一个用户预设，再改参数、读回来
        setFloat (processor.apvts, BesselAudioProcessor::paramCarrier, 123.4f);
        const auto savedName = processor.presets.save ("Unit Test");
        check (savedName == "Unit Test", "user preset saved", savedName);

        check (processor.presets.getFile (processor.presets.size() - 1)
                             .getParentDirectory() == userDir,
               "user preset went to Presets/User", "ok");

        setFloat (processor.apvts, BesselAudioProcessor::paramCarrier, 55.0f);

        int loadedIndex = -1;

        for (int i = 0; i < processor.presets.size(); ++i)
            if (processor.presets.getName (i) == "Unit Test" && ! processor.presets.isFactory (i))
                loadedIndex = i;

        check (loadedIndex >= 0, "user preset appears in the list",
               "index=" + juce::String (loadedIndex));

        if (loadedIndex >= 0)
        {
            const auto result = processor.presets.load (loadedIndex);
            check (result.ok && result.missing == 0 && result.unknown == 0,
                   "round trip loads without parameter mismatch",
                   "missing=" + juce::String (result.missing)
                     + " unknown=" + juce::String (result.unknown));

            const auto* carrierParam = processor.apvts.getRawParameterValue (
                                           BesselAudioProcessor::paramCarrier);
            check (carrierParam != nullptr && std::abs (carrierParam->load() - 123.4f) < 0.6f,
                   "carrier value round-trips",
                   "carrier=" + juce::String (carrierParam != nullptr ? carrierParam->load() : -1.0f));
        }

        // 回归：同一个名字连续保存两次，文件里必须仍然只有一份状态。
        // 之前 FileOutputStream 不截断，会把整份状态追加到尾部，
        // 加载时只读最旧那份 → "改了参数再 SAVE 不生效"。
        {
            setFloat (processor.apvts, BesselAudioProcessor::paramCarrier, 200.0f);
            processor.presets.save ("Twice");
            setFloat (processor.apvts, BesselAudioProcessor::paramCarrier, 210.0f);
            processor.presets.save ("Twice");

            const auto file = sfx::PresetManager::getUserDirectory().getChildFile ("Twice.bessel");
            const auto text = file.loadFileAsString();

            int paramCount = 0;
            int searchFrom = 0;

            while ((searchFrom = text.indexOf (searchFrom, "<PARAM ")) >= 0)
            {
                ++paramCount;
                searchFrom += 7;
            }

            check (paramCount == 32,
                   "saving the same preset twice overwrites (no append)",
                   "PARAM count=" + juce::String (paramCount) + " (expect 32)");
        }

        // 伪造一个「老格式 + 缺参数 + 有未知参数」的文件
        {
            juce::XmlElement bogus ("PARAMETERS");      // 没有 besselFormat 属性
            bogus.createNewChildElement ("PARAM")->setAttribute ("id", "carrier");
            bogus.getChildElement (0)->setAttribute ("value", "300");
            bogus.createNewChildElement ("PARAM")->setAttribute ("id", "notarealparam");
            bogus.getChildElement (1)->setAttribute ("value", "1");

            auto file = userDir.getChildFile ("Bogus.bessel");

            {
                juce::FileOutputStream out (file);
                bogus.writeTo (out);
                out.flush();
            }                                  // 必须关掉流，否则 Windows 上读不到

            processor.presets.rescan();

            int bogusIndex = -1;

            for (int i = 0; i < processor.presets.size(); ++i)
                if (processor.presets.getName (i) == "Bogus")
                    bogusIndex = i;

            if (bogusIndex >= 0)
            {
                // 先把 comb 调起来，再加载一个不含 comb 参数的文件：
                // 加载后必须回到默认值，而不是留着上一个预设的值
                setFloat (processor.apvts, BesselAudioProcessor::paramCombMix, 0.9f);
                setFloat (processor.apvts, BesselAudioProcessor::paramCombFb,  0.8f);

                const auto result = processor.presets.load (bogusIndex);

                const auto* combMix = processor.apvts.getRawParameterValue (
                                          BesselAudioProcessor::paramCombMix);
                const auto* combFb = processor.apvts.getRawParameterValue (
                                          BesselAudioProcessor::paramCombFb);

                check (combMix != nullptr && combFb != nullptr
                         && std::abs (combMix->load()) < 0.001f
                         && std::abs (combFb->load()) < 0.001f,
                       "params missing from a preset fall back to defaults",
                       "combmix=" + juce::String (combMix != nullptr ? combMix->load() : -1.0f)
                         + " combfb=" + juce::String (combFb != nullptr ? combFb->load() : -1.0f));

                check (result.ok && result.missing > 0 && result.unknown > 0,
                       "old/partial preset is reported, not swallowed silently",
                       "missing=" + juce::String (result.missing)
                         + " unknown=" + juce::String (result.unknown)
                         + " fmt=" + juce::String (result.fromFormat));
            }
        }
    }

#if JUCE_WINDOWS
    _putenv_s ("BESSEL_PRESET_DIR", "");
#endif

    dir.deleteRecursively();
}

} // namespace

//==============================================================================
int main()
{
    std::cout << "PM effect test @ " << (int) kSampleRate << " Hz, block " << kBlockSize << "\n";

    testPureCarrier();
    testDcOffsetDoesNotDetune();
    testTrueStereo();
    testSaw();
    testMix();
    testPluginLevel();
    testAliasing();
    testPitchFollow();
    testInputAm();
    testModulationModes();
    testMidiPitch();
    testComb();
    testSubAndDrive();
    testLfo();
    testEnvModulation();
    testEnvModulationThroughPlugin();
    testEnvToCutoff();
    testEnvToLfoRate();
    testEnvToLfoRateThroughPlugin();
    testShapeMorph();
    testZeroCarrierFolder();
    testSubsonicFilter();
    testPresets();

    std::cout << "\n";

    if (failures == 0)
    {
        std::cout << "ALL CHECKS PASSED\n";
        return 0;
    }

    std::cout << failures << " CHECK(S) FAILED\n";
    return 1;
}
