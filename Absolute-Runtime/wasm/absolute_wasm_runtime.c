/*
 * Absolute runtime subset for wasm32-unknown-unknown.
 *
 * Provides:
 *   - bump + freelist heap (malloc/free/calloc/realloc)
 *   - managed pointer table (absolute_managed_*)
 *   - exception TLS (absolute_error_*)
 *   - libc helpers used by Absolute codegen (printf family, mem*, strcmp, abort)
 *
 * Console: optional host import env.absolute_log(ptr,len). When the host
 * provides it (Node/browser helpers), println/assert text is visible; when
 * omitted, wasm-ld can still link if the import is satisfied at instantiate.
 *
 * Tasks: sync spawn/await by default (entry runs immediately). When the host
 * reports a task worker pool (Node `taskWorkers`), spawn enqueues the entry +
 * context bytes and await blocks until a worker finishes (isolated wasm heap;
 * primitive/pointer-slot contexts only — see docs/wasm-target.md).
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

size_t strlen(const char* text);
int strncmp(const char* a, const char* b, size_t n);

#if defined(ABSOLUTE_WASM_USE_WASI)
/* WASI preview1 — wasmtime / Node WASI / wasmer without a custom Absolute host. */
typedef struct absolute_wasi_ciovec {
    const void* buf;
    size_t buf_len;
} absolute_wasi_ciovec;

enum {
    ABSOLUTE_WASI_CLOCK_REALTIME = 0,
    ABSOLUTE_WASI_CLOCK_MONOTONIC = 1
};

__attribute__((import_module("wasi_snapshot_preview1"), import_name("fd_write")))
int32_t absolute_wasi_fd_write(int32_t fd, const absolute_wasi_ciovec* iovs,
    size_t iovs_len, size_t* nwritten);

__attribute__((import_module("wasi_snapshot_preview1"), import_name("proc_exit")))
__attribute__((noreturn))
void absolute_wasi_proc_exit(int32_t rval);

__attribute__((import_module("wasi_snapshot_preview1"), import_name("clock_time_get")))
int32_t absolute_wasi_clock_time_get(uint32_t id, uint64_t precision, uint64_t* time);

__attribute__((import_module("wasi_snapshot_preview1"), import_name("random_get")))
int32_t absolute_wasi_random_get(uint8_t* buf, size_t buf_len);

__attribute__((import_module("wasi_snapshot_preview1"), import_name("args_sizes_get")))
int32_t absolute_wasi_args_sizes_get(size_t* argc, size_t* argv_buf_size);

__attribute__((import_module("wasi_snapshot_preview1"), import_name("args_get")))
int32_t absolute_wasi_args_get(uint8_t** argv, uint8_t* argv_buf);

__attribute__((import_module("wasi_snapshot_preview1"), import_name("environ_sizes_get")))
int32_t absolute_wasi_environ_sizes_get(size_t* count, size_t* buf_size);

__attribute__((import_module("wasi_snapshot_preview1"), import_name("environ_get")))
int32_t absolute_wasi_environ_get(uint8_t** environ, uint8_t* environ_buf);

void absolute_host_log(const uint8_t* data, int32_t length) {
    if (!data || length <= 0)
        return;
    absolute_wasi_ciovec iov;
    iov.buf = data;
    iov.buf_len = (size_t)length;
    size_t written = 0;
    (void)absolute_wasi_fd_write(1, &iov, 1, &written);
}

/* Node WASI reactors require an `_initialize` export before imports work. */
void _initialize(void) {}
#else
/* Custom host hook (Node/browser): tools/absolute-wasm-host.js */
__attribute__((import_module("env"), import_name("absolute_log")))
void absolute_host_log(const uint8_t* data, int32_t length);
#endif

/* Optional host HTTP GET: writes response body into out[0..out_cap).
 * Returns bytes written, or -1 on error. Not available under pure WASI. */
#if !defined(ABSOLUTE_WASM_USE_WASI)
__attribute__((import_module("env"), import_name("absolute_http_get")))
int32_t absolute_host_http_get(const char* url, uint8_t* out, int32_t out_cap);
#else
static int32_t absolute_host_http_get(const char* url, uint8_t* out, int32_t out_cap) {
    (void)url;
    (void)out;
    (void)out_cap;
    return -1;
}
#endif

static void host_log_cstr(const char* text) {
    if (!text)
        return;
    size_t n = 0;
    while (text[n])
        ++n;
    absolute_host_log((const uint8_t*)text, (int32_t)n);
}

/* Absolute-callable HTTP helper (host-backed). */
int32_t absolute_http_get(const char* url, uint8_t* out, int32_t out_cap) {
    if (!url || !out || out_cap <= 0)
        return -1;
    return absolute_host_http_get(url, out, out_cap);
}

/* ---------- heap ---------- */

enum { ABSOLUTE_WASM_HEAP_ALIGN = 16 };

typedef struct HeapBlock {
    size_t size;
    int free;
    struct HeapBlock* next;
} HeapBlock;

static unsigned char g_static_heap[2 * 1024 * 1024];

static size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

#if defined(ABSOLUTE_WASM_SHARED)
#include <stdatomic.h>
/*
 * Heap control lives in linear memory so every module instance that shares
 * env.memory sees the same break/free-list (wasm globals are per-instance).
 */
typedef struct SharedHeapCtrl {
    atomic_int lock;
    atomic_int ready; /* 0 = uninit, 1 = ready */
    uint32_t break_off; /* offset from g_static_heap to next free byte */
    uint32_t free_off; /* 0 = empty; else offset of HeapBlock head */
    uint32_t pad;
} SharedHeapCtrl;

enum { ABSOLUTE_HEAP_CTRL_SIZE = 64 };

static SharedHeapCtrl* heap_ctrl(void) {
    return (SharedHeapCtrl*)(void*)g_static_heap;
}

static unsigned char* heap_arena_start(void) {
    return g_static_heap + ABSOLUTE_HEAP_CTRL_SIZE;
}

static unsigned char* heap_arena_end(void) {
    return g_static_heap + sizeof(g_static_heap);
}

static void heap_lock(void) {
    SharedHeapCtrl* ctrl = heap_ctrl();
    int expected = 0;
    while (!atomic_compare_exchange_weak_explicit(
            &ctrl->lock, &expected, 1,
            memory_order_acquire, memory_order_relaxed)) {
        expected = 0;
    }
}

static void heap_unlock(void) {
    atomic_store_explicit(&heap_ctrl()->lock, 0, memory_order_release);
}

static void heap_init(void) {
    SharedHeapCtrl* ctrl = heap_ctrl();
    if (atomic_load_explicit(&ctrl->ready, memory_order_acquire) == 1)
        return;
    heap_lock();
    if (atomic_load_explicit(&ctrl->ready, memory_order_relaxed) == 0) {
        ctrl->break_off = (uint32_t)ABSOLUTE_HEAP_CTRL_SIZE;
        ctrl->free_off = 0;
        atomic_store_explicit(&ctrl->ready, 1, memory_order_release);
    }
    heap_unlock();
}

static HeapBlock* heap_block_from_off(uint32_t off) {
    if (off == 0)
        return NULL;
    return (HeapBlock*)(void*)(g_static_heap + off);
}

static uint32_t heap_off_from_ptr(void* pointer) {
    if (!pointer)
        return 0;
    return (uint32_t)((unsigned char*)pointer - g_static_heap);
}

static void* heap_alloc(size_t size) {
    heap_init();
    if (size == 0)
        size = 1;
    size = align_up(size, ABSOLUTE_WASM_HEAP_ALIGN);
    heap_lock();
    SharedHeapCtrl* ctrl = heap_ctrl();

    uint32_t prev_off = 0;
    uint32_t cur_off = ctrl->free_off;
    while (cur_off) {
        HeapBlock* block = heap_block_from_off(cur_off);
        uint32_t next_off = heap_off_from_ptr(block->next);
        if (block->free && block->size >= size) {
            block->free = 0;
            if (prev_off == 0)
                ctrl->free_off = next_off;
            else
                heap_block_from_off(prev_off)->next = block->next;
            void* result = (unsigned char*)block + sizeof(HeapBlock);
            heap_unlock();
            return result;
        }
        prev_off = cur_off;
        cur_off = next_off;
    }

    size_t total = sizeof(HeapBlock) + size;
    total = align_up(total, ABSOLUTE_WASM_HEAP_ALIGN);
    if ((size_t)(heap_arena_end() - (g_static_heap + ctrl->break_off)) < total) {
        heap_unlock();
        return NULL;
    }
    HeapBlock* block = (HeapBlock*)(void*)(g_static_heap + ctrl->break_off);
    ctrl->break_off += (uint32_t)total;
    block->size = size;
    block->free = 0;
    block->next = NULL;
    void* result = (unsigned char*)block + sizeof(HeapBlock);
    heap_unlock();
    return result;
}

void free(void* pointer) {
    if (!pointer)
        return;
    heap_init();
    heap_lock();
    SharedHeapCtrl* ctrl = heap_ctrl();
    HeapBlock* block = (HeapBlock*)((unsigned char*)pointer - sizeof(HeapBlock));
    block->free = 1;
    block->next = heap_block_from_off(ctrl->free_off);
    ctrl->free_off = heap_off_from_ptr(block);
    heap_unlock();
}

#else /* !ABSOLUTE_WASM_SHARED */

static unsigned char* g_heap_start;
static unsigned char* g_heap_end;
static unsigned char* g_heap_break;
static HeapBlock* g_free_list;
static int g_heap_ready;

static void heap_lock(void) {}
static void heap_unlock(void) {}

static void heap_init(void) {
    if (g_heap_ready)
        return;
    g_heap_start = g_static_heap;
    g_heap_end = g_static_heap + sizeof(g_static_heap);
    g_heap_break = g_heap_start;
    g_free_list = NULL;
    g_heap_ready = 1;
}

static void* heap_alloc(size_t size) {
    heap_init();
    if (size == 0)
        size = 1;
    size = align_up(size, ABSOLUTE_WASM_HEAP_ALIGN);

    HeapBlock** link = &g_free_list;
    while (*link) {
        HeapBlock* block = *link;
        if (block->free && block->size >= size) {
            block->free = 0;
            *link = block->next;
            return (unsigned char*)block + sizeof(HeapBlock);
        }
        link = &block->next;
    }

    size_t total = sizeof(HeapBlock) + size;
    total = align_up(total, ABSOLUTE_WASM_HEAP_ALIGN);
    if ((size_t)(g_heap_end - g_heap_break) < total)
        return NULL;
    HeapBlock* block = (HeapBlock*)g_heap_break;
    g_heap_break += total;
    block->size = size;
    block->free = 0;
    block->next = NULL;
    return (unsigned char*)block + sizeof(HeapBlock);
}

void free(void* pointer) {
    if (!pointer)
        return;
    HeapBlock* block = (HeapBlock*)((unsigned char*)pointer - sizeof(HeapBlock));
    block->free = 1;
    block->next = g_free_list;
    g_free_list = block;
}

#endif /* ABSOLUTE_WASM_SHARED */

/* Absolute LLVM always declares malloc(i64) even on wasm32; match that ABI. */
void* malloc(uint64_t size) {
    return heap_alloc((size_t)size);
}

void* calloc(uint64_t count, uint64_t size) {
    uint64_t total = count * size;
    void* memory = heap_alloc((size_t)(total == 0 ? 1 : total));
    if (!memory)
        return NULL;
    unsigned char* bytes = (unsigned char*)memory;
    for (size_t i = 0; i < (size_t)total; ++i)
        bytes[i] = 0;
    return memory;
}

void* realloc(void* pointer, uint64_t size) {
    if (!pointer)
        return malloc(size);
    if (size == 0) {
        free(pointer);
        return NULL;
    }
    HeapBlock* block = (HeapBlock*)((unsigned char*)pointer - sizeof(HeapBlock));
    if (block->size >= (size_t)size)
        return pointer;
    size_t old_size = block->size;
    void* next = heap_alloc((size_t)size);
    if (!next)
        return NULL;
    unsigned char* dst = (unsigned char*)next;
    unsigned char* src = (unsigned char*)pointer;
    for (size_t i = 0; i < old_size; ++i)
        dst[i] = src[i];
    free(pointer);
    return next;
}

#if defined(ABSOLUTE_WASM_SHARED)
int32_t absolute_wasm_shared_memory_enabled(void) {
    return 1;
}
#else
int32_t absolute_wasm_shared_memory_enabled(void) {
    return 0;
}
#endif

/* ---------- libc helpers ---------- */

void abort(void) {
    __builtin_trap();
}

void exit(int code) {
    (void)code;
    __builtin_trap();
}

int puts(const char* text) {
    host_log_cstr(text ? text : "");
    absolute_host_log((const uint8_t*)"\n", 1);
    return 0;
}

// Nothing generated calls putchar or fputc by name. LLVM rewrites them out of
// printf: `printf("%c", value)` becomes putchar, and printing a char is
// ordinary, so without these the module failed to link for wasm -- with a
// message about a symbol the source never mentions -- while the identical
// program built natively.
int putchar(int value) {
    const uint8_t byte = (uint8_t)value;
    absolute_host_log(&byte, 1);
    return value;
}

int fputc(int value, void* stream) {
    (void)stream;
    return putchar(value);
}

int putc(int value, void* stream) {
    (void)stream;
    return putchar(value);
}

// The host gives a module a console to write to and no standard input to read
// from, so a read reports end-of-input rather than failing to link. Programs
// that read interactively already have to handle EOF; on wasm they take that
// path immediately. docs/wasm-target.md says so.
/* Nothing here buffers: the console writes straight through the host import.
   Defined because generated code calls it before abort(). */
int fflush(void* stream) {
    (void)stream;
    return 0;
}

int getchar(void) {
    return -1;
}

int fgetc(void* stream) {
    (void)stream;
    return -1;
}

int getc(void* stream) {
    (void)stream;
    return -1;
}

static void format_putc(char* out, size_t cap, size_t* offset, char value) {
    if (out && *offset + 1 < cap)
        out[*offset] = value;
    ++*offset;
}

static void format_unsigned(
    char* out,
    size_t cap,
    size_t* offset,
    uint64_t value,
    unsigned base,
    int width,
    char padding
) {
    static const char digits[] = "0123456789abcdef";
    char scratch[32];
    int count = 0;
    do {
        scratch[count++] = digits[value % base];
        value /= base;
    } while (value && count < (int)sizeof(scratch));
    while (width-- > count)
        format_putc(out, cap, offset, padding);
    while (count--)
        format_putc(out, cap, offset, scratch[count]);
}

/* Small, freestanding formatter used by the runtime and std implementations. */
static int format_into(char* out, size_t cap, const char* format, va_list args) {
    size_t o = 0;
    if (!format)
        format = "";
    for (size_t i = 0; format[i]; ++i) {
        if (format[i] != '%' || !format[i + 1]) {
            if (out && o + 1 < cap)
                out[o] = format[i];
            ++o;
            continue;
        }
        ++i;
        if (format[i] == '%') {
            format_putc(out, cap, &o, '%');
            continue;
        }
        char padding = ' ';
        int width = 0;
        if (format[i] == '0') {
            padding = '0';
            ++i;
        }
        while (format[i] >= '0' && format[i] <= '9') {
            width = width * 10 + format[i] - '0';
            ++i;
        }
        int long_count = 0;
        while (format[i] == 'l') {
            ++long_count;
            ++i;
        }
        if (format[i] == 's') {
            const char* s = va_arg(args, const char*);
            if (!s)
                s = "(null)";
            int length = (int)strlen(s);
            while (width-- > length)
                format_putc(out, cap, &o, padding);
            for (size_t k = 0; s[k]; ++k) {
                format_putc(out, cap, &o, s[k]);
            }
            continue;
        }
        if (format[i] == 'c') {
            format_putc(out, cap, &o, (char)va_arg(args, int));
            continue;
        }
        if (format[i] == 'd' || format[i] == 'i') {
            int64_t value = long_count ? va_arg(args, int64_t) : va_arg(args, int);
            uint64_t magnitude;
            if (value < 0) {
                format_putc(out, cap, &o, '-');
                if (width > 0)
                    --width;
                magnitude = (uint64_t)(-(value + 1)) + 1u;
            } else {
                magnitude = (uint64_t)value;
            }
            format_unsigned(out, cap, &o, magnitude, 10, width, padding);
            continue;
        }
        if (format[i] == 'u' || format[i] == 'x') {
            uint64_t value = long_count ? va_arg(args, uint64_t) : va_arg(args, unsigned);
            format_unsigned(out, cap, &o, value, format[i] == 'x' ? 16 : 10, width, padding);
            continue;
        }
        /* Keep unsupported directives visible instead of silently deleting them. */
        format_putc(out, cap, &o, '%');
        format_putc(out, cap, &o, format[i]);
    }
    if (out && cap > 0)
        out[o < cap ? o : cap - 1] = '\0';
    return (int)o;
}

int printf(const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    int n = format_into(buffer, sizeof(buffer), format, args);
    va_end(args);
    absolute_host_log((const uint8_t*)buffer, n > 0 ? n : 0);
    return n;
}

/* Absolute LLVM models buffer sizes as i64 on every target, including wasm32. */
int snprintf(char* buffer, uint64_t size, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int n = format_into(buffer, (size_t)size, format, args);
    va_end(args);
    return n;
}

int sprintf(char* buffer, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int n = format_into(buffer, (size_t)-1 / 4, format, args);
    va_end(args);
    return n;
}

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

int strcmp(const char* a, const char* b) {
    if (!a || !b)
        return (a == b) ? 0 : (a ? 1 : -1);
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char* a, const char* b, size_t n) {
    if (n == 0) return 0;
    if (!a) a = "";
    if (!b) b = "";
    while (n-- && *a && *a == *b) {
        if (n == 0) return 0;
        ++a; ++b;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/* ---------- managed pointers ---------- */

/* Layout must match CodeGen absolute.Slot: { ptr, i32 gen, i32 pad, i64 type }. */
typedef struct ManagedSlot {
    void* pointer;
    uint32_t generation;
    uint32_t padding;
    uint64_t type;
} ManagedSlot;

enum { ABSOLUTE_MANAGED_CAPACITY = 4096 };

static ManagedSlot g_slots[ABSOLUTE_MANAGED_CAPACITY];
static uint32_t g_slot_count;
static uint32_t g_free_slots[ABSOLUTE_MANAGED_CAPACITY];
static uint32_t g_free_count;

/* Exported for Absolute fast-path codegen. */
ManagedSlot* absolute_managed_slots_data = g_slots;
uint32_t absolute_managed_slots_size = 0;

static uint64_t make_handle(uint32_t id, uint32_t generation) {
    return ((uint64_t)generation << 32) | id;
}

static uint32_t handle_id(uint64_t handle) {
    return (uint32_t)handle;
}

static uint32_t handle_generation(uint64_t handle) {
    return (uint32_t)(handle >> 32);
}

static ManagedSlot* find_slot(uint64_t handle) {
    if (handle == 0)
        return NULL;
    uint32_t id = handle_id(handle);
    if (id >= g_slot_count)
        return NULL;
    ManagedSlot* slot = &g_slots[id];
    return slot->generation == handle_generation(handle) ? slot : NULL;
}

uint64_t absolute_managed_create(uint64_t size) {
    size_t bytes = (size_t)(size == 0 ? 1 : size);
    void* allocation = calloc(1, (uint64_t)bytes);
    if (!allocation)
        abort();

    uint32_t id;
    if (g_free_count > 0) {
        id = g_free_slots[--g_free_count];
        g_slots[id].pointer = allocation;
        g_slots[id].type = 0;
    } else {
        if (g_slot_count >= ABSOLUTE_MANAGED_CAPACITY) {
            free(allocation);
            abort();
        }
        id = g_slot_count++;
        g_slots[id].pointer = allocation;
        g_slots[id].generation = 1;
        g_slots[id].type = 0;
        absolute_managed_slots_data = g_slots;
        absolute_managed_slots_size = g_slot_count;
    }
    return make_handle(id, g_slots[id].generation);
}

void* absolute_managed_get(uint64_t handle) {
    ManagedSlot* slot = find_slot(handle);
    return slot ? slot->pointer : NULL;
}

/* Returns 0/1; Absolute LLVM calls treat this as an i1/i8 predicate. */
uint8_t absolute_managed_valid(uint64_t handle) {
    return absolute_managed_get(handle) != NULL ? 1 : 0;
}

void absolute_managed_set_type(uint64_t handle, uint64_t type) {
    ManagedSlot* slot = find_slot(handle);
    if (slot)
        slot->type = type;
}

uint64_t absolute_managed_type(uint64_t handle) {
    ManagedSlot* slot = find_slot(handle);
    return slot ? slot->type : 0;
}

void* absolute_managed_require(uint64_t handle) {
    void* pointer = absolute_managed_get(handle);
    if (!pointer)
        abort();
    return pointer;
}

void absolute_managed_destroy(uint64_t handle) {
    ManagedSlot* slot = find_slot(handle);
    if (!slot || !slot->pointer)
        return;
    uint32_t id = handle_id(handle);
    free(slot->pointer);
    slot->pointer = NULL;
    slot->type = 0;
    ++slot->generation;
    if (slot->generation == 0)
        ++slot->generation;
    if (g_free_count < ABSOLUTE_MANAGED_CAPACITY)
        g_free_slots[g_free_count++] = id;
}

uint64_t absolute_managed_transfer(uint64_t handle) {
    ManagedSlot* slot = find_slot(handle);
    if (!slot || !slot->pointer)
        abort();
    uint32_t id = handle_id(handle);
    ++slot->generation;
    if (slot->generation == 0)
        ++slot->generation;
    return make_handle(id, slot->generation);
}

void absolute_managed_check_leaks(void) {
    for (uint32_t i = 0; i < g_slot_count; ++i) {
        if (g_slots[i].pointer != NULL)
            abort();
    }
}

/* ---------- errors ---------- */

typedef struct ErrorState {
    uint64_t handle;
    uint64_t type;
} ErrorState;

/* Single-threaded wasm: plain static TLS stand-in. */
static ErrorState g_error;

/* Copied rather than aliased: the strings live in module memory that a later
   allocation may reuse, and a report happens after the throw site is gone. */
static char g_error_type_name[128];
static char g_error_message[256];

static void copy_bounded(char* out, size_t capacity, const char* text) {
    size_t index = 0;
    if (!text) {
        out[0] = '\0';
        return;
    }
    while (text[index] && index + 1 < capacity) {
        out[index] = text[index];
        ++index;
    }
    out[index] = '\0';
}

void absolute_error_set_details(const char* typeName, const char* message) {
    copy_bounded(g_error_type_name, sizeof(g_error_type_name), typeName);
    copy_bounded(g_error_message, sizeof(g_error_message), message);
}

const char* absolute_error_type_name(void) {
    return g_error_type_name;
}

const char* absolute_error_message(void) {
    return g_error_message;
}

void absolute_error_set(uint64_t handle, uint64_t type) {
    if (g_error.handle && g_error.handle != handle)
        absolute_managed_destroy(g_error.handle);
    g_error.handle = handle;
    g_error.type = type;
}

uint8_t absolute_error_pending(void) {
    return g_error.handle != 0 ? 1 : 0;
}

uint64_t absolute_error_type(void) {
    return g_error.type;
}

uint64_t absolute_error_take(void) {
    uint64_t handle = g_error.handle;
    g_error.handle = 0;
    g_error.type = 0;
    return handle;
}

void absolute_error_discard(void) {
    if (g_error.handle)
        absolute_managed_destroy(g_error.handle);
    g_error.handle = 0;
    g_error.type = 0;
    g_error_type_name[0] = '\0';
    g_error_message[0] = '\0';
}

void absolute_error_report(void) {
    /* The host gives a module a console, so an unhandled error says what it
       was here too rather than vanishing into the exit code. */
    if (g_error_type_name[0]) {
        host_log_cstr("Unhandled Absolute exception: ");
        host_log_cstr(g_error_type_name);
        if (g_error_message[0]) {
            host_log_cstr(": ");
            host_log_cstr(g_error_message);
        }
        absolute_host_log((const uint8_t*)"\n", 1);
    }
    absolute_error_discard();
}

/* ---------- dynamic load (unavailable on pure wasm) ---------- */

static char g_load_error[96] = "dynamic load is not available on wasm";

int32_t absolute_load_library(const char* path) {
    (void)path;
    return 0;
}

int32_t absolute_library_is_loaded(const char* path) {
    (void)path;
    return 0;
}

const char* absolute_load_error(void) {
    return g_load_error;
}

/* ---------- tasks: sync scheduler or host worker pool ---------- */

/* Max context bytes copied to/from a host worker (i64 slots from codegen). */
enum { ABSOLUTE_WASM_TASK_CONTEXT_MAX = 512 };

int64_t absolute_time_monotonic_nanos(void);

typedef struct WasmTaskControl {
    struct WasmTaskControl* parent;
    int32_t references;
    int32_t cancelled;
    int64_t deadline_nanos;
} WasmTaskControl;

typedef struct WasmTask {
    void* context;
    uint64_t error_handle;
    uint64_t error_type;
    int32_t job_id; /* host pool job slot, or -1 when run synchronously */
    int done;
    int metrics_completed;
    WasmTaskControl* control;
} WasmTask;

static int32_t g_task_core = -1;
static int32_t g_task_priority = 0;
static const char* g_task_role = "";
static WasmTask* g_current_task = NULL;
static int64_t g_metric_started_nanos = 0;
static int64_t g_metric_runnable = 0;
static int64_t g_metric_completed = 0;
static int64_t g_metric_queue_samples = 0;
static int64_t g_metric_worker_busy_nanos = 0;

static int64_t wasm_metric_load(int64_t* value) {
#if defined(ABSOLUTE_WASM_SHARED)
    return __atomic_load_n(value, __ATOMIC_RELAXED);
#else
    return *value;
#endif
}

static void wasm_metric_add(int64_t* value, int64_t amount) {
#if defined(ABSOLUTE_WASM_SHARED)
    __atomic_add_fetch(value, amount, __ATOMIC_RELAXED);
#else
    *value += amount;
#endif
}

static void wasm_metric_start(int64_t now) {
#if defined(ABSOLUTE_WASM_SHARED)
    int64_t expected = 0;
    (void)__atomic_compare_exchange_n(
        &g_metric_started_nanos, &expected, now, 0,
        __ATOMIC_RELAXED, __ATOMIC_RELAXED);
#else
    if (g_metric_started_nanos == 0)
        g_metric_started_nanos = now;
#endif
}

static void wasm_task_metric_complete(
    WasmTask* task, int64_t busy_nanos, int queue_sample) {
    if (!task || task->metrics_completed)
        return;
    task->metrics_completed = 1;
    wasm_metric_add(&g_metric_runnable, -1);
    wasm_metric_add(&g_metric_completed, 1);
    if (queue_sample)
        wasm_metric_add(&g_metric_queue_samples, 1);
    if (busy_nanos > 0)
        wasm_metric_add(&g_metric_worker_busy_nanos, busy_nanos);
}

static void wasm_task_control_retain(WasmTaskControl* control) {
    if (!control) return;
#if defined(ABSOLUTE_WASM_SHARED)
    __atomic_add_fetch(&control->references, 1, __ATOMIC_RELAXED);
#else
    ++control->references;
#endif
}

static void wasm_task_control_release(WasmTaskControl* control) {
    if (!control) return;
    int32_t references = 0;
#if defined(ABSOLUTE_WASM_SHARED)
    references =
        __atomic_sub_fetch(&control->references, 1, __ATOMIC_ACQ_REL);
#else
    references = --control->references;
#endif
    if (references != 0) return;
    WasmTaskControl* parent = control->parent;
    free(control);
    wasm_task_control_release(parent);
}

static WasmTaskControl* wasm_task_control_create(
    WasmTaskControl* parent) {
    WasmTaskControl* control =
        (WasmTaskControl*)heap_alloc(sizeof(WasmTaskControl));
    if (!control) return NULL;
    control->parent = parent;
    control->references = 1;
    control->cancelled = 0;
    control->deadline_nanos = 0;
    wasm_task_control_retain(parent);
    return control;
}

static int64_t wasm_task_effective_deadline(
    WasmTaskControl* control) {
    int64_t effective = 0;
    for (WasmTaskControl* current = control;
         current; current = current->parent) {
#if defined(ABSOLUTE_WASM_SHARED)
        const int64_t candidate = __atomic_load_n(
            &current->deadline_nanos, __ATOMIC_ACQUIRE);
#else
        const int64_t candidate = current->deadline_nanos;
#endif
        if (candidate > 0 &&
            (effective == 0 || candidate < effective))
            effective = candidate;
    }
    return effective;
}

static uint8_t wasm_task_control_cancelled(
    WasmTaskControl* control) {
    for (WasmTaskControl* current = control;
         current; current = current->parent) {
#if defined(ABSOLUTE_WASM_SHARED)
        if (__atomic_load_n(
                &current->cancelled, __ATOMIC_ACQUIRE))
            return 1;
#else
        if (current->cancelled) return 1;
#endif
    }
    const int64_t deadline =
        wasm_task_effective_deadline(control);
    return deadline > 0 &&
        deadline <= absolute_time_monotonic_nanos();
}

#if !defined(ABSOLUTE_WASM_USE_WASI)
__attribute__((import_module("env"), import_name("absolute_task_pool_size")))
int32_t absolute_host_task_pool_size(void);
__attribute__((import_module("env"), import_name("absolute_task_enqueue")))
int32_t absolute_host_task_enqueue(
    int32_t entry, void* context, int32_t context_bytes, int32_t core, int32_t priority);
__attribute__((import_module("env"), import_name("absolute_task_await_job")))
void absolute_host_task_await_job(int32_t job_id, void* context, int32_t context_bytes);
#else
static int32_t absolute_host_task_pool_size(void) { return 0; }
static int32_t absolute_host_task_enqueue(
    int32_t entry, void* context, int32_t context_bytes, int32_t core, int32_t priority) {
    (void)entry;
    (void)context;
    (void)context_bytes;
    (void)core;
    (void)priority;
    return -1;
}
static void absolute_host_task_await_job(int32_t job_id, void* context, int32_t context_bytes) {
    (void)job_id;
    (void)context;
    (void)context_bytes;
}
#endif

void* absolute_task_spawn_config(
    void (*entry)(void*), void* context, int32_t core, int32_t priority, const char* role) {
    if (!entry || !context)
        abort();
    if (core < -1 || priority < -3 || priority > 3)
        abort();

    WasmTask* task = (WasmTask*)heap_alloc(sizeof(WasmTask));
    if (!task)
        abort();
    task->context = context;
    task->error_handle = 0;
    task->error_type = 0;
    task->job_id = -1;
    task->done = 0;
    task->metrics_completed = 0;
    task->control = wasm_task_control_create(
        g_current_task ? g_current_task->control : NULL);
    if (!task->control)
        abort();

    const int32_t prev_core = g_task_core;
    const int32_t prev_priority = g_task_priority;
    const char* prev_role = g_task_role;
    WasmTask* prev_task = g_current_task;
    g_task_core = core;
    g_task_priority = priority;
    g_task_role = role ? role : "";
    g_current_task = task;
    const int64_t metric_started =
        absolute_time_monotonic_nanos();
    wasm_metric_start(metric_started);
    wasm_metric_add(&g_metric_runnable, 1);

    if (absolute_host_task_pool_size() > 0) {
        /* Offload to host worker pool (isolated module instance per worker). */
        const int32_t entry_index = (int32_t)(uintptr_t)entry;
        const int32_t job = absolute_host_task_enqueue(
            entry_index, context, ABSOLUTE_WASM_TASK_CONTEXT_MAX, core, priority);
        if (job < 0)
            abort();
        task->job_id = job;
        /* done remains 0 until await joins the worker. */
    } else {
        /* Run immediately on the calling "thread" (single-threaded wasm). */
        entry(context);
        task->done = 1;

        if (absolute_error_pending()) {
            task->error_type = absolute_error_type();
            task->error_handle = absolute_error_take();
        }
        const int64_t metric_finished =
            absolute_time_monotonic_nanos();
        wasm_task_metric_complete(
            task,
            metric_finished > metric_started
                ? metric_finished - metric_started : 0,
            1);
    }

    g_task_core = prev_core;
    g_task_priority = prev_priority;
    g_task_role = prev_role;
    g_current_task = prev_task;
    return task;
}

void* absolute_task_spawn(void (*entry)(void*), void* context) {
    return absolute_task_spawn_config(entry, context, -1, 0, NULL);
}

int32_t absolute_task_current_core(void) {
    return g_task_core;
}

int32_t absolute_task_current_priority(void) {
    return g_task_priority;
}

const char* absolute_task_current_role(void) {
    return g_task_role ? g_task_role : "";
}

uint8_t absolute_task_current_role_is(const char* role) {
    if (!role)
        return 0;
    const char* current = absolute_task_current_role();
    while (*role && *role == *current) {
        ++role;
        ++current;
    }
    return (*role == 0 && *current == 0) ? 1 : 0;
}

int32_t absolute_scheduler_worker_count(void) {
    const int32_t host_workers = absolute_host_task_pool_size();
    return host_workers > 0 ? host_workers : 1;
}

uint8_t absolute_scheduler_affinity_supported(void) {
    return 0;
}

uint8_t absolute_scheduler_core_available(int32_t core) {
    (void)core;
    return 0;
}

uint8_t absolute_task_current_affinity_applied(void) {
    return 0;
}

enum {
    ABSOLUTE_SCHEDULER_METRIC_RUNNABLE = 0,
    ABSOLUTE_SCHEDULER_METRIC_SUSPENDED = 1,
    ABSOLUTE_SCHEDULER_METRIC_COMPLETED = 2,
    ABSOLUTE_SCHEDULER_METRIC_QUEUE_SAMPLES = 3,
    ABSOLUTE_SCHEDULER_METRIC_QUEUE_LATENCY_TOTAL_NANOS = 4,
    ABSOLUTE_SCHEDULER_METRIC_QUEUE_LATENCY_MAX_NANOS = 5,
    ABSOLUTE_SCHEDULER_METRIC_WORKER_BUSY_NANOS = 6,
    ABSOLUTE_SCHEDULER_METRIC_WORKER_UTILIZATION_PERMILLE = 7,
    ABSOLUTE_SCHEDULER_METRIC_STEALS = 8,
    ABSOLUTE_SCHEDULER_METRIC_WAKE_UPS = 9,
    ABSOLUTE_SCHEDULER_METRIC_BLOCKED_NANOS = 10,
    ABSOLUTE_SCHEDULER_METRIC_STARVATION_EVENTS = 11,
    ABSOLUTE_SCHEDULER_METRIC_COUNT = 12
};

typedef struct WasmSchedulerMetricsSnapshot {
    int64_t values[ABSOLUTE_SCHEDULER_METRIC_COUNT];
} WasmSchedulerMetricsSnapshot;

void* absolute_scheduler_metrics_snapshot(void) {
    WasmSchedulerMetricsSnapshot* snapshot =
        (WasmSchedulerMetricsSnapshot*)heap_alloc(
            sizeof(WasmSchedulerMetricsSnapshot));
    if (!snapshot)
        abort();
    for (int32_t index = 0;
         index < ABSOLUTE_SCHEDULER_METRIC_COUNT; ++index)
        snapshot->values[index] = 0;
    snapshot->values[ABSOLUTE_SCHEDULER_METRIC_RUNNABLE] =
        wasm_metric_load(&g_metric_runnable);
    snapshot->values[ABSOLUTE_SCHEDULER_METRIC_COMPLETED] =
        wasm_metric_load(&g_metric_completed);
    snapshot->values[ABSOLUTE_SCHEDULER_METRIC_QUEUE_SAMPLES] =
        wasm_metric_load(&g_metric_queue_samples);
    const int64_t busy =
        wasm_metric_load(&g_metric_worker_busy_nanos);
    snapshot->values[ABSOLUTE_SCHEDULER_METRIC_WORKER_BUSY_NANOS] =
        busy;
    const int64_t started =
        wasm_metric_load(&g_metric_started_nanos);
    const int64_t now = absolute_time_monotonic_nanos();
    const int64_t elapsed = now > started ? now - started : 0;
    const int64_t workers = absolute_scheduler_worker_count();
    if (elapsed > 0 && workers > 0) {
        int64_t utilization = (int64_t)(
            ((double)busy * 1000.0) /
            ((double)elapsed * (double)workers));
        if (utilization < 0) utilization = 0;
        if (utilization > 1000) utilization = 1000;
        snapshot->values[
            ABSOLUTE_SCHEDULER_METRIC_WORKER_UTILIZATION_PERMILLE] =
            utilization;
    }
    return snapshot;
}

int64_t absolute_scheduler_metrics_value(
    void* snapshot_handle, int32_t metric) {
    if (!snapshot_handle || metric < 0 ||
        metric >= ABSOLUTE_SCHEDULER_METRIC_COUNT)
        return 0;
    WasmSchedulerMetricsSnapshot* snapshot =
        (WasmSchedulerMetricsSnapshot*)snapshot_handle;
    return snapshot->values[metric];
}

void absolute_scheduler_metrics_destroy(void* snapshot_handle) {
    free(snapshot_handle);
}

void* absolute_task_await(void* handle) {
    if (!handle)
        abort();
    WasmTask* task = (WasmTask*)handle;
    if (!task->done) {
        if (task->job_id < 0)
            abort();
        absolute_host_task_await_job(
            task->job_id, task->context, ABSOLUTE_WASM_TASK_CONTEXT_MAX);
        task->done = 1;
        wasm_task_metric_complete(task, 0, 0);
    }
    if (task->error_handle)
        absolute_error_set(task->error_handle, task->error_type);
    void* context = task->context;
    wasm_task_control_release(task->control);
    free(task);
    return context;
}

void absolute_task_destroy(void* handle) {
    if (!handle)
        return;
    WasmTask* task = (WasmTask*)handle;
    if (!task->done) {
        if (task->job_id >= 0) {
            absolute_host_task_await_job(
                task->job_id, task->context, ABSOLUTE_WASM_TASK_CONTEXT_MAX);
            task->done = 1;
            wasm_task_metric_complete(task, 0, 0);
        } else {
            abort();
        }
    }
    if (task->error_handle)
        absolute_managed_destroy(task->error_handle);
    free(task->context);
    wasm_task_control_release(task->control);
    free(task);
}

uint8_t absolute_task_current_cancelled(void) {
    return g_current_task
        ? wasm_task_control_cancelled(g_current_task->control) : 0;
}

void absolute_task_cancel(void* handle) {
    if (!handle)
        return;
    WasmTask* task = (WasmTask*)handle;
#if defined(ABSOLUTE_WASM_SHARED)
    __atomic_store_n(
        &task->control->cancelled, 1, __ATOMIC_RELEASE);
#else
    task->control->cancelled = 1;
#endif
}

uint8_t absolute_task_current_set_deadline_after(
    int32_t milliseconds) {
    if (!g_current_task || milliseconds < 0)
        return 0;
    const int64_t now = absolute_time_monotonic_nanos();
    const int64_t duration =
        (int64_t)milliseconds * 1000000;
    const int64_t maximum = INT64_MAX;
    const int64_t deadline =
        duration > maximum - now ? maximum : now + duration;
    WasmTaskControl* control = g_current_task->control;
#if defined(ABSOLUTE_WASM_SHARED)
    int64_t current = __atomic_load_n(
        &control->deadline_nanos, __ATOMIC_ACQUIRE);
    while ((current == 0 || deadline < current) &&
        !__atomic_compare_exchange_n(
            &control->deadline_nanos, &current, deadline,
            1, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {}
#else
    if (control->deadline_nanos == 0 ||
        deadline < control->deadline_nanos)
        control->deadline_nanos = deadline;
#endif
    return 1;
}

int32_t absolute_task_current_deadline_remaining(void) {
    if (!g_current_task) return -1;
    const int64_t deadline =
        wasm_task_effective_deadline(g_current_task->control);
    if (deadline == 0) return -1;
    const int64_t remaining =
        deadline - absolute_time_monotonic_nanos();
    if (remaining <= 0) return 0;
    const int64_t milliseconds =
        (remaining + 999999) / 1000000;
    return milliseconds > INT32_MAX
        ? INT32_MAX : (int32_t)milliseconds;
}

typedef struct WasmTaskGroup {
    WasmTask** children;
    int32_t count;
    int32_t capacity;
    int closed;
} WasmTaskGroup;

void* absolute_task_group_create(void) {
    WasmTaskGroup* group =
        (WasmTaskGroup*)heap_alloc(sizeof(WasmTaskGroup));
    if (!group)
        return NULL;
    group->children = NULL;
    group->count = 0;
    group->capacity = 0;
    group->closed = 0;
    return group;
}

uint8_t absolute_task_group_add(void* handle, void* child_handle) {
    if (!child_handle)
        return 0;
    WasmTaskGroup* group = (WasmTaskGroup*)handle;
    if (!group || group->closed) {
        absolute_task_cancel(child_handle);
        absolute_task_destroy(child_handle);
        return 0;
    }
    if (group->count == group->capacity) {
        int32_t next_capacity =
            group->capacity > 0 ? group->capacity * 2 : 4;
        WasmTask** children = (WasmTask**)realloc(
            group->children,
            (uint64_t)next_capacity * sizeof(WasmTask*));
        if (!children) {
            absolute_task_cancel(child_handle);
            absolute_task_destroy(child_handle);
            return 0;
        }
        group->children = children;
        group->capacity = next_capacity;
    }
    group->children[group->count++] = (WasmTask*)child_handle;
    return 1;
}

int32_t absolute_task_group_count(void* handle) {
    WasmTaskGroup* group = (WasmTaskGroup*)handle;
    return group ? group->count : 0;
}

void absolute_task_group_cancel(void* handle) {
    WasmTaskGroup* group = (WasmTaskGroup*)handle;
    if (!group)
        return;
    for (int32_t index = 0; index < group->count; ++index)
        absolute_task_cancel(group->children[index]);
}

void absolute_task_group_join(void* handle) {
    WasmTaskGroup* group = (WasmTaskGroup*)handle;
    if (!group)
        return;
    group->closed = 1;
    for (int32_t index = 0; index < group->count; ++index)
        absolute_task_destroy(group->children[index]);
    group->count = 0;
}

void absolute_task_group_cancel_and_join(void* handle) {
    absolute_task_group_cancel(handle);
    absolute_task_group_join(handle);
}

void absolute_task_group_destroy(void* handle) {
    WasmTaskGroup* group = (WasmTaskGroup*)handle;
    if (!group)
        return;
    absolute_task_group_cancel_and_join(group);
    free(group->children);
    free(group);
}

/* Platform services: virtual FS, env, process, network stubs, capsules. */
/* The same real-to-text algorithm the host runtime compiles, included before
 * the platform and std layers because both of them write numbers. The two
 * targets agree by construction rather than by matching each other's
 * rounding; before it existed, the formatter above had no float directive and
 * printed the text "%g". See docs/known-defects.md section 22. */
#include "../src/real_text.h"

#include "absolute_wasm_platform.c"

/* Remaining std runtime services implemented without a native libc. */
#include "absolute_wasm_std.c"

