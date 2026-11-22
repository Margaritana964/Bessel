//==============================================================================
// 音高跟踪的诊断工具。
//
//   TrackReport.exe
//
// 把 test-material/ 里的每个 wav 喂给插件，每 200 ms 采一次
// 「检测到的音高」和「实际送给振荡器的载波频率」，然后打一张表。
//
// 这是验证 Follow 最快的方式：不用开宿主、不用耳朵，直接看数字。
//==============================================================================

#include "../Source/PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <iostream>
#include <vector>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 512;
constexpr double kSampleEvery = 0.2;   // 秒

struct Report
{
    double lockSeconds  = -1.0;   // 第一次跟到音高用了多久
    double medianHz     = 0.0;
    double minHz        = 0.0;
    double maxHz        = 0.0;
    double medianCarrier = 0.0;
    double coverage     = 0.0;    // 有音高的采样占比
};

Report analyse (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    Report report;

    if (reader == nullptr)
        return report;

    BesselAudioProcessor processor;
    processor.setPlayConfigDetails (2, 2, kSampleRate, kBlockSize);
    processor.prepareToPlay (kSampleRate, kBlockSize);

    // 关掉调制，输出就是纯载波；跟随打开，包络保持默认
    auto setFloat = [&processor] (const char* id, float v)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*> (processor.apvts.getParameter (id)))
            p->setValueNotifyingHost (p->convertTo0to1 (v));
    };
    auto setChoice = [&processor] (const char* id, int v)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (id)))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) v));
    };

    setFloat (BesselAudioProcessor::paramCarrier,  500.0f);   // 跟丢时的回退值
    setFloat (BesselAudioProcessor::paramPmAmount, 0.0f);
    setFloat (BesselAudioProcessor::paramFmAmount, 0.0f);
    setFloat (BesselAudioProcessor::paramMix,      1.0f);
    setChoice (BesselAudioProcessor::paramPitchMode, 1);   // Follow

    const auto totalSamples = (juce::int64) reader->lengthInSamples;
    const auto totalBlocks  = (int) (totalSamples / kBlockSize);

    juce::AudioBuffer<float> block (2, kBlockSize);
    juce::MidiBuffer midi;

    std::vector<double> pitches, carriers;
    double pendingSamples = 0.0;
    const auto sampleStep = kSampleEvery * kSampleRate;

    for (int b = 0; b < totalBlocks; ++b)
    {
        reader->read (&block, 0, kBlockSize, (juce::int64) b * kBlockSize, true, true);
        processor.processBlock (block, midi);

        pendingSamples += kBlockSize;

        if (pendingSamples >= sampleStep)
        {
            pendingSamples = 0.0;

            const auto hz = processor.detectedPitchHz.load();
            pitches.push_back (hz);
            carriers.push_back (processor.currentCarrierHz.load());

            if (hz > 0.0f && report.lockSeconds < 0.0)
                report.lockSeconds = (double) b * kBlockSize / kSampleRate;
        }
    }

    std::vector<double> valid;

    for (auto hz : pitches)
        if (hz > 0.0)
            valid.push_back (hz);

    report.coverage = pitches.empty() ? 0.0
                                      : 100.0 * (double) valid.size() / (double) pitches.size();

    // 载波中位数要按「全部采样」算，不能只算跟到音高的那些：
    // 没跟到时载波会回退到旋钮值，这本身就是最需要看到的信息。
    if (! carriers.empty())
    {
        auto car = carriers;
        std::sort (car.begin(), car.end());
        report.medianCarrier = car[car.size() / 2];
    }

    if (! valid.empty())
    {
        std::sort (valid.begin(), valid.end());
        report.minHz    = valid.front();
        report.maxHz    = valid.back();
        report.medianHz = valid[valid.size() / 2];
    }

    return report;
}
} // namespace

//==============================================================================
// 跟踪阶跃响应：先喂 440 Hz，再突变到 110 Hz，看载波多久跟上。
// 这个数字就是「跟着输入换音高时，输出会慢多少」。
namespace
{
void stepResponse()
{
    BesselAudioProcessor processor;
    processor.setPlayConfigDetails (2, 2, kSampleRate, kBlockSize);
    processor.prepareToPlay (kSampleRate, kBlockSize);

    auto setFloat = [&processor] (const char* id, float v)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*> (processor.apvts.getParameter (id)))
            p->setValueNotifyingHost (p->convertTo0to1 (v));
    };
    auto setChoice = [&processor] (const char* id, int v)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (id)))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) v));
    };

    setFloat (BesselAudioProcessor::paramCarrier,  500.0f);
    setFloat (BesselAudioProcessor::paramPmAmount, 0.0f);
    setFloat (BesselAudioProcessor::paramFmAmount, 0.0f);
    setFloat (BesselAudioProcessor::paramMix,      1.0f);
    setChoice (BesselAudioProcessor::paramPitchMode, 1);   // Follow

    juce::AudioBuffer<float> block (2, kBlockSize);
    juce::MidiBuffer midi;

    const auto switchSample = (juce::int64) (0.4 * kSampleRate);      // 0.4 s 处突变
    const auto totalBlocks  = (int) (1.0 * kSampleRate / kBlockSize);

    struct Sample { double t; float hz; };
    std::vector<Sample> trace;
    double sinceSample = 0.0;

    for (int b = 0; b < totalBlocks; ++b)
    {
        for (int i = 0; i < kBlockSize; ++i)
        {
            const auto n = (juce::int64) b * kBlockSize + i;
            const auto hz = n < switchSample ? 440.0 : 110.0;
            const auto v = (float) std::sin (juce::MathConstants<double>::twoPi * hz
                                             * (double) n / kSampleRate);
            block.getWritePointer (0)[i] = v;
            block.getWritePointer (1)[i] = v;
        }

        processor.processBlock (block, midi);

        sinceSample += kBlockSize;

        if (sinceSample >= 0.01 * kSampleRate)     // 每 10 ms 采一次
        {
            sinceSample = 0.0;
            trace.push_back ({ (double) (b + 1) * kBlockSize / kSampleRate,
                               processor.currentCarrierHz.load() });
        }
    }

    // 突变发生在 0.4 s；找载波从 440 掉到 110 的 10% / 50% / 90% 时刻
    constexpr double from = 440.0, to = 110.0;

    auto crossing = [&trace, from, to] (double fraction)
    {
        const auto target = from + (to - from) * fraction;

        for (const auto& s : trace)
            if (s.t > 0.4 && s.hz <= target)
                return s.t - 0.4;

        return -1.0;
    };

    std::cout << "\n跟踪阶跃响应（440 Hz → 110 Hz，突变点 0.40 s）\n\n";
    std::cout << "   在 0.40 s 之后，载波跟随到新音高所需时间：\n";
    std::cout << "     10%  " << juce::String (crossing (0.1), 3).toRawUTF8() << " s\n";
    std::cout << "     50%  " << juce::String (crossing (0.5), 3).toRawUTF8() << " s\n";
    std::cout << "     90%  " << juce::String (crossing (0.9), 3).toRawUTF8() << " s\n";

    std::cout << "\n   前 0.5 s 的载波频率轨迹：\n      ";
    for (const auto& s : trace)
        if (s.t > 0.38 && s.t < 0.72)
            std::cout << juce::String (s.hz, 0).toRawUTF8() << " ";

    std::cout << " Hz\n";
}
} // namespace

//==============================================================================
int main()
{
    const auto materialDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                                 .getParentDirectory()     // Release
                                 .getParentDirectory()     // TrackReport_artefacts
                                 .getParentDirectory()     // build
                                 .getParentDirectory()     // 工程根
                                 .getChildFile ("test-material");

    if (! materialDir.isDirectory())
    {
        std::cout << "找不到 test-material: "
                  << materialDir.getFullPathName().toRawUTF8() << "\n";
        return 1;
    }

    const auto files = materialDir.findChildFiles (juce::File::findFiles, false, "*.wav");

    std::cout << "音高跟踪诊断（载波回退值 500 Hz）\n\n";
    std::cout << "文件                              跟随锁定   中位音高    范围            载波中位   有效占比\n";
    std::cout << "--------------------------------------------------------------------------------------------\n";

    for (const auto& f : files)
    {
        const auto r = analyse (f);

        std::cout << f.getFileName().toRawUTF8();
        const auto pad = 34 - (int) f.getFileName().length();
        if (pad > 0) std::cout << std::string ((size_t) pad, ' ');

        std::cout << (r.lockSeconds >= 0.0
                        ? juce::String (r.lockSeconds, 2)
                        : juce::String ("---")).toRawUTF8();
        std::cout << " s   ";

        std::cout << juce::String (r.medianHz, 1).toRawUTF8();
        std::cout << " Hz   ";

        const auto range = juce::String (r.minHz, 1) + "-" + juce::String (r.maxHz, 1);
        std::cout << range.toRawUTF8();
        std::cout << std::string ((size_t) juce::jmax (1, 16 - (int) range.length()), ' ');

        std::cout << juce::String (r.medianCarrier, 1).toRawUTF8();
        std::cout << " Hz   ";

        std::cout << juce::String (r.coverage, 0).toRawUTF8() << " %\n";
    }

    stepResponse();

    return 0;
}
