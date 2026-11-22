#include "ParamKnob.h"
#include "Palette.h"

namespace sfx::ui
{

ParamKnob::ParamKnob (juce::AudioProcessorValueTreeState& apvts,
                      const juce::String& parameterID,
                      const juce::String& titleText,
                      juce::Colour accentColourToUse,
                      bool largeKnob,
                      const juce::String& suffixText)
    : parameter (apvts.getParameter (parameterID)),
      suffix (suffixText),
      large (largeKnob)
{
    jassert (parameter != nullptr);

    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f,
                                true);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accentColourToUse);
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, Palette::panelEdge());
    slider.setDoubleClickReturnValue (true, parameter != nullptr
                                              ? parameter->convertFrom0to1 (parameter->getDefaultValue())
                                              : 0.0);
    slider.setMouseDragSensitivity (140);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, parameterID, slider);

    titleLabel.setText (titleText.toUpperCase(), juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centred);
    titleLabel.setFont (font (large ? 11.0f : 9.5f, juce::Font::bold, 0.16f));
    titleLabel.setColour (juce::Label::textColourId, Palette::muted());
    titleLabel.setInterceptsMouseClicks (false, false);
    titleLabel.setVisible (titleText.isNotEmpty());   // 大旋钮的标题画在面板角上，这里留空

    valueLabel.setJustificationType (juce::Justification::centred);
    valueLabel.setFont (font (large ? 15.0f : 12.0f, juce::Font::bold));
    valueLabel.setColour (juce::Label::textColourId, Palette::text());
    valueLabel.setInterceptsMouseClicks (false, false);

    addAndMakeVisible (slider);
    addAndMakeVisible (titleLabel);
    addAndMakeVisible (valueLabel);

    refresh();
}

void ParamKnob::resized()
{
    auto area = getLocalBounds();

    if (titleLabel.isVisible())
        titleLabel.setBounds (area.removeFromTop (large ? 18 : 15));

    valueLabel.setBounds (area.removeFromBottom (large ? 20 : 17));
    slider.setBounds (area.reduced (2));
}

void ParamKnob::refresh()
{
    if (parameter == nullptr)
        return;

    valueLabel.setText (parameter->getCurrentValueAsText() + suffix,
                        juce::dontSendNotification);
}

} // namespace sfx::ui
