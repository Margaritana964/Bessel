#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sfx::ui
{

//==============================================================================
// 全部自绘：旋钮画成弧形轨道 + 指针，按钮画成胶囊，唯一目的是让这个插件
// 看起来不像一个调试工具。
//==============================================================================
class PmLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    PmLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
};

} // namespace sfx::ui
