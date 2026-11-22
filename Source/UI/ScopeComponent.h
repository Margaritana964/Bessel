#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class BesselAudioProcessor;

namespace sfx::ui
{

//==============================================================================
// 示波器：从插件的环形缓冲取最近一段输出，以向上过零点做触发，
// 这样画面不会左右乱飘。
//==============================================================================
class ScopeComponent final : public juce::Component
{
public:
    explicit ScopeComponent (BesselAudioProcessor& processorToUse);

    /** 由编辑器的 timer 调用。 */
    void refresh();

    void paint (juce::Graphics&) override;

private:
    BesselAudioProcessor& processor;

    static constexpr int captureSize = 1024;
    float captured[captureSize] {};
};

} // namespace sfx::ui
