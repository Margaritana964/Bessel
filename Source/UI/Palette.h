#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
// 界面的配色和字体都收在这里，改主题只动这一处。
//==============================================================================
namespace sfx::ui
{

struct Palette
{
    // 底
    static juce::Colour background()    { return juce::Colour (0xff0a0e14); }
    static juce::Colour backgroundTop() { return juce::Colour (0xff121820); }

    // 面板用上下两色做竖向渐变，靠这个才有「面」而不是「框」
    static juce::Colour panelTop()    { return juce::Colour (0xff1a212a); }
    static juce::Colour panelBottom() { return juce::Colour (0xff11171e); }
    static juce::Colour panelEdge()   { return juce::Colour (0xff2b3644); }
    static juce::Colour highlight()   { return juce::Colour (0x16ffffff); }
    static juce::Colour shadow()      { return juce::Colour (0x66000000); }

    // 旋钮凹槽
    static juce::Colour knobWell()    { return juce::Colour (0xff0d1219); }

    static juce::Colour text()        { return juce::Colour (0xffe4ecf4); }
    static juce::Colour muted()       { return juce::Colour (0xff718091); }
    static juce::Colour accent()      { return juce::Colour (0xff35e0c4); }   // 载波 / 主体
    static juce::Colour accentWarm()  { return juce::Colour (0xffffb03a); }   // 调制深度
    static juce::Colour scope()       { return juce::Colour (0xff070a0e); }

    // ── 新增：分组标记和增强视觉层次 ──────────────────────────
    // 分组标题前面的小方块颜色
    static juce::Colour sectionDot()     { return accent().withAlpha (0.75f); }
    static juce::Colour sectionDotWarm() { return accentWarm().withAlpha (0.75f); }

    // 面板内部更深的分隔区
    static juce::Colour panelInset()     { return juce::Colour (0xff0f141b); }

    // 更深的投影
    static juce::Colour deepShadow()     { return juce::Colour (0x99000000); }

    // 副强调色：比 accent 更暗淡，用于次要交互元素
    static juce::Colour accentDim()      { return accent().withAlpha (0.35f); }
    static juce::Colour warmDim()        { return accentWarm().withAlpha (0.35f); }
};

/** tracking = 字距。小号全大写标签加一点字距，观感立刻不一样。 */
inline juce::Font font (float height, int style = juce::Font::plain, float tracking = 0.0f)
{
    return juce::Font (juce::FontOptions (height, style).withKerningFactor (tracking));
}

/** juce::String 的 const char* 构造按 Latin-1 解码，不是 UTF-8。
 *  界面文案里凡是带非 ASCII 字符（· × — 中文）的，都必须包一层这个，
 *  否则会显示成「Â·」这种乱码。 */
inline juce::String text (const char* utf8)
{
    return juce::String::fromUTF8 (utf8);
}

} // namespace sfx::ui
