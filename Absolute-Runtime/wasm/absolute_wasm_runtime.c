/*
 * Absolute runtime subset for wasm32-unknown-unknown.
 *
 * Provides:
 *   - bump + freelist heap (malloc/free/calloc/realloc)
 *   - managed pointer table (absolute_managed_*)
 *   - exception TLS (absolute_error_*)
 *   - libc helpers used by Absolute codegen (printf family, mem*, strcmp, abort)
 *
 * Not provided: tasks, load(), FS, sockets, full stdlib Absolute-Runtime.
 * Console I/O is a no-op so modules instantiate with zero imports.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

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

void* malloc(size_t size) {
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
        /* After grow, end moves; break stays. */
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

void free(void* pointer) {
    if (!pointer)
        return;
    HeapBlock* block = (HeapBlock*)((unsigned char*)pointer - sizeof(HeapBlock));
    block->free = 1;
    block->next = g_free_list;
    g_free_list = block;
}

void* calloc(size_t count, size_t size) {
    size_t total = count * size;
    void* memory = malloc(total == 0 ? 1 : total);
    if (!memory)
        return NULL;
    unsigned char* bytes = (unsigned char*)memory;
    for (size_t i = 0; i < total; ++i)
        bytes[i] = 0;
    return memory;
}

void* realloc(void* pointer, size_t size) {
    if (!pointer)
        return malloc(size);
    if (size == 0) {
        free(pointer);
        return NULL;
    }
    HeapBlock* block = (HeapBlock*)((unsigned char*)pointer - sizeof(HeapBlock));
    if (block->size >= size)
        return pointer;
    void* next = malloc(size);
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
    void* allocation = calloc(1, bytes);
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

/* ---------- stubs for APIs not yet ported ---------- */

int32_t absolute_load_library(const char* path) {
    (void)path;
    return 0;
}

int32_t absolute_library_is_loaded(const char* path) {
    (void)path;
    return 0;
}

const char* absolute_load_error(void) {
    return "dynamic load is not available on wasm";
}

void* absolute_task_spawn_config(
    void (*entry)(void*), void* context, int32_t core, int32_t priority, const char* role) {
    (void)entry;
    (void)context;
    (void)core;
    (void)priority;
    (void)role;
    return NULL;
}

void* absolute_task_await(void* handle) {
    (void)handle;
    return NULL;
}

void absolute_task_destroy(void* handle) {
    (void)handle;
}
