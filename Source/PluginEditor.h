#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "UI/PmLookAndFeel.h"
#include "UI/ParamKnob.h"
#include "UI/ScopeComponent.h"
#include "UI/LfoWave.h"
#include "UI/PresetPanel.h"
#include "UI/SegmentedControl.h"

class BesselAudioProcessor;

//==============================================================================
// 自定义界面：模块化卡片布局，清晰消除所有控件歧义与信息冗余。
//
// 参数全部通过 attachment 走，界面不直接碰 DSP。
//==============================================================================
class BesselAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit BesselAudioProcessorEditor (BesselAudioProcessor& processorToEdit);
    ~BesselAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** 手动刷一次（截图工具和测试用，正常运行时由 timer 驱动）。 */
    void refreshNow() { timerCallback(); }

    /** 预设浏览器开/关（截图工具也用它出图）。 */
    void setPresetBrowserVisible (bool shouldBeVisible);

private:
    void timerCallback() override;

    /** 预设名字跟着列表刷新。 */
    void refreshPresetName();

    void drawPanel (juce::Graphics&, juce::Rectangle<int> area) const;

    BesselAudioProcessor& processorRef;

    sfx::ui::PmLookAndFeel lookAndFeel;

    // ── 顶部主区 ────────────────────────────────────────────────────────
    sfx::ui::ParamKnob carrierKnob, shapeKnob, glideKnob;
    sfx::ui::ParamKnob pmAmountKnob, fmAmountKnob;
    sfx::ui::SegmentedControl pitchModeSelector;
    sfx::ui::ScopeComponent scope;
    sfx::ui::LfoWave lfoWave;
    sfx::ui::PresetPanel presetPanel;

    // ── 中层信号链路区：AM 动态 / 调制滤波 / 谐波质感 / 主输出 ──────────
    sfx::ui::ParamKnob amGainKnob, amAttackKnob, amReleaseKnob;
    sfx::ui::ParamKnob modGainKnob, modFreqKnob, modQKnob;
    sfx::ui::SegmentedControl filterSelector;
    sfx::ui::ParamKnob combKnob, combFbKnob, subKnob, driveKnob;
    sfx::ui::ParamKnob mixKnob, outputKnob;

    // ── 底层调制矩阵区：LFO 系统 / 动态包络调制 ─────────────────────────
    sfx::ui::ParamKnob lfoRateKnob, lfoPitchKnob, lfoCutoffKnob;
    sfx::ui::SegmentedControl lfoShapeSelector;
    sfx::ui::SegmentedControl lfoSyncSelector, gridSelector, gridModSelector;

    sfx::ui::ParamKnob envPitchKnob, envCutoffKnob, envRateKnob;
    sfx::ui::ParamKnob env2AttackKnob, env2ReleaseKnob;

    // ── 顶部栏与状态控件 ────────────────────────────────────────────────
    juce::TextButton bypassButton { "BYPASS" };
    juce::TextButton presetPrev { "<" }, presetNext { ">" }, presetSave { "SAVE" };
    juce::Label presetLabel;
    std::unique_ptr<juce::ButtonParameterAttachment> bypassAttachment;

    juce::Label titleLabel, subtitleLabel, pitchLabel, footerLabel;
    juce::Label filterTitleLabel, lfoShapeLabel;
    juce::Label syncLabel, gridLabel, gridModLabel;

    // ── 布局区域定义 ────────────────────────────────────────────────────
    juce::Rectangle<int> headerArea, footerArea;
    juce::Rectangle<int> carrierPanel, scopePanel, indexPanel;
    juce::Rectangle<int> signalPanel, tonePanel;
    juce::Rectangle<int> lfoPanel, envPanel;
    juce::Rectangle<int> lfoWaveArea, pitchMeterArea, presetBoxArea;

    // 内部划分线坐标
    int signalDividerX = 0;
    int toneDividerX = 0;
    int lfoDividerY = 0;
    int envDividerY = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BesselAudioProcessorEditor)
};
