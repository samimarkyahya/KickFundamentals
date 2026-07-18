cmake_minimum_required(VERSION 3.22)

project(KickFundamentals VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# JUCE is pulled in automatically. Nothing to vendor.
#   - Default: fetched from GitHub via FetchContent.
#   - If you already have JUCE locally, pass -DJUCE_LOCAL_DIR=/path/to/JUCE
#     to avoid the download.
# ---------------------------------------------------------------------------
# JUCE version. Defaults to 8.0.4 for native builds. MinGW cross-compiles can't
# supply the Direct2D 1.3 headers JUCE 8 requires, so those pass -DKF_JUCE_TAG=7.0.12.
set(KF_JUCE_TAG "8.0.4" CACHE STRING "JUCE git tag to fetch")

if(DEFINED JUCE_LOCAL_DIR)
    add_subdirectory(${JUCE_LOCAL_DIR} juce_build)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${KF_JUCE_TAG}
        GIT_SHALLOW    ON
    )
    FetchContent_MakeAvailable(JUCE)
endif()

# Assemble juce_add_plugin arguments, adjusting for cross-compilation and JUCE
# version differences.
set(KF_PLUGIN_ARGS
    COMPANY_NAME              "EVO"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    PLUGIN_MANUFACTURER_CODE  Evof
    PLUGIN_CODE               Kfnd
    FORMATS                   VST3 Standalone
    PRODUCT_NAME              "Kick Fundamentals")

if(CMAKE_CROSSCOMPILING)
    # The post-build copy targets a host path and the VST3 manifest helper is a
    # host-unrunnable .exe, so skip both when cross-compiling.
    list(APPEND KF_PLUGIN_ARGS COPY_PLUGIN_AFTER_BUILD FALSE)
    # The manifest helper is a host-unrunnable Windows .exe; disable it.
    # (VST3_AUTO_MANIFEST is supported in both JUCE 7.0.12 and JUCE 8.)
    list(APPEND KF_PLUGIN_ARGS VST3_AUTO_MANIFEST FALSE)
else()
    list(APPEND KF_PLUGIN_ARGS COPY_PLUGIN_AFTER_BUILD TRUE)
endif()

juce_add_plugin(KickFundamentals ${KF_PLUGIN_ARGS})

# Embed the logo artwork so the plugin is self-contained (BinaryData namespace).
juce_add_binary_data(KickFundamentalsAssets
    SOURCES
        Assets/logo_title.png
        Assets/logo_byline.png)

target_sources(KickFundamentals PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/KickAnalyzer.cpp)

target_compile_definitions(KickFundamentals PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(KickFundamentals
    PRIVATE
        KickFundamentalsAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
