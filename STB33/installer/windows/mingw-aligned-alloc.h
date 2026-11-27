// Forced include for MinGW builds of the VST3 SDK (see mingw-w64.cmake).
// MinGW's C runtime has no aligned_alloc. Only the SDK's data-exchange helper
// calls it, and DW-303 never uses that API; aborting keeps a mismatched
// aligned/free pair from ever happening.
#pragma once
#if defined(__cplusplus) && defined(__MINGW32__)
#include <cstdlib>
namespace std {
inline void* aligned_alloc(size_t, size_t) { std::abort(); }
}
#endif
