cmake_minimum_required(VERSION 3.22)

# Universal binary on macOS (Apple Silicon + Intel). Must be set before project().
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

project(FilterFish VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(FilterFish
    COMPANY_NAME "SKAZI"
    PLUGIN_MANUFACTURER_CODE Aswi
    PLUGIN_CODE Ffsh
    PRODUCT_NAME "The FilterFish"
    FORMATS VST3 AU Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Filter
    AU_MAIN_TYPE kAudioUnitType_Effect)

target_sources(FilterFish PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(FilterFish PRIVATE Source)

target_compile_definitions(FilterFish PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

option(FILTERFISH_TESTS "Build the stress-test host" OFF)
if(FILTERFISH_TESTS)
    juce_add_console_app(FilterFishTest PRODUCT_NAME "FilterFishTest")
    target_sources(FilterFishTest PRIVATE tests/HostTest.cpp)
    target_compile_definitions(FilterFishTest PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(FilterFishTest
        PRIVATE juce::juce_audio_processors juce::juce_gui_extra
        PUBLIC juce::juce_recommended_config_flags)
endif()

# The SKAZI logo (../Resources/skazi_logo.png) shown in the header
set(LOGO "${CMAKE_CURRENT_SOURCE_DIR}/../Resources/skazi_logo.png")
if(EXISTS "${LOGO}")
    juce_add_binary_data(FilterFishLogo SOURCES "${LOGO}")
    target_link_libraries(FilterFish PRIVATE FilterFishLogo)
    target_compile_definitions(FilterFish PRIVATE SKAZI_HAS_LOGO=1)
endif()

if(FILTERFISH_TESTS)
    # Renders the editor to a PNG: FilterFishShot <out.png> [program]
    juce_add_console_app(FilterFishShot PRODUCT_NAME "FilterFishShot")
    target_sources(FilterFishShot PRIVATE tests/Shot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_include_directories(FilterFishShot PRIVATE Source)
    target_compile_definitions(FilterFishShot PRIVATE JucePlugin_Name="The FilterFish" JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(FilterFishShot PRIVATE juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
    if(TARGET FilterFishLogo)
        target_link_libraries(FilterFishShot PRIVATE FilterFishLogo)
        target_compile_definitions(FilterFishShot PRIVATE SKAZI_HAS_LOGO=1)
    endif()
endif()

target_link_libraries(FilterFish
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
