#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <memory>

#include "dsp/PmEffect.h"
#include "dsp/PitchTracker.h"
#include "dsp/PitchController.h"
#include "dsp/InputEnvelope.h"
#include "PresetManager.h"

//==============================================================================
// 插件核心。
//
// 信号路径：
//   宿主 buffer(基础采样率) → [4x 上采样] → PmEffect(在 4x 上跑) → [4x 下采样] → 宿主
//
// 过采样不是为了"更干净"，是因为音频速率 PM 的贝塞尔边带会冲到几十 kHz，
// 不升频就在基础采样率上折叠回来，听感是廉价数码杂音。
//
// 参数全部走 APVTS；processBlock 里只 load() 缓存的原子指针，不做查找。
//==============================================================================
class BesselAudioProcessor final : public juce::AudioProcessor
{
public:
    BesselAudioProcessor();
    ~BesselAudioProcessor() override = default;

    //--- AudioProcessor 接口 ---------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }

    const juce::String getName() const override              { return "Bessel"; }
    bool acceptsMidi() const override                        { return false; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 0.0; }

    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override          { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    //--- 参数 ------------------------------------------------------------
    static constexpr const char* paramCarrier = "carrier";   // Hz
    static constexpr const char* paramShape    = "shape";     // 0 正弦 / 0.5 三角 / 1 带限锯齿
    static constexpr const char* paramPmAmount = "pmamount";  // 相位偏移，圈
    static constexpr const char* paramFmAmount = "fmamount";  // 频率偏移，× 500 Hz
    static constexpr const char* paramModGain = "modgain";   // dB
    static constexpr const char* paramModType = "modtype";   // 0 LP / 1 BP / 2 HP
    static constexpr const char* paramModFreq = "modfreq";   // Hz
    static constexpr const char* paramModQ    = "modq";
    static constexpr const char* paramMix     = "mix";       // 0..1
    static constexpr const char* paramOutput  = "output";    // dB

    // 音高来源：0 = 旋钮固定 / 1 = 跟随素材 / 2 = MIDI 音符
    static constexpr const char* paramPitchMode = "pitchmode";
    static constexpr const char* paramGlide     = "glide";     // ms

    // Riddim 层
    static constexpr const char* paramCombMix  = "combmix";    // 0..1
    static constexpr const char* paramCombFb   = "combfb";     // −0.98..0.98，负值 = 奇次谐波
    static constexpr const char* paramSub      = "sub";        // 0..1
    static constexpr const char* paramDrive    = "drive";      // dB

    // 自由运行 LFO：音高 / 截止
    static constexpr const char* paramLfoRate   = "lforate";
    static constexpr const char* paramLfoShape  = "lfoshape";
    static constexpr const char* paramLfoPitch  = "lfopitch";
    static constexpr const char* paramLfoCutoff = "lfocutoff";
    static constexpr const char* paramLfoSync   = "lfosync";     // 0 Free / 1 Sync
    static constexpr const char* paramLfoGrid   = "lfogrid";     // 1/4 1/8 1/16 1/32
    static constexpr const char* paramLfoGridMod= "lfogridmod";  // straight / dotted / triplet

    // 输入包络 → 调制矩阵
    static constexpr const char* paramEnvPitch  = "envpitch";    // 半音
    static constexpr const char* paramEnvCutoff = "envcutoff";   // 八度
    static constexpr const char* paramEnvRate   = "envrate";     // 八度

    // 调制专用的第二个包络（AM 仍然用上面那组 Attack/Release）
    static constexpr const char* paramEnv2Attack  = "env2attack";
    static constexpr const char* paramEnv2Release = "env2release";

    // 输入包络 → AM：没输入自动静音；Gain 只缩放包络，不放干信号漏出来
    static constexpr const char* paramAmGain    = "amgain";
    static constexpr const char* paramAmAttack  = "amattack";
    static constexpr const char* paramAmRelease = "amrelease";

    static constexpr const char* paramBypass  = "bypass";

    static constexpr int numParamsWithoutBypass = 31;

    // 2^2 = 4 倍过采样
    static constexpr int oversamplingPower = 2;

    // 给界面示波器用的环形缓冲（只在 UI 里读，撕裂无所谓）
    static constexpr int scopeSize = 1024;

    juce::AudioProcessorValueTreeState apvts;
    sfx::PresetManager presets;

    // 给冒烟测试 / 电平表读
    std::atomic<float> lastOutputPeak { 0.0f };

    // 给界面显示"跟到的音高"，0 表示没跟到
    std::atomic<float> detectedPitchHz { 0.0f };

    // 给界面显示输入包络当前的开合程度（0 = 静音，>1 = 包络被提起）
    std::atomic<float> currentAmGain { 0.0f };

    // 实际送去振荡器的载波频率（跟随时是检测值，跟丢时是旋钮值）
    std::atomic<float> currentCarrierHz { 0.0f };

    // 宿主 BPM（同步 LFO 用）；宿主没给就按 120
    std::atomic<double> currentBpm { 120.0 };

    // 实际生效的 LFO 频率（同步时是换算结果，界面可以显示）
    std::atomic<float> currentLfoHz { 1.0f };

    /** 把最近 numSamples 个输出样本按时间顺序拷出来（仅供 UI）。 */
    void readScope (float* destination, int numSamples) const noexcept;

    /** 把 LFO 最近的波形拷出来（仅供界面）。 */
    void readLfoHistory (float* destination, int numSamples) const noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void createFactoryPresetsIfNeeded();

    std::atomic<float>* carrier = nullptr;
    std::atomic<float>* shape   = nullptr;
    std::atomic<float>* pmAmount = nullptr;
    std::atomic<float>* fmAmount = nullptr;
    std::atomic<float>* modGain = nullptr;
    std::atomic<float>* modType = nullptr;
    std::atomic<float>* modFreq = nullptr;
    std::atomic<float>* modQ    = nullptr;
    std::atomic<float>* mix     = nullptr;
    std::atomic<float>* outDb   = nullptr;
    std::atomic<float>* pitchMode = nullptr;
    std::atomic<float>* glide     = nullptr;
    std::atomic<float>* combMix   = nullptr;
    std::atomic<float>* combFb    = nullptr;
    std::atomic<float>* subLevel  = nullptr;
    std::atomic<float>* drive     = nullptr;
    std::atomic<float>* lfoRate   = nullptr;
    std::atomic<float>* lfoShape  = nullptr;
    std::atomic<float>* lfoPitch  = nullptr;
    std::atomic<float>* lfoCutoff = nullptr;
    std::atomic<float>* lfoSync   = nullptr;
    std::atomic<float>* lfoGrid   = nullptr;
    std::atomic<float>* lfoGridMod= nullptr;
    std::atomic<float>* envPitch  = nullptr;
    std::atomic<float>* envCutoff = nullptr;
    std::atomic<float>* envRate   = nullptr;
    std::atomic<float>* env2Attack  = nullptr;
    std::atomic<float>* env2Release = nullptr;
    std::atomic<float>* amGain    = nullptr;
    std::atomic<float>* amAttack  = nullptr;
    std::atomic<float>* amRelease = nullptr;
    std::atomic<float>* bypass  = nullptr;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    sfx::PmEffect pm;
    sfx::PitchTracker tracker;
    sfx::PitchController pitchControl;
    sfx::InputEnvelope inputEnvelope;   // AM 用
    sfx::InputEnvelope modEnvelope;     // 调制用（独立 Attack / Release）

    bool wasFollowing = false;

    std::array<float, scopeSize> scopeBuffer {};
    std::atomic<int> scopeWritePos { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BesselAudioProcessor)
};
