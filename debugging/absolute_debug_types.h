// Optional helper types for debugger watch windows.
// Cast Absolute values explicitly, e.g. (AbsoluteManagedHandle)myHandle.
// These are not used by the Absolute compiler; they exist only for native debuggers.

#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AbsoluteArray1D {
    void* data;
    void* owner;
    int64_t length0;
} AbsoluteArray1D;

typedef struct AbsoluteArray2D {
    void* data;
    void* owner;
    int64_t length0;
    int64_t length1;
} AbsoluteArray2D;

typedef struct AbsoluteManagedHandle {
    uint64_t raw;
} AbsoluteManagedHandle;

typedef struct AbsoluteTaskHandle {
    void* ptr;
} AbsoluteTaskHandle;

#ifdef __cplusplus
}
#endif
