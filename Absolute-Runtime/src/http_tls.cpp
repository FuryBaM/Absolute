#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <utility>

#include "scheduler_io.h"

// Allocated behind a reference-counted header, like every other string the
// language hands out; see Absolute-Runtime/src/string.cpp. The declaration
// belongs to every target: receive copies the body into that storage on
// Windows as well as on the POSIX path.
extern "C" char* absolute_string_alloc(std::size_t bytes);

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#else
#include <dlfcn.h>
#endif
