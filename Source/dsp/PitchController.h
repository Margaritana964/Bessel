#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace sfx
{

//==============================================================================
// MIDI 音高控制：接管载波频率。
//
// - 单音，late-note priority（后按的覆盖前按的）
// - Pitch Bend 范围 ±bendSemitones（默认 12）
// - 连奏滑音：每块一次一极点，振荡器里还有 30 ms 平滑兜底
// - Note-On 会请求一次相位复位（配合 1 ms 淡入防爆音）
//
// 没有按住的音时 isActive() 为假，调用方回落到 Carrier 旋钮 ——
// 这样它仍然是个"能挂 MIDI 的效果器"，而不是非按不可的合成器。
//==============================================================================
class PitchController
{
public:
    void prepare (double sampleRateToUse) noexcept
    {
        sr = sampleRateToUse;
        reset();
    }

    void reset() noexcept
    {
        note           = -1;
        bend           = 0.0f;
        targetHz       = 0.0f;
        currentHz      = 0.0f;
        retriggerArmed = false;
    }

    void setGlideMs (float ms) noexcept        { glideMs = juce::jlimit (1.0f, 1000.0f, ms); }
    void setBendSemitones (float semis) noexcept { bendRange = juce::jmax (0.0f, semis); }

    /** 解析一个块里的 MIDI，并把滑音推进一步。 */
    void process (const juce::MidiBuffer& midi, int numSamples) noexcept
    {
        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();

            if (message.isNoteOn())
            {
                note = message.getNoteNumber();
                retriggerArmed = true;      // 下一块开始处复位相位
                updateTarget();
            }
            else if (message.isNoteOff())
            {
                if (message.getNoteNumber() == note)
                {
                    note = -1;
                    updateTarget();
                }
            }
            else if (message.isPitchWheel())
            {
                bend = (float) (message.getPitchWheelValue() - 8192) / 8192.0f;
                updateTarget();
            }
            else if (message.isAllNotesOff() || message.isAllSoundOff())
            {
                note = -1;
                updateTarget();
            }
        }

        if (targetHz <= 0.0f)
            return;                          // 没音：保持现状，别往 0 Hz 滑

        if (currentHz <= 0.0f)
            currentHz = targetHz;            // 从无到有：直接落位，不从 0 滑上来

        // 每块一次一极点；块较粗，但振荡器里还有 30 ms 平滑
        const auto timeConstant = juce::jmax (1.0e-4, (double) glideMs * 0.001);
        const auto coefficient  = 1.0 - std::exp (-(double) numSamples / (sr * timeConstant));
        currentHz += (float) ((targetHz - currentHz) * coefficient);
    }

    bool  isActive()      const noexcept { return targetHz > 0.0f; }
    float getFrequencyHz() const noexcept { return currentHz; }

    /** 取走一次"需要复位相位"的请求。 */
    bool consumeRetrigger() noexcept
    {
        const auto armed = retriggerArmed;
        retriggerArmed = false;
        return armed;
    }

private:
    void updateTarget() noexcept
    {
        if (note < 0)
        {
            targetHz = 0.0f;
            return;
        }

        const auto base = (float) juce::MidiMessage::getMidiNoteInHertz (note);
        targetHz = base * std::pow (2.0f, bend * bendRange / 12.0f);
    }

    double sr         = 48000.0;
    int    note       = -1;
    float  bend       = 0.0f;
    float  bendRange  = 12.0f;
    float  glideMs    = 60.0f;
    float  targetHz   = 0.0f;
    float  currentHz  = 0.0f;
    bool   retriggerArmed = false;
};

} // namespace sfx
