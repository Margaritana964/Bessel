#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
namespace
{
juce::String formatHz (float value)
{
    return value >= 1000.0f ? juce::String (value / 1000.0f, 2) + " kHz"
                            : juce::String (value, value < 100.0f ? 1 : 0) + " Hz";
}

juce::String formatDb (float value)
{
    return juce::String (value, 1) + " dB";
}

/** 把「怎么显示」单独包出来，参数定义那几行才不会淹没在 lambda 里。 */
juce::AudioParameterFloatAttributes formatted (std::function<juce::String (float)> formatter)
{
    return juce::AudioParameterFloatAttributes().withStringFromValueFunction (
        [formatter] (float value, int) { return formatter (value); });
}
} // namespace

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout BesselAudioProcessor::createLayout()
{
    using namespace juce;
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

    Layout layout;

    // 载波频率。
    //
    // 底部是一个 "DC" 卡位，其余是 20 Hz – 5 kHz 的对数区。
    // 为什么要卡位：1–20 Hz 是个死区 —— 那么慢的载波只会产生听不见的亚声波，
    // 除了糊频谱没有音乐价值。真正有用的是 0 Hz（相位不动，输出 = wave(调制信号)，
    // 也就是 Wavefolder），所以把它做成旋钮底部的硬档位。
    NormalisableRange<float> carrierRange (
        0.0f, 5000.0f,
        [] (float, float, float x)                 // 0..1 → Hz
        {
            if (x < 0.03f)                         // 底部 3% 全部吸附到 DC
                return 0.0f;

            const auto t = (x - 0.03f) / 0.97f;
            return 20.0f * std::pow (250.0f, t);   // 20 Hz → 5000 Hz
        },
        [] (float, float, float hz)                // Hz → 0..1
        {
            if (hz <= 20.0f)                       // DC 和 20 Hz 以下都归到卡位
                return 0.0f;

            return 0.03f + 0.97f * (std::log (hz / 20.0f) / std::log (250.0f));
        });

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { paramCarrier, 1 }, "Carrier",
        carrierRange, 220.0f,
        formatted ([] (float hz)
        {
            if (hz < 0.5f)
                return juce::String ("DC / Folder");

            return juce::String (hz, hz < 100.0f ? 1 : 0) + " Hz";
        })));

    // 连续形变：0 = 正弦，0.5 = 三角，1 = 带限锯齿
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { paramShape, 1 }, "Shape",
        NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.0f,
        formatted ([] (float v)
        {
            if (v < 0.02f)  return juce::String ("Sine");
            if (v > 0.98f)  return juce::String ("Saw");
            if (std::abs (v - 0.5f) < 0.02f) return juce::String ("Tri");
            return juce::String (juce::roundToInt (v * 100.0f)) + " %";
        })));

    // 两个调制量各自独立，可以同时开。
    // 同时不为零时等价于给调制源加一条 1/f 倾斜，不是简单的效果叠加。
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { paramPmAmount, 1 }, "PM Amount",
        NormalisableRange<float> { 0.0f, 8.0f, 0.001f, 0.5f }, 1.0f,
        formatted ([] (float v) { return juce::String (v, 2) + " cyc"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { paramFmAmount, 1 }, "FM Amount",
        NormalisableRange<float> { 0.0f, 8.0f, 0.001f, 0.5f }, 0.0f,
        formatted ([] (float v) { return juce::String (juce::roundToInt (v * 500.0f)) + " Hz"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { paramModGain, 1 }, "Mod Gain",
        NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f,
        formatted (formatDb)));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { paramModType, 1 }, "Mod Filter",
        StringArray { "Low Pass", "Band Pass", "High Pass" }, 0));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { paramModFreq, 1 }, "Mod Freq",
        NormalisableRange<float> { 20.0f, 20000.0f, 0.0f, 0.25f }, 2000.0f,
        formatted (formatHz)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { paramModQ, 1 }, "Mod Q",
        NormalisableRange<float> { 0.3f, 12.0f, 0.01f, 0.5f }, 0.707f,
        formatted ([] (float v) { return juce::String (v, 2); })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { paramMix, 1 }, "Mix",
        NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 1.0f,
        formatted ([] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { paramOutput, 1 }, "Output Gain",
        NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f,
        formatted (formatDb)));

    // 音高来源：旋钮固定 / 跟随素材 / MIDI 音符
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { paramPitchMode, 1 }, "Pitch Source",
        StringArray { "Free", "Follow", "MIDI" }, 0));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { paramGlide, 1 }, "Glide",
        NormalisableRange<float> { 1.0f, 500.0f, 0.5f, 0.5f }, 60.0f,
        formatted ([] (float v) { return juce::String (v, v < 10.0f ? 1 : 0) + " ms"; })));

    // Riddim 层：把 PM 炸出来的宽频噪波梳回谐波列，再补一条干净 sub
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { paramCombMix, 1 }, "Comb",
        NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.0f,
        formatted ([] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { paramCombFb, 1 }, "Comb FB",
        NormalisableRange<float> { -0.98f, 0.98f, 0.001f }, 0.0f,
        formatted ([] (float v)
        {
            return juce::String (juce::roundToInt (v * 100.0f)) + (v < 0.0f ? " % (odd)" : " %");
        })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { paramSub, 1 }, "Clean Sub",
        NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.0f,
        formatted ([] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { paramDrive, 1 }, "Drive",
        NormalisableRange<float> { 0.0f, 18.0f, 0.1f }, 0.0f,
        formatted (formatDb)));

    // 自由运行 LFO：控载波音高 / 控调制滤波截止
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { paramLfoRate, 1 }, "LFO Rate",
        NormalisableRange<float> { 0.01f, 50.0f, 0.0f, 0.3f }, 1.0f,
        formatted ([] (float v) { return juce::String (v, v < 10.0f ? 2 : 1) + " Hz"; })));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { paramLfoShape, 1 }, "LFO Shape",
        StringArray { "Sine", "Triangle", "Sample & Hold" }, 0));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { paramLfoPitch, 1 }, "LFO to Pitch",
        NormalisableRange<float> { -48.0f, 48.0f, 0.1f }, 0.0f,
        formatted ([] (float v) { return juce::String (v, 1) + " st"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { paramLfoCutoff, 1 }, "LFO to Cutoff",
        NormalisableRange<float> { -6.0f, 6.0f, 0.01f }, 0.0f,
        formatted ([] (float v) { return juce::String (v, 2) + " oct"; })));

    // 同步：Free 用 Hz 旋钮，Sync 用工程 BPM × 音符长度
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { paramLfoSync, 1 }, "LFO Sync",
        StringArray { "Free", "Sync" }, 0));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { paramLfoGrid, 1 }, "LFO Grid",
        StringArray { "1/4", "1/8", "1/16", "1/32" }, 1));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { paramLfoGridMod, 1 }, "Grid Modifier",
        StringArray { "Straight", "Dotted", "Triplet" }, 0));

    // 输入包络 → 调制矩阵（和 LFO 共用三个目的地）
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { paramEnvPitch, 1 }, "Env to Pitch",
        NormalisableRange<float> { -48.0f, 48.0f, 0.1f }, 0.0f,
        formatted ([] (float v) { return juce::String (v, 1) + " st"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { paramEnvCutoff, 1 }, "Env to Cutoff",
        NormalisableRange<float> { -6.0f, 6.0f, 0.01f }, 0.0f,
        formatted ([] (float v) { return juce::String (v, 2) + " oct"; })));

    // 加性 Hz，不是倍频程：
    // 0.1 Hz 的 LFO 加 2 个八度只有 0.4 Hz —— 听不出东西。
    // 改成直接加 Hz 之后，"响的地方推进到 8 Hz"这种效果才做得出来。
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { paramEnvRate, 1 }, "Env to LFO Rate",
        NormalisableRange<float> { -40.0f, 40.0f, 0.01f }, 0.0f,
        formatted ([] (float v) { return juce::String (v, 2) + " Hz"; })));

    // 调制专用的第二个包络：AM 用上面那组，调制用这组，互不干扰
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { paramEnv2Attack, 1 }, "Mod Env Attack",
        NormalisableRange<float> { 1.0f, 200.0f, 0.1f, 0.5f }, 5.0f,
        formatted ([] (float v) { return juce::String (v, v < 10.0f ? 1 : 0) + " ms"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { paramEnv2Release, 1 }, "Mod Env Release",
        NormalisableRange<float> { 30.0f, 2000.0f, 1.0f, 0.5f }, 180.0f,
        formatted ([] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; })));

    // 输入包络 → AM：AM 恒为满，这个旋钮只缩放包络本身（提安静素材 / 压响素材）
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { paramAmGain, 1 }, "Envelope Gain",
        NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f,
        formatted (formatDb)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { paramAmAttack, 1 }, "AM Attack",
        NormalisableRange<float> { 1.0f, 100.0f, 0.1f, 0.5f }, 5.0f,
        formatted ([] (float v) { return juce::String (v, v < 10.0f ? 1 : 0) + " ms"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { paramAmRelease, 1 }, "AM Release",
        NormalisableRange<float> { 30.0f, 2000.0f, 1.0f, 0.4f }, 250.0f,
        formatted ([] (float v) { return juce::String (v, v < 100.0f ? 0 : 0) + " ms"; })));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { paramBypass, 1 }, "Bypass", false));

    return layout;
}

//==============================================================================
BesselAudioProcessor::BesselAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createLayout()),
      presets (apvts)
{
    createFactoryPresetsIfNeeded();

    carrier = apvts.getRawParameterValue (paramCarrier);
    shape   = apvts.getRawParameterValue (paramShape);
    pmAmount = apvts.getRawParameterValue (paramPmAmount);
    fmAmount = apvts.getRawParameterValue (paramFmAmount);
    modGain = apvts.getRawParameterValue (paramModGain);
    modType = apvts.getRawParameterValue (paramModType);
    modFreq = apvts.getRawParameterValue (paramModFreq);
    modQ    = apvts.getRawParameterValue (paramModQ);
    mix     = apvts.getRawParameterValue (paramMix);
    outDb   = apvts.getRawParameterValue (paramOutput);
    pitchMode = apvts.getRawParameterValue (paramPitchMode);
    glide     = apvts.getRawParameterValue (paramGlide);
    combMix   = apvts.getRawParameterValue (paramCombMix);
    combFb    = apvts.getRawParameterValue (paramCombFb);
    subLevel  = apvts.getRawParameterValue (paramSub);
    drive     = apvts.getRawParameterValue (paramDrive);
    lfoRate   = apvts.getRawParameterValue (paramLfoRate);
    lfoShape  = apvts.getRawParameterValue (paramLfoShape);
    lfoPitch  = apvts.getRawParameterValue (paramLfoPitch);
    lfoCutoff = apvts.getRawParameterValue (paramLfoCutoff);
    lfoSync   = apvts.getRawParameterValue (paramLfoSync);
    lfoGrid   = apvts.getRawParameterValue (paramLfoGrid);
    lfoGridMod= apvts.getRawParameterValue (paramLfoGridMod);
    envPitch  = apvts.getRawParameterValue (paramEnvPitch);
    envCutoff = apvts.getRawParameterValue (paramEnvCutoff);
    envRate   = apvts.getRawParameterValue (paramEnvRate);
    env2Attack  = apvts.getRawParameterValue (paramEnv2Attack);
    env2Release = apvts.getRawParameterValue (paramEnv2Release);
    amGain    = apvts.getRawParameterValue (paramAmGain);
    amAttack  = apvts.getRawParameterValue (paramAmAttack);
    amRelease = apvts.getRawParameterValue (paramAmRelease);
    bypass  = apvts.getRawParameterValue (paramBypass);
}

//==============================================================================
void BesselAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // FIR 等波纹半带滤波：比 IIR polyphase 多花一点 CPU，换更陡的阻带。
    // 这个插件存在的意义就是不混叠，所以选它。
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) 2, (size_t) oversamplingPower,
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
        true);

    oversampling->initProcessing ((size_t) samplesPerBlock);

    // 过采样带来的延迟必须如实上报，否则宿主对不齐轨
    setLatencySamples (juce::roundToInt (oversampling->getLatencyInSamples()));

    const auto factor = (double) oversampling->getOversamplingFactor();
    pm.prepare (sampleRate * factor, samplesPerBlock);

    tracker.prepare (sampleRate);       // 音高跟踪跑在基础采样率上
    inputEnvelope.prepare (sampleRate, samplesPerBlock);
    modEnvelope.prepare (sampleRate, samplesPerBlock);
    pitchControl.prepare (sampleRate);
    pitchControl.setBendSemitones (12.0f);   // 弯音 ±12 st，Riddim 需要大跨度
    wasFollowing = false;
    detectedPitchHz.store (0.0f);
    currentAmGain.store (1.0f);

    lastOutputPeak.store (0.0f);
}

void BesselAudioProcessor::releaseResources()
{
    pm.reset();
}

bool BesselAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return in == out;
}

void BesselAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    if (oversampling == nullptr)
        return;

    // 过采样器按 2 声道建；多出来的声道不参与（总线布局已限制成 mono/stereo）
    juce::dsp::AudioBlock<float> fullBlock (buffer);
    auto block = fullBlock.getSubsetChannelBlock (0, (size_t) juce::jmin (2, buffer.getNumChannels()));

    // 宿主旁通：仍然走一遍上/下采样，保证实际延迟和上报的一致，
    // 否则旁通时这条轨会比别的轨提前
    if (bypass != nullptr && bypass->load() > 0.5f)
    {
        auto up = oversampling->processSamplesUp (block);
        juce::ignoreUnused (up);
        oversampling->processSamplesDown (block);
        lastOutputPeak.store (buffer.getMagnitude (0, numSamples));
        return;
    }

    // ---- 输入包络 → AM ---------------------------------------------------
    // 判断依据必须是干信号。输出里永远有载波，拿它当判据会自己撑住自己、永远不衰减。
    inputEnvelope.setGainDb (amGain->load());
    inputEnvelope.setAttackMs (amAttack->load());
    inputEnvelope.setReleaseMs (amRelease->load());
    inputEnvelope.process (buffer);

    // 调制专用包络：同一路干信号，独立的 Attack / Release
    modEnvelope.setGainDb (0.0f);
    modEnvelope.setAttackMs (env2Attack->load());
    modEnvelope.setReleaseMs (env2Release->load());
    modEnvelope.process (buffer);

    // ---- 音高来源：旋钮 / 跟随素材 / MIDI --------------------------------
    // 跟随的跟踪器跑在基础采样率上（便宜 4 倍），用的是上采样之前的干信号。
    const auto mode      = (int) pitchMode->load();
    const auto following = (mode == 1);

    if (following != wasFollowing)
    {
        tracker.reset();          // 刚打开时别拿旧素材的历史去猜
        wasFollowing = following;
    }

    float carrierHz = carrier->load();   // 没跟随 / 跟丢 / 没按音时的值

    if (following)
    {
        tracker.push (buffer);

        const auto tracked = tracker.getFrequencyHz();

        if (tracked > 0.0f)
            carrierHz = juce::jlimit (20.0f, 5000.0f, tracked);

        detectedPitchHz.store (tracked);
    }
    else if (mode == 2)
    {
        // MIDI：音符接管载波；没按住的音就回落旋钮值（它仍是效果器，不是合成器）
        pitchControl.setGlideMs (glide->load());
        pitchControl.process (midi, numSamples);

        if (pitchControl.isActive())
        {
            carrierHz = juce::jlimit (20.0f, 5000.0f, pitchControl.getFrequencyHz());
            detectedPitchHz.store (carrierHz);
        }
        else
        {
            detectedPitchHz.store (0.0f);
        }

        if (pitchControl.consumeRetrigger())
            pm.requestPhaseReset();    // Note-On：相位归零 + 1 ms 淡入
    }
    else
    {
        detectedPitchHz.store (0.0f);
    }

    // ---- LFO 频率：Free 用旋钮，Sync 用工程 BPM × 音符长度 ----------------
    double bpm = 120.0;

    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
            if (auto hostBpm = position->getBpm())
                if (*hostBpm > 1.0)
                    bpm = *hostBpm;

    currentBpm.store (bpm);

    float lfoHz = lfoRate->load();

    if (lfoSync->load() > 0.5f)
    {
        // 0 → 1/4（1 拍）、1 → 1/8（0.5 拍）、2 → 1/16、3 → 1/32
        const auto beatsPerCycle = 1.0 / std::pow (2.0, (double) (int) lfoGrid->load());

        const auto modifier = (int) lfoGridMod->load() == 1 ? 1.5        // 附点
                            : (int) lfoGridMod->load() == 2 ? 2.0 / 3.0  // 三连
                                                            : 1.0;

        lfoHz = (float) ((bpm / 60.0) / (beatsPerCycle * modifier));
    }

    // 界面上显示的是"实际生效"的 LFO 速率：把包络的调制算进去，
    // 这样拧 ENV>RATE 时能直接看到数字在变，不用靠耳朵猜。
    {
        const auto envNow  = juce::jlimit (0.0f, 1.0f, modEnvelope.getCurrentEnvelope());
        const auto effective = lfoHz + std::sqrt (envNow) * envRate->load();
        currentLfoHz.store (juce::jlimit (0.0f, 400.0f, effective));
    }

    pm.setParams ({ carrierHz,
                    shape->load(),
                    pmAmount->load(),
                    fmAmount->load(),
                    modGain->load(),
                    modFreq->load(),
                    (int) modType->load(),
                    modQ->load(),
                    mix->load(),
                    outDb->load(),
                    combMix->load(),
                    combFb->load(),
                    subLevel->load(),
                    drive->load(),
                    lfoHz,
                    (int) lfoShape->load(),
                    lfoPitch->load(),
                    lfoCutoff->load(),
                    envPitch->load(),
                    envCutoff->load(),
                    envRate->load() });

    // 包络增益交给 PmEffect，只乘在湿信号上（干信号不经过 AM）
    // 第一路是 AM 增益（乘在湿信号上），第二路是调制源的归一化包络
    pm.setEnvelopeGains (inputEnvelope.getGains().data(),
                         modEnvelope.getEnvelope().data(),
                         numSamples);
    currentCarrierHz.store (carrierHz);

    auto upsampled = oversampling->processSamplesUp (block);
    pm.process (upsampled);
    oversampling->processSamplesDown (block);

    currentAmGain.store (inputEnvelope.getCurrentGain());

    // 示波器缓冲：只取左声道，最新样本覆盖最旧的
    {
        const auto* data = buffer.getReadPointer (0);
        int pos = scopeWritePos.load (std::memory_order_relaxed);

        for (int i = 0; i < numSamples; ++i)
        {
            scopeBuffer[(size_t) pos] = data[i];

            if (++pos >= scopeSize)
                pos = 0;
        }

        scopeWritePos.store (pos, std::memory_order_relaxed);
    }

    lastOutputPeak.store (buffer.getMagnitude (0, numSamples));
}

//==============================================================================
// 工厂预设：只在预设目录**空的时候**写一次。
// 之后就是普通文件，用户改了 / 删了都不会被覆盖回来。
void BesselAudioProcessor::createFactoryPresetsIfNeeded()
{
    presets.rescan();

    // 版本戳不匹配、或 Factory 目录空了才重建。
    // 用户自己存的预设一半在 User 目录，这里永远不碰。
    if (! sfx::PresetManager::factoryNeedsRebuild())
        return;

    struct FactoryPreset
    {
        const char* name;
        std::vector<std::pair<const char*, float>> values;
    };

    // 工厂预设表。Init 之外全部是作者实际调出来的音色 ——
    // 只记录与默认值不同的参数，其余保持默认（这样"不带 comb 的预设"里
    // comb 相关参数天然就是初始值，不会带着上一个预设的残留）。
    const std::vector<FactoryPreset> factories
    {
        { "Init", {} },

        { "Folder Chop", { { "amattack", 1.0f }, { "carrier", 0.0f }, { "drive", 8.0f },
                           { "lfocutoff", 2.0f }, { "modfreq", 7999.9819f }, { "modq", 1.57f },
                           { "pmamount", 4.0f }, { "shape", 0.2f } } },

        { "Metal Comb", { { "carrier", 41.9183f }, { "combfb", 0.6f }, { "combmix", 0.8f },
                          { "drive", 6.4f }, { "modfreq", 3000.0f }, { "modtype", 1.0f },
                          { "pmamount", 3.5f }, { "shape", 0.36f }, { "sub", 0.394f } } },

        { "Sub Rumble", { { "amgain", 6.5f }, { "carrier", 43.5599f }, { "drive", 3.0f },
                          { "env2attack", 26.6f }, { "envcutoff", 2.06f }, { "modfreq", 400.0001f },
                          { "modgain", 6.6f }, { "pmamount", 1.747f }, { "shape", 0.149f },
                          { "sub", 0.503f } } },

        { "All Glitch", { { "carrier", 41.9183f }, { "lfocutoff", 2.94f }, { "lforate", 21.0363f },
                          { "lfoshape", 2.0f }, { "modq", 7.65f }, { "modtype", 1.0f },
                          { "pmamount", 8.0f }, { "shape", 0.4f } } },

        { "All Glitch Comb", { { "carrier", 41.9183f }, { "combfb", 0.605f }, { "combmix", 0.697f },
                               { "lfocutoff", 2.94f }, { "lforate", 21.0363f }, { "lfoshape", 2.0f },
                               { "modq", 7.65f }, { "modtype", 1.0f }, { "pmamount", 8.0f },
                               { "shape", 0.4f } } },

        // 给有音高的素材补高频谐波：载波 0 Hz（folder）+ 包络推截止 + Follow
        { "Overtone", { { "carrier", 0.0f }, { "drive", 4.0f }, { "env2attack", 1.0f },
                        { "envcutoff", 6.0f }, { "modfreq", 1984.9417f }, { "pitchmode", 1.0f },
                        { "pmamount", 8.0f }, { "shape", 0.303f }, { "sub", 0.2f } } }
    };

    for (const auto& factory : factories)
    {
        // 先回到默认值，再逐项覆盖 —— 否则上一个预设的残留会漏进来
        for (auto* parameter : getParameters())
            if (auto* withId = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            {
                withId->setValueNotifyingHost (withId->getDefaultValue());
                juce::ignoreUnused (withId);
            }

        for (const auto& [id, value] : factory.values)
            if (auto* parameter = apvts.getParameter (id))
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                    ranged->setValueNotifyingHost (ranged->convertTo0to1 (value));

        presets.save (factory.name, true);   // true = 写进 Factory 目录
    }

    sfx::PresetManager::writeVersionStamp();
    presets.rescan();

    // 不管写没写成功，参数都必须回到默认值 ——
    // 否则插件会以上一个工厂预设（Folder Chop，载波 0）启动。
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());
}

//==============================================================================
void BesselAudioProcessor::readLfoHistory (float* destination, int numSamples) const noexcept
{
    pm.readLfoHistory (destination, numSamples);
}

void BesselAudioProcessor::readScope (float* destination, int numSamples) const noexcept
{
    if (destination == nullptr || numSamples <= 0)
        return;

    numSamples = juce::jmin (numSamples, scopeSize);

    int index = scopeWritePos.load (std::memory_order_relaxed) - numSamples;

    while (index < 0)
        index += scopeSize;

    for (int i = 0; i < numSamples; ++i)
    {
        destination[i] = scopeBuffer[(size_t) (index % scopeSize)];
        ++index;
    }
}

//==============================================================================
juce::AudioProcessorEditor* BesselAudioProcessor::createEditor()
{
    auto* editor = new BesselAudioProcessorEditor (*this);

    // 恢复上次的窗口大小（存在同一个 ValueTree 里，随工程一起保存）
    const auto width  = (int) apvts.state.getProperty ("uiWidth", 1000);
    const auto height = (int) apvts.state.getProperty ("uiHeight", 838);

    if (width >= 820 && height >= 500)
        editor->setSize (width, height);

    return editor;
}

//==============================================================================
void BesselAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void BesselAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessorParameter* BesselAudioProcessor::getBypassParameter() const
{
    return apvts.getParameter (paramBypass);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BesselAudioProcessor();
}
