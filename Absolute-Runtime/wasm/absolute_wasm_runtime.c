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
static unsigned char* g_heap_start;
static unsigned char* g_heap_end;
static unsigned char* g_heap_break;
static HeapBlock* g_free_list;
static int g_heap_ready;

static size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

static void heap_init(void) {
    if (g_heap_ready)
        return;
    /* Fixed linear-memory arena: simple and import-free. */
    g_heap_start = g_static_heap;
    g_heap_end = g_static_heap + sizeof(g_static_heap);
    g_heap_break = g_heap_start;
    g_free_list = NULL;
    g_heap_ready = 1;
}

static int heap_grow(size_t need) {
    (void)need;
    return 0;
}

/* Absolute LLVM always declares malloc(i64) even on wasm32; match that ABI. */
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
    if ((size_t)(g_heap_end - g_heap_break) < total) {
        if (!heap_grow(total - (size_t)(g_heap_end - g_heap_break) + 65536))
            return NULL;
        if ((size_t)(g_heap_end - g_heap_break) < total)
            return NULL;
    }
    HeapBlock* block = (HeapBlock*)g_heap_break;
    g_heap_break += total;
    block->size = size;
    block->free = 0;
    block->next = NULL;
    return (unsigned char*)block + sizeof(HeapBlock);
}

void* malloc(uint64_t size) {
    return heap_alloc((size_t)size);
}

void free(void* pointer) {
    if (!pointer)
        return;
    HeapBlock* block = (HeapBlock*)((unsigned char*)pointer - sizeof(HeapBlock));
    block->free = 1;
    block->next = g_free_list;
    g_free_list = block;
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
    void* next = heap_alloc((size_t)size);
    if (!next)
        return NULL;
    unsigned char* dst = (unsigned char*)next;
    unsigned char* src = (unsigned char*)pointer;
    for (size_t i = 0; i < block->size; ++i)
        dst[i] = src[i];
    free(pointer);
    return next;
}

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

/* Minimal formatter: supports %s %d %i %u %lld %llu %% only (Absolute assert/println). */
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
            if (out && o + 1 < cap)
                out[o] = '%';
            ++o;
            continue;
        }
        if (format[i] == 's') {
            const char* s = va_arg(args, const char*);
            if (!s)
                s = "(null)";
            for (size_t k = 0; s[k]; ++k) {
                if (out && o + 1 < cap)
                    out[o] = s[k];
                ++o;
            }
            continue;
        }
        if (format[i] == 'd' || format[i] == 'i') {
            int value = va_arg(args, int);
            char tmp[16];
            int n = 0;
            unsigned u;
            if (value < 0) {
                if (out && o + 1 < cap)
                    out[o] = '-';
                ++o;
                u = (unsigned)(-(value + 1)) + 1u;
            } else {
                u = (unsigned)value;
            }
            if (u == 0)
                tmp[n++] = '0';
            while (u) {
                tmp[n++] = (char)('0' + (u % 10));
                u /= 10;
            }
            while (n--) {
                if (out && o + 1 < cap)
                    out[o] = tmp[n];
                ++o;
            }
            continue;
        }
        if (format[i] == 'u') {
            unsigned u = va_arg(args, unsigned);
            char tmp[16];
            int n = 0;
            if (u == 0)
                tmp[n++] = '0';
            while (u) {
                tmp[n++] = (char)('0' + (u % 10));
                u /= 10;
            }
            while (n--) {
                if (out && o + 1 < cap)
                    out[o] = tmp[n];
                ++o;
            }
            continue;
        }
        /* Fallback: skip unknown specifier. */
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

int snprintf(char* buffer, size_t size, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int n = format_into(buffer, size, format, args);
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
}

void absolute_error_report(void) {
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

typedef struct WasmTask {
    void* context;
    uint64_t error_handle;
    uint64_t error_type;
    int32_t job_id; /* host pool job slot, or -1 when run synchronously */
    int done;
} WasmTask;

static int32_t g_task_core = -1;
static int32_t g_task_priority = 0;
static const char* g_task_role = "";

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

    const int32_t prev_core = g_task_core;
    const int32_t prev_priority = g_task_priority;
    const char* prev_role = g_task_role;
    g_task_core = core;
    g_task_priority = priority;
    g_task_role = role ? role : "";

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
    }

    g_task_core = prev_core;
    g_task_priority = prev_priority;
    g_task_role = prev_role;
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
    }
    if (task->error_handle)
        absolute_error_set(task->error_handle, task->error_type);
    void* context = task->context;
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
        } else {
            abort();
        }
    }
    if (task->error_handle)
        absolute_managed_destroy(task->error_handle);
    free(task->context);
    free(task);
}

/* Platform services: virtual FS, env, process, network stubs, capsules. */
#include "absolute_wasm_platform.c"

