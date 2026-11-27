# CMake toolchain: Windows x64 binaries from Linux with MinGW-w64 (POSIX threads).
# Used by `scripts/bundle-plugins.py --cross-windows`; see docs/plugins.md.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# MinGW lacks std::aligned_alloc, which the VST3 SDK's data-exchange helper names.
set(CMAKE_CXX_FLAGS_INIT "-include ${CMAKE_CURRENT_LIST_DIR}/mingw-aligned-alloc.h")
# Self-contained binaries: no libstdc++/libgcc/winpthread DLLs beside the plug-ins.
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-static")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
# Run build-time tools (moduleinfotool after the VST3 links) under Wine.
find_program(DW303_WINE wine)
if(DW303_WINE)
    set(CMAKE_CROSSCOMPILING_EMULATOR "${DW303_WINE}")
endif()
