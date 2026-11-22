#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

#include "../PluginProcessor.h"
#include "Palette.h"

namespace sfx::ui
{

//==============================================================================
// LFO 实时波形带。
//
// 数字（Hz）能说明"多快"，但看不出形状、极性和包络把它推成什么样。
// 这条带子画的是 LFO 最近 1.5 秒的实际输出：
//
//   · 波峰密度 = 当前速率（ENV>RATE 一推，密度当场变）
//   · 形状 = SIN / TRI / S&H
//   · 右端的光点 = 此刻的 LFO 值（也就是此刻推到音高/截止的量）
//
// 数据来自 PmEffect 内部的降采样环形缓冲，不额外占音频线程的算力。
//==============================================================================
class LfoWave : public juce::Component
{
public:
    explicit LfoWave (BesselAudioProcessor& p) : processor (p) {}

    void refresh()
    {
        processor.readLfoHistory (history.data(), (int) history.size());
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);

        // 屏幕底色
        g.setColour (Palette::scope());
        g.fillRoundedRectangle (bounds, 5.0f);

        g.setColour (Palette::panelEdge().withAlpha (0.7f));
        g.drawRoundedRectangle (bounds, 5.0f, 1.0f);

        const auto inner = bounds.reduced (4.0f, 3.0f);
        const auto midY  = inner.getCentreY();

        // 左侧小芯片：LFO 徽章
        const auto badgeArea = juce::Rectangle<float> (bounds.getX() + 3.0f, bounds.getY() + 3.0f,
                                                       30.0f, bounds.getHeight() - 6.0f);
        g.setColour (Palette::panelBottom());
        g.fillRoundedRectangle (badgeArea, 3.0f);
        g.setColour (Palette::panelEdge().withAlpha (0.6f));
        g.drawRoundedRectangle (badgeArea, 3.0f, 1.0f);

        g.setColour (Palette::accentWarm().withAlpha (0.85f));
        g.setFont (font (8.0f, juce::Font::bold, 0.20f));
        g.drawText ("LFO", badgeArea, juce::Justification::centred, false);

        // 波形区域：从芯片右侧开始，到右边缘留空
        const auto plot = inner.withTrimmedLeft (34.0f).withTrimmedRight (6.0f);

        // 零轴微弱中线
        g.setColour (Palette::accentWarm().withAlpha (0.08f));
        g.drawHorizontalLine ((int) midY, plot.getX(), plot.getRight());

        if (plot.getWidth() < 4.0f)
            return;

        juce::Path path;
        const auto count = (int) history.size();

        for (int i = 0; i < count; ++i)
        {
            const auto x = plot.getX() + plot.getWidth() * (float) i / (float) (count - 1);
            const auto y = midY - juce::jlimit (-1.0f, 1.0f, history[(size_t) i]) * plot.getHeight() * 0.42f;

            if (i == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }

        // 淡辉光 + 主线
        g.setColour (Palette::accentWarm().withAlpha (0.12f));
        g.strokePath (path, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        g.setColour (Palette::accentWarm());
        g.strokePath (path, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        // 右端光点 = 当前值（带柔和外光晕）
        const auto last = juce::jlimit (-1.0f, 1.0f, history.back());
        const auto dotY = midY - last * plot.getHeight() * 0.42f;

        g.setColour (Palette::accentWarm().withAlpha (0.25f));
        g.fillEllipse (plot.getRight() - 5.0f, dotY - 4.0f, 9.0f, 9.0f);

        g.setColour (Palette::accentWarm());
        g.fillEllipse (plot.getRight() - 2.5f, dotY - 2.0f, 4.5f, 4.5f);
    }

private:
    BesselAudioProcessor& processor;
    std::array<float, 1200> history { };   // 约 1.5 秒 @ 800 Hz

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LfoWave)
};

} // namespace sfx::ui
