//==============================================================================
// 把编辑器离屏渲染成 PNG —— 不开宿主、不弹窗口，改完界面立刻能看。
//
//   UiSnapshot.exe [输出.png]
//
// 默认写到可执行文件旁边，避免中文路径经 argv 传进来被转成 ANSI。
//==============================================================================

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

#include <cmath>
#include <iostream>

namespace
{
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
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    BesselAudioProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    // 摆一个像样的状态：220 Hz 载波、深一点的调制、跟随打开
    setFloat (processor.apvts, BesselAudioProcessor::paramCarrier, 220.0f);
    setFloat (processor.apvts, BesselAudioProcessor::paramPmAmount,   2.4f);
    setFloat (processor.apvts, BesselAudioProcessor::paramModGain, 3.0f);
    setFloat (processor.apvts, BesselAudioProcessor::paramModFreq, 3000.0f);
    setFloat (processor.apvts, BesselAudioProcessor::paramModQ,    1.4f);
    setFloat (processor.apvts, BesselAudioProcessor::paramMix,     0.85f);
    setFloat (processor.apvts, BesselAudioProcessor::paramOutput,  -1.5f);
    setFloat (processor.apvts, BesselAudioProcessor::paramCombMix, 0.35f);
    setFloat (processor.apvts, BesselAudioProcessor::paramCombFb,  0.45f);
    setFloat (processor.apvts, BesselAudioProcessor::paramSub,     0.4f);
    setFloat (processor.apvts, BesselAudioProcessor::paramDrive,   4.0f);
    setFloat (processor.apvts, BesselAudioProcessor::paramGlide,   45.0f);
    setChoice (processor.apvts, BesselAudioProcessor::paramPitchMode, 1);   // Follow

    // 灌一段 110 Hz 进去：示波器有波形，音高跟到 110 Hz
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    for (int block = 0; block < 60; ++block)
    {
        for (int i = 0; i < 512; ++i)
        {
            const auto t = (double) (block * 512 + i) / 48000.0;
            const auto v = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 110.0 * t);

            buffer.getWritePointer (0)[i] = v;
            buffer.getWritePointer (1)[i] = v;
        }

        processor.processBlock (buffer, midi);
    }

    BesselAudioProcessorEditor editor (processor);
    editor.setSize (1000, 838);

    // 手动刷一拍：标签、示波器、跟随状态都变成"活"的样子（不依赖消息循环）
    editor.refreshNow();

    const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);

    // 第二张：预设浏览器打开的样子（同一个 exe，多出一张图）
    const auto presetImage = [&]
    {
        editor.setPresetBrowserVisible (true);
        const auto shot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
        editor.setPresetBrowserVisible (false);
        return shot;
    }();

    const auto presetFile = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                                .getParentDirectory()
                                .getChildFile ("ui-snapshot-presets.png");

    if (presetImage.isValid())
    {
        presetFile.deleteFile();

        if (juce::FileOutputStream presetStream (presetFile); presetStream.openedOk())
        {
            juce::PNGImageFormat png;
            png.writeImageToStream (presetImage, presetStream);
            std::cout << "wrote " << presetFile.getFullPathName().toStdString()
                      << " (" << presetImage.getWidth() << "x" << presetImage.getHeight() << ")\n";
        }
    }

    auto outputFile = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                          .getParentDirectory()
                          .getChildFile ("ui-snapshot.png");

    if (argc > 1)
        outputFile = juce::File (juce::String::fromUTF8 (argv[1]));

    outputFile.deleteFile();

    juce::FileOutputStream stream (outputFile);

    if (! stream.openedOk())
    {
        std::cerr << "cannot open output file\n";
        return 1;
    }

    juce::PNGImageFormat png;

    if (! png.writeImageToStream (image, stream))
    {
        std::cerr << "PNG write failed\n";
        return 1;
    }

    stream.flush();

    std::cout << "wrote " << outputFile.getFullPathName().toRawUTF8()
              << " (" << image.getWidth() << "x" << image.getHeight() << ")\n";

    return 0;
}
