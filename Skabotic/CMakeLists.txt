cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary (Apple Silicon + Intel)")

# SKABOTIC by Asher Swissa: robot voice mutator (vocoder, talk box, MIDI poly/mono, 8-bit, metal, sci-fi FX).
project(SKABOTIC VERSION 1.2.0)

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

# Same codes as the version installed by hand, so saved projects still load it
juce_add_plugin(Skabotic
    COMPANY_NAME             "SKAZI"
    BUNDLE_ID                com.asherswissa.skabotic
    PLUGIN_MANUFACTURER_CODE Ashs
    PLUGIN_CODE              Skbt
    FORMATS                  ${FORMATS_LIST}
    PRODUCT_NAME             "SKABOTIC"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD  FALSE
    VST3_CATEGORIES          Fx
    AU_MAIN_TYPE             kAudioUnitType_MusicEffect)

set(SKABOTIC_SOURCES Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_sources(Skabotic PRIVATE ${SKABOTIC_SOURCES})

juce_add_binary_data(SkaboticAssets SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../Resources/skazi_logo.png")

target_compile_definitions(Skabotic PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Skabotic
    PRIVATE
        SkaboticAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

option(SKABOTIC_TESTS "Build the offline test tool" OFF)
if(SKABOTIC_TESTS)
    # Runs every preset on a synthetic voice, checks MIDI/auto-tune/width, writes WAVs and an editor screenshot
    juce_add_console_app(SkaboticTest PRODUCT_NAME "SkaboticTest")
    target_sources(SkaboticTest PRIVATE Tests/SkaboticTest.cpp ${SKABOTIC_SOURCES})
    target_compile_definitions(SkaboticTest PRIVATE JucePlugin_Name="SKABOTIC" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(SkaboticTest PRIVATE SkaboticAssets juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
