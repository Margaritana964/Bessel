//==============================================================================
// 宿主视角验证：像 DAW 那样去找、去加载这个 .vst3，然后跑一段音频。
//
//   PluginScanTest.exe       返回 0 = 通过
//
// 只有这个测试过了，才说明「DAW 能扫到、能用」；ChainSmokeTest 只证明 DSP 对。
//
// 路径从可执行文件位置反推，不走 argv —— 项目路径里有中文，
// Windows 的 main(argv) 是 ANSI，传进来会乱码。
//==============================================================================

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <iostream>

namespace
{
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
} // namespace

//==============================================================================
int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // build/PluginScanTest_artefacts/Release/PluginScanTest.exe → 上三级到 build/
    const auto exeDir   = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    const auto buildDir = exeDir.getParentDirectory().getParentDirectory();

    const auto vst3 = buildDir.getChildFile ("Bessel_artefacts")
                              .getChildFile ("Release")
                              .getChildFile ("VST3")
                              .getChildFile ("Bessel.vst3");

    std::cout << "VST3 bundle: " << vst3.getFullPathName() << "\n\n";

    check (vst3.isDirectory(), "VST3 bundle exists on disk");

    if (! vst3.isDirectory())
        return 1;

    //--- 1. 扫描：宿主能不能认出来 ------------------------------------------
    juce::AudioPluginFormatManager formatManager;
    juce::addDefaultFormatsToManager (formatManager);

    juce::OwnedArray<juce::PluginDescription> found;

    std::cout << "    registered formats: " << formatManager.getNumFormats() << "\n";

    for (auto* format : formatManager.getFormats())
    {
        std::cout << "      - " << format->getName() << "\n";

        juce::OwnedArray<juce::PluginDescription> perFormat;

        try
        {
            format->findAllTypesForFile (perFormat, vst3.getFullPathName());
        }
        catch (...)
        {
            // 某些格式会拒绝别的格式的 bundle，忽略即可
        }

        std::cout << "        found: " << perFormat.size() << "\n";
        found.addCopiesOf (perFormat);
    }

    check (found.size() >= 1, "host scanner finds the plugin",
           "types=" + juce::String (found.size()));

    if (found.isEmpty())
        return 1;

    const auto& desc = *found[0];
    std::cout << "    name         : " << desc.name << "\n"
              << "    manufacturer : " << desc.manufacturerName << "\n"
              << "    category     : " << desc.category << "\n"
              << "    format       : " << desc.pluginFormatName << "\n"
              << "    uniqueId     : " << desc.uniqueId << "\n\n";

    check (desc.name == "Bessel", "reported name is correct", desc.name);
    check (desc.pluginFormatName == "VST3", "reported as VST3", desc.pluginFormatName);

    //--- 2. 实例化 ----------------------------------------------------------
    juce::String error;
    auto instance = formatManager.createPluginInstance (desc, 48000.0, 512, error);

    check (instance != nullptr, "host can instantiate it", error);

    if (instance == nullptr)
        return 1;

    check (instance->getParameters().size() == 32, "exposes 32 automatable parameters",
           "count=" + juce::String (instance->getParameters().size()));

    check (instance->getName() == "Bessel", "instance reports its name", instance->getName());

    //--- 3. 跑音频 ----------------------------------------------------------
    instance->setPlayConfigDetails (2, 2, 48000.0, 512);
    instance->prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    juce::Random rng (7);

    bool finite = true;
    double sumSquares = 0.0;
    long long count = 0;

    for (int block = 0; block < 30; ++block)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.getWritePointer (ch)[i] = 0.1f * (rng.nextFloat() * 2.0f - 1.0f);

        instance->processBlock (buffer, midi);

        if (block < 4)
            continue;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const auto v = buffer.getReadPointer (ch)[i];

                if (! std::isfinite (v))
                    finite = false;

                sumSquares += (double) v * (double) v;
                ++count;
            }
    }

    const auto rms = count > 0 ? std::sqrt (sumSquares / (double) count) : 0.0;

    check (finite, "no NaN / Inf through the hosted instance");
    check (rms > 0.001, "hosted instance passes audio", "rms=" + juce::String (rms, 6));

    instance->releaseResources();

    //--- 4. 编辑器也能建起来 -------------------------------------------------
    // 参数 ID 对不上、attachment 绑错，都会在这一步炸。宿主里一旦崩就是用户的锅。
    if (auto* editor = instance->createEditorIfNeeded())
    {
        check (editor->getWidth() > 400 && editor->getHeight() > 300,
               "editor can be created and has a sensible size",
               juce::String (editor->getWidth()) + "x" + juce::String (editor->getHeight()));

        delete editor;
    }
    else
    {
        check (false, "editor can be created", "createEditorIfNeeded returned nullptr");
    }

    std::cout << "\n";

    if (failures == 0)
    {
        std::cout << "ALL CHECKS PASSED\n";
        return 0;
    }

    std::cout << failures << " CHECK(S) FAILED\n";
    return 1;
}
