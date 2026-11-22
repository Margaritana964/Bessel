#include "ScopeComponent.h"
#include "Palette.h"
#include "../PluginProcessor.h"

namespace sfx::ui
{

ScopeComponent::ScopeComponent (BesselAudioProcessor& processorToUse)
    : processor (processorToUse)
{
    setInterceptsMouseClicks (false, false);
}

void ScopeComponent::refresh()
{
    processor.readScope (captured, captureSize);
    repaint();
}

void ScopeComponent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto corner = 8.0f;

    // 屏幕底层：深黑径向渐变
    {
        juce::ColourGradient screen (Palette::scope().brighter (0.22f),
                                     bounds.getCentreX(), bounds.getCentreY(),
                                     Palette::scope().darker (0.5f),
                                     bounds.getCentreX(), bounds.getBottom(), true);
        g.setGradientFill (screen);
        g.fillRoundedRectangle (bounds, corner);
    }

    // 极淡的强调色辉光——模拟屏幕通电时的微光
    {
        juce::ColourGradient glow (Palette::accent().withAlpha (0.03f),
                                   bounds.getCentreX(), bounds.getCentreY(),
                                   juce::Colours::transparentBlack,
                                   bounds.getCentreX(), bounds.getBottom(), true);
        g.setGradientFill (glow);
        g.fillRoundedRectangle (bounds, corner);
    }

    // 内阴影 + 边框
    g.setColour (Palette::deepShadow().withAlpha (0.5f));
    g.drawRoundedRectangle (bounds.reduced (1.0f), corner - 1.0f, 2.0f);

    g.setColour (Palette::panelEdge().withAlpha (0.9f));
    g.drawRoundedRectangle (bounds, corner, 1.0f);

    // 网格：细密、极淡，只提供空间感
    g.setColour (Palette::accent().withAlpha (0.035f));

    for (int i = 1; i < 8; ++i)
    {
        const auto y = bounds.getY() + bounds.getHeight() * (float) i / 8.0f;
        g.drawHorizontalLine ((int) y, bounds.getX() + 2.0f, bounds.getRight() - 2.0f);
    }

    for (int i = 1; i < 12; ++i)
    {
        const auto x = bounds.getX() + bounds.getWidth() * (float) i / 12.0f;
        g.drawVerticalLine ((int) x, bounds.getY() + 2.0f, bounds.getBottom() - 2.0f);
    }

    // 中线稍亮，作为零轴
    g.setColour (Palette::accent().withAlpha (0.10f));
    g.drawHorizontalLine ((int) bounds.getCentreY(), bounds.getX() + 2.0f, bounds.getRight() - 2.0f);

    const auto inner = bounds.reduced (6.0f, 8.0f);
    const auto midY  = inner.getCentreY();

    // 输入包络把输出压到 0 的时候
    if (processor.currentAmGain.load() < 0.002f)
    {
        g.setColour (Palette::muted().withAlpha (0.20f));
        g.drawHorizontalLine ((int) midY, inner.getX(), inner.getRight());

        g.setColour (Palette::muted().withAlpha (0.75f));
        g.setFont (font (12.0f, juce::Font::bold, 0.18f));
        g.drawText ("NO INPUT", inner, juce::Justification::centred, false);
        return;
    }

    // 静音时给个提示
    float peak = 0.0f;

    for (const auto v : captured)
        peak = juce::jmax (peak, std::abs (v));

    if (peak < 1.0e-4f)
    {
        g.setColour (Palette::muted().withAlpha (0.25f));
        g.drawHorizontalLine ((int) midY, inner.getX(), inner.getRight());

        g.setColour (Palette::muted().withAlpha (0.6f));
        g.setFont (font (12.0f, juce::Font::bold));
        g.drawText ("NO SIGNAL", inner, juce::Justification::centred, false);
        return;
    }

    // 以上升过零点做触发，画面不飘
    int start = 0;

    for (int i = 1; i < captureSize / 2; ++i)
    {
        if (captured[i - 1] < 0.0f && captured[i] >= 0.0f)
        {
            start = i;
            break;
        }
    }

    // 用前两个上升过零点估周期，画大约 3 个周期
    int period = 0;

    for (int i = start + 8; i < captureSize; ++i)
    {
        if (captured[i - 1] < 0.0f && captured[i] >= 0.0f)
        {
            period = i - start;
            break;
        }
    }

    int drawCount = captureSize - start;

    if (period > 16)
        drawCount = juce::jmin (drawCount, period * 3);

    drawCount = juce::jlimit (32, juce::jmax (32, captureSize - start), drawCount);

    const auto halfHeight = inner.getHeight() * 0.44f;
    const auto xStep = inner.getWidth() / (float) juce::jmax (1, drawCount - 1);

    juce::Path waveform;

    for (int i = 0; i < drawCount; ++i)
    {
        const auto x = inner.getX() + (float) i * xStep;
        const auto y = midY - juce::jlimit (-1.3f, 1.3f, captured[start + i]) * halfHeight;

        if (i == 0)
            waveform.startNewSubPath (x, y);
        else
            waveform.lineTo (x, y);
    }

    // 填充：从波形铺到底
    {
        auto filled = waveform;
        filled.lineTo (inner.getRight(), inner.getBottom());
        filled.lineTo (inner.getX(), inner.getBottom());
        filled.closeSubPath();

        juce::ColourGradient gradient (Palette::accent().withAlpha (0.22f),
                                       inner.getCentreX(), inner.getY(),
                                       Palette::accent().withAlpha (0.005f),
                                       inner.getCentreX(), inner.getBottom(),
                                       false);
        g.setGradientFill (gradient);
        g.fillPath (filled);
    }

    // 四段辉光 + 主线条：宽而淡 → 窄而亮，才有"发光"的层次
    g.setColour (Palette::accent().withAlpha (0.03f));
    g.strokePath (waveform, juce::PathStrokeType (16.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

    g.setColour (Palette::accent().withAlpha (0.06f));
    g.strokePath (waveform, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

    g.setColour (Palette::accent().withAlpha (0.16f));
    g.strokePath (waveform, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

    g.setColour (Palette::accent());
    g.strokePath (waveform, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

    // 左右边缘渐隐，让波形不是突然断掉
    {
        const auto fadeW = juce::jmin (20.0f, inner.getWidth() * 0.08f);

        juce::ColourGradient fadeL (Palette::scope(), inner.getX(), 0.0f,
                                    Palette::scope().withAlpha (0.0f), inner.getX() + fadeW, 0.0f, false);
        g.setGradientFill (fadeL);
        g.fillRect (inner.getX(), bounds.getY() + 2.0f, fadeW, bounds.getHeight() - 4.0f);

        juce::ColourGradient fadeR (Palette::scope().withAlpha (0.0f), inner.getRight() - fadeW, 0.0f,
                                    Palette::scope(), inner.getRight(), 0.0f, false);
        g.setGradientFill (fadeR);
        g.fillRect (inner.getRight() - fadeW, bounds.getY() + 2.0f, fadeW, bounds.getHeight() - 4.0f);
    }
}

} // namespace sfx::ui
