#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

#include "../PluginProcessor.h"
#include "Palette.h"

namespace sfx::ui
{

//==============================================================================
// 预设浏览器（覆盖在编辑器上的浮动面板）。
//
// 为什么要有它：◀ ▶ 只能顺序翻，预设一多就没法用了，而且"存"和"另存"在
// 一个按钮上会互相打架。这里把它们拆开：
//
//   点列表里的名字   → 加载
//   SAVE            → 覆盖选中的用户预设（工厂预设只读）
//   SAVE AS         → 用输入框的名字新建一个
//   RENAME          → 把选中的用户预设改成输入框的名字
//
// 工厂预设画一个 F 标签，用户预设画 U —— 一眼分清哪个能改。
//==============================================================================
class PresetPanel : public juce::Component,
                    private juce::ListBoxModel
{
public:
    explicit PresetPanel (BesselAudioProcessor& p)
        : processor (p)
    {
        setOpaque (false);

        list.setModel (this);
        list.setRowHeight (26);
        list.setOutlineThickness (1);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setColour (juce::ListBox::outlineColourId, Palette::panelEdge());
        addAndMakeVisible (list);

        nameEditor.setColour (juce::TextEditor::backgroundColourId, Palette::knobWell());
        nameEditor.setColour (juce::TextEditor::textColourId, Palette::text());
        nameEditor.setColour (juce::TextEditor::outlineColourId, Palette::panelEdge());
        nameEditor.setColour (juce::TextEditor::focusedOutlineColourId, Palette::accent());
        nameEditor.setFont (font (12.0f, juce::Font::bold, 0.04f));
        nameEditor.setJustification (juce::Justification::centredLeft);
        nameEditor.setTextToShowWhenEmpty ("preset name", Palette::muted());
        addAndMakeVisible (nameEditor);

        auto setupButton = [this] (juce::TextButton& b, const char* text)
        {
            b.setButtonText (text);
            addAndMakeVisible (b);
        };

        setupButton (saveButton,   "SAVE");
        setupButton (saveAsButton, "SAVE AS");
        setupButton (renameButton, "RENAME");
        setupButton (deleteButton, "DELETE");
        setupButton (closeButton,  "CLOSE");

        // 删除是两步：点一下变 SURE?，再点才真删（进回收站）
        deleteButton.onClick = [this]
        {
            if (! deleteArmed)
            {
                deleteArmed = true;
                deleteButton.setButtonText ("SURE?");
                return;
            }

            processor.presets.deleteCurrent();
            disarmDelete();
            refresh();
        };

        closeButton.onClick = [this] { if (onClose) onClose(); };

        saveButton.onClick = [this]
        {
            processor.presets.overwriteCurrent();
            refresh();
        };

        saveAsButton.onClick = [this]
        {
            auto name = nameEditor.getText().trim();

            if (name.isEmpty())
                name = juce::String ("User ") + juce::String (processor.presets.size() + 1);

            processor.presets.save (name);
            refresh();
        };

        renameButton.onClick = [this]
        {
            if (processor.presets.renameCurrent (nameEditor.getText()))
                refresh();
        };

        closeButton.onClick = [this] { if (onClose) onClose(); };

        nameEditor.onReturnKey = [this] { saveAsButton.onClick(); };

        refresh();
    }

    std::function<void()> onClose;

    void refresh()
    {
        processor.presets.rescan();
        list.updateContent();
        list.repaint();

        const auto current = processor.presets.getCurrentIndex();

        if (current >= 0)
        {
            list.selectRow (current, true, true);
            nameEditor.setText (processor.presets.getName (current), juce::dontSendNotification);
        }

        const auto canModify = processor.presets.canModifyCurrent();
        saveButton.setEnabled (canModify);
        renameButton.setEnabled (canModify);
        deleteButton.setEnabled (canModify);
        disarmDelete();
    }

    void disarmDelete()
    {
        deleteArmed = false;
        deleteButton.setButtonText ("DELETE");
    }

    /** 卡片区域（铺满的暗场中间那块）。 */
    juce::Rectangle<int> getCardBounds() const
    {
        return getLocalBounds().withSizeKeepingCentre (juce::jmin (470, getWidth() - 80),
                                                       juce::jmin (570, getHeight() - 80));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // 点暗场（卡片外面）就关掉
        if (! getCardBounds().contains (e.getPosition()) && onClose != nullptr)
            onClose();
    }

    void paint (juce::Graphics& g) override
    {
        // 暗场：让底下的界面退到背景去
        g.fillAll (juce::Colours::black.withAlpha (0.55f));

        auto r = getCardBounds().toFloat().reduced (0.5f);

        g.setColour (Palette::shadow().withAlpha (0.55f));
        g.fillRoundedRectangle (r.translated (0.0f, 3.0f), 10.0f);

        juce::ColourGradient fill (Palette::panelTop(), r.getCentreX(), r.getY(),
                                   Palette::panelBottom(), r.getCentreX(), r.getBottom(), false);
        g.setGradientFill (fill);
        g.fillRoundedRectangle (r, 10.0f);

        g.setColour (Palette::panelEdge());
        g.drawRoundedRectangle (r, 10.0f, 1.0f);

        g.setColour (Palette::text());
        g.setFont (font (12.0f, juce::Font::bold, 0.20f));
        g.drawText ("PRESETS", getCardBounds().reduced (16, 12).removeFromTop (20),
                    juce::Justification::topLeft, false);
    }

    void resized() override
    {
        auto area = getCardBounds().reduced (14);
        area.removeFromTop (24);                 // 标题

        auto footer = area.removeFromBottom (64);

        list.setBounds (area);

        footer.removeFromTop (10);
        nameEditor.setBounds (footer.removeFromTop (24).reduced (0, 0));

        footer.removeFromTop (6);
        auto buttons = footer;
        const auto width = juce::jmax (58, (buttons.getWidth() - 32) / 5);

        saveButton.setBounds   (buttons.removeFromLeft (width).reduced (2, 0));
        saveAsButton.setBounds (buttons.removeFromLeft (width).reduced (2, 0));
        renameButton.setBounds (buttons.removeFromLeft (width).reduced (2, 0));
        deleteButton.setBounds (buttons.removeFromLeft (width).reduced (2, 0));
        closeButton.setBounds  (buttons.reduced (2, 0));
    }

private:
    //==========================================================================
    int getNumRows() override { return processor.presets.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height,
                           bool rowIsSelected) override
    {
        if (row < 0 || row >= processor.presets.size())
            return;

        const auto factory = processor.presets.isFactory (row);
        auto item = juce::Rectangle<int> (0, 0, width, height).reduced (2, 1);

        if (rowIsSelected)
        {
            g.setColour (Palette::accent().withAlpha (0.16f));
            g.fillRoundedRectangle (item.toFloat(), 4.0f);
        }

        // F / U 标签
        auto tag = item.removeFromLeft (26);
        g.setColour ((factory ? Palette::accentWarm() : Palette::accent()).withAlpha (0.85f));
        g.setFont (font (9.0f, juce::Font::bold, 0.10f));
        g.drawText (factory ? "F" : "U", tag, juce::Justification::centred, false);

        g.setColour (rowIsSelected ? Palette::text() : Palette::muted());
        g.setFont (font (12.0f, juce::Font::plain, 0.02f));
        g.drawText (processor.presets.getName (row), item.reduced (6, 0),
                    juce::Justification::centredLeft, true);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        if (row < 0 || row >= processor.presets.size())
            return;

        processor.presets.load (row);
        nameEditor.setText (processor.presets.getName (row), juce::dontSendNotification);

        saveButton.setEnabled (processor.presets.canModifyCurrent());
        renameButton.setEnabled (processor.presets.canModifyCurrent());
    }

    BesselAudioProcessor& processor;

    juce::ListBox list;
    juce::TextEditor nameEditor;
    juce::TextButton saveButton, saveAsButton, renameButton, deleteButton, closeButton;
    bool deleteArmed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetPanel)
};

} // namespace sfx::ui
