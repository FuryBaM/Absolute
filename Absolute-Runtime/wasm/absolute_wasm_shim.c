/*
 * Minimal Absolute host stubs for wasm32-unknown-unknown.
 *
 * Enough for println/assert/format-style Absolute programs that lower to
 * libc-like calls without a full WASI sysroot. I/O is a no-op so modules load
 * with zero imports in Node / browsers; abort traps on failed assert.
 *
 * Not a substitute for a real Absolute-Runtime port (managed heap, tasks, FS).
 */

#include <stdarg.h>
#include <stddef.h>

void abort(void) {
    __builtin_trap();
}

int puts(const char* text) {
    (void)text;
    return 0;
}

int printf(const char* format, ...) {
    (void)format;
    return 0;
}

int snprintf(char* buffer, size_t size, const char* format, ...) {
    (void)format;
    if (buffer && size > 0)
        buffer[0] = '\0';
    return 0;
}

int sprintf(char* buffer, const char* format, ...) {
    (void)format;
    if (buffer)
        buffer[0] = '\0';
    return 0;
}

/* Common C helpers some Absolute/runtime paths may reference. */
void* memcpy(void* dest, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    while (n--)
        *d++ = *s++;
    return dest;
}

void* memmove(void* dest, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dest;
}

void* memset(void* dest, int value, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    while (n--)
        *d++ = (unsigned char)value;
    return dest;
}

int memcmp(const void* a, const void* b, size_t n) {
    const unsigned char* x = (const unsigned char*)a;
    const unsigned char* y = (const unsigned char*)b;
    while (n--) {
        if (*x != *y)
            return (int)*x - (int)*y;
        ++x;
        ++y;
    }
    return 0;
}

size_t strlen(const char* text) {
    size_t n = 0;
    if (!text)
        return 0;
    while (text[n])
        ++n;
    return n;
}

/* Weak no-op heap: Absolute managed runtime still needs a real port. */
void* malloc(size_t size) {
    (void)size;
    return NULL;
}

void free(void* pointer) {
    (void)pointer;
}

void* realloc(void* pointer, size_t size) {
    (void)pointer;
    (void)size;
    return NULL;
}

void* calloc(size_t count, size_t size) {
    (void)count;
    (void)size;
    return NULL;
}
