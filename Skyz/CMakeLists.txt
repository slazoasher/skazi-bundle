cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary (Apple Silicon + Intel)")

# SKY Z by Asher Swissa: additive "star field" synth. The UI is web/index.html, shown in the plugin through
# JUCE's WebView (WebView2 on Windows, WebKit on Mac). Windows builds need the Microsoft.Web.WebView2 NuGet
# package (build_bundle.ps1 installs it when it is missing).
project(SKYZ VERSION 1.0.0)

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

# Same codes as the SKY Z installed by hand before it joined the bundle, so existing projects still find it
juce_add_plugin(Skyz
    COMPANY_NAME             "SKAZI"
    BUNDLE_ID                com.asherswissa.skyz
    PLUGIN_MANUFACTURER_CODE Ashs
    PLUGIN_CODE              Skyz
    FORMATS                  ${FORMATS_LIST}
    PRODUCT_NAME             "SKY Z"
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    NEEDS_WEBVIEW2           TRUE
    COPY_PLUGIN_AFTER_BUILD  FALSE
    VST3_CATEGORIES          Instrument Synth
    AU_MAIN_TYPE             kAudioUnitType_MusicDevice)

juce_add_binary_data(SkyzWeb SOURCES web/index.html web/skazi_logo.png)

set(SKYZ_SOURCES Source/PluginProcessor.cpp Source/Presets.cpp)
target_sources(Skyz PRIVATE ${SKYZ_SOURCES} Source/PluginEditor.cpp)
target_compile_definitions(Skyz PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Skyz
    PRIVATE
        SkyzWeb
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

option(SKYZ_TESTS "Build the offline test app" OFF)
if(SKYZ_TESTS)
    # All 555 presets, diversity, 200 RANDOM sounds, extreme FX/filters, sample rates and blocks, automation,
    # state, arp, header buttons, CPU. Also:
    #   SkyzTest trim Source/PresetTrims.h     re-level the presets after a recipe change
    juce_add_console_app(SkyzTest PRODUCT_NAME "SkyzTest")
    target_sources(SkyzTest PRIVATE Tests/SkyzTest.cpp ${SKYZ_SOURCES})
    target_compile_definitions(SkyzTest PRIVATE SKYZ_NO_EDITOR=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="SKY Z" JucePlugin_IsSynth=1)
    target_link_libraries(SkyzTest PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
