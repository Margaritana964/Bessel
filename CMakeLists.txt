cmake_minimum_required(VERSION 3.22)

project(Bessel VERSION 0.3.0 LANGUAGES C CXX)

set(BESSEL_JUCE_TAG 9.0.5 CACHE STRING "JUCE 版本（FetchContent 用）")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# 静态 CRT：给"发给别人"用的构建
#
# 默认 /MD 会依赖 VCRUNTIME140.dll / MSVCP140.dll，对方机器上没装
# VC++ 2015-2022 运行库就加载失败。打开这个开关后运行库编进 DLL，解压即用。
#   cmake -S . -B build-static -G Ninja -DCMAKE_BUILD_TYPE=Release -DBESSEL_STATIC_CRT=ON
# 必须在任何 target 之前设置。
# ---------------------------------------------------------------------------
option(BESSEL_STATIC_CRT "Link the MSVC runtime statically (self-contained build)" OFF)

if(MSVC AND BESSEL_STATIC_CRT)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
    message(STATUS "Bessel: 静态 CRT —— 产物不依赖 VC++ 运行库")
endif()

# 源码里有中文和「·」「×」这类符号，必须让 MSVC 按 UTF-8 解释，
# 否则字符串字面量会按系统代码页（GBK）落地，界面上的「·」变成「Â·」。
if(MSVC)
    add_compile_options(/utf-8)
endif()

# JUCE 依赖。
#
# 本地开发时用同级目录的浅克隆（快、不用每次拉）：
#   ../JUCE
#
# 开源之后别人 clone 下来没有这个目录，就走 FetchContent 自动拉指定 tag。
# 两种都能用，不用改任何脚本。
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../JUCE/CMakeLists.txt)
    message(STATUS "Bessel: 使用本地 JUCE（../JUCE）")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../JUCE ${CMAKE_CURRENT_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    message(STATUS "Bessel: 本地没有 JUCE，用 FetchContent 拉取 ${BESSEL_JUCE_TAG}")

    include(FetchContent)

    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${BESSEL_JUCE_TAG}
        GIT_SHALLOW    TRUE)

    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
# 插件本体
# ---------------------------------------------------------------------------
# 只出 VST3，不打算出 VST2，所以关掉 JUCE 的「VST2/VST3 参数 ID 冲突」保护。
# 一旦你以后真要出 VST2，这两个版本必须用同一套参数 ID，别回头改这个开关。
add_compile_definitions(JUCE_VST3_CAN_REPLACE_VST2=0)

# 开了 MIDI 输入之后，JUCE 的 VST3 包装层默认会「把每个 MIDI CC 都变成一个自动化参数」
# —— 16 通道 × 128 CC = 2048 个，参数总数直接涨到 2100。
# 那是给"要用 CC 调参数"的插件准备的；这里只用 Note / Pitch Bend，不需要。
add_compile_definitions(JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0)

# 版本号传给代码：工厂预设的版本戳用它判断要不要重建
add_compile_definitions(BESSEL_VERSION="${PROJECT_VERSION}")

# 注意：PLUGIN_MANUFACTURER_CODE / PLUGIN_CODE 各 4 个字符，
# 一起决定 VST3 的类 ID。上线前必须改成你自己的，改完 DAW 里旧实例会失效。
juce_add_plugin(Bessel
    COMPANY_NAME                "DIY Audio"
    BUNDLE_ID                   "com.diyaudio.bessel"
    PLUGIN_MANUFACTURER_CODE    Diya
    PLUGIN_CODE                 Bsse
    PRODUCT_NAME                "Bessel"
    FORMATS                     VST3 Standalone
    IS_SYNTH                    FALSE
    # 仍然是效果器（能挂在音频轨的 FX 槽里吃外部素材），但额外接收 MIDI，
    # 用来接管载波音高 / 弯音。宿主里表现为「带 MIDI 输入的效果器」。
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx
    VST3_AUTO_MANIFEST          FALSE
)

# 插件本体和所有需要链接它的测试共用这份源码清单
set(BESSEL_PLUGIN_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UI/PmLookAndFeel.cpp
    Source/UI/ParamKnob.cpp
    Source/UI/ScopeComponent.cpp
    Source/UI/SegmentedControl.cpp
)

target_sources(Bessel PRIVATE ${BESSEL_PLUGIN_SOURCES})

target_compile_definitions(Bessel PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(Bessel
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# 无头冒烟测试：不开 DAW 就能验证 DSP 链路
# ---------------------------------------------------------------------------
juce_add_console_app(PmEffectTest PRODUCT_NAME "PmEffectTest")

target_sources(PmEffectTest PRIVATE
    Tests/PmEffectTest.cpp
    ${BESSEL_PLUGIN_SOURCES}
)

target_compile_definitions(PmEffectTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(PmEffectTest
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# 音高跟踪诊断：把 test-material 里的 wav 喂进去，打一张检测结果表
# ---------------------------------------------------------------------------
juce_add_console_app(TrackReport PRODUCT_NAME "TrackReport")

target_sources(TrackReport PRIVATE
    Tests/TrackReport.cpp
    ${BESSEL_PLUGIN_SOURCES}
)

target_compile_definitions(TrackReport PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(TrackReport
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# 宿主视角验证：像 DAW 一样扫描并加载 build 出来的 .vst3
# ---------------------------------------------------------------------------
juce_add_console_app(PluginScanTest PRODUCT_NAME "PluginScanTest")

target_sources(PluginScanTest PRIVATE
    Tests/PluginScanTest.cpp
)

target_compile_definitions(PluginScanTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    # JUCE 9 起插件宿主默认关闭，要显式打开才认得出 VST3 bundle
    JUCE_PLUGINHOST_VST3=1
)

target_link_libraries(PluginScanTest
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# 界面截图：不开宿主、不弹窗口，把编辑器渲染成 PNG（改界面时看效果用）
# ---------------------------------------------------------------------------
juce_add_console_app(UiSnapshot PRODUCT_NAME "UiSnapshot")

target_sources(UiSnapshot PRIVATE
    Tests/UiSnapshot.cpp
    ${BESSEL_PLUGIN_SOURCES}
)

target_compile_definitions(UiSnapshot PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(UiSnapshot
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
