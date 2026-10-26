cmake_minimum_required(VERSION 3.22)
project(SKAZI_BUNDLE_SETUP VERSION 1.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Pass -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE to use a local JUCE checkout
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# The plugins are not compiled in: build_bundle.ps1 appends them to the finished exe as a zip.
juce_add_gui_app(SkaziBundleSetup
    PRODUCT_NAME "SKAZI Bundle Setup"
    COMPANY_NAME "SKAZI"
    VERSION      1.1)
target_sources(SkaziBundleSetup PRIVATE SetupMain.cpp)
target_compile_definitions(SkaziBundleSetup PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

set(LOGO "${CMAKE_CURRENT_SOURCE_DIR}/../Resources/skazi_logo.png")
if(EXISTS "${LOGO}")
    juce_add_binary_data(SkaziBundleData SOURCES "${LOGO}")
    target_link_libraries(SkaziBundleSetup PRIVATE SkaziBundleData)
    target_compile_definitions(SkaziBundleSetup PRIVATE SHAK_HAS_LOGO=1)
endif()

target_link_libraries(SkaziBundleSetup PRIVATE juce::juce_gui_extra
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
target_link_options(SkaziBundleSetup PRIVATE "/MANIFESTUAC:level='requireAdministrator'")
