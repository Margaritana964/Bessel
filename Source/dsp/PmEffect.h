#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>

#include "CombFilter.h"

//==============================================================================
// PM 载波效果器
//
//   外部音频 → [调制输入增益] → [SVF 滤波] → 调制量
//                                                ↓
//                        内部载波(sine/saw) ← sin(phase + index × 调制量)
//                                                ↓
//                                     [干湿混合] → [输出增益]
//
// 三条硬约束（改代码时别破坏）：
//
//   1. 相位累加器只受「载波频率」控制。调制只加到输出的读相位上：
//          out = wave(phase + index * mod)
//      绝不能写成 phase += freq + index * mod —— 那是 FM，外部音频的直流偏移
//      会让载波音高跑偏。PM 下直流只造成静态相位偏移，音高绝对稳定。
//
//   2. 处理跑在过采样率上（见 PluginProcessor 的 4x）。
//      音频速率调制会产生大量贝塞尔边带，不做过采样直接在 44.1/48 kHz 下折叠，
//      听感就是廉价的数码杂音。
//
//   3. 所有状态按声道独立：滤波器状态 st[2]、相位累加器 phase[2]。
//      左右共用一份状态会把立体声宽度压扁。
//==============================================================================

namespace sfx
{

//==============================================================================
// TPT 状态变量滤波器（Zavalishin 拓扑）。
// 没有分配、没有分支到堆，Q 和截止频率可以逐块改。
//==============================================================================
struct Svf
{
    enum class Type { lowpass = 0, bandpass = 1, highpass = 2 };

    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        reset();
    }

    void reset() noexcept
    {
        for (auto& s : st)
            s = {};
    }

    // 每块调一次：tan 只在这里算，不进采样循环
    void setParams (Type newType, float cutoffHz, float resonance) noexcept
    {
        type = newType;

        const auto fc = juce::jlimit (10.0f, (float) (sr * 0.49), cutoffHz);
        const auto g  = std::tan (juce::MathConstants<float>::pi * fc / (float) sr);
        const auto k  = 1.0f / juce::jmax (0.05f, resonance);

        kRes  = k;
        a1    = 1.0f / (1.0f + g * (g + k));
        a2    = g * a1;
        a3    = g * a2;
    }

    float processSample (int channel, float x) noexcept
    {
        auto& s = st[(size_t) (channel & 1)];

        const auto v3 = x - s.ic2;
        const auto v1 = a1 * s.ic1 + a2 * v3;
        const auto v2 = s.ic2 + a2 * s.ic1 + a3 * v3;

        s.ic1 = 2.0f * v1 - s.ic1;
        s.ic2 = 2.0f * v2 - s.ic2;

        if (type == Type::bandpass) return v1;
        if (type == Type::highpass) return x - kRes * v1 - v2;

        return v2;   // lowpass
    }

private:
    struct State { float ic1 = 0.0f, ic2 = 0.0f; };

    std::array<State, 2> st {};
    double sr   = 48000.0;
    Type   type = Type::lowpass;
    float  a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, kRes = 1.414f;
};

//==============================================================================
// PM 载波振荡器
//==============================================================================
struct PmOscillator
{
    enum Waveform { sine = 0, saw = 1 };

    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        incSm.reset (sr, 0.03);      // 30 ms 滑音，跟随时音高不会一格一格跳
        buildTables();
        reset();
    }

    void reset() noexcept
    {
        phase.fill (0.0f);
        incSm.setCurrentAndTargetValue (incSm.getTargetValue());
    }

    /** Note-On 相位复位：每一拍的低频冲击相位一致。 */
    void resetPhase() noexcept { phase.fill (0.0f); }

    /** 当前的相位累加器（未受 PM/FM 影响）。子振荡器用它保证同相。 */
    float phaseOf (int channel) const noexcept { return phase[(size_t) (channel & 1)]; }

    /** 形变：0 = 正弦，0.5 = 三角，1 = 带限锯齿。中间值连续过渡。 */
    void setShape (float shape01) noexcept
    {
        shape = juce::jlimit (0.0f, 1.0f, shape01);
    }

    void setFrequency (float hz) noexcept
    {
        // 单个累加器最多到 Nyquist 之前，留点余量
        incSm.setTargetValue (juce::jlimit (0.0f, 0.49f, hz / (float) sr));
    }

    /** 每「采样」调一次，推进频率平滑；返回本采样要用的相位增量。 */
    float tick() noexcept
    {
        return incSm.getNextValue();
    }

    bool isSmoothing() const noexcept { return incSm.isSmoothing(); }

    /** PM 和 FM 可以同时开：两个量各自独立加进同一个相位。
     *
     *  phaseOffsetCycles：PM 用，单位「圈」。
     *  frequencyOffsetHz：FM 用，单位 Hz（按内部采样率折成步长）。
     *  inc：载波自身的相位增量。
     *
     *  数学上两个同时开等价于用 (A·m + B·∫m) 去做纯 PM ——
     *  也就是给调制源加了一条 1/f 倾斜，不是"两种效果叠加"。
     */
    float next (int channel, float phaseOffsetCycles, float frequencyOffsetHz, float inc) noexcept
    {
        const auto ch = (size_t) (channel & 1);
        const auto p  = phase[ch];

        // FM 那一份改进相位步长；直流偏移在这里变成持续频偏 → 音高会跑
        const auto step = juce::jlimit (-0.45f, 0.45f, inc + frequencyOffsetHz / (float) sr);

        // PM 那一份只改读取相位，累加器不受影响
        auto readPhase = p + phaseOffsetCycles;
        readPhase -= std::floor (readPhase);

        // 波表读取：带限 + mipmap + 形变插值。
        // 纯几何锯齿在深 PM 下每采样会跨过跳变沿好几次，必然爆裂；
        // 带限表根本没有那个跳变，代价是高音区谐波数受限（本来就是该限的）。
        const auto out = readTable (readPhase, std::abs (step));

        auto nextPhase = p + step;
        nextPhase -= std::floor (nextPhase);
        phase[ch] = nextPhase;

        return out;
    }

private:
    //==========================================================================
    // 带限波表
    //
    // 为什么要 mipmap：单个谐波上限没法同时伺候 20 Hz 和 5 kHz。
    //   20 Hz 要 128 阶才亮；5 kHz 只允许 16 阶，再多就超过 Nyquist。
    // 所以按载波频率挑表，8 档（128 / 64 / 32 / 16 / 8 / 4 / 2 / 1 阶）。
    //
    // 这也直接解决了爆裂：几何锯齿的 2π→0 跳变在深 PM 下会被反复跨过，
    // 每跨一次就是一次阶跃；带限表是连续函数，没有那个跳变点。
    //==========================================================================
    static constexpr int tableSize = 2048;
    static constexpr int numMips   = 8;
    static constexpr int numShapes = 3;      // sine / triangle / saw

    using Table = std::array<float, tableSize>;

    void buildTables()
    {
        for (int mip = 0; mip < numMips; ++mip)
        {
            const auto maxHarm = 1 << (numMips - 1 - mip);   // 128 … 1

            for (int s = 0; s < numShapes; ++s)
            {
                auto& table = tables[(size_t) mip][(size_t) s];

                double peak = 0.0;

                for (int i = 0; i < tableSize; ++i)
                {
                    const auto ph = juce::MathConstants<double>::twoPi
                                      * (double) i / (double) tableSize;

                    double v = 0.0;

                    for (int n = 1; n <= maxHarm; ++n)
                        v += harmonicAmp (s, n) * std::sin ((double) n * ph);

                    table[(size_t) i] = (float) v;
                    peak = juce::jmax (peak, std::abs (v));
                }

                // 峰值归一化：形变时音量不要跟着跳
                if (peak > 1.0e-9)
                {
                    const auto scale = (float) (1.0 / peak);

                    for (auto& sample : table)
                        sample *= scale;
                }
            }
        }
    }

    static double harmonicAmp (int shapeIndex, int n) noexcept
    {
        if (shapeIndex == 0)                 // 正弦
            return n == 1 ? 1.0 : 0.0;

        if (shapeIndex == 1)                 // 三角：奇次，1/n²，符号交替
        {
            if ((n & 1) == 0)
                return 0.0;

            const auto mag = 1.0 / ((double) n * (double) n);
            return ((n & 3) == 1) ? mag : -mag;
        }

        return 1.0 / (double) n;             // 锯齿
    }

    /** 按当前相位步长挑最亮的、又不会混叠的那一档。 */
    int mipForStep (float step) const noexcept
    {
        const auto hz = std::abs (step) * (float) sr;

        if (hz < 1.0f)
            return numMips - 1;              // 0 Hz（folder 模式）：相位不动，随便挑

        const auto allowed = juce::jlimit (1, 1 << (numMips - 1),
                                           (int) (0.45 * sr / (double) hz));

        for (int mip = 0; mip < numMips; ++mip)
            if ((1 << (numMips - 1 - mip)) <= allowed)
                return mip;

        return numMips - 1;
    }

    float readTable (float phase01, float speed) const noexcept
    {
        const auto mip = mipForStep (speed);

        // 形变：0 → 正弦，0.5 → 三角，1 → 锯齿
        const auto seg = shape * 2.0f;
        const auto a   = juce::jlimit (0, numShapes - 1, (int) seg);
        const auto b   = juce::jmin (numShapes - 1, a + 1);
        const auto mix = seg - (float) a;

        auto pos = phase01;
        pos -= std::floor (pos);

        const auto scaled = pos * (float) tableSize;
        const auto i0     = (int) scaled % tableSize;
        const auto i1     = (i0 + 1) % tableSize;
        const auto frac   = scaled - std::floor (scaled);

        const auto& ta = tables[(size_t) mip][(size_t) a];
        const auto& tb = tables[(size_t) mip][(size_t) b];

        const auto va = ta[(size_t) i0] + (ta[(size_t) i1] - ta[(size_t) i0]) * frac;
        const auto vb = tb[(size_t) i0] + (tb[(size_t) i1] - tb[(size_t) i0]) * frac;

        return va + (vb - va) * mix;
    }

    std::array<std::array<Table, numShapes>, numMips> tables { };

    std::array<float, 2> phase { 0.0f, 0.0f };   // 左、右各自独立
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> incSm;
    double sr    = 48000.0;
    float  shape = 0.0f;
};

//==============================================================================
// 整个效果
//==============================================================================
class PmEffect
{
public:
    struct Params
    {
        float carrierHz     = 220.0f;   // 载波频率
        float shape         = 0.0f;     // 0 正弦 / 0.5 三角 / 1 带限锯齿
        float pmAmount      = 1.0f;     // PM 深度，单位「圈」
        float fmAmount      = 0.0f;     // FM 深度，1 单位 = 500 Hz 频偏
        float modGainDb     = 0.0f;     // 调制信号进滤波器之前的增益
        float modCutoffHz   = 2000.0f;  // 调制滤波器截止
        int   modFilterType = 0;        // 0 LP / 1 BP / 2 HP
        float modQ          = 0.707f;
        float mix           = 1.0f;     // 0 = 干, 1 = 湿
        float outGainDb     = 0.0f;

        // Riddim 层：MIDI 追踪梳状滤波 + 同相干净 sub + 软削波
        float combMix       = 0.0f;     // 0 = 不经过梳状滤波
        float combFeedback  = 0.0f;     // 负值 = 只留奇次谐波
        float subLevel      = 0.0f;     // 同相纯正弦的混入量
        float driveDb       = 0.0f;     // 4x 域内 tanh 软削波

        // 调制源：自由运行 LFO → 载波音高 / 调制滤波截止
        float lfoRateHz     = 1.0f;     // 0.01 – 50 Hz（高到音频速率就是第二路 FM）
        int   lfoShape      = 0;        // 0 sine / 1 triangle / 2 sample&hold
        float lfoToPitch    = 0.0f;     // 半音，±48
        float lfoToCutoff   = 0.0f;     // 八度，±6

        // 输入包络 → 同一组目的地（调制矩阵的第二列）
        float envToPitch    = 0.0f;     // 半音，±48
        float envToCutoff   = 0.0f;     // 八度，±6
        float envToRate     = 0.0f;     // Hz，±40（加性推 LFO 速率）
    };

    /** FM 深度 1 个单位对应多少 Hz 的频率偏移。
     *
     *  PM 和 FM 的量不是同一种东西：PM 是相位偏移（圈），FM 是频率偏移（Hz）。
     *  定成绝对 Hz，换采样率时手感一致。
     *
     *  两个量同时不为零时，等价于给调制源加了一条 1/f 倾斜：
     *      总相位偏差 = PM量·m + FM量·∫m
     *  FM 那一份只放大调制信号的低频，所以会带来"滑"和"被低频拖着走"的感觉。
     */
    static constexpr float fmDeviationHzPerIndex = 500.0f;

    void prepare (double internalSampleRate, int maxBlockSize) noexcept
    {
        sr = internalSampleRate;

        svf.prepare (sr);
        osc.prepare (sr);
        comb.prepare (sr, maxBlockSize);

        modGainSm.reset (sr, 0.02);
        pmAmountSm.reset (sr, 0.02);
        fmAmountSm.reset (sr, 0.02);
        mixSm    .reset (sr, 0.02);
        outGainSm.reset (sr, 0.02);
        subLevelSm.reset (sr, 0.02);
        driveSm  .reset (sr, 0.02);

        // 波形显示：约 800 Hz 采样率就够画 5 秒
        // 12 Hz 一阶高通；内部是 4x 率，所以系数按 4x 算
        dcCoef = (float) std::exp (-juce::MathConstants<double>::twoPi * 15.0 / sr);
        dcState[0][0] = dcState[0][1] = dcState[1][0] = dcState[1][1] = 0.0f;

        lfoScopeStride = juce::jmax (1, (int) (sr / 800.0));
        lfoScopeCount  = 0;

        // Note-On 防爆音：1 ms 淡入
        retriggerFade.reset (sr, 0.001);
        retriggerFade.setCurrentAndTargetValue (1.0f);

        reset();
        setParams ({});
    }

    void reset() noexcept
    {
        svf.reset();
        osc.reset();
        comb.reset();

        modGainSm.setCurrentAndTargetValue (1.0f);
        pmAmountSm.setCurrentAndTargetValue (1.0f);
        fmAmountSm.setCurrentAndTargetValue (0.0f);
        mixSm    .setCurrentAndTargetValue (1.0f);
        outGainSm.setCurrentAndTargetValue (1.0f);
        subLevelSm.setCurrentAndTargetValue (0.0f);
        driveSm  .setCurrentAndTargetValue (1.0f);

        dcState[0][0] = dcState[0][1] = dcState[1][0] = dcState[1][1] = 0.0f;
        lfoPhase = 0.0f;
        lfoHeld  = 0.0f;
        pitchMul = 1.0f;
    }

    /** 下一块开头把载波相位归零，并做一次 1 ms 淡入。 */
    void requestPhaseReset() noexcept { pendingPhaseReset = true; }

    /** 每块调一次。内部只改目标值，实际的去拉链在采样循环里做。 */
    void setParams (const Params& p) noexcept
    {
        osc.setShape (p.shape);
        osc.setFrequency (p.carrierHz);

        // 梳状滤波的延迟长度锁在载波频率上 —— 这是"谐波对齐"的全部来源
        comb.setFrequency (p.carrierHz);
        comb.setMix (p.combMix);
        comb.setFeedback (p.combFeedback);

        // 滤波器基准值要留着：LFO 会在这个基准上按八度上下扫
        filterType    = (Svf::Type) juce::jlimit (0, 2, p.modFilterType);
        baseCutoffHz  = p.modCutoffHz;
        filterQ       = p.modQ;
        svf.setParams (filterType, baseCutoffHz, filterQ);

        // LFO
        lfoRate      = juce::jlimit (0.01f, 200.0f, p.lfoRateHz);
        lfoStep      = (float) (lfoRate / sr);       // 每采样推进的相位
        lfoShape     = p.lfoShape;
        lfoToPitch   = p.lfoToPitch;
        lfoToCutoff  = p.lfoToCutoff;

        // 包络 → 调制矩阵
        envToPitch   = p.envToPitch;
        envToCutoff  = p.envToCutoff;
        envToRate    = p.envToRate;

        modGainSm .setTargetValue (juce::Decibels::decibelsToGain (p.modGainDb));
        pmAmountSm.setTargetValue (juce::jmax (0.0f, p.pmAmount));
        fmAmountSm.setTargetValue (juce::jmax (0.0f, p.fmAmount));
        mixSm     .setTargetValue (juce::jlimit (0.0f, 1.0f, p.mix));
        outGainSm .setTargetValue (juce::Decibels::decibelsToGain (p.outGainDb));
        subLevelSm.setTargetValue (juce::jlimit (0.0f, 1.0f, p.subLevel));
        driveSm   .setTargetValue (juce::Decibels::decibelsToGain (p.driveDb));
    }

    /** 每块调一次：把输入包络算出的逐采样增益交给它。
     *
     *  这个增益**只乘在湿信号上**，干信号不经过它——否则 Mix < 100% 时，
     *  干声会被自己的包络调制，听起来像莫名其妙的泵动。
     */
    void setEnvelopeGains (const float* gains, const float* normalizedEnvelope, int count) noexcept
    {
        amGains   = gains;
        envValues = normalizedEnvelope;
        amCount   = count;
    }

    /** 就地处理。block 的采样率必须是 prepare 时给的那个（过采样率）。 */
    void process (juce::dsp::AudioBlock<float> block) noexcept
    {
        const auto numChannels = (int) juce::jmin ((size_t) 2, block.getNumChannels());
        const auto numSamples  = (int) block.getNumSamples();

        // 过采样后每 base 采样对应 envStride 个样本，增益按整数采样保持
        const auto envStride = (amGains != nullptr && amCount > 0)
                                 ? juce::jmax (1, numSamples / amCount)
                                 : 0;

        for (int i = 0; i < numSamples; ++i)
        {
            // 平滑值每「采样」推进一次，不能按声道推进
            const auto modGain  = modGainSm .getNextValue();
            const auto pmAmt    = pmAmountSm.getNextValue();
            const auto fmAmt    = fmAmountSm.getNextValue();
            const auto mix      = mixSm     .getNextValue();
            const auto outGain  = outGainSm .getNextValue();
            const auto subAmt   = subLevelSm.getNextValue();
            const auto drive    = driveSm   .getNextValue();
            const auto fade     = retriggerFade.getNextValue();

            // LFO：相位每采样推进（精确），但音高/截止每 32 采样才应用一次
            // —— 4x 域下 32 采样 = 0.167 ms，足够平滑，tan/exp2 也不用进采样循环
            lfoPhase += lfoStep;

            if (lfoPhase >= 1.0f)
            {
                lfoPhase -= 1.0f;

                if (lfoShape == 2)                    // sample & hold：每周期换一个值
                    lfoHeld = random.nextFloat() * 2.0f - 1.0f;
            }

            // 降采样写一份给界面画波形（不改 DSP，只做可视化）
            if (++lfoScopeCount >= lfoScopeStride)
            {
                lfoScopeCount = 0;
                lfoScope[lfoScopeWrite] = getLfoValue();
                lfoScopeWrite = (lfoScopeWrite + 1) % lfoScopeSize;
            }

            if ((i & 31) == 0)
            {
                const auto lfo = getLfoValue();

                // 归一化包络：调制矩阵的第二个源
                //
                // 开方是刻意的：包络是线性的 0–1，而正常素材只落在 0.2–0.6，
                // 直接用的话旋钮永远只用得上两三成。开方之后 0.3 → 0.55，
                // 整段行程都能用上，听感也更接近"随音量连续变化"。
                const auto rawEnv = (envValues != nullptr && envStride > 0)
                                      ? envValues[juce::jmin (amCount - 1, i / envStride)]
                                      : 0.0f;

                const auto envMod = std::sqrt (juce::jlimit (0.0f, 1.0f, rawEnv));

                // 音高：LFO 与包络的调制量相加（都是半音）
                pitchMul = std::exp2 ((lfo * lfoToPitch + envMod * envToPitch) / 12.0f);

                // 截止：两个源都按八度叠加
                if (std::abs (lfoToCutoff) > 1.0e-4f || std::abs (envToCutoff) > 1.0e-4f)
                    svf.setParams (filterType,
                                   baseCutoffHz * std::exp2 (lfo * lfoToCutoff
                                                             + envMod * envToCutoff),
                                   filterQ);

                // LFO 速率：包络直接**加 Hz**（不是乘倍率）
                // 慢速 LFO 也能被明显推动：0.1 Hz + 8 Hz 就变成 8.1 Hz
                if (std::abs (envToRate) > 1.0e-4f)
                {
                    const auto modulated = lfoRate + envMod * envToRate;
                    lfoStep = juce::jlimit (0.0f, 200.0f, modulated) / (float) sr;
                }
            }

            const auto inc = osc.tick() * pitchMul;   // 载波频率平滑 × LFO 调制
            const auto envGain  = envStride > 0
                                    ? amGains[juce::jmin (amCount - 1, i / envStride)]
                                    : 1.0f;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* data = block.getChannelPointer ((size_t) ch);

                const auto dry = data[i];
                const auto mod = svf.processSample (ch, dry * modGain);

                // 先取累加器相位：子振荡器要用同一个相位才叫"同相"
                const auto carrierPhase = osc.phaseOf (ch);

                // 两个量同时生效：PM 改读相位，FM 改相位步长
                const auto wet = osc.next (ch,
                                           pmAmt * mod,
                                           fmAmt * mod * fmDeviationHzPerIndex,
                                           inc) * envGain * fade;

                // 干净的同相载波：不受 PM/FM 干扰，底盘靠它
                const auto sub = std::sin (juce::MathConstants<float>::twoPi * carrierPhase)
                                   * envGain * fade;

                // 梳状滤波（MIDI 追踪）把 PM 炸出的宽频噪波对齐到谐波列
                auto voice = comb.processSample (ch, wet);
                voice += sub * subAmt;

                // DC / 亚声波高通：两级一阶串联，先清掉听不见的低端再进削波器
                for (int stage = 0; stage < 2; ++stage)
                {
                    dcState[stage][ch] += (1.0f - dcCoef) * (voice - dcState[stage][ch]);
                    voice -= dcState[stage][ch];
                }

                // 4x 域内软削波：互调让上层纹理焊死在底层 sub 的周期上。
                // drive = 0 dB 时完全旁通，保证默认是干净的。
                if (drive > 1.0001f)
                    voice = std::tanh (voice * drive);

                data[i] = (dry + (voice - dry) * mix) * outGain;
            }
        }
    }

    /** 测试用：当前是否还有平滑没走完。 */
    bool isSmoothing() const noexcept
    {
        return modGainSm.isSmoothing() || pmAmountSm.isSmoothing()
            || fmAmountSm.isSmoothing() || mixSm.isSmoothing()
            || outGainSm.isSmoothing() || subLevelSm.isSmoothing()
            || driveSm.isSmoothing() || osc.isSmoothing()
            || retriggerFade.isSmoothing();
    }

    /** 给界面画实时波形用：最近 n 个 LFO 采样（约 800 Hz）。 */
    void readLfoHistory (float* destination, int numSamples) const noexcept
    {
        if (destination == nullptr || numSamples <= 0)
            return;

        numSamples = juce::jmin (numSamples, lfoScopeSize);

        int index = lfoScopeWrite - numSamples;

        while (index < 0)
            index += lfoScopeSize;

        for (int i = 0; i < numSamples; ++i)
            destination[i] = lfoScope[(size_t) ((index + i) % lfoScopeSize)];
    }

private:
    double sr = 48000.0;

    Svf          svf;
    PmOscillator osc;
    CombFilter   comb;

    // 滤波器的基准参数（LFO 在这个基准上按八度扫）
    Svf::Type filterType   = Svf::Type::lowpass;
    float     baseCutoffHz = 2000.0f;
    float     filterQ      = 0.707f;

    // LFO
    float lfoPhase    = 0.0f;
    float lfoStep     = 0.0f;
    float lfoRate     = 1.0f;
    int   lfoShape    = 0;
    float lfoToPitch  = 0.0f;
    float lfoToCutoff = 0.0f;
    float envToPitch  = 0.0f;
    float envToCutoff = 0.0f;
    float envToRate   = 0.0f;
    float lfoHeld     = 0.0f;
    float pitchMul    = 1.0f;
    juce::Random random;

    // DC / 亚声波高通（两级串联 ≈ 15 Hz 二阶级）：
    // PM 和 Folder 都会在 20 Hz 以下堆能量 —— 听不见、占动态余量、还会顶坏扬声器。
    // 单级 12 Hz 在 5 Hz 只压 −8 dB，不够；两级之后 5 Hz 压到 −20 dB，
    // 而 40 Hz 以上几乎不动（60 Hz 只掉 0.5 dB）。
    float dcState[2][2] { { 0.0f, 0.0f }, { 0.0f, 0.0f } };
    float dcCoef = 0.999f;

    // LFO 实时波形的环形缓冲（降采样到约 800 Hz，够画 5 秒）
    static constexpr int lfoScopeSize = 4096;
    float lfoScope[lfoScopeSize] { };
    int   lfoScopeWrite  = 0;
    int   lfoScopeStride = 1;
    int   lfoScopeCount  = 0;

    float getLfoValue() const noexcept
    {
        if (lfoShape == 1)                                  // triangle
            return 4.0f * std::abs (lfoPhase - 0.5f) - 1.0f;

        if (lfoShape == 2)                                  // sample & hold
            return lfoHeld;

        return std::sin (juce::MathConstants<float>::twoPi * lfoPhase);
    }

    const float* amGains   = nullptr;
    const float* envValues = nullptr;    // 归一化包络，调制源
    int          amCount   = 0;
    bool         pendingPhaseReset = false;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        modGainSm, pmAmountSm, fmAmountSm, mixSm, outGainSm,
        subLevelSm, driveSm, retriggerFade;
};

} // namespace sfx
