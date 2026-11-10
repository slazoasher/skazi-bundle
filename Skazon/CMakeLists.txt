cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary (Apple Silicon + Intel)")

# SKAZON by Asher Swissa: hybrid wavetable + granular synth.
project(SKAZON VERSION 2.0.0)

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

# The codes are the ones SKAZON's own installer used, so existing projects still find it
juce_add_plugin(Skazon
    COMPANY_NAME             "SKAZI"
    BUNDLE_ID                com.asherswissa.skazon
    PLUGIN_MANUFACTURER_CODE Ashs
    PLUGIN_CODE              Skzn
    FORMATS                  ${FORMATS_LIST}
    PRODUCT_NAME             "SKAZON"
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    COPY_PLUGIN_AFTER_BUILD  FALSE
    VST3_CATEGORIES          Instrument Synth
    AU_MAIN_TYPE             kAudioUnitType_MusicDevice)

set(SKAZON_SOURCES
    Source/Engine.cpp
    Source/FxRack.cpp
    Source/SoundMatch.cpp
    Source/NoteTrack.cpp
    Source/Presets.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Widgets.cpp)
set(SKAZON_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_DISPLAY_SPLASH_SCREEN=0)

target_sources(Skazon PRIVATE ${SKAZON_SOURCES})

juce_add_binary_data(SkazonAssets SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../Resources/skazi_logo.png")

target_compile_definitions(Skazon PUBLIC ${SKAZON_DEFS} JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Skazon
    PRIVATE
        SkazonAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

option(SKAZON_TESTS "Build the offline test app" OFF)
if(SKAZON_TESTS)
    # Pitch, matrix, macro links, SOUND MATCH, every preset; also:
    #   SkazonTest shot <out.png> [preset]       editor screenshot
    #   SkazonTest trim Source/PresetTrims.h     re-level the presets after a preset change
    juce_add_console_app(SkazonTest PRODUCT_NAME "SkazonTest")
    target_sources(SkazonTest PRIVATE Tests/SkazonTest.cpp ${SKAZON_SOURCES})
    target_compile_definitions(SkazonTest PRIVATE ${SKAZON_DEFS} JucePlugin_Name="SKAZON" JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(SkazonTest PRIVATE SkazonAssets juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
