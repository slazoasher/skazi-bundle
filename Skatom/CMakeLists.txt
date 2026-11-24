cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary (Apple Silicon + Intel)")

# SKATOM by Asher Swissa: kinetic reactor filter & multi-FX. The UI is web/index.html, shown in the plugin through
# JUCE's WebView (WebView2 on Windows, WebKit on Mac). Windows builds need the Microsoft.Web.WebView2 NuGet
# package (build_bundle.ps1 installs it when it is missing).
project(SKATOM VERSION 1.0.0)

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

# Same codes as the SKATOM installed by hand before it joined the bundle, so existing projects still find it
juce_add_plugin(Skatom
    COMPANY_NAME             "SKAZI"
    BUNDLE_ID                com.asherswissa.skatom
    PLUGIN_MANUFACTURER_CODE Ashs
    PLUGIN_CODE              Sktm
    FORMATS                  ${FORMATS_LIST}
    PRODUCT_NAME             "SKATOM"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    NEEDS_WEBVIEW2           TRUE
    COPY_PLUGIN_AFTER_BUILD  FALSE
    VST3_CATEGORIES          Fx Filter
    AU_MAIN_TYPE             kAudioUnitType_Effect)

juce_add_binary_data(SkatomWeb SOURCES
    web/index.html web/skatom.css web/state.js web/presets.js web/audio.js web/host.js web/ui.js web/skazi_logo.png)

target_sources(Skatom PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(Skatom PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Skatom
    PRIVATE
        SkatomWeb
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

option(SKATOM_TESTS "Build the offline test host" OFF)
if(SKATOM_TESTS)
    # Loads the built VST3 and checks every stage stays finite and limited:  SkatomTest <path to SKATOM.vst3>
    juce_add_console_app(SkatomTest PRODUCT_NAME "SkatomTest")
    target_sources(SkatomTest PRIVATE tools/SkatomTest.cpp)
    target_compile_definitions(SkatomTest PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(SkatomTest PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_recommended_config_flags)
endif()
