#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "UI/Palette.h"

using namespace sfx::ui;

//==============================================================================
BesselAudioProcessorEditor::BesselAudioProcessorEditor (BesselAudioProcessor& processorToEdit)
    : juce::AudioProcessorEditor (&processorToEdit),
      processorRef (processorToEdit),

      // ── 顶部主区 ──────────────────────────────────────────────────────────
      carrierKnob (processorToEdit.apvts, BesselAudioProcessor::paramCarrier,
                   "", Palette::accent(), true),
      shapeKnob   (processorToEdit.apvts, BesselAudioProcessor::paramShape,
                   "Shape", Palette::accent(), false),
      glideKnob   (processorToEdit.apvts, BesselAudioProcessor::paramGlide,
                   "Glide", Palette::accent(), false),

      pmAmountKnob (processorToEdit.apvts, BesselAudioProcessor::paramPmAmount,
                    "PM Depth", Palette::accentWarm(), false),
      fmAmountKnob (processorToEdit.apvts, BesselAudioProcessor::paramFmAmount,
                    "FM Depth", Palette::accentWarm(), false),

      pitchModeSelector (*processorToEdit.apvts.getParameter (BesselAudioProcessor::paramPitchMode),
                         juce::StringArray { "FREE", "FOLLOW", "MIDI" }, Palette::accent()),
      scope (processorToEdit),
      lfoWave (processorToEdit),
      presetPanel (processorToEdit),

      // ── 中层：输入动态与调制滤波 ──────────────────────────────────────────
      amGainKnob    (processorToEdit.apvts, BesselAudioProcessor::paramAmGain,
                     "Env Gain", Palette::accent(), false),
      amAttackKnob  (processorToEdit.apvts, BesselAudioProcessor::paramAmAttack,
                     "Attack", Palette::text(), false),
      amReleaseKnob (processorToEdit.apvts, BesselAudioProcessor::paramAmRelease,
                     "Release", Palette::text(), false),

      modGainKnob (processorToEdit.apvts, BesselAudioProcessor::paramModGain,
                   "In Gain", Palette::text(), false),
      modFreqKnob (processorToEdit.apvts, BesselAudioProcessor::paramModFreq,
                   "Cutoff", Palette::text(), false),
      modQKnob    (processorToEdit.apvts, BesselAudioProcessor::paramModQ,
                   "Reso / Q", Palette::text(), false),
      filterSelector (*processorToEdit.apvts.getParameter (BesselAudioProcessor::paramModType),
                      juce::StringArray { "LP", "BP", "HP" }, Palette::accentWarm()),

      // ── 中层：谐波质感与主输出 ────────────────────────────────────────────
      combKnob   (processorToEdit.apvts, BesselAudioProcessor::paramCombMix,
                  "Comb", Palette::accentWarm(), false),
      combFbKnob (processorToEdit.apvts, BesselAudioProcessor::paramCombFb,
                  "Feedback", Palette::accentWarm(), false),
      subKnob    (processorToEdit.apvts, BesselAudioProcessor::paramSub,
                  "Sub", Palette::accent(), false),
      driveKnob  (processorToEdit.apvts, BesselAudioProcessor::paramDrive,
                  "Drive", Palette::text(), false),

      mixKnob    (processorToEdit.apvts, BesselAudioProcessor::paramMix,
                  "Mix", Palette::accent(), false),
      outputKnob (processorToEdit.apvts, BesselAudioProcessor::paramOutput,
                  "Output", Palette::text(), false),

      // ── 底层：LFO 系统 ───────────────────────────────────────────────────
      lfoRateKnob   (processorToEdit.apvts, BesselAudioProcessor::paramLfoRate,
                     "Rate", Palette::accent(), false),
      lfoPitchKnob  (processorToEdit.apvts, BesselAudioProcessor::paramLfoPitch,
                     "LFO > Pitch", Palette::accent(), false),
      lfoCutoffKnob (processorToEdit.apvts, BesselAudioProcessor::paramLfoCutoff,
                     "LFO > Cut", Palette::accent(), false),

      lfoShapeSelector (*processorToEdit.apvts.getParameter (BesselAudioProcessor::paramLfoShape),
                        juce::StringArray { "SIN", "TRI", "S&H" }, Palette::accentWarm()),
      lfoSyncSelector  (*processorToEdit.apvts.getParameter (BesselAudioProcessor::paramLfoSync),
                        juce::StringArray { "FREE", "SYNC" }, Palette::accent()),
      gridSelector     (*processorToEdit.apvts.getParameter (BesselAudioProcessor::paramLfoGrid),
                        juce::StringArray { "1/4", "1/8", "1/16", "1/32" }, Palette::text()),
      gridModSelector  (*processorToEdit.apvts.getParameter (BesselAudioProcessor::paramLfoGridMod),
                        juce::StringArray { "STR", "DOT", "TRIP" }, Palette::text()),

      // ── 底层：动态包络调制 ────────────────────────────────────────────────
      envPitchKnob    (processorToEdit.apvts, BesselAudioProcessor::paramEnvPitch,
                       "Env > Pitch", Palette::accent(), false),
      envCutoffKnob   (processorToEdit.apvts, BesselAudioProcessor::paramEnvCutoff,
                       "Env > Cut", Palette::accent(), false),
      envRateKnob     (processorToEdit.apvts, BesselAudioProcessor::paramEnvRate,
                       "Env > Rate", Palette::accent(), false),

      env2AttackKnob  (processorToEdit.apvts, BesselAudioProcessor::paramEnv2Attack,
                       "Attack", Palette::text(), false),
      env2ReleaseKnob (processorToEdit.apvts, BesselAudioProcessor::paramEnv2Release,
                       "Release", Palette::text(), false)
{
    setLookAndFeel (&lookAndFeel);

    for (auto* child : std::initializer_list<juce::Component*> {
             &carrierKnob, &shapeKnob, &glideKnob,
             &pmAmountKnob, &fmAmountKnob,
             &pitchModeSelector, &scope, &lfoWave,
             &amGainKnob, &amAttackKnob, &amReleaseKnob,
             &modGainKnob, &modFreqKnob, &modQKnob, &filterSelector,
             &combKnob, &combFbKnob, &subKnob, &driveKnob,
             &mixKnob, &outputKnob,
             &lfoRateKnob, &lfoPitchKnob, &lfoCutoffKnob, &lfoShapeSelector,
             &lfoSyncSelector, &gridSelector, &gridModSelector,
             &envPitchKnob, &envCutoffKnob, &envRateKnob,
             &env2AttackKnob, &env2ReleaseKnob,
             &bypassButton,
             &presetPrev, &presetNext, &presetSave, &presetLabel,
             &titleLabel, &subtitleLabel, &pitchLabel, &footerLabel,
             &filterTitleLabel, &lfoShapeLabel,
             &syncLabel, &gridLabel, &gridModLabel })
        addAndMakeVisible (child);

    // --- 标题 ------------------------------------------------------------
    titleLabel.setText ("BESSEL", juce::dontSendNotification);
    titleLabel.setFont (font (23.0f, juce::Font::bold, 0.06f));
    titleLabel.setColour (juce::Label::textColourId, Palette::text());
    titleLabel.setInterceptsMouseClicks (false, false);

    subtitleLabel.setText (text ("PHASE / FREQUENCY MODULATION  \xC2\xB7  4\xC3\x97 OVERSAMPLED"),
                           juce::dontSendNotification);
    subtitleLabel.setFont (font (9.5f, juce::Font::bold, 0.24f));
    subtitleLabel.setColour (juce::Label::textColourId, Palette::muted());
    subtitleLabel.setInterceptsMouseClicks (false, false);

    filterTitleLabel.setText ("TYPE", juce::dontSendNotification);
    filterTitleLabel.setFont (font (9.0f, juce::Font::bold, 0.16f));
    filterTitleLabel.setColour (juce::Label::textColourId, Palette::muted());
    filterTitleLabel.setJustificationType (juce::Justification::centred);
    filterTitleLabel.setInterceptsMouseClicks (false, false);

    lfoShapeLabel.setText ("SHAPE", juce::dontSendNotification);
    lfoShapeLabel.setFont (font (9.0f, juce::Font::bold, 0.16f));
    lfoShapeLabel.setColour (juce::Label::textColourId, Palette::muted());
    lfoShapeLabel.setJustificationType (juce::Justification::centred);
    lfoShapeLabel.setInterceptsMouseClicks (false, false);

    auto setupTag = [] (juce::Label& l, const char* labelText)
    {
        l.setText (labelText, juce::dontSendNotification);
        l.setFont (font (8.5f, juce::Font::bold, 0.16f));
        l.setColour (juce::Label::textColourId, Palette::muted());
        l.setJustificationType (juce::Justification::centred);
        l.setInterceptsMouseClicks (false, false);
    };

    setupTag (syncLabel,   "SYNC MODE");
    setupTag (gridLabel,   "GRID RATE");
    setupTag (gridModLabel, "FEEL");

    pitchLabel.setFont (font (10.5f, juce::Font::bold, 0.08f));
    pitchLabel.setColour (juce::Label::textColourId, Palette::muted());
    pitchLabel.setJustificationType (juce::Justification::centredLeft);
    pitchLabel.setInterceptsMouseClicks (false, false);
    pitchLabel.setText ("TRACK OFF", juce::dontSendNotification);

    footerLabel.setFont (font (9.5f, juce::Font::plain, 0.08f));
    footerLabel.setColour (juce::Label::textColourId, Palette::muted().withAlpha (0.75f));
    footerLabel.setJustificationType (juce::Justification::centredRight);
    footerLabel.setInterceptsMouseClicks (false, false);
    footerLabel.setText ("4\xC3\x97 OS", juce::dontSendNotification);

    // --- 开关 ------------------------------------------------------------
    bypassButton.setClickingTogglesState (true);

    if (auto* bypassParam = processorRef.apvts.getParameter (BesselAudioProcessor::paramBypass))
        bypassAttachment = std::make_unique<juce::ButtonParameterAttachment> (*bypassParam, bypassButton);

    // --- 预设 ------------------------------------------------------------
    presetLabel.setText (processorRef.presets.getCurrentName(), juce::dontSendNotification);
    presetLabel.setFont (font (11.0f, juce::Font::bold, 0.10f));
    presetLabel.setColour (juce::Label::textColourId, Palette::text());
    presetLabel.setJustificationType (juce::Justification::centred);
    presetLabel.setInterceptsMouseClicks (false, false);

    presetPrev.onClick = [this] { processorRef.presets.step (-1); refreshPresetName(); };
    presetNext.onClick = [this] { processorRef.presets.step ( 1); refreshPresetName(); };

    // 点名字打开预设浏览器；SAVE / SAVE AS / RENAME 都在面板里
    presetSave.setButtonText ("LIST");
    presetSave.onClick = [this] { setPresetBrowserVisible (true); };

    presetPanel.onClose = [this] { setPresetBrowserVisible (false); };
    addChildComponent (presetPanel);

    setResizable (true, true);
    setResizeLimits (920, 780, 1900, 1300);
    setSize (1000, 838);

    startTimerHz (30);
}

BesselAudioProcessorEditor::~BesselAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
void BesselAudioProcessorEditor::drawPanel (juce::Graphics& g, juce::Rectangle<int> area) const
{
    if (area.isEmpty())
        return;

    const auto r = area.toFloat().reduced (0.5f);
    constexpr float corner = 10.0f;

    // 投影
    g.setColour (Palette::deepShadow().withAlpha (0.4f));
    g.fillRoundedRectangle (r.translated (0.0f, 2.5f), corner);

    // 面板本体渐变
    juce::ColourGradient fill (Palette::panelTop(), r.getCentreX(), r.getY(),
                               Palette::panelBottom(), r.getCentreX(), r.getBottom(), false);
    g.setGradientFill (fill);
    g.fillRoundedRectangle (r, corner);

    // 上沿高光
    {
        juce::Path topEdge;
        topEdge.addRoundedRectangle (r.reduced (1.0f), corner - 1.0f);
        g.saveState();
        g.reduceClipRegion (r.toNearestInt());
        g.setColour (Palette::highlight());
        g.strokePath (topEdge, juce::PathStrokeType (1.0f));
        g.restoreState();
    }

    g.setColour (Palette::panelEdge());
    g.drawRoundedRectangle (r, corner, 1.0f);
}

static void drawSectionMarker (juce::Graphics& g, int x, int y,
                                const juce::String& label, juce::Colour colour)
{
    // 发光小药丸
    g.setColour (colour.withAlpha (0.25f));
    g.fillRoundedRectangle ((float) x - 1.0f, (float) y + 1.0f, 6.0f, 11.0f, 2.0f);

    g.setColour (colour);
    g.fillRoundedRectangle ((float) x, (float) y + 2.0f, 4.0f, 9.0f, 1.5f);

    // 标题文字
    g.setColour (colour.withAlpha (0.85f));
    g.setFont (font (8.5f, juce::Font::bold, 0.22f));
    g.drawText (label, x + 8, y, 160, 14, juce::Justification::centredLeft, false);
}

static void drawGradientDivider (juce::Graphics& g, int x, int top, int bottom, juce::Colour colour)
{
    const auto h = bottom - top;
    const auto mid = top + h / 2;

    juce::ColourGradient grad (colour.withAlpha (0.0f), (float) x, (float) top,
                               colour.withAlpha (0.85f), (float) x, (float) mid, false);
    grad.addColour (1.0, colour.withAlpha (0.0f));
    g.setGradientFill (grad);
    g.drawVerticalLine (x, (float) top, (float) bottom);
}

void BesselAudioProcessorEditor::paint (juce::Graphics& g)
{
    // ── 背景 ────────────────────────────────────────────────────────
    {
        juce::ColourGradient background (
            Palette::backgroundTop(), (float) getWidth() * 0.5f, -80.0f,
            Palette::background().darker (0.25f), (float) getWidth() * 0.5f, (float) getHeight(),
            false);
        g.setGradientFill (background);
        g.fillAll();
    }

    // ── 顶部强调色双色光带 ───────────────────────────────────────────
    {
        juce::ColourGradient glow (Palette::accent().withAlpha (0.12f), 0.0f, 0.0f,
                                   Palette::accentWarm().withAlpha (0.06f), (float) getWidth(), 0.0f,
                                   false);
        g.setGradientFill (glow);
        g.fillRect (0, 0, getWidth(), 2);
    }

    // ── 标题下方分隔线 ──────────────────────────────────────────────
    {
        const auto lineY = headerArea.getBottom() + 3;
        juce::ColourGradient line (Palette::panelEdge().withAlpha (0.0f),
                                   (float) headerArea.getX(), (float) lineY,
                                   Palette::panelEdge().withAlpha (0.7f),
                                   (float) headerArea.getCentreX(), (float) lineY, false);
        line.addColour (1.0, Palette::panelEdge().withAlpha (0.0f));
        g.setGradientFill (line);
        g.fillRect (headerArea.getX(), lineY, headerArea.getWidth(), 1);
    }

    // ── 预设条外框 ──────────────────────────────────────────────────
    if (! presetBoxArea.isEmpty())
    {
        const auto r = presetBoxArea.toFloat().reduced (0.5f);
        g.setColour (Palette::panelBottom().darker (0.2f));
        g.fillRoundedRectangle (r, 13.0f);
        g.setColour (Palette::panelEdge().withAlpha (0.6f));
        g.drawRoundedRectangle (r, 13.0f, 1.0f);
    }

    // ── 示波器背后的环境光晕 ────────────────────────────────────────
    if (! scopePanel.isEmpty())
    {
        juce::ColourGradient glow (Palette::accent().withAlpha (0.035f),
                                   (float) scopePanel.getCentreX(), (float) scopePanel.getCentreY(),
                                   juce::Colours::transparentBlack,
                                   (float) scopePanel.getCentreX(),
                                   (float) scopePanel.getCentreY() + (float) scopePanel.getHeight() * 0.75f,
                                   true);
        g.setGradientFill (glow);
        g.fillRect (scopePanel.expanded (20, 20));
    }

    // ── 面板本体绘制 ────────────────────────────────────────────────
    drawPanel (g, carrierPanel);
    drawPanel (g, indexPanel);
    drawPanel (g, signalPanel);
    drawPanel (g, tonePanel);
    drawPanel (g, lfoPanel);
    drawPanel (g, envPanel);

    // ── 示波器下方频率跟踪读数条 ────────────────────────────────────
    if (! pitchMeterArea.isEmpty())
    {
        const auto r = pitchMeterArea.toFloat().reduced (0.5f);
        g.setColour (Palette::knobWell());
        g.fillRoundedRectangle (r, 4.0f);

        g.setColour (Palette::shadow().withAlpha (0.4f));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.5f, 1.0f);

        g.setColour (Palette::panelEdge().withAlpha (0.75f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);

        const auto* modeValue = processorRef.apvts.getRawParameterValue (BesselAudioProcessor::paramPitchMode);
        const auto mode = modeValue != nullptr ? (int) modeValue->load() : 0;
        const auto trackedHz = processorRef.detectedPitchHz.load();

        juce::Colour ledColour = Palette::muted().withAlpha (0.4f);
        if (mode != 0)
            ledColour = (trackedHz > 0.0f) ? Palette::accent() : Palette::accentWarm();

        const auto ledX = r.getX() + 10.0f;
        const auto ledY = r.getCentreY();
        g.setColour (ledColour.withAlpha (0.35f));
        g.fillEllipse (ledX - 4.0f, ledY - 4.0f, 8.0f, 8.0f);
        g.setColour (ledColour);
        g.fillEllipse (ledX - 2.0f, ledY - 2.0f, 4.0f, 4.0f);
    }

    // ── 面板角标 ────────────────────────────────────────────────────
    auto caption = [&g] (juce::String captionText, juce::Rectangle<int> panel, juce::Colour colour)
    {
        g.setColour (colour.withAlpha (0.65f));
        g.setFont (font (9.0f, juce::Font::bold, 0.22f));
        g.drawText (captionText, panel.reduced (13, 10), juce::Justification::topLeft, false);
    };

    caption ("CARRIER OSC", carrierPanel, Palette::accent());
    caption ("MODULATION DEPTH", indexPanel, Palette::accentWarm());

    // ── 信号处理区（Signal Panel）分组标记 ──────────────────────────
    if (! signalPanel.isEmpty())
    {
        const auto markerY = signalPanel.getY() + 10;
        const auto inner = signalPanel.reduced (12, 0);

        drawSectionMarker (g, inner.getX() + 2, markerY, "INPUT AM DYNAMICS", Palette::sectionDot());
        if (signalDividerX > 0)
            drawSectionMarker (g, signalDividerX + 12, markerY, "MODULATOR FILTER", Palette::sectionDotWarm());
    }

    // ── 谐波与主控区（Tone Panel）分组标记 ──────────────────────────
    if (! tonePanel.isEmpty())
    {
        const auto markerY = tonePanel.getY() + 10;
        const auto inner = tonePanel.reduced (12, 0);

        drawSectionMarker (g, inner.getX() + 2, markerY, "TONE & RIDDIM", Palette::sectionDotWarm());
        if (toneDividerX > 0)
            drawSectionMarker (g, toneDividerX + 12, markerY, "MASTER", Palette::sectionDot());
    }

    // ── LFO 调制区（LFO Panel）分组标记 ─────────────────────────────
    if (! lfoPanel.isEmpty())
    {
        const auto markerY = lfoPanel.getY() + 10;
        const auto inner = lfoPanel.reduced (12, 0);

        drawSectionMarker (g, inner.getX() + 2, markerY, "LFO GENERATOR", Palette::sectionDotWarm());
        if (lfoDividerY > 0)
            drawSectionMarker (g, inner.getX() + 2, lfoDividerY + 6, "TEMPO SYNC & TIMING", Palette::sectionDot());
    }

    // ── 包络调制区（Env Panel）分组标记 ─────────────────────────────
    if (! envPanel.isEmpty())
    {
        const auto markerY = envPanel.getY() + 10;
        const auto inner = envPanel.reduced (12, 0);

        drawSectionMarker (g, inner.getX() + 2, markerY, "ENVELOPE TO TARGETS", Palette::sectionDot());
        if (envDividerY > 0)
            drawSectionMarker (g, inner.getX() + 2, envDividerY + 6, "MOD FILTER ENVELOPE", Palette::muted());
    }

    // ── 渐变分隔线 ──────────────────────────────────────────────────
    const auto divColour = Palette::panelEdge().withAlpha (0.9f);

    if (signalDividerX > 0)
        drawGradientDivider (g, signalDividerX, signalPanel.getY() + 22, signalPanel.getBottom() - 14, divColour);

    if (toneDividerX > 0)
        drawGradientDivider (g, toneDividerX, tonePanel.getY() + 22, tonePanel.getBottom() - 14, divColour);

    // 水平微弱分隔线（LFO 与 ENV 面板内部上下层）
    if (lfoDividerY > 0)
    {
        const auto left  = (float) lfoPanel.getX() + 14.0f;
        const auto right = (float) lfoPanel.getRight() - 14.0f;
        juce::ColourGradient hLine (divColour.withAlpha (0.0f), left, (float) lfoDividerY,
                                    divColour.withAlpha (0.7f), (left + right) * 0.5f, (float) lfoDividerY, false);
        hLine.addColour (1.0, divColour.withAlpha (0.0f));
        g.setGradientFill (hLine);
        g.fillRect ((int) left, lfoDividerY, (int) (right - left), 1);
    }

    if (envDividerY > 0)
    {
        const auto left  = (float) envPanel.getX() + 14.0f;
        const auto right = (float) envPanel.getRight() - 14.0f;
        juce::ColourGradient hLine (divColour.withAlpha (0.0f), left, (float) envDividerY,
                                    divColour.withAlpha (0.7f), (left + right) * 0.5f, (float) envDividerY, false);
        hLine.addColour (1.0, divColour.withAlpha (0.0f));
        g.setGradientFill (hLine);
        g.fillRect ((int) left, envDividerY, (int) (right - left), 1);
    }
}

void BesselAudioProcessorEditor::resized()
{
    // 记住窗口大小
    processorRef.apvts.state.setProperty ("uiWidth",  getWidth(),  nullptr);
    processorRef.apvts.state.setProperty ("uiHeight", getHeight(), nullptr);

    auto area = getLocalBounds().reduced (18);

    // ── 1. 顶部 Header 栏 ──────────────────────────────────────────
    headerArea = area.removeFromTop (44);
    {
        auto header = headerArea;
        bypassButton.setBounds (header.removeFromRight (84).withSizeKeepingCentre (84, 26));
        header.removeFromRight (14);

        // 预设控制胶囊
        {
            auto bar = header.removeFromRight (juce::jmin (360, header.getWidth() / 2));
            bar = bar.withSizeKeepingCentre (bar.getWidth(), 26);
            presetBoxArea = bar;

            presetSave.setBounds (bar.removeFromRight (62).reduced (1, 1));
            bar.removeFromRight (4);
            presetNext.setBounds (bar.removeFromRight (24).reduced (1, 1));
            presetLabel.setBounds (bar.removeFromRight (juce::jmin (200, bar.getWidth())));
            presetPrev.setBounds (bar.removeFromRight (24).reduced (1, 1));
        }

        auto text = header.removeFromLeft (juce::jmin (420, header.getWidth()));
        titleLabel.setBounds (text.removeFromTop (28));
        subtitleLabel.setBounds (text.removeFromTop (14));
    }

    area.removeFromTop (8);

    // ── 2. 底部状态栏 ──────────────────────────────────────────────
    footerArea = area.removeFromBottom (16);
    footerLabel.setBounds (footerArea);
    area.removeFromBottom (6);

    // ── 3. 底层：调制矩阵区（MODULATION ENGINE） ──────────────────
    auto modRow = area.removeFromBottom (196);
    area.removeFromBottom (10);

    const auto lfoW = (modRow.getWidth() - 12) * 51 / 100;
    lfoPanel = modRow.removeFromLeft (lfoW);
    modRow.removeFromLeft (12);
    envPanel = modRow;

    // LFO Panel 布局（上层：旋钮与波形；下层：工程同步）
    {
        auto inner = lfoPanel.reduced (12, 10);
        inner.removeFromTop (16);

        const auto halfH = inner.getHeight() / 2;
        auto topHalf = inner.removeFromTop (halfH - 2);
        lfoDividerY = inner.getY() + 1;
        inner.removeFromTop (18);

        // 上半部 4 列：Rate, LFO>Pitch, LFO>Cut, Shape
        const auto colW = topHalf.getWidth() / 4;
        lfoRateKnob.setBounds   (topHalf.removeFromLeft (colW).reduced (3, 0));
        lfoPitchKnob.setBounds  (topHalf.removeFromLeft (colW).reduced (3, 0));
        lfoCutoffKnob.setBounds (topHalf.removeFromLeft (colW).reduced (3, 0));

        {
            auto col = topHalf.reduced (3, 0);
            lfoShapeLabel.setBounds (col.removeFromTop (15));
            col.removeFromBottom (16);
            lfoShapeSelector.setBounds (col.reduced (4, 4));
        }

        // 下半部 3 列：Sync, Grid, Feel
        const auto botColW = inner.getWidth() / 3;
        auto placeSelector = [&inner, botColW] (juce::Label& label, juce::Component& control)
        {
            auto column = inner.removeFromLeft (botColW).reduced (4, 0);
            label.setBounds (column.removeFromTop (15));
            column.removeFromBottom (14);
            control.setBounds (column.reduced (4, 3));
        };

        placeSelector (syncLabel,    lfoSyncSelector);
        placeSelector (gridLabel,    gridSelector);
        placeSelector (gridModLabel, gridModSelector);
    }

    // ENV Panel 布局（上层：目标路由；下层：调制包络）
    {
        auto inner = envPanel.reduced (12, 10);
        inner.removeFromTop (16);

        const auto halfH = inner.getHeight() / 2;
        auto topHalf = inner.removeFromTop (halfH - 2);
        envDividerY = inner.getY() + 1;
        inner.removeFromTop (18);

        // 上半部 3 列：Env>Pitch, Env>Cut, Env>Rate
        const auto colW = topHalf.getWidth() / 3;
        envPitchKnob.setBounds  (topHalf.removeFromLeft (colW).reduced (4, 0));
        envCutoffKnob.setBounds (topHalf.removeFromLeft (colW).reduced (4, 0));
        envRateKnob.setBounds   (topHalf.reduced (4, 0));

        // 下半部 2 列等分居中：Attack, Release
        const auto botColW = inner.getWidth() / 2;
        env2AttackKnob.setBounds  (inner.removeFromLeft (botColW).reduced (16, 0));
        env2ReleaseKnob.setBounds (inner.reduced (16, 0));
    }

    // ── 4. 中层：信号链路与音色区（PROCESSING & TONE） ───────────
    auto midRow = area.removeFromBottom (154);
    area.removeFromBottom (10);

    const auto signalW = (midRow.getWidth() - 12) * 56 / 100;
    signalPanel = midRow.removeFromLeft (signalW);
    midRow.removeFromLeft (12);
    tonePanel = midRow;

    // Signal Panel 布局（INPUT AM 3列 + MOD FILTER 4列）
    {
        auto inner = signalPanel.reduced (12, 10);
        inner.removeFromTop (18);

        const auto colW = inner.getWidth() / 7;

        amGainKnob.setBounds    (inner.removeFromLeft (colW).reduced (3, 0));
        amAttackKnob.setBounds  (inner.removeFromLeft (colW).reduced (3, 0));
        amReleaseKnob.setBounds (inner.removeFromLeft (colW).reduced (3, 0));

        signalDividerX = inner.getX() - 4;

        modGainKnob.setBounds (inner.removeFromLeft (colW).reduced (3, 0));
        modFreqKnob.setBounds (inner.removeFromLeft (colW).reduced (3, 0));
        modQKnob.setBounds    (inner.removeFromLeft (colW).reduced (3, 0));

        {
            auto col = inner.reduced (3, 0);
            filterTitleLabel.setBounds (col.removeFromTop (15));
            col.removeFromBottom (16);
            filterSelector.setBounds (col.reduced (4, 6));
        }
    }

    // Tone Panel 布局（COMB 2列 + SUB/DRIVE 2列 + MASTER 2列）
    {
        auto inner = tonePanel.reduced (12, 10);
        inner.removeFromTop (18);

        const auto colW = inner.getWidth() / 6;

        combKnob.setBounds   (inner.removeFromLeft (colW).reduced (3, 0));
        combFbKnob.setBounds (inner.removeFromLeft (colW).reduced (3, 0));
        subKnob.setBounds    (inner.removeFromLeft (colW).reduced (3, 0));
        driveKnob.setBounds  (inner.removeFromLeft (colW).reduced (3, 0));

        toneDividerX = inner.getX() - 4;

        mixKnob.setBounds    (inner.removeFromLeft (colW).reduced (3, 0));
        outputKnob.setBounds (inner.reduced (3, 0));
    }

    // ── 5. 顶部主视觉区（HERO OSC & SCOPE & DEPTH） ───────────────
    auto heroRow = area;

    carrierPanel = heroRow.removeFromLeft (204);
    heroRow.removeFromLeft (12);

    indexPanel = heroRow.removeFromRight (192);
    heroRow.removeFromRight (12);

    scopePanel = heroRow;

    // Carrier Panel：上面大旋钮，下面并排 Shape 和 Glide！
    {
        auto inner = carrierPanel.reduced (12, 14);
        inner.removeFromTop (12);

        auto bottomRow = inner.removeFromBottom (106);
        inner.removeFromBottom (4);

        carrierKnob.setBounds (inner);

        const auto halfW = bottomRow.getWidth() / 2;
        shapeKnob.setBounds (bottomRow.removeFromLeft (halfW).reduced (2, 0));
        glideKnob.setBounds (bottomRow.reduced (2, 0));
    }

    // Depth Panel：PM / FM 两个调制量上下等分
    {
        auto inner = indexPanel.reduced (12, 16);
        inner.removeFromTop (12);

        const auto half = inner.getHeight() / 2;
        pmAmountKnob.setBounds (inner.removeFromTop (half - 4));
        inner.removeFromTop (8);
        fmAmountKnob.setBounds (inner);
    }

    // Scope Panel：示波器 + LFO 视窗 + 硬件频率跟踪读数条
    {
        auto inner = scopePanel.reduced (12, 12);
        auto followRow = inner.removeFromBottom (28);
        inner.removeFromBottom (8);

        lfoWaveArea = inner.removeFromBottom (34);
        inner.removeFromBottom (8);
        lfoWave.setBounds (lfoWaveArea);

        scope.setBounds (inner);

        pitchModeSelector.setBounds (followRow.removeFromLeft (186).reduced (0, 1));
        followRow.removeFromLeft (10);
        pitchMeterArea = followRow;
        pitchLabel.setBounds (followRow.withTrimmedLeft (22));
    }
}

//==============================================================================
void BesselAudioProcessorEditor::refreshPresetName()
{
    presetLabel.setText (processorRef.presets.getCurrentName(), juce::dontSendNotification);
}

void BesselAudioProcessorEditor::setPresetBrowserVisible (bool shouldBeVisible)
{
    if (shouldBeVisible)
    {
        presetPanel.refresh();
        presetPanel.setBounds (getLocalBounds());      // 铺满：内部自己画暗场和卡片
        presetPanel.setVisible (true);
        presetPanel.toFront (false);
    }
    else
    {
        presetPanel.setVisible (false);
        refreshPresetName();
    }
}

//==============================================================================
void BesselAudioProcessorEditor::timerCallback()
{
    carrierKnob.refresh();
    shapeKnob.refresh();
    glideKnob.refresh();

    pmAmountKnob.refresh();
    fmAmountKnob.refresh();

    amGainKnob.refresh();
    amAttackKnob.refresh();
    amReleaseKnob.refresh();

    modGainKnob.refresh();
    modFreqKnob.refresh();
    modQKnob.refresh();

    combKnob.refresh();
    combFbKnob.refresh();
    subKnob.refresh();
    driveKnob.refresh();

    mixKnob.refresh();
    outputKnob.refresh();

    lfoRateKnob.refresh();
    lfoPitchKnob.refresh();
    lfoCutoffKnob.refresh();

    envPitchKnob.refresh();
    envCutoffKnob.refresh();
    envRateKnob.refresh();

    env2AttackKnob.refresh();
    env2ReleaseKnob.refresh();

    scope.refresh();
    lfoWave.refresh();

    const auto lfoNow = processorRef.currentLfoHz.load();

    // 预设操作的结果（保存失败 / 老格式 / 缺参数）优先占用页脚 6 秒
    const auto presetStatus = processorRef.presets.getLastMessage();
    const auto statusAge    = juce::Time::getMillisecondCounterHiRes() * 0.001
                                - processorRef.presets.getLastMessageTime();

    if (presetStatus.isNotEmpty() && statusAge < 6.0)
    {
        footerLabel.setText (presetStatus, juce::dontSendNotification);
        footerLabel.setColour (juce::Label::textColourId, Palette::accentWarm());
    }
    else
    {
        footerLabel.setColour (juce::Label::textColourId, Palette::muted());
        footerLabel.setText (text ("4\xC3\x97 OS  \xC2\xB7  LATENCY ") + juce::String (processorRef.getLatencySamples())
                             + text (" smp  \xC2\xB7  ")
                             + juce::String ((float) processorRef.getSampleRate() / 1000.0f, 1)
                             + text (" kHz  \xC2\xB7  LFO ")
                             + juce::String (lfoNow, lfoNow < 10.0f ? 2 : 1) + " Hz",
                             juce::dontSendNotification);
    }

    const auto* modeValue = processorRef.apvts.getRawParameterValue (BesselAudioProcessor::paramPitchMode);
    const auto mode       = modeValue != nullptr ? (int) modeValue->load() : 0;
    const auto carrierHz  = processorRef.currentCarrierHz.load();
    const auto carrierText = juce::String (carrierHz, carrierHz < 100.0f ? 2 : 1) + " Hz";
    const auto trackedHz  = processorRef.detectedPitchHz.load();

    if (mode == 0)
    {
        pitchLabel.setText (text ("FREE  \xC2\xB7  CARRIER ") + carrierText, juce::dontSendNotification);
        pitchLabel.setColour (juce::Label::textColourId, Palette::muted());
    }
    else if (trackedHz > 0.0f)
    {
        pitchLabel.setText ((mode == 2 ? "MIDI " : "TRACKING ")
                            + juce::String (trackedHz, 1) + text ("  \xE2\x86\x92  CARRIER ") + carrierText,
                            juce::dontSendNotification);
        pitchLabel.setColour (juce::Label::textColourId, Palette::accent());
    }
    else
    {
        pitchLabel.setText (text ("NO PITCH  \xC2\xB7  CARRIER ") + carrierText,
                            juce::dontSendNotification);
        pitchLabel.setColour (juce::Label::textColourId, Palette::muted());
    }
}
