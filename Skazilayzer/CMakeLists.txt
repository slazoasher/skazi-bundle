cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary (Apple Silicon + Intel)")

# SKAZILAYZER by Asher Swissa: analyser (spectrum blocks with note readout + peak freeze, peak/RMS/VU meter,
# goniometer, EBU R128 loudness, scrolling waveform). The UI is web/index.html, shown through JUCE's WebView
# like SKATOM (WebView2 on Windows, WebKit on Mac).
project(SKAZILAYZER VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Pass -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE to use a local JUCE checkout
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

set(FORMATS_LIST VST3 Standalone)
if(APPLE)
    list(APPEND FORMATS_LIST AU)
endif()

# Same codes as the SKAZILAYZER installed by hand before it joined the bundle, so existing projects still find it
juce_add_plugin(Skazilayzer
    COMPANY_NAME             "SKAZI"
    BUNDLE_ID                com.asherswissa.skazilayzer
    PLUGIN_MANUFACTURER_CODE Ashs
    PLUGIN_CODE              Skzl
    FORMATS                  ${FORMATS_LIST}
    PRODUCT_NAME             "SKAZILAYZER"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    NEEDS_WEBVIEW2           TRUE
    COPY_PLUGIN_AFTER_BUILD  FALSE
    VST3_CATEGORIES          Fx Analyzer
    AU_MAIN_TYPE             kAudioUnitType_Effect)

juce_add_binary_data(SkazilayzerWeb SOURCES web/index.html)

target_sources(Skazilayzer PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(Skazilayzer PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Skazilayzer
    PRIVATE
        SkazilayzerWeb
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

option(SKAZILAYZER_TESTS "Build the offline loudness test" OFF)
if(SKAZILAYZER_TESTS)
    # Checks Source/Loudness.h against EBU Tech 3341 / 3342 reference signals:  SkazilayzerTest
    juce_add_console_app(SkazilayzerTest PRODUCT_NAME "SkazilayzerTest")
    target_sources(SkazilayzerTest PRIVATE tools/SkazilayzerTest.cpp)
    target_include_directories(SkazilayzerTest PRIVATE Source)
    target_compile_definitions(SkazilayzerTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(SkazilayzerTest PRIVATE juce::juce_core juce::juce_recommended_config_flags)
endif()
