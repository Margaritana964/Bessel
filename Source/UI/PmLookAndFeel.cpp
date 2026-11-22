#include "PmLookAndFeel.h"
#include "Palette.h"

#include <cmath>

namespace sfx::ui
{

PmLookAndFeel::PmLookAndFeel()
{
    setColour (juce::Slider::rotarySliderFillColourId,    Palette::accent());
    setColour (juce::Slider::rotarySliderOutlineColourId, Palette::panelEdge());
    setColour (juce::Slider::thumbColourId,               Palette::text());

    setColour (juce::Label::textColourId,                 Palette::text());
    setColour (juce::TextButton::buttonColourId,          Palette::panelBottom());
    setColour (juce::TextButton::buttonOnColourId,        Palette::accent());

    setColour (juce::ComboBox::backgroundColourId,        Palette::panelBottom());
    setColour (juce::ComboBox::textColourId,              Palette::text());
    setColour (juce::ComboBox::outlineColourId,           Palette::panelEdge());
    setColour (juce::ComboBox::arrowColourId,             Palette::muted());

    setColour (juce::PopupMenu::backgroundColourId,       Palette::panelBottom());
    setColour (juce::PopupMenu::textColourId,             Palette::text());
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::accent().withAlpha (0.25f));
}

//==============================================================================
void PmLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                      float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                      juce::Slider& slider)
{
    const auto area   = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const auto radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
    const auto centre = area.getCentre();

    const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const auto track  = slider.findColour (juce::Slider::rotarySliderOutlineColourId);

    if (radius <= 4.0f)
        return;

    const auto hovered    = slider.isMouseOverOrDragging();
    const auto thickness  = juce::jmax (3.0f, radius * 0.115f);
    const auto arcRadius  = radius - thickness * 0.75f - 1.0f;
    const auto angle      = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const auto bright     = hovered ? 1.0f : 0.88f;

    // ── 1. 凹槽：径向渐变 + 下沿一圈内阴影，看起来是「嵌进去」的 ──────────
    {
        const auto wellRadius = radius - 0.5f;

        // 外圈投影——给凹槽一个额外的厚度感
        g.setColour (Palette::deepShadow().withAlpha (0.35f));
        g.fillEllipse (juce::Rectangle<float> ((wellRadius + 2.0f) * 2.0f,
                                               (wellRadius + 2.0f) * 2.0f)
                           .withCentre (centre.translated (0.0f, 1.0f)));

        juce::ColourGradient well (Palette::knobWell().brighter (0.10f),
                                   centre.x, centre.y - wellRadius * 0.55f,
                                   Palette::knobWell().darker (0.45f),
                                   centre.x, centre.y + wellRadius, true);
        g.setGradientFill (well);
        g.fillEllipse (juce::Rectangle<float> (wellRadius * 2.0f, wellRadius * 2.0f).withCentre (centre));

        // 内阴影：上沿暗、下沿略微反光
        g.setColour (Palette::shadow().withAlpha (0.55f));
        g.drawEllipse (juce::Rectangle<float> ((wellRadius - 0.5f) * 2.0f,
                                               (wellRadius - 0.5f) * 2.0f).withCentre (centre),
                       juce::jmax (1.0f, radius * 0.03f));

        g.setColour (Palette::highlight().withAlpha (0.26f));
        juce::Path rim;
        rim.addCentredArc (centre.x, centre.y, wellRadius - 0.5f, wellRadius - 0.5f, 0.0f,
                           juce::MathConstants<float>::pi * 1.15f,
                           juce::MathConstants<float>::pi * 1.85f, true);
        g.strokePath (rim, juce::PathStrokeType (1.0f));
    }

    // ── 2. 刻度：只在大旋钮上画，小旋钮画了会糊 ──────────────────────────
    if (radius > 40.0f)
    {
        const auto tickRadius = arcRadius - thickness * 0.95f;

        for (int i = 0; i <= 10; ++i)
        {
            const auto t     = (float) i / 10.0f;
            const auto a     = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);
            const auto dir   = juce::Point<float> (std::sin (a), -std::cos (a));
            const auto outer = centre + dir * tickRadius;
            const auto inner = centre + dir * (tickRadius - radius * (i % 5 == 0 ? 0.075f : 0.045f));

            g.setColour (Palette::text().withAlpha (i % 5 == 0 ? 0.28f : 0.14f));
            g.drawLine ({ inner, outer }, i % 5 == 0 ? 1.4f : 1.0f);
        }
    }

    // ── 3. 轨道 ─────────────────────────────────────────────────────────
    {
        juce::Path p;
        p.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (track.withAlpha (0.45f));
        g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

    // ── 4. 值弧：先画宽的做辉光，再画细的做本体 ──────────────────────────
    if (sliderPos > 0.0008f)
    {
        juce::Path p;
        p.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         rotaryStartAngle, angle, true);

        // 外层辉光：悬停时更亮
        if (hovered)
        {
            g.setColour (accent.withAlpha (0.14f));
            g.strokePath (p, juce::PathStrokeType (thickness * 3.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        }

        g.setColour (accent.withAlpha (0.20f));
        g.strokePath (p, juce::PathStrokeType (thickness * 2.2f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

        g.setColour (accent.withMultipliedAlpha (bright));
        g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

        // 弧尖高光，让发光有个"源头"
        const auto tip = centre + juce::Point<float> (std::sin (angle), -std::cos (angle)) * arcRadius;
        g.setColour (juce::Colours::white.withAlpha (0.60f * bright));
        g.fillEllipse (juce::Rectangle<float> (thickness * 0.5f, thickness * 0.5f).withCentre (tip));
    }

    // ── 5. 指针：锥形，靠近中心细、靠近弧粗 ──────────────────────────────
    {
        const auto direction = juce::Point<float> (std::sin (angle), -std::cos (angle));
        const auto normal    = juce::Point<float> (-direction.y, direction.x);
        const auto tipR      = arcRadius - thickness * 1.15f;
        const auto baseR     = radius * 0.13f;
        const auto halfWidth = juce::jmax (1.2f, radius * 0.028f);

        juce::Path pointer;
        pointer.startNewSubPath (centre + direction * tipR);
        pointer.lineTo (centre + direction * (baseR + radius * 0.10f) + normal * halfWidth);
        pointer.lineTo (centre + direction * baseR);
        pointer.lineTo (centre + direction * (baseR + radius * 0.10f) - normal * halfWidth);
        pointer.closeSubPath();

        // 指针阴影
        g.setColour (Palette::shadow().withAlpha (0.3f));
        g.fillPath (pointer, juce::AffineTransform::translation (0.0f, 1.0f));

        g.setColour (Palette::text().withMultipliedAlpha (bright));
        g.fillPath (pointer);
    }

    // ── 6. 轴心 ─────────────────────────────────────────────────────────
    {
        const auto dotSize = juce::jmax (2.5f, radius * 0.075f);
        g.setColour (Palette::knobWell().brighter (0.30f));
        g.fillEllipse (juce::Rectangle<float> (dotSize * 1.7f, dotSize * 1.7f).withCentre (centre));

        g.setColour (accent.withMultipliedAlpha (bright));
        g.fillEllipse (juce::Rectangle<float> (dotSize, dotSize).withCentre (centre));
    }
}

//==============================================================================
void PmLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                          const juce::Colour&, bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto radius = bounds.getHeight() * 0.5f;
    const auto on     = button.getToggleState();

    // 竖向渐变：上亮下暗，比纯色有体积
    juce::Colour top, bottom;

    if (on)
    {
        top    = Palette::accent().brighter (0.15f);
        bottom = Palette::accent().darker (0.18f);
    }
    else
    {
        top    = Palette::panelTop().brighter (0.05f);
        bottom = Palette::panelBottom();
    }

    if (highlighted) { top = top.brighter (0.12f); bottom = bottom.brighter (0.12f); }
    if (down)        { top = top.darker (0.15f);   bottom = bottom.darker (0.15f); }

    // 按钮投影
    if (! on)
    {
        g.setColour (Palette::shadow().withAlpha (0.3f));
        g.fillRoundedRectangle (bounds.translated (0.0f, 1.0f), radius);
    }

    juce::ColourGradient fill (top, bounds.getCentreX(), bounds.getY(),
                               bottom, bounds.getCentreX(), bounds.getBottom(), false);
    g.setGradientFill (fill);
    g.fillRoundedRectangle (bounds, radius);

    // 顶部高光 + 边框
    g.setColour (Palette::highlight().withAlpha (on ? 0.35f : 0.6f));
    g.drawRoundedRectangle (bounds.reduced (1.0f), radius - 1.0f, 1.0f);

    g.setColour (on ? Palette::accent().withAlpha (0.9f) : Palette::panelEdge());
    g.drawRoundedRectangle (bounds, radius, 1.0f);
}

void PmLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                    bool, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (button.getToggleState() ? Palette::background()
                                         : (button.isEnabled() ? Palette::text() : Palette::muted()));

    g.drawText (button.getButtonText(), button.getLocalBounds().reduced (6, 0),
                juce::Justification::centred, false);
}

juce::Font PmLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return font (juce::jmin (12.0f, (float) buttonHeight * 0.52f), juce::Font::bold, 0.12f);
}

} // namespace sfx::ui
