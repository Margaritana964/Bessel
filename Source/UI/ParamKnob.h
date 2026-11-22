#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace sfx::ui
{

//==============================================================================
// 一个旋钮 + 上面标题 + 下面数值。数值用参数自己的格式化文本（Hz / dB 都对）。
//==============================================================================
class ParamKnob final : public juce::Component
{
public:
    ParamKnob (juce::AudioProcessorValueTreeState& apvts,
               const juce::String& parameterID,
               const juce::String& titleText,
               juce::Colour accentColourToUse,
               bool large = false,
               const juce::String& suffix = {});

    void resized() override;

    /** 由编辑器的 timer 定期调用，跟上自动化和预设切换。 */
    void refresh();

private:
    juce::Slider slider;
    juce::Label  titleLabel, valueLabel;

    juce::RangedAudioParameter* parameter = nullptr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    juce::String suffix;
    bool large;
};

} // namespace sfx::ui
