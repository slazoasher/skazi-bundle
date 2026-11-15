cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary (Apple Silicon + Intel)")

# SKACID by Asher Swissa: corrosive acid bassline machine (303-style voice, evolving sequencer, distortion rack).
project(SKACID VERSION 1.2.0)

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

juce_add_plugin(Skacid
    COMPANY_NAME             "SKAZI"
    PLUGIN_MANUFACTURER_CODE Skzi
    PLUGIN_CODE              Skac
    FORMATS                  ${FORMATS_LIST}
    PRODUCT_NAME             "SKACID"
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD  FALSE
    VST3_CATEGORIES          Instrument Synth
    AU_MAIN_TYPE             kAudioUnitType_MusicDevice)

target_sources(Skacid PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/AcidEngine.cpp
    Source/Presets.cpp)

# Save the SKAZI logo as ../Resources/skazi_logo.png to use it instead of the drawn stand-in
set(LOGO "${CMAKE_CURRENT_SOURCE_DIR}/../Resources/skazi_logo.png")
if(EXISTS "${LOGO}")
    juce_add_binary_data(SkacidData SOURCES "${LOGO}")
    target_link_libraries(Skacid PRIVATE SkacidData)
    target_compile_definitions(Skacid PRIVATE SHAK_HAS_LOGO=1)
endif()

target_compile_definitions(Skacid PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

option(SKACID_TESTS "Build the engine test" OFF)
if(SKACID_TESTS)
    juce_add_console_app(SkacidTest PRODUCT_NAME "SkacidTest")
    target_sources(SkacidTest PRIVATE Tests/SkacidTest.cpp Source/AcidEngine.cpp Source/Presets.cpp)
    target_compile_definitions(SkacidTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(SkacidTest PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_dsp juce::juce_recommended_config_flags)

    # Editor snapshot for checking the skin
    juce_add_console_app(SkacidShot PRODUCT_NAME "SkacidShot")
    target_sources(SkacidShot PRIVATE Tests/SkacidShot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/AcidEngine.cpp Source/Presets.cpp)
    target_compile_definitions(SkacidShot PRIVATE JucePlugin_Name="SKACID" JucePlugin_IsSynth=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    if(TARGET SkacidData)
        target_link_libraries(SkacidShot PRIVATE SkacidData)
        target_compile_definitions(SkacidShot PRIVATE SHAK_HAS_LOGO=1)
    endif()
    target_link_libraries(SkacidShot PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)

    # Loads the built VST3 like a DAW and stress-tests it
    juce_add_console_app(SkacidHostTest PRODUCT_NAME "SkacidHostTest")
    target_sources(SkacidHostTest PRIVATE Tests/HostTest.cpp)
    target_compile_definitions(SkacidHostTest PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(SkacidHostTest PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
endif()

target_link_libraries(Skacid
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
