cmake_minimum_required(VERSION 3.24)

project(ANATOMY VERSION 1.0.0 LANGUAGES C CXX)

# ==============================================================================
# VST3の互換エラーと過去キャッシュの干渉を徹底排除 (重要)
# ==============================================================================
set(JUCE_VST3_AUTO_MANIFEST OFF)
add_compile_definitions(JUCE_VST3_CAN_REPLACE_VST2=0)

# ==============================================================================
# C++ / ビルド出力設定
# ==============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# ==============================================================================
# JUCEの組み込み（OTODESK様の安定パス構造を完全維持）
# ==============================================================================
set(JUCE_DIR "C:/JUCE")
add_subdirectory(${JUCE_DIR} build_juce)

# ==============================================================================
# プラグインの定義
# ==============================================================================
juce_add_plugin(${PROJECT_NAME}
    COMPANY_NAME "OTODESK"
    PLUGIN_MANUFACTURER_CODE "Otdk"
    PLUGIN_CODE "Anat"
    FORMATS VST3 Standalone
    PRODUCT_NAME "ANATOMY"
    PLUGIN_DESCRIPTION "HPSS Audio Splice & Synthesis"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_AUTO_MANIFEST FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VERSION "1.1.0"
)

# ==============================================================================
# コンパイラオプション (文字コードエラーの無視と最適化)
# ==============================================================================
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:
        $<$<CONFIG:Release>:/O2 /fp:fast> /arch:AVX2 /wd4819 /utf-8
    >
)

# ==============================================================================
# ソースファイルの指定
# ==============================================================================
target_sources(${PROJECT_NAME} PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    "Source/DSP/HpssSeparator.h"
    "Source/DSP/HpssSeparator.cpp"
    "Source/DSP/SharedSampleData.h"
    "Source/DSP/VoiceState.h"
    "Source/DSP/TransientReplacer.h"
    "Source/DSP/TonalReplacer.h"
    "Source/DSP/Effects/AudioEffect.h"
    "Source/DSP/Effects/EffectChain.h"
    "Source/DSP/Effects/ADAA_Saturation.h"
    "Source/DSP/Effects/BitCrusher.h"
    "Source/DSP/Effects/NoiseGenerator.h"
    "Source/DSP/Effects/OTT_Multiband.h"
    "Source/DSP/Effects/GlueCompressor.h"
    "Source/DSP/Effects/Limiter.h"
    "Source/DSP/BeforeAfterBypasser.h"
    "Source/DSP/OfflineMixRenderer.h"
    "Source/UI/ColorPalette.h"
    "Source/UI/ArcDial.h"
    "Source/UI/ArcDial.cpp"
    "Source/UI/ValueKnob.h"
    "Source/UI/GlowToggle.h"
    "Source/UI/WaveformComponent.h"
    "Source/UI/WaveformComponent.cpp"
    "Source/UI/TransientLaneView.h"
    "Source/UI/TonalLaneView.h"
    "Source/UI/FxSlotCard.h"
    "Source/UI/FxSlotCard.cpp"
    "Source/UI/FxRackView.h"
    "Source/UI/FxRackView.cpp"
)

# ==============================================================================
# JUCEモジュールのリンク
# ==============================================================================
target_link_libraries(${PROJECT_NAME} PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_plugin_client
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_dsp
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
)