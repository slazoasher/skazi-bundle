cmake_minimum_required(VERSION 3.25)
# The version and host identities are defined here once and flow into every bundle.
project(DW303 VERSION 0.1.1 LANGUAGES C CXX)
# macOS: VST3, AU, AAX, LV2. Windows: VST3. Linux: VST3, LV2.
if(APPLE)
    enable_language(OBJCXX)
elseif(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "DW-303 native adapters build on macOS, Windows and Linux")
endif()
math(EXPR DW303_VERSION_INT "(${PROJECT_VERSION_MAJOR} << 16) | (${PROJECT_VERSION_MINOR} << 8) | ${PROJECT_VERSION_PATCH}")
set(DW303_NAME "STB33")
set(DW303_COMPANY "SKAZI")
set(DW303_BUNDLE_ID "com.asherswissa.stb33")
set(DW303_MANUFACTURER "AsSw")
set(DW303_PRODUCT "ST33")
set(DW303_LV2_URI "https://asherswissa.com/plugins/stb33")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS")

# External SDKs are supplied, never vendored. See docs/plugins.md.
set(DW303_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../target/sdks" CACHE PATH "Directory with vst3/, AudioUnitSDK/, AAX/ and lv2/")
set(VST3_SDK "${DW303_SDK_ROOT}/vst3" CACHE PATH "Steinberg VST3 SDK")
set(AU_SDK "${DW303_SDK_ROOT}/AudioUnitSDK" CACHE PATH "Apple AudioUnitSDK")
set(AAX_SDK "${DW303_SDK_ROOT}/AAX" CACHE PATH "Avid AAX SDK")
set(LV2_SDK "${DW303_SDK_ROOT}/lv2" CACHE PATH "LV2 headers")
if(MSVC)
    set(DW303_RUST_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/../target/release/dw303_native.lib")
else()
    set(DW303_RUST_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/../target/release/libdw303_native.a")
endif()
set(DW303_RUST_LIBRARY "${DW303_RUST_DEFAULT}" CACHE FILEPATH "Rust static library (universal for universal builds)")
# System libraries the Rust static library needs, from
# `cargo rustc -p dw303-native --release --crate-type staticlib -- --print native-static-libs`.
set(DW303_RUST_LINK_LIBS "" CACHE STRING "Native libraries for the Rust static library (Windows/Linux)")
set(DW303_WEBVIEW2_LOADER "${DW303_SDK_ROOT}/WebView2/build/native/x64/WebView2Loader.dll" CACHE FILEPATH "WebView2Loader.dll shipped beside Windows binaries")
set(DW303_REQUIRED "${VST3_SDK}/CMakeLists.txt" "${DW303_RUST_LIBRARY}")
if(APPLE)
    list(APPEND DW303_REQUIRED "${AU_SDK}/include/AudioUnitSDK/AudioUnitSDK.h")
elseif(WIN32)
    list(APPEND DW303_REQUIRED "${DW303_WEBVIEW2_LOADER}")
endif()
foreach(required ${DW303_REQUIRED})
    if(NOT EXISTS "${required}")
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()

configure_file(identity.h.in "${CMAKE_CURRENT_BINARY_DIR}/identity.h" @ONLY)
if(APPLE)
    foreach(kind vst3 au aax)
        configure_file(${kind}-info.plist.in "${CMAKE_CURRENT_BINARY_DIR}/${kind}-info.plist" @ONLY)
    endforeach()
    # Bundle type/creator records; PACE wraptool requires them to treat AAX as a package.
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/PkgInfo-BNDL" "BNDL????")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/PkgInfo-AAX" "TDMwPTul")
endif()

add_library(dw303-rust STATIC IMPORTED)
set_target_properties(dw303-rust PROPERTIES IMPORTED_LOCATION "${DW303_RUST_LIBRARY}")
target_include_directories(dw303-rust INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/../crates/dw303-native/include" "${CMAKE_CURRENT_BINARY_DIR}")
if(APPLE)
    target_link_libraries(dw303-rust INTERFACE "-framework Cocoa" "-framework WebKit" "-framework CoreFoundation" "-framework Security" iconv)
else()
    if(NOT DW303_RUST_LINK_LIBS)
        message(FATAL_ERROR "Set DW303_RUST_LINK_LIBS (scripts/bundle-plugins.py does this)")
    endif()
    separate_arguments(DW303_RUST_LINK_LIST NATIVE_COMMAND "${DW303_RUST_LINK_LIBS}")
    # MSVC prints its CRT as /defaultlib:msvcrt; CMake already links the CRT.
    list(FILTER DW303_RUST_LINK_LIST EXCLUDE REGEX "^/defaultlib:")
    target_link_libraries(dw303-rust INTERFACE ${DW303_RUST_LINK_LIST})
endif()
set(DW303_NOTICES "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE")

function(dw303_finish target kind)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "${DW303_NOTICES}" "$<TARGET_BUNDLE_DIR:${target}>/Contents/Resources/LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/PkgInfo-${kind}" "$<TARGET_BUNDLE_DIR:${target}>/Contents/PkgInfo")
endfunction()

# VST3 ---------------------------------------------------------------------
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_AUV2_BUILDS OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_AAX_BUILDS OFF CACHE BOOL "" FORCE)
if(WIN32)  # also native Ninja (single-config)
    # The SDK's Windows bundle steps assume a Windows host and a multi-config
    # generator; scripts/bundle-plugins.py --cross-windows assembles the bundle.
    set(SMTG_CREATE_BUNDLE_FOR_WINDOWS OFF CACHE BOOL "" FORCE)
    set(SMTG_CREATE_MODULE_INFO OFF CACHE BOOL "" FORCE)
endif()
add_subdirectory("${VST3_SDK}" "${CMAKE_BINARY_DIR}/vst3" EXCLUDE_FROM_ALL)
if(MINGW)
    # The SDK's command-line tools enter through wmain.
    foreach(tool validator moduleinfotool)
        if(TARGET ${tool})
            target_link_options(${tool} PRIVATE -municode)
        endif()
    endforeach()
endif()
if(APPLE)
    set(DW303_VST3_MAIN "${VST3_SDK}/public.sdk/source/main/macmain.cpp")
elseif(WIN32)
    set(DW303_VST3_MAIN "${VST3_SDK}/public.sdk/source/main/dllmain.cpp")
else()
    set(DW303_VST3_MAIN "${VST3_SDK}/public.sdk/source/main/linuxmain.cpp")
endif()
smtg_add_vst3plugin(DW303VST3 PACKAGE_NAME "${DW303_NAME}" SOURCES_LIST vst3.cpp "${DW303_VST3_MAIN}"
    "${VST3_SDK}/public.sdk/source/vst/vstsinglecomponenteffect.cpp")
target_link_libraries(DW303VST3 PRIVATE sdk dw303-rust)
get_target_property(DW303_VST3_PATH DW303VST3 SMTG_PLUGIN_PACKAGE_PATH)
if(APPLE)
    smtg_target_set_bundle(DW303VST3 BUNDLE_IDENTIFIER "${DW303_BUNDLE_ID}.vst3" COMPANY_NAME "${DW303_COMPANY}")
    set_target_properties(DW303VST3 PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/vst3-info.plist")
    add_custom_command(TARGET DW303VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "${DW303_NOTICES}" "${DW303_VST3_PATH}/Contents/Resources/LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/PkgInfo-BNDL" "${DW303_VST3_PATH}/Contents/PkgInfo")
elseif(NOT WIN32 OR SMTG_CREATE_BUNDLE_FOR_WINDOWS)
    add_custom_command(TARGET DW303VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DW303_VST3_PATH}/Contents/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy "${DW303_NOTICES}" "${DW303_VST3_PATH}/Contents/Resources/LICENSE.txt")
endif()
if(WIN32 AND SMTG_CREATE_BUNDLE_FOR_WINDOWS)
    # The WebView2 backend loads its loader from beside the module that contains it.
    add_custom_command(TARGET DW303VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "${DW303_WEBVIEW2_LOADER}" "$<TARGET_FILE_DIR:DW303VST3>/WebView2Loader.dll")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Keep RAVET/Rust symbols private so the CLAP and VST3 copies never interpose.
    target_link_options(DW303VST3 PRIVATE "LINKER:--exclude-libs,ALL")
endif()

if(APPLE)
# AUv2 ---------------------------------------------------------------------
file(GLOB AU_SOURCES CONFIGURE_DEPENDS "${AU_SDK}/src/AudioUnitSDK/*.cpp")
add_library(dw303-au-sdk STATIC ${AU_SOURCES})
target_compile_features(dw303-au-sdk PUBLIC cxx_std_23)
target_include_directories(dw303-au-sdk PUBLIC "${AU_SDK}/include")
target_link_libraries(dw303-au-sdk PUBLIC "-framework AudioToolbox" "-framework CoreAudio" "-framework CoreMIDI")
add_library(DW303AU MODULE au.mm)
target_compile_options(DW303AU PRIVATE -fobjc-arc)
target_link_libraries(DW303AU PRIVATE dw303-au-sdk dw303-rust "-framework AudioUnit")
set_target_properties(DW303AU PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION component PREFIX "" OUTPUT_NAME "${DW303_NAME}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/AU" MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/au-info.plist")
dw303_finish(DW303AU BNDL)

endif()

# AAX ----------------------------------------------------------------------
if(APPLE AND EXISTS "${AAX_SDK}/Interfaces/AAX.h")
    file(GLOB AAX_SOURCES CONFIGURE_DEPENDS "${AAX_SDK}/Libs/AAXLibrary/source/*.cpp")
    file(GLOB ACF_SOURCES CONFIGURE_DEPENDS "${AAX_SDK}/Interfaces/ACF/*.cpp")
    add_library(dw303-aax-sdk STATIC ${AAX_SOURCES} ${ACF_SOURCES})
    target_include_directories(dw303-aax-sdk PUBLIC "${AAX_SDK}/Interfaces" "${AAX_SDK}/Interfaces/ACF")
    target_compile_definitions(dw303-aax-sdk PUBLIC MAC_VERSION=1)
    target_compile_options(dw303-aax-sdk PRIVATE -Wno-deprecated-declarations)
    add_library(DW303AAX MODULE aax.cpp)
    target_link_libraries(DW303AAX PRIVATE dw303-aax-sdk dw303-rust)
    set_target_properties(DW303AAX PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION aaxplugin PREFIX "" OUTPUT_NAME "${DW303_NAME}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/AAX" MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/aax-info.plist")
    dw303_finish(DW303AAX AAX)
elseif(APPLE)
    message(STATUS "AAX SDK not found at ${AAX_SDK}; skipping AAX")
endif()

# LV2 ----------------------------------------------------------------------
if(NOT WIN32 AND EXISTS "${LV2_SDK}/include/lv2/core/lv2.h")
    set(DW303_LV2_BUNDLE "${CMAKE_BINARY_DIR}/LV2/dw-303.lv2")
    add_library(DW303LV2 MODULE lv2.cpp)
    target_include_directories(DW303LV2 PRIVATE "${LV2_SDK}/include")
    target_link_libraries(DW303LV2 PRIVATE dw303-rust)
    if(APPLE)
        set(DW303_LV2_SUFFIX ".dylib")
    else()
        set(DW303_LV2_SUFFIX ".so")
        target_link_options(DW303LV2 PRIVATE "LINKER:--exclude-libs,ALL")
    endif()
    set_target_properties(DW303LV2 PROPERTIES PREFIX "" SUFFIX "${DW303_LV2_SUFFIX}" OUTPUT_NAME "dw-303" LIBRARY_OUTPUT_DIRECTORY "${DW303_LV2_BUNDLE}"
        CXX_VISIBILITY_PRESET hidden)
    add_executable(dw303-lv2-ttl lv2-ttl.cpp)
    target_link_libraries(dw303-lv2-ttl PRIVATE ${CMAKE_DL_LIBS})
    add_dependencies(DW303LV2 dw303-lv2-ttl)
    add_custom_command(TARGET DW303LV2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "${DW303_NOTICES}" "${DW303_LV2_BUNDLE}/LICENSE.txt"
        COMMAND $<TARGET_FILE:dw303-lv2-ttl> "$<TARGET_FILE:DW303LV2>")
elseif(NOT WIN32)
    message(STATUS "LV2 headers not found at ${LV2_SDK}; skipping LV2")
endif()

# Checks -------------------------------------------------------------------
if(APPLE)
    add_executable(dw303-au-smoke EXCLUDE_FROM_ALL au-smoke.mm)
    target_compile_options(dw303-au-smoke PRIVATE -fobjc-arc)
    target_include_directories(dw303-au-smoke PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(dw303-au-smoke PRIVATE "-framework Cocoa" "-framework WebKit" "-framework AudioToolbox" "-framework AudioUnit")
endif()
if(TARGET DW303LV2)
    add_executable(dw303-lv2-smoke EXCLUDE_FROM_ALL lv2-smoke.cpp)
    target_include_directories(dw303-lv2-smoke PRIVATE "${LV2_SDK}/include")
    target_link_libraries(dw303-lv2-smoke PRIVATE ${CMAKE_DL_LIBS})
endif()
