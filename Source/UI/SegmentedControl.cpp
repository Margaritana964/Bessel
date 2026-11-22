#include "SegmentedControl.h"
#include "Palette.h"

namespace sfx::ui
{

SegmentedControl::SegmentedControl (juce::RangedAudioParameter& parameterToControl,
                                    juce::StringArray itemLabels,
                                    juce::Colour accentColourToUse)
    : parameter (parameterToControl),
      items (std::move (itemLabels)),
      accentColour (accentColourToUse),
      attachment (parameterToControl, [this] (float value) { applyFromHost (value); })
{
    jassert (items.size() > 0);

    setInterceptsMouseClicks (true, false);
}

void SegmentedControl::applyFromHost (float denormalisedValue)
{
    current = juce::jlimit (0, juce::jmax (0, items.size() - 1),
                            juce::roundToInt (denormalisedValue));
    repaint();
}

void SegmentedControl::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto corner = 6.0f;

    // 凹槽：竖向渐变 + 顶部暗边，和旋钮的凹槽是同一种语言
    {
        juce::ColourGradient well (Palette::knobWell().darker (0.2f), bounds.getCentreX(), bounds.getY(),
                                   Palette::knobWell().brighter (0.08f), bounds.getCentreX(), bounds.getBottom(),
                                   false);
        g.setGradientFill (well);
        g.fillRoundedRectangle (bounds, corner);
    }

    g.setColour (Palette::panelEdge());
    g.drawRoundedRectangle (bounds, corner, 1.0f);

    if (items.isEmpty())
        return;

    const auto segmentWidth = bounds.getWidth() / (float) items.size();

    for (int i = 0; i < items.size(); ++i)
    {
        auto cell = bounds.withWidth (segmentWidth)
                          .translated ((float) i * segmentWidth, 0.0f)
                          .reduced (3.0f);

        const auto selected = (i == current);

        if (selected)
        {
            juce::ColourGradient lit (accentColour.brighter (0.20f), cell.getCentreX(), cell.getY(),
                                      accentColour.darker (0.25f), cell.getCentreX(), cell.getBottom(),
                                      false);
            g.setGradientFill (lit);
            g.fillRoundedRectangle (cell, corner - 2.0f);

            g.setColour (Palette::highlight().withAlpha (0.4f));
            g.drawRoundedRectangle (cell.reduced (1.0f), corner - 3.0f, 1.0f);
        }
        else if (i == hovered)
        {
            g.setColour (Palette::text().withAlpha (0.06f));
            g.fillRoundedRectangle (cell, corner - 2.0f);
        }

        g.setColour (selected ? Palette::background() : Palette::muted());

        // 字号同时受高度和宽度限制：窄格子里 "S&H" 这种三字符会把字裁掉
        const auto fitted = juce::jmin (cell.getHeight() * 0.52f, cell.getWidth() / 3.4f);
        g.setFont (font (juce::jlimit (7.5f, 12.0f, fitted), juce::Font::bold, 0.10f));

        g.drawText (items[i], cell, juce::Justification::centred, false);
    }
}

void SegmentedControl::mouseDown (const juce::MouseEvent& event)
{
    if (items.isEmpty() || getWidth() <= 0)
        return;

    const auto segmentWidth = (float) getWidth() / (float) items.size();
    const auto index = juce::jlimit (0, items.size() - 1,
                                     (int) ((float) event.x / juce::jmax (1.0f, segmentWidth)));

    attachment.setValueAsCompleteGesture ((float) index);
}

void SegmentedControl::mouseMove (const juce::MouseEvent& event)
{
    if (items.isEmpty() || getWidth() <= 0)
        return;

    const auto segmentWidth = (float) getWidth() / (float) items.size();
    const auto index = juce::jlimit (0, items.size() - 1,
                                     (int) ((float) event.x / juce::jmax (1.0f, segmentWidth)));

    if (index != hovered)
    {
        hovered = index;
        repaint();
    }
}

void SegmentedControl::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

} // namespace sfx::ui
