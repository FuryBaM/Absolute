/*
 * Portable std services for wasm32. Included by absolute_wasm_runtime.c after
 * the platform layer, so heap/string/path helpers are available in this TU.
 */

/* ---------- atomics, mutexes, cancellation ---------- */

typedef struct WasmAtomicI64 { int64_t value; } WasmAtomicI64;
typedef struct WasmMutex { int32_t state; } WasmMutex;
typedef struct WasmSemaphore {
    int32_t permits;
    int32_t maximum;
} WasmSemaphore;
typedef struct WasmRwLock { int32_t state; } WasmRwLock;
typedef struct WasmCondition { int32_t generation; } WasmCondition;
typedef struct WasmOnce { int32_t state; } WasmOnce;
typedef struct WasmCancellation { int32_t cancelled; } WasmCancellation;

void* absolute_atomic_create(int64_t initial) {
    WasmAtomicI64* value = (WasmAtomicI64*)heap_alloc(sizeof(WasmAtomicI64));
    if (!value) return NULL;
    value->value = initial;
    return value;
}
int64_t absolute_atomic_fetch_add(void* p, int64_t value) {
    if (!p) return 0;
#if defined(ABSOLUTE_WASM_SHARED)
    return __atomic_fetch_add(&((WasmAtomicI64*)p)->value, value, __ATOMIC_SEQ_CST);
#else
    int64_t old = ((WasmAtomicI64*)p)->value; ((WasmAtomicI64*)p)->value += value; return old;
#endif
}
int64_t absolute_atomic_fetch_sub(void* p, int64_t value) {
    return absolute_atomic_fetch_add(p, -value);
}
int64_t absolute_atomic_load(void* p) {
    if (!p) return 0;
#if defined(ABSOLUTE_WASM_SHARED)
    return __atomic_load_n(&((WasmAtomicI64*)p)->value, __ATOMIC_SEQ_CST);
#else
    return ((WasmAtomicI64*)p)->value;
#endif
}
void absolute_atomic_store(void* p, int64_t value) {
    if (!p) return;
#if defined(ABSOLUTE_WASM_SHARED)
    __atomic_store_n(&((WasmAtomicI64*)p)->value, value, __ATOMIC_SEQ_CST);
#else
    ((WasmAtomicI64*)p)->value = value;
#endif
}
uint8_t absolute_atomic_compare_exchange(void* p, int64_t expected, int64_t desired) {
    if (!p) return 0;
#if defined(ABSOLUTE_WASM_SHARED)
    return __atomic_compare_exchange_n(&((WasmAtomicI64*)p)->value, &expected, desired,
        0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) ? 1 : 0;
#else
    if (((WasmAtomicI64*)p)->value != expected) return 0;
    ((WasmAtomicI64*)p)->value = desired; return 1;
#endif
}
void absolute_atomic_destroy(void* p) { free(p); }

void* absolute_mutex_create(void) {
    WasmMutex* mutex = (WasmMutex*)heap_alloc(sizeof(WasmMutex));
    if (mutex) mutex->state = 0;
    return mutex;
}
uint8_t absolute_mutex_try_lock(void* p) {
    if (!p) return 0;
#if defined(ABSOLUTE_WASM_SHARED)
    int32_t expected = 0;
    return __atomic_compare_exchange_n(&((WasmMutex*)p)->state, &expected, 1,
        0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED) ? 1 : 0;
#else
    if (((WasmMutex*)p)->state) return 0;
    ((WasmMutex*)p)->state = 1; return 1;
#endif
}
void absolute_mutex_lock(void* p) {
    if (!p) return;
    while (!absolute_mutex_try_lock(p)) {
#if defined(ABSOLUTE_WASM_SHARED)
        __builtin_wasm_memory_atomic_wait32(&((WasmMutex*)p)->state, 1, 1000000);
#endif
    }
}
void absolute_mutex_unlock(void* p) {
    if (!p) return;
#if defined(ABSOLUTE_WASM_SHARED)
    __atomic_store_n(&((WasmMutex*)p)->state, 0, __ATOMIC_RELEASE);
    __builtin_wasm_memory_atomic_notify(&((WasmMutex*)p)->state, 1);
#else
    ((WasmMutex*)p)->state = 0;
#endif
}
void absolute_mutex_destroy(void* p) { free(p); }

void* absolute_semaphore_create(int32_t initial, int32_t maximum) {
    if (maximum <= 0 || initial < 0 || initial > maximum) return NULL;
    WasmSemaphore* semaphore =
        (WasmSemaphore*)heap_alloc(sizeof(WasmSemaphore));
    if (!semaphore) return NULL;
    semaphore->permits = initial;
    semaphore->maximum = maximum;
    return semaphore;
}
uint8_t absolute_semaphore_try_acquire(void* p) {
    if (!p) return 0;
    WasmSemaphore* semaphore = (WasmSemaphore*)p;
#if defined(ABSOLUTE_WASM_SHARED)
    int32_t current =
        __atomic_load_n(&semaphore->permits, __ATOMIC_ACQUIRE);
    while (current > 0) {
        if (__atomic_compare_exchange_n(
                &semaphore->permits, &current, current - 1, 0,
                __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            return 1;
        }
    }
    return 0;
#else
    if (semaphore->permits <= 0) return 0;
    --semaphore->permits;
    return 1;
#endif
}
void absolute_semaphore_acquire(void* p) {
    if (!p) return;
    WasmSemaphore* semaphore = (WasmSemaphore*)p;
    while (!absolute_semaphore_try_acquire(p)) {
#if defined(ABSOLUTE_WASM_SHARED)
        __builtin_wasm_memory_atomic_wait32(
            &semaphore->permits, 0, 1000000);
#endif
    }
}
uint8_t absolute_semaphore_release(void* p, int32_t permits) {
    if (!p || permits <= 0) return 0;
    WasmSemaphore* semaphore = (WasmSemaphore*)p;
#if defined(ABSOLUTE_WASM_SHARED)
    int32_t current =
        __atomic_load_n(&semaphore->permits, __ATOMIC_ACQUIRE);
    for (;;) {
        if (permits > semaphore->maximum - current) return 0;
        if (__atomic_compare_exchange_n(
                &semaphore->permits, &current, current + permits, 0,
                __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
            __builtin_wasm_memory_atomic_notify(
                &semaphore->permits, (uint32_t)permits);
            return 1;
        }
    }
#else
    if (permits > semaphore->maximum - semaphore->permits) return 0;
    semaphore->permits += permits;
    return 1;
#endif
}
int32_t absolute_semaphore_available(void* p) {
    if (!p) return 0;
#if defined(ABSOLUTE_WASM_SHARED)
    return __atomic_load_n(
        &((WasmSemaphore*)p)->permits, __ATOMIC_ACQUIRE);
#else
    return ((WasmSemaphore*)p)->permits;
#endif
}
void absolute_semaphore_destroy(void* p) { free(p); }

void* absolute_rwlock_create(void) {
    WasmRwLock* rwlock = (WasmRwLock*)heap_alloc(sizeof(WasmRwLock));
    if (rwlock) rwlock->state = 0;
    return rwlock;
}
uint8_t absolute_rwlock_try_lock_read(void* p) {
    if (!p) return 0;
    WasmRwLock* rwlock = (WasmRwLock*)p;
#if defined(ABSOLUTE_WASM_SHARED)
    int32_t current =
        __atomic_load_n(&rwlock->state, __ATOMIC_ACQUIRE);
    while (current >= 0) {
        if (__atomic_compare_exchange_n(
                &rwlock->state, &current, current + 1, 0,
                __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            return 1;
        }
    }
    return 0;
#else
    if (rwlock->state < 0) return 0;
    ++rwlock->state;
    return 1;
#endif
}
void absolute_rwlock_lock_read(void* p) {
    if (!p) return;
    WasmRwLock* rwlock = (WasmRwLock*)p;
    while (!absolute_rwlock_try_lock_read(p)) {
#if defined(ABSOLUTE_WASM_SHARED)
        __builtin_wasm_memory_atomic_wait32(
            &rwlock->state, -1, 1000000);
#endif
    }
}
void absolute_rwlock_unlock_read(void* p) {
    if (!p) return;
    WasmRwLock* rwlock = (WasmRwLock*)p;
#if defined(ABSOLUTE_WASM_SHARED)
    __atomic_fetch_sub(&rwlock->state, 1, __ATOMIC_RELEASE);
    __builtin_wasm_memory_atomic_notify(&rwlock->state, 1);
#else
    if (rwlock->state > 0) --rwlock->state;
#endif
}
uint8_t absolute_rwlock_try_lock_write(void* p) {
    if (!p) return 0;
    WasmRwLock* rwlock = (WasmRwLock*)p;
#if defined(ABSOLUTE_WASM_SHARED)
    int32_t expected = 0;
    return __atomic_compare_exchange_n(
        &rwlock->state, &expected, -1, 0,
        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED) ? 1 : 0;
#else
    if (rwlock->state != 0) return 0;
    rwlock->state = -1;
    return 1;
#endif
}
void absolute_rwlock_lock_write(void* p) {
    if (!p) return;
    WasmRwLock* rwlock = (WasmRwLock*)p;
    while (!absolute_rwlock_try_lock_write(p)) {
#if defined(ABSOLUTE_WASM_SHARED)
        int32_t observed =
            __atomic_load_n(&rwlock->state, __ATOMIC_RELAXED);
        __builtin_wasm_memory_atomic_wait32(
            &rwlock->state, observed, 1000000);
#endif
    }
}
void absolute_rwlock_unlock_write(void* p) {
    if (!p) return;
    WasmRwLock* rwlock = (WasmRwLock*)p;
#if defined(ABSOLUTE_WASM_SHARED)
    __atomic_store_n(&rwlock->state, 0, __ATOMIC_RELEASE);
    __builtin_wasm_memory_atomic_notify(&rwlock->state, UINT32_MAX);
#else
    if (rwlock->state == -1) rwlock->state = 0;
#endif
}
void absolute_rwlock_destroy(void* p) { free(p); }

void* absolute_condition_create(void) {
    WasmCondition* condition =
        (WasmCondition*)heap_alloc(sizeof(WasmCondition));
    if (condition) condition->generation = 0;
    return condition;
}
void absolute_condition_wait(void* conditionHandle, void* mutexHandle) {
    if (!conditionHandle || !mutexHandle) return;
    WasmCondition* condition = (WasmCondition*)conditionHandle;
#if defined(ABSOLUTE_WASM_SHARED)
    int32_t observed =
        __atomic_load_n(&condition->generation, __ATOMIC_ACQUIRE);
    absolute_mutex_unlock(mutexHandle);
    __builtin_wasm_memory_atomic_wait32(
        &condition->generation, observed, -1);
    absolute_mutex_lock(mutexHandle);
#else
    /* Non-shared wasm cannot be notified by another worker. */
    (void)condition;
#endif
}
void absolute_condition_notify_one(void* p) {
    if (!p) return;
    WasmCondition* condition = (WasmCondition*)p;
#if defined(ABSOLUTE_WASM_SHARED)
    __atomic_fetch_add(&condition->generation, 1, __ATOMIC_RELEASE);
    __builtin_wasm_memory_atomic_notify(&condition->generation, 1);
#else
    ++condition->generation;
#endif
}
void absolute_condition_notify_all(void* p) {
    if (!p) return;
    WasmCondition* condition = (WasmCondition*)p;
#if defined(ABSOLUTE_WASM_SHARED)
    __atomic_fetch_add(&condition->generation, 1, __ATOMIC_RELEASE);
    __builtin_wasm_memory_atomic_notify(
        &condition->generation, UINT32_MAX);
#else
    ++condition->generation;
#endif
}
void absolute_condition_destroy(void* p) { free(p); }

void* absolute_once_create(void) {
    WasmOnce* once = (WasmOnce*)heap_alloc(sizeof(WasmOnce));
    if (once) once->state = 0;
    return once;
}
uint8_t absolute_once_begin(void* p) {
    if (!p) return 0;
    WasmOnce* once = (WasmOnce*)p;
    for (;;) {
#if defined(ABSOLUTE_WASM_SHARED)
        int32_t state = __atomic_load_n(&once->state, __ATOMIC_ACQUIRE);
        if (state == 2) return 0;
        if (state == 0) {
            int32_t expected = 0;
            if (__atomic_compare_exchange_n(
                    &once->state, &expected, 1, 0,
                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
                return 1;
            }
            continue;
        }
        __builtin_wasm_memory_atomic_wait32(&once->state, 1, 1000000);
#else
        if (once->state == 2) return 0;
        if (once->state == 0) {
            once->state = 1;
            return 1;
        }
#endif
    }
}
void absolute_once_complete(void* p) {
    if (!p) return;
    WasmOnce* once = (WasmOnce*)p;
#if defined(ABSOLUTE_WASM_SHARED)
    __atomic_store_n(&once->state, 2, __ATOMIC_RELEASE);
    __builtin_wasm_memory_atomic_notify(&once->state, UINT32_MAX);
#else
    once->state = 2;
#endif
}
void absolute_once_reset(void* p) {
    if (!p) return;
    WasmOnce* once = (WasmOnce*)p;
#if defined(ABSOLUTE_WASM_SHARED)
    __atomic_store_n(&once->state, 0, __ATOMIC_RELEASE);
    __builtin_wasm_memory_atomic_notify(&once->state, UINT32_MAX);
#else
    once->state = 0;
#endif
}
uint8_t absolute_once_is_complete(void* p) {
    if (!p) return 0;
#if defined(ABSOLUTE_WASM_SHARED)
    return __atomic_load_n(
        &((WasmOnce*)p)->state, __ATOMIC_ACQUIRE) == 2;
#else
    return ((WasmOnce*)p)->state == 2;
#endif
}
void absolute_once_destroy(void* p) { free(p); }

void* absolute_cancellation_token_create(void) {
    WasmCancellation* token = (WasmCancellation*)heap_alloc(sizeof(WasmCancellation));
    if (token) token->cancelled = 0;
    return token;
}
void absolute_cancellation_token_cancel(void* p) {
    if (!p) return;
#if defined(ABSOLUTE_WASM_SHARED)
    __atomic_store_n(&((WasmCancellation*)p)->cancelled, 1, __ATOMIC_RELEASE);
#else
    ((WasmCancellation*)p)->cancelled = 1;
#endif
}
uint8_t absolute_cancellation_token_is_cancelled(void* p) {
    if (!p) return 0;
#if defined(ABSOLUTE_WASM_SHARED)
    return __atomic_load_n(&((WasmCancellation*)p)->cancelled, __ATOMIC_ACQUIRE) ? 1 : 0;
#else
    return ((WasmCancellation*)p)->cancelled ? 1 : 0;
#endif
}
void absolute_cancellation_token_destroy(void* p) { free(p); }

/* ---------- host-backed HTTPS response stream ---------- */

typedef struct WasmHttpTlsResponse {
    uint8_t* body;
    int32_t length;
    int32_t offset;
    char* scratch;
    int32_t scratchCapacity;
} WasmHttpTlsResponse;

static char g_http_tls_error[256];
static const char g_http_tls_headers[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/octet-stream\r\n"
    "Connection: close\r\n";

void* absolute_http_tls_open(
    const char* method, const char* url, const char* headers,
    const char* body, int32_t timeoutMilliseconds,
    int32_t maximumResponseBytes) {
    (void)headers;
    (void)body;
    (void)timeoutMilliseconds;
    g_http_tls_error[0] = '\0';
    if (!method || strcmp(method, "GET") != 0) {
        fs_copy_path(
            g_http_tls_error, sizeof(g_http_tls_error),
            "WebAssembly HTTPS host currently supports GET only");
        return NULL;
    }
    if (!url || maximumResponseBytes <= 0) {
        fs_copy_path(
            g_http_tls_error, sizeof(g_http_tls_error),
            "invalid WebAssembly HTTPS request");
        return NULL;
    }
    WasmHttpTlsResponse* response =
        (WasmHttpTlsResponse*)heap_alloc(
            sizeof(WasmHttpTlsResponse));
    if (!response) return NULL;
    memset(response, 0, sizeof(*response));
    response->body =
        (uint8_t*)heap_alloc((size_t)maximumResponseBytes + 1);
    if (!response->body) {
        free(response);
        return NULL;
    }
    const int32_t received = absolute_http_get(
        url, response->body, maximumResponseBytes);
    if (received < 0) {
        fs_copy_path(
            g_http_tls_error, sizeof(g_http_tls_error),
            "HTTPS URL is unavailable from the WebAssembly host");
        free(response->body);
        free(response);
        return NULL;
    }
    response->body[received] = 0;
    response->length = received;
    return response;
}

const char* absolute_http_tls_headers(void* p) {
    if (!p) {
        fs_copy_path(
            g_http_tls_error, sizeof(g_http_tls_error),
            "HTTPS response is closed");
        return "";
    }
    g_http_tls_error[0] = '\0';
    return g_http_tls_headers;
}

const char* absolute_http_tls_receive(
    void* p, int32_t maximumBytes) {
    WasmHttpTlsResponse* response = (WasmHttpTlsResponse*)p;
    if (!response || maximumBytes <= 0) return "";
    const int32_t remaining =
        response->length - response->offset;
    const int32_t count =
        remaining < maximumBytes ? remaining : maximumBytes;
    if (count <= 0) {
        g_http_tls_error[0] = '\0';
        return "";
    }
    if (response->scratchCapacity < count + 1) {
        char* next = (char*)realloc(
            response->scratch, (size_t)count + 1);
        if (!next) {
            fs_copy_path(
                g_http_tls_error, sizeof(g_http_tls_error),
                "HTTPS receive allocation failed");
            return "";
        }
        response->scratch = next;
        response->scratchCapacity = count + 1;
    }
    memcpy(
        response->scratch,
        response->body + response->offset,
        (size_t)count);
    response->scratch[count] = '\0';
    response->offset += count;
    g_http_tls_error[0] = '\0';
    return response->scratch;
}

void absolute_http_tls_close(void* p) {
    WasmHttpTlsResponse* response = (WasmHttpTlsResponse*)p;
    if (!response) return;
    free(response->scratch);
    free(response->body);
    free(response);
}

const char* absolute_http_tls_error(void) {
    return g_http_tls_error;
}

void absolute_task_delay(int32_t milliseconds) {
    const int32_t deadline =
        absolute_task_current_deadline_remaining();
    if (deadline >= 0 && deadline < milliseconds)
        milliseconds = deadline;
    if (milliseconds > 0) absolute_time_sleep_millis(milliseconds);
}

int32_t absolute_process_set_cwd(const char* path) {
    if (!path || !absolute_fs_is_directory(path)) {
        fs_copy_path(g_process_error, sizeof(g_process_error), "directory not found");
        return 0;
    }
    fs_path_normalize_into(path, g_fs_cwd, sizeof(g_fs_cwd), 1);
    g_process_error[0] = '\0';
    return 1;
}

/* ---------- binary serialization ---------- */

typedef struct WasmBinaryWriter { uint8_t* data; size_t size; size_t capacity; } WasmBinaryWriter;
typedef struct WasmBinaryReader { uint8_t* data; size_t size; size_t position; } WasmBinaryReader;

static int binary_reserve(WasmBinaryWriter* writer, size_t extra) {
    if (!writer || extra > (size_t)-1 - writer->size) return 0;
    size_t required = writer->size + extra;
    if (required <= writer->capacity) return 1;
    size_t capacity = writer->capacity ? writer->capacity : 64;
    while (capacity < required) capacity *= 2;
    uint8_t* next = (uint8_t*)realloc(writer->data, capacity);
    if (!next) return 0;
    writer->data = next; writer->capacity = capacity; return 1;
}
static void binary_write(WasmBinaryWriter* writer, const void* data, size_t size) {
    if (!data || !binary_reserve(writer, size)) return;
    memcpy(writer->data + writer->size, data, size); writer->size += size;
}
void* absolute_binary_writer_create(void) {
    WasmBinaryWriter* writer = (WasmBinaryWriter*)calloc(1, sizeof(WasmBinaryWriter));
    return writer;
}
void absolute_binary_writer_free(void* p) {
    WasmBinaryWriter* writer = (WasmBinaryWriter*)p;
    if (!writer) return; free(writer->data); free(writer);
}
void absolute_binary_writer_write_byte(void* p, uint8_t v) { binary_write((WasmBinaryWriter*)p, &v, 1); }
void absolute_binary_writer_write_i16(void* p, int16_t v) { binary_write((WasmBinaryWriter*)p, &v, 2); }
void absolute_binary_writer_write_i32(void* p, int32_t v) { binary_write((WasmBinaryWriter*)p, &v, 4); }
void absolute_binary_writer_write_i64(void* p, int64_t v) { binary_write((WasmBinaryWriter*)p, &v, 8); }
void absolute_binary_writer_write_float(void* p, float v) { binary_write((WasmBinaryWriter*)p, &v, 4); }
void absolute_binary_writer_write_double(void* p, double v) { binary_write((WasmBinaryWriter*)p, &v, 8); }
void absolute_binary_writer_write_string(void* p, const char* text) {
    int32_t size = (int32_t)strlen(text ? text : "");
    absolute_binary_writer_write_i32(p, size);
    binary_write((WasmBinaryWriter*)p, text ? text : "", (size_t)size);
}
void absolute_binary_writer_write_bytes(void* p, const void* data, int64_t size) {
    if (size > 0) binary_write((WasmBinaryWriter*)p, data, (size_t)size);
}
int64_t absolute_binary_writer_size(const void* p) {
    return p ? (int64_t)((const WasmBinaryWriter*)p)->size : 0;
}
const void* absolute_binary_writer_get_data(const void* p) {
    return p ? ((const WasmBinaryWriter*)p)->data : NULL;
}
static char* g_binary_result;

const char* absolute_binary_writer_to_hex(const void* p) {
    static const char digits[] = "0123456789abcdef";
    const WasmBinaryWriter* writer = (const WasmBinaryWriter*)p;
    if (!writer) return "";
    size_t count = writer->size;
    char* out = (char*)heap_alloc(count * 2 + 1);
    if (!out) return "";
    for (size_t i = 0; i < count; ++i) {
        out[i * 2] = digits[writer->data[i] >> 4];
        out[i * 2 + 1] = digits[writer->data[i] & 15];
    }
    out[count * 2] = '\0';
    free(g_binary_result);
    g_binary_result = out;
    return g_binary_result;
}
void* absolute_binary_reader_create(const void* data, int64_t size) {
    WasmBinaryReader* reader = (WasmBinaryReader*)calloc(1, sizeof(WasmBinaryReader));
    if (!reader) return NULL;
    if (data && size > 0) {
        reader->data = (uint8_t*)heap_alloc((size_t)size);
        if (!reader->data) { free(reader); return NULL; }
        memcpy(reader->data, data, (size_t)size); reader->size = (size_t)size;
    }
    return reader;
}
void absolute_binary_reader_free(void* p) {
    WasmBinaryReader* reader = (WasmBinaryReader*)p;
    if (!reader) return; free(reader->data); free(reader);
}
static int binary_read(WasmBinaryReader* reader, void* out, size_t size) {
    if (!reader || !out || reader->position + size > reader->size) return 0;
    memcpy(out, reader->data + reader->position, size); reader->position += size; return 1;
}
uint8_t absolute_binary_reader_read_byte(void* p) { uint8_t v=0; binary_read(p,&v,1); return v; }
int16_t absolute_binary_reader_read_i16(void* p) { int16_t v=0; binary_read(p,&v,2); return v; }
int32_t absolute_binary_reader_read_i32(void* p) { int32_t v=0; binary_read(p,&v,4); return v; }
int64_t absolute_binary_reader_read_i64(void* p) { int64_t v=0; binary_read(p,&v,8); return v; }
float absolute_binary_reader_read_float(void* p) { float v=0; binary_read(p,&v,4); return v; }
double absolute_binary_reader_read_double(void* p) { double v=0; binary_read(p,&v,8); return v; }
const char* absolute_binary_reader_read_string(void* p) {
    WasmBinaryReader* reader = (WasmBinaryReader*)p;
    int32_t size = absolute_binary_reader_read_i32(p);
    if (!reader || size < 0 || reader->position + (size_t)size > reader->size) return "";
    const char* copy = wasm_string_copy_range(
        (const char*)(reader->data + reader->position), (size_t)size);
    reader->position += (size_t)size;
    return copy;
}
int64_t absolute_binary_reader_read_bytes(void* p, void* out, int64_t size) {
    WasmBinaryReader* reader=(WasmBinaryReader*)p;
    if (!reader || !out || size<=0) return 0;
    size_t available=reader->size-reader->position, count=(size_t)size<available?(size_t)size:available;
    memcpy(out,reader->data+reader->position,count); reader->position+=count; return (int64_t)count;
}
int64_t absolute_binary_reader_position(const void* p) { return p?(int64_t)((const WasmBinaryReader*)p)->position:0; }
int64_t absolute_binary_reader_remaining(const void* p) {
    const WasmBinaryReader* r=(const WasmBinaryReader*)p;
    if (!r || r->position >= r->size) return 0;
    return (int64_t)(r->size-r->position);
}
int32_t absolute_binary_reader_is_eof(const void* p) {
    const WasmBinaryReader* r=(const WasmBinaryReader*)p; return !r||r->position>=r->size;
}
void absolute_binary_reader_seek(void* p,int64_t pos) {
    WasmBinaryReader* r=(WasmBinaryReader*)p; if(!r||pos<0)return;
    r->position=(size_t)pos>r->size?r->size:(size_t)pos;
}

typedef struct {
    uint8_t* data;
    int64_t capacity;
    int32_t is_owner;
} WasmByteBuffer;

void* absolute_byte_buffer_create(int64_t capacity) {
    if (capacity <= 0) capacity = 1024;
    WasmByteBuffer* buf = (WasmByteBuffer*)malloc(sizeof(WasmByteBuffer));
    buf->data = (uint8_t*)calloc(1, (size_t)capacity);
    buf->capacity = capacity;
    buf->is_owner = 1;
    return buf;
}

void* absolute_byte_buffer_wrap(const void* data, int64_t size) {
    WasmByteBuffer* buf = (WasmByteBuffer*)malloc(sizeof(WasmByteBuffer));
    buf->data = (uint8_t*)data;
    buf->capacity = size > 0 ? size : 0;
    buf->is_owner = 0;
    return buf;
}

void absolute_byte_buffer_free(void* p) {
    if (!p) return;
    WasmByteBuffer* buf = (WasmByteBuffer*)p;
    if (buf->is_owner && buf->data) free(buf->data);
    free(buf);
}

void* absolute_byte_buffer_slice(void* p, int64_t offset, int64_t length) {
    if (!p) return NULL;
    WasmByteBuffer* buf = (WasmByteBuffer*)p;
    if (offset < 0) offset = 0;
    if (offset > buf->capacity) offset = buf->capacity;
    if (length < 0) length = 0;
    if (offset + length > buf->capacity) length = buf->capacity - offset;
    WasmByteBuffer* slice = (WasmByteBuffer*)malloc(sizeof(WasmByteBuffer));
    slice->data = buf->data + offset;
    slice->capacity = length;
    slice->is_owner = 0;
    return slice;
}

uint8_t* absolute_byte_buffer_get_data(void* p) {
    return p ? ((WasmByteBuffer*)p)->data : NULL;
}

int64_t absolute_byte_buffer_capacity(void* p) {
    return p ? ((WasmByteBuffer*)p)->capacity : 0;
}

uint8_t absolute_byte_buffer_get_byte(void* p, int64_t offset) {
    if (!p) return 0;
    WasmByteBuffer* buf = (WasmByteBuffer*)p;
    if (offset < 0 || offset >= buf->capacity) return 0;
    return buf->data[offset];
}

void absolute_byte_buffer_set_byte(void* p, int64_t offset, uint8_t val) {
    if (!p) return;
    WasmByteBuffer* buf = (WasmByteBuffer*)p;
    if (offset < 0 || offset >= buf->capacity) return;
    buf->data[offset] = val;
}

void absolute_byte_buffer_compact(void* p, int64_t position, int64_t remaining) {
    if (!p) return;
    WasmByteBuffer* buf = (WasmByteBuffer*)p;
    if (position <= 0 || remaining <= 0) return;
    if (position + remaining > buf->capacity) remaining = buf->capacity - position;
    memmove(buf->data, buf->data + position, (size_t)remaining);
}

/* ---------- datetime ---------- */

static char g_datetime_error[96], g_datetime_scratch[96];
static int64_t wasm_days_from_civil(int32_t y,int32_t m,int32_t d) {
    y-=m<=2; int64_t era=(y>=0?y:y-399)/400; uint32_t yoe=(uint32_t)(y-era*400);
    uint32_t doy=(153*(uint32_t)(m>2?m-3:m+9)+2)/5+(uint32_t)d-1;
    return era*146097+(int64_t)(yoe*365+yoe/4-yoe/100+doy)-719468;
}
static void wasm_civil_from_days(int64_t z,int32_t* y,int32_t* m,int32_t* d) {
    z+=719468; int64_t era=(z>=0?z:z-146096)/146097; uint32_t doe=(uint32_t)(z-era*146097);
    uint32_t yoe=(doe-doe/1460+doe/36524-doe/146096)/365; int64_t yy=(int64_t)yoe+era*400;
    uint32_t doy=doe-(365*yoe+yoe/4-yoe/100),mp=(5*doy+2)/153;
    *d=(int32_t)(doy-(153*mp+2)/5+1); *m=(int32_t)(mp<10?mp+3:mp-9); *y=(int32_t)(yy+(*m<=2));
}
const char* absolute_datetime_error(void){return g_datetime_error;}
int32_t absolute_datetime_local_offset_minutes(void){return 0;}
const char* absolute_datetime_local_zone_name(void){return "UTC";}
void absolute_datetime_civil_from_unix_millis(int64_t value,int32_t offset,int32_t*y,int32_t*m,int32_t*d,int32_t*h,int32_t*mi,int32_t*s,int32_t*ms){
    int64_t adjusted=value+(int64_t)offset*60000,sec=adjusted/1000; int32_t milli=(int32_t)(adjusted%1000);
    if(milli<0){milli+=1000;--sec;} int64_t days=sec/86400; int32_t sod=(int32_t)(sec%86400);
    if(sod<0){sod+=86400;--days;} int32_t yy,mm,dd; wasm_civil_from_days(days,&yy,&mm,&dd);
    if(y)*y=yy;if(m)*m=mm;if(d)*d=dd;if(h)*h=sod/3600;if(mi)*mi=(sod%3600)/60;if(s)*s=sod%60;if(ms)*ms=milli;
}
int64_t absolute_datetime_civil_to_unix_millis(int32_t y,int32_t m,int32_t d,int32_t h,int32_t mi,int32_t s,int32_t ms,int32_t off){
    return (wasm_days_from_civil(y,m,d)*86400+h*3600+mi*60+s)*1000+ms-(int64_t)off*60000;
}
const char* absolute_datetime_format_iso(int32_t y,int32_t m,int32_t d,int32_t h,int32_t mi,int32_t s,int32_t ms,int32_t off){
    /* The same text the native runtime writes, including a negative year:
       "%04d" would spend one of the four digits on the sign. */
    char yearText[16];
    if(y<0) snprintf(yearText,sizeof(yearText),"-%04lld",(long long)(-(int64_t)y));
    else snprintf(yearText,sizeof(yearText),"%04d",y);
    char zone[16];
    if(off==0) snprintf(zone,sizeof(zone),"Z");
    else{int32_t a=off>=0?off:-off;snprintf(zone,sizeof(zone),"%c%02d:%02d",off>=0?'+':'-',a/60,a%60);}
    if(ms) snprintf(g_datetime_scratch,sizeof(g_datetime_scratch),"%s-%02d-%02dT%02d:%02d:%02d.%03d%s",yearText,m,d,h,mi,s,ms,zone);
    else snprintf(g_datetime_scratch,sizeof(g_datetime_scratch),"%s-%02d-%02dT%02d:%02d:%02d%s",yearText,m,d,h,mi,s,zone);
    return g_datetime_scratch;
}
static int parse_digits(const char* p,int count,int32_t*out){int32_t v=0;for(int i=0;i<count;++i){if(p[i]<'0'||p[i]>'9')return 0;v=v*10+p[i]-'0';}*out=v;return 1;}
static int datetime_fail(const char* message){fs_copy_path(g_datetime_error,sizeof(g_datetime_error),message);return 0;}
static int read_fixed(const char**c,int count,int32_t*out){if(!parse_digits(*c,count,out))return 0;*c+=count;return 1;}
/* The same grammar the native runtime reads, and the same refusals, because a
   program that parses a timestamp must not depend on which target it is built
   for. See docs/known-defects.md section 20. */
int32_t absolute_datetime_parse_iso(const char*t,int32_t*y,int32_t*m,int32_t*d,int32_t*h,int32_t*mi,int32_t*s,int32_t*ms,int32_t*off){
    g_datetime_error[0]='\0';
    if(!t||!*t) return datetime_fail("Empty ISO-8601 text");
    const char* c=t;
    int32_t yearSign=1;
    if(*c=='-'||*c=='+'){yearSign=(*c=='-')?-1:1;++c;}
    int32_t yy=0,mm=0,dd=0,hh=0,mn=0,ss=0,milli=0,zone=0;
    {   int64_t acc=0;int digits=0;
        while(*c>='0'&&*c<='9'){if(acc>100000000)return datetime_fail("Invalid ISO-8601 format");acc=acc*10+(*c-'0');++digits;++c;}
        if(digits<4) return datetime_fail("Invalid ISO-8601 format");
        yy=(int32_t)acc*yearSign;
    }
    if(*c!='-'||(++c,!read_fixed(&c,2,&mm))||*c!='-'||(++c,!read_fixed(&c,2,&dd)))
        return datetime_fail("Invalid ISO-8601 format");
    if(*c=='T'||*c=='t'||*c==' '){
        ++c;
        if(!read_fixed(&c,2,&hh)||*c!=':'||(++c,!read_fixed(&c,2,&mn)))
            return datetime_fail("Invalid ISO-8601 time");
        if(*c==':'){++c;if(!read_fixed(&c,2,&ss))return datetime_fail("Invalid ISO-8601 time");}
        if(*c=='.'||*c==','){
            ++c;
            if(*c<'0'||*c>'9') return datetime_fail("Invalid ISO-8601 fractional second");
            int digits=0;
            while(*c>='0'&&*c<='9'){if(digits<3)milli=milli*10+(*c-'0');++digits;++c;}
            for(;digits<3;++digits)milli*=10;
        }
        if(*c=='Z'||*c=='z')++c;
        else if(*c=='+'||*c=='-'){
            int32_t sign=(*c=='-')?-1:1;++c;
            int32_t zh=0,zm=0;
            if(!read_fixed(&c,2,&zh)) return datetime_fail("Invalid ISO-8601 zone offset");
            if(*c==':')++c;
            if(*c>='0'&&*c<='9'){if(!read_fixed(&c,2,&zm))return datetime_fail("Invalid ISO-8601 zone offset");}
            if(zh>23||zm>59) return datetime_fail("Zone offset out of range");
            zone=sign*(zh*60+zm);
        }
    }
    while(*c==' '||*c=='\t'||*c=='\r'||*c=='\n')++c;
    if(*c) return datetime_fail("Trailing text after ISO-8601 value");
    if(y)*y=yy;if(m)*m=mm;if(d)*d=dd;if(h)*h=hh;if(mi)*mi=mn;if(s)*s=ss;if(ms)*ms=milli;if(off)*off=zone;
    return 1;
}

/* ---------- JSON ---------- */

typedef struct WasmJsonNode WasmJsonNode;
struct WasmJsonNode {
    int32_t type;
    int32_t boolean;
    double number;
    char* string;
    WasmJsonNode** children;
    char** keys;
    size_t size;
    size_t capacity;
};

static char g_json_error[160];
static char* g_json_result;

static char* json_copy(const char* text) {
    size_t size = strlen(text ? text : "");
    char* copy = (char*)heap_alloc(size + 1);
    if (!copy) return NULL;
    memcpy(copy, text ? text : "", size + 1);
    return copy;
}

static WasmJsonNode* json_create(int32_t type) {
    WasmJsonNode* node = (WasmJsonNode*)calloc(1, sizeof(WasmJsonNode));
    if (node) node->type = type;
    return node;
}

void* absolute_json_create_null(void) { return json_create(0); }
void* absolute_json_create_bool(int32_t value) {
    WasmJsonNode* node = json_create(1);
    if (node) node->boolean = value != 0;
    return node;
}
void* absolute_json_create_number(double value) {
    WasmJsonNode* node = json_create(2);
    if (node) node->number = value;
    return node;
}
void* absolute_json_create_string(const char* value) {
    WasmJsonNode* node = json_create(3);
    if (node) node->string = json_copy(value);
    return node;
}
void* absolute_json_create_array(void) { return json_create(4); }
void* absolute_json_create_object(void) { return json_create(5); }

void absolute_json_free(void* handle) {
    WasmJsonNode* node = (WasmJsonNode*)handle;
    if (!node) return;
    for (size_t i = 0; i < node->size; ++i) {
        absolute_json_free(node->children[i]);
        if (node->keys) free(node->keys[i]);
    }
    free(node->children);
    free(node->keys);
    free(node->string);
    free(node);
}

int32_t absolute_json_get_type(const void* handle) {
    return handle ? ((const WasmJsonNode*)handle)->type : 0;
}
int32_t absolute_json_as_bool(const void* handle) {
    return handle ? ((const WasmJsonNode*)handle)->boolean : 0;
}
double absolute_json_as_number(const void* handle) {
    return handle ? ((const WasmJsonNode*)handle)->number : 0.0;
}
const char* absolute_json_as_string(const void* handle) {
    const WasmJsonNode* node = (const WasmJsonNode*)handle;
    return node && node->string ? node->string : "";
}
int32_t absolute_json_size(const void* handle) {
    const WasmJsonNode* node = (const WasmJsonNode*)handle;
    return node && (node->type == 4 || node->type == 5) ? (int32_t)node->size : 0;
}
void* absolute_json_get_array_elem(const void* handle, int32_t index) {
    const WasmJsonNode* node = (const WasmJsonNode*)handle;
    if (!node || node->type != 4 || index < 0 || (size_t)index >= node->size) return NULL;
    return node->children[index];
}
void* absolute_json_get_property(const void* handle, const char* key);
int32_t absolute_json_has_property(const void* handle, const char* key) {
    return absolute_json_get_property(handle, key) != NULL;
}
void* absolute_json_get_property(const void* handle, const char* key) {
    const WasmJsonNode* node = (const WasmJsonNode*)handle;
    if (!node || node->type != 5 || !key) return NULL;
    for (size_t i = 0; i < node->size; ++i)
        if (strcmp(node->keys[i], key) == 0) return node->children[i];
    return NULL;
}

static int json_reserve(WasmJsonNode* node, size_t required, int object) {
    if (required <= node->capacity) return 1;
    size_t capacity = node->capacity ? node->capacity * 2 : 4;
    while (capacity < required) capacity *= 2;
    WasmJsonNode** children = (WasmJsonNode**)realloc(node->children, capacity * sizeof(WasmJsonNode*));
    if (!children) return 0;
    node->children = children;
    if (object) {
        char** keys = (char**)realloc(node->keys, capacity * sizeof(char*));
        if (!keys) return 0;
        node->keys = keys;
    }
    node->capacity = capacity;
    return 1;
}

void absolute_json_array_add(void* handle, void* item) {
    WasmJsonNode* node = (WasmJsonNode*)handle;
    if (!node || node->type != 4 || !item || !json_reserve(node, node->size + 1, 0)) return;
    node->children[node->size++] = (WasmJsonNode*)item;
}
void absolute_json_object_set(void* handle, const char* key, void* item) {
    WasmJsonNode* node = (WasmJsonNode*)handle;
    if (!node || node->type != 5 || !key || !item) return;
    size_t position = 0;
    while (position < node->size && strcmp(node->keys[position], key) < 0)
        ++position;
    if (position < node->size && strcmp(node->keys[position], key) == 0) {
        absolute_json_free(node->children[position]);
        node->children[position] = (WasmJsonNode*)item;
        return;
    }
    if (!json_reserve(node, node->size + 1, 1)) return;
    for (size_t i = node->size; i > position; --i) {
        node->keys[i] = node->keys[i - 1];
        node->children[i] = node->children[i - 1];
    }
    node->keys[position] = json_copy(key);
    node->children[position] = (WasmJsonNode*)item;
    ++node->size;
}

typedef struct JsonBuffer { char* data; size_t size; size_t capacity; } JsonBuffer;
static int json_buffer_reserve(JsonBuffer* buffer, size_t extra) {
    size_t required = buffer->size + extra + 1;
    if (required <= buffer->capacity) return 1;
    size_t capacity = buffer->capacity ? buffer->capacity * 2 : 128;
    while (capacity < required) capacity *= 2;
    char* next = (char*)realloc(buffer->data, capacity);
    if (!next) return 0;
    buffer->data = next; buffer->capacity = capacity; return 1;
}
static void json_buffer_char(JsonBuffer* buffer, char value) {
    if (!json_buffer_reserve(buffer, 1)) return;
    buffer->data[buffer->size++] = value; buffer->data[buffer->size] = '\0';
}
static void json_buffer_text(JsonBuffer* buffer, const char* text) {
    size_t size = strlen(text);
    if (!json_buffer_reserve(buffer, size)) return;
    memcpy(buffer->data + buffer->size, text, size + 1); buffer->size += size;
}
static void json_buffer_indent(JsonBuffer* buffer, int depth) {
    for (int i = 0; i < depth * 2; ++i) json_buffer_char(buffer, ' ');
}
static int json_is_negative_zero(double value) {
    /* No signbit() here; 1/-0.0 is -inf and 1/+0.0 is +inf. */
    return value == 0.0 && (1.0 / value) < 0.0;
}
static void json_stringify_string(JsonBuffer* buffer, const char* text) {
    static const char hex[] = "0123456789abcdef";
    json_buffer_char(buffer, '"');
    for (size_t i = 0; text && text[i]; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c == '"' || c == '\\') { json_buffer_char(buffer, '\\'); json_buffer_char(buffer, (char)c); }
        else if (c == '\b') json_buffer_text(buffer, "\\b");
        else if (c == '\f') json_buffer_text(buffer, "\\f");
        else if (c == '\n') json_buffer_text(buffer, "\\n");
        else if (c == '\r') json_buffer_text(buffer, "\\r");
        else if (c == '\t') json_buffer_text(buffer, "\\t");
        else if (c < 0x20) {
            json_buffer_text(buffer, "\\u00");
            json_buffer_char(buffer, hex[c >> 4]); json_buffer_char(buffer, hex[c & 15]);
        } else json_buffer_char(buffer, (char)c);
    }
    json_buffer_char(buffer, '"');
}
static void json_stringify_node(JsonBuffer* buffer, const WasmJsonNode* node, int pretty, int depth) {
    if (!node || node->type == 0) { json_buffer_text(buffer, "null"); return; }
    if (node->type == 1) { json_buffer_text(buffer, node->boolean ? "true" : "false"); return; }
    if (node->type == 2) {
        /* This shim has no float formatter -- the freestanding snprintf has no
         * %g -- so a number it cannot write in full is written as null rather
         * than as text that is not JSON. What it used to write: 1e19 became
         * "-9223372036854775808." (an undefined cast, then a bare trailing
         * dot) and 1e-7 became "0.", and its own parser refused both.
         * docs/known-defects.md section 25 has what needs building. */
        char number[64];
        double value = node->number;
        int64_t integer;
        double fraction;
        uint64_t decimals;
        size_t length;
        if (value != value || value > 1.7976931348623157e308 ||
            value < -1.7976931348623157e308) {
            json_buffer_text(buffer, "null"); return;   /* nan, inf */
        }
        if (!(value >= -9223372036854775808.0 && value < 9223372036854775808.0)) {
            json_buffer_text(buffer, "null"); return;   /* the cast would be undefined */
        }
        integer = (int64_t)value;
        if ((double)integer == value) {
            if (value == 0.0 && json_is_negative_zero(value))
                json_buffer_text(buffer, "-0");
            else {
                snprintf(number, sizeof(number), "%lld", (long long)integer);
                json_buffer_text(buffer, number);
            }
            return;
        }
        fraction = value - (double)integer;
        if (fraction < 0) fraction = -fraction;
        decimals = (uint64_t)(fraction * 1000000.0 + 0.5);
        if (decimals == 0 || decimals >= 1000000) {
            /* Six decimal places cannot say what this value is. */
            json_buffer_text(buffer, "null"); return;
        }
        if (integer == 0 && value < 0)
            snprintf(number, sizeof(number), "-0.%06llu", (unsigned long long)decimals);
        else
            snprintf(number, sizeof(number), "%lld.%06llu", (long long)integer,
                (unsigned long long)decimals);
        length = strlen(number);
        while (length > 1 && number[length - 1] == '0' && number[length - 2] != '.')
            number[--length] = '\0';
        json_buffer_text(buffer, number); return;
    }
    if (node->type == 3) { json_stringify_string(buffer, node->string); return; }
    int object = node->type == 5;
    json_buffer_char(buffer, object ? '{' : '[');
    for (size_t i = 0; i < node->size; ++i) {
        if (i) json_buffer_char(buffer, ',');
        if (pretty) { json_buffer_char(buffer, '\n'); json_buffer_indent(buffer, depth + 1); }
        if (object) {
            json_stringify_string(buffer, node->keys[i]);
            json_buffer_char(buffer, ':');
            if (pretty) json_buffer_char(buffer, ' ');
        }
        json_stringify_node(buffer, node->children[i], pretty, depth + 1);
    }
    if (pretty && node->size) { json_buffer_char(buffer, '\n'); json_buffer_indent(buffer, depth); }
    json_buffer_char(buffer, object ? '}' : ']');
}

typedef struct JsonParser { const char* text; size_t position; const char* error; } JsonParser;
static void json_skip(JsonParser* parser) {
    char c;
    while ((c = parser->text[parser->position]) == ' ' || c == '\t' || c == '\r' || c == '\n') ++parser->position;
}
static int json_hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static void json_utf8(JsonBuffer* buffer, uint32_t code) {
    if (code <= 0x7f) json_buffer_char(buffer, (char)code);
    else if (code <= 0x7ff) {
        json_buffer_char(buffer, (char)(0xc0 | (code >> 6)));
        json_buffer_char(buffer, (char)(0x80 | (code & 0x3f)));
    } else if (code <= 0xffff) {
        json_buffer_char(buffer, (char)(0xe0 | (code >> 12)));
        json_buffer_char(buffer, (char)(0x80 | ((code >> 6) & 0x3f)));
        json_buffer_char(buffer, (char)(0x80 | (code & 0x3f)));
    } else {
        json_buffer_char(buffer, (char)(0xf0 | (code >> 18)));
        json_buffer_char(buffer, (char)(0x80 | ((code >> 12) & 0x3f)));
        json_buffer_char(buffer, (char)(0x80 | ((code >> 6) & 0x3f)));
        json_buffer_char(buffer, (char)(0x80 | (code & 0x3f)));
    }
}
static char* json_parse_string(JsonParser* parser) {
    if (parser->text[parser->position++] != '"') return NULL;
    JsonBuffer buffer = {0};
    for (;;) {
        unsigned char c = (unsigned char)parser->text[parser->position++];
        if (!c) { parser->error = "unterminated JSON string"; free(buffer.data); return NULL; }
        if (c == '"') return buffer.data ? buffer.data : json_copy("");
        if (c < 0x20) { parser->error = "control character in JSON string"; free(buffer.data); return NULL; }
        if (c != '\\') { json_buffer_char(&buffer, (char)c); continue; }
        c = (unsigned char)parser->text[parser->position++];
        if (c == '"' || c == '\\' || c == '/') json_buffer_char(&buffer, (char)c);
        else if (c == 'b') json_buffer_char(&buffer, '\b');
        else if (c == 'f') json_buffer_char(&buffer, '\f');
        else if (c == 'n') json_buffer_char(&buffer, '\n');
        else if (c == 'r') json_buffer_char(&buffer, '\r');
        else if (c == 't') json_buffer_char(&buffer, '\t');
        else if (c == 'u') {
            uint32_t code = 0;
            for (int i = 0; i < 4; ++i) {
                int digit = json_hex(parser->text[parser->position++]);
                if (digit < 0) { parser->error = "invalid JSON unicode escape"; free(buffer.data); return NULL; }
                code = code * 16 + (uint32_t)digit;
            }
            if (code >= 0xd800 && code <= 0xdbff &&
                parser->text[parser->position] == '\\' && parser->text[parser->position + 1] == 'u') {
                parser->position += 2;
                uint32_t low = 0;
                for (int i = 0; i < 4; ++i) {
                    int digit = json_hex(parser->text[parser->position++]);
                    if (digit < 0) { parser->error = "invalid JSON surrogate"; free(buffer.data); return NULL; }
                    low = low * 16 + (uint32_t)digit;
                }
                if (low < 0xdc00 || low > 0xdfff) { parser->error = "invalid JSON surrogate"; free(buffer.data); return NULL; }
                code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
            }
            /* A half of a pair on its own is not a character, and encoding it
             * would put three bytes of invalid UTF-8 into an Absolute string. */
            if (code >= 0xd800 && code <= 0xdfff) {
                parser->error = "unpaired UTF-16 surrogate in JSON string";
                free(buffer.data); return NULL;
            }
            json_utf8(&buffer, code);
        } else { parser->error = "invalid JSON escape"; free(buffer.data); return NULL; }
    }
}
static WasmJsonNode* json_parse_value(JsonParser* parser);
static WasmJsonNode* json_parse_collection(JsonParser* parser, int object) {
    WasmJsonNode* node = json_create(object ? 5 : 4);
    ++parser->position; json_skip(parser);
    if (parser->text[parser->position] == (object ? '}' : ']')) { ++parser->position; return node; }
    for (;;) {
        char* key = NULL;
        if (object) {
            if (parser->text[parser->position] != '"') { parser->error = "expected JSON object key"; goto fail; }
            key = json_parse_string(parser); if (!key) goto fail;
            json_skip(parser);
            if (parser->text[parser->position++] != ':') { parser->error = "expected ':' after JSON key"; free(key); goto fail; }
        }
        WasmJsonNode* child = json_parse_value(parser);
        if (!child) { free(key); goto fail; }
        if (object) {
            absolute_json_object_set(node, key, child);
            free(key);
        } else {
            if (!json_reserve(node, node->size + 1, 0)) { absolute_json_free(child); parser->error = "out of memory"; goto fail; }
            node->children[node->size++] = child;
        }
        json_skip(parser);
        char c = parser->text[parser->position++];
        if (c == (object ? '}' : ']')) return node;
        if (c != ',') { parser->error = object ? "expected ',' or '}'" : "expected ',' or ']'"; goto fail; }
        json_skip(parser);
    }
fail:
    absolute_json_free(node); return NULL;
}
static WasmJsonNode* json_parse_value(JsonParser* parser) {
    json_skip(parser);
    const char* text = parser->text + parser->position;
    if (text[0] == 'n' && strncmp(text, "null", 4) == 0) { parser->position += 4; return json_create(0); }
    if (text[0] == 't' && strncmp(text, "true", 4) == 0) { parser->position += 4; return absolute_json_create_bool(1); }
    if (text[0] == 'f' && strncmp(text, "false", 5) == 0) { parser->position += 5; return absolute_json_create_bool(0); }
    if (text[0] == '"') {
        char* value = json_parse_string(parser);
        if (!value) return NULL;
        WasmJsonNode* node = json_create(3); node->string = value; return node;
    }
    if (text[0] == '[') return json_parse_collection(parser, 0);
    if (text[0] == '{') return json_parse_collection(parser, 1);
    size_t p = parser->position;
    int sign = 1;
    if (parser->text[p] == '-') { sign = -1; ++p; }
    if (parser->text[p] < '0' || parser->text[p] > '9') { parser->error = "unexpected JSON token"; return NULL; }
    double number = 0.0;
    if (parser->text[p] == '0') ++p;
    else while (parser->text[p] >= '0' && parser->text[p] <= '9') number = number * 10.0 + parser->text[p++] - '0';
    if (parser->text[p] == '.') {
        ++p; double place = 0.1;
        if (parser->text[p] < '0' || parser->text[p] > '9') { parser->error = "invalid JSON number"; return NULL; }
        while (parser->text[p] >= '0' && parser->text[p] <= '9') { number += (parser->text[p++] - '0') * place; place *= 0.1; }
    }
    if (parser->text[p] == 'e' || parser->text[p] == 'E') {
        ++p; int exponent_sign = 1, exponent = 0;
        if (parser->text[p] == '+' || parser->text[p] == '-') { if (parser->text[p++] == '-') exponent_sign = -1; }
        if (parser->text[p] < '0' || parser->text[p] > '9') { parser->error = "invalid JSON exponent"; return NULL; }
        while (parser->text[p] >= '0' && parser->text[p] <= '9') exponent = exponent * 10 + parser->text[p++] - '0';
        while (exponent-- > 0) number = exponent_sign > 0 ? number * 10.0 : number / 10.0;
    }
    parser->position = p;
    return absolute_json_create_number(number * sign);
}
void* absolute_json_parse(const char* text) {
    g_json_error[0] = '\0';
    if (!text || !*text) { fs_copy_path(g_json_error, sizeof(g_json_error), "empty JSON input"); return NULL; }
    JsonParser parser = { text, 0, NULL };
    WasmJsonNode* node = json_parse_value(&parser);
    json_skip(&parser);
    if (node && parser.text[parser.position]) {
        absolute_json_free(node); node = NULL; parser.error = "trailing data after JSON value";
    }
    if (!node) fs_copy_path(g_json_error, sizeof(g_json_error), parser.error ? parser.error : "invalid JSON");
    return node;
}
const char* absolute_json_stringify(const void* handle, int32_t pretty) {
    free(g_json_result); g_json_result = NULL;
    JsonBuffer buffer = {0};
    json_stringify_node(&buffer, (const WasmJsonNode*)handle, pretty != 0, 0);
    if (!buffer.data) buffer.data = json_copy("null");
    g_json_result = buffer.data;
    return g_json_result;
}
const char* absolute_json_get_last_error(void) { return g_json_error; }
