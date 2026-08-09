/*
 * C probe linked with a selective wasi-libc kit + Absolute WASI runtime.
 * Absolute owns malloc/printf/exit; wasi-libc supplies strtol / optional strtod.
 *
 * Compile with -DWASI_PROBE_NO_STRTOD for the STRTOL-only kit.
 */
#include <stdlib.h>

__attribute__((export_name("wasi_libc_strtol")))
int wasi_libc_strtol(const char* text) {
    if (!text)
        return 0;
    return (int)strtol(text, NULL, 10);
}

#if !defined(WASI_PROBE_NO_STRTOD)
__attribute__((export_name("wasi_libc_strtod")))
double wasi_libc_strtod(const char* text) {
    if (!text)
        return 0.0;
    return strtod(text, NULL);
}
#endif

__attribute__((export_name("wasi_libc_probe_version")))
int wasi_libc_probe_version(void) {
#if defined(WASI_PROBE_NO_STRTOD)
    return 2;
#else
    return 3;
#endif
}
