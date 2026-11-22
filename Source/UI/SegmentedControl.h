#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace sfx::ui
{

//==============================================================================
// 分段选择器：把一个 choice 参数画成并排的几格，点哪格设哪个值。
// Waveform（2 格）和 Mod Filter（3 格）共用这一个。
//==============================================================================
class SegmentedControl final : public juce::Component
{
public:
    SegmentedControl (juce::RangedAudioParameter& parameterToControl,
                      juce::StringArray itemLabels,
                      juce::Colour accentColourToUse);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void applyFromHost (float denormalisedValue);

    juce::RangedAudioParameter& parameter;

    // items 必须声明在 attachment 之前：attachment 构造时会立刻回调一次
    juce::StringArray items;
    juce::Colour accentColour;

    juce::ParameterAttachment attachment;
    int current = 0;
    int hovered = -1;
};

} // namespace sfx::ui
