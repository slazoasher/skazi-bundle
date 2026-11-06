cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary (Apple Silicon + Intel)")

# S-QUALIZER by Asher Swissa: 6-band console EQ (low cut, 4 dynamic M/S bands, high cut) with a passive tube EQ.
project(SQUALIZER VERSION 1.0.0)

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

# The codes are the ones S-QUALIZER used in its local test builds, so those projects still find it
juce_add_plugin(Squalizer
    COMPANY_NAME             "SKAZI"
    BUNDLE_ID                com.asherswissa.squalizer
    PLUGIN_MANUFACTURER_CODE Ashs
    PLUGIN_CODE              Sqlz
    FORMATS                  ${FORMATS_LIST}
    PRODUCT_NAME             "S-QUALIZER"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD  FALSE
    VST3_CATEGORIES          Fx EQ
    AU_MAIN_TYPE             kAudioUnitType_Effect)

set(SQUALIZER_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/EqDisplay.cpp
    Source/BandStrip.cpp
    Source/Widgets.cpp)

target_sources(Squalizer PRIVATE ${SQUALIZER_SOURCES})

juce_add_binary_data(SqualizerAssets SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../Resources/skazi_logo.png")

target_compile_definitions(Squalizer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Squalizer
    PRIVATE
        SqualizerAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

option(SQUALIZER_TESTS "Build the offline test app" OFF)
if(SQUALIZER_TESTS)
    # Responses per phase mode, M/S, dynamics, sidechain, monitor, A/B and undo, a random stress run and every preset
    #   SqTest                               audio tests
    #   SqTest shot <out.png> [preset] [solo] editor screenshot after playing drums through it
    juce_add_console_app(SqTest PRODUCT_NAME "SqTest")
    target_sources(SqTest PRIVATE Tests/SqTest.cpp ${SQUALIZER_SOURCES})
    target_compile_definitions(SqTest PRIVATE JucePlugin_Name="S-QUALIZER" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(SqTest PRIVATE SqualizerAssets juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
