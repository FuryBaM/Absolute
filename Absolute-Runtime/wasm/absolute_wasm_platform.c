/*
 * Virtual FS + env/process stubs + network stubs for Absolute wasm runtime.
 * Included from absolute_wasm_runtime.c (shares heap_alloc / host log).
 */

/* ---------- UTF-8 strings ---------- */

typedef struct WasmStringBuilder {
    char* data;
    size_t length;
    size_t capacity;
} WasmStringBuilder;

static const char* wasm_string_copy_range(const char* text, size_t length) {
    char* result = (char*)heap_alloc(length + 1);
    if (!result)
        return "";
    if (text && length)
        memcpy(result, text, length);
    result[length] = '\0';
    return result;
}

static const char* wasm_string_copy(const char* text) {
    return wasm_string_copy_range(text ? text : "", text ? strlen(text) : 0);
}

static uint32_t wasm_utf8_decode(const char** cursor) {
    if (!cursor || !*cursor || !**cursor)
        return 0;
    const uint8_t* p = (const uint8_t*)*cursor;
    uint32_t cp = *p++;
    if (cp < 0x80) {
        *cursor = (const char*)p;
        return cp;
    }
    if ((cp & 0xe0) == 0xc0) {
        cp &= 0x1f;
        if ((*p & 0xc0) == 0x80)
            cp = (cp << 6) | (*p++ & 0x3f);
    } else if ((cp & 0xf0) == 0xe0) {
        cp &= 0x0f;
        for (int i = 0; i < 2 && (*p & 0xc0) == 0x80; ++i)
            cp = (cp << 6) | (*p++ & 0x3f);
    } else if ((cp & 0xf8) == 0xf0) {
        cp &= 0x07;
        for (int i = 0; i < 3 && (*p & 0xc0) == 0x80; ++i)
            cp = (cp << 6) | (*p++ & 0x3f);
    }
    *cursor = (const char*)p;
    return cp;
}

static size_t wasm_utf8_encode(uint32_t cp, char out[4]) {
    if (cp <= 0x7f) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp <= 0x7ff) {
        out[0] = (char)(0xc0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3f));
        return 2;
    }
    if (cp <= 0xffff) {
        out[0] = (char)(0xe0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
        out[2] = (char)(0x80 | (cp & 0x3f));
        return 3;
    }
    if (cp <= 0x10ffff) {
        out[0] = (char)(0xf0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
        out[3] = (char)(0x80 | (cp & 0x3f));
        return 4;
    }
    return 0;
}

static size_t wasm_utf8_byte_offset(const char* text, int32_t index) {
    if (!text || index <= 0)
        return 0;
    const char* cursor = text;
    int32_t current = 0;
    while (*cursor && current < index) {
        wasm_utf8_decode(&cursor);
        ++current;
    }
    return (size_t)(cursor - text);
}

static int32_t wasm_utf8_index_at_byte(const char* text, size_t byte_offset) {
    if (!text)
        return 0;
    const char* cursor = text;
    const char* end = text + byte_offset;
    int32_t index = 0;
    while (*cursor && cursor < end) {
        wasm_utf8_decode(&cursor);
        ++index;
    }
    return index;
}

static const char* wasm_string_find(const char* text, const char* needle) {
    if (!text || !needle)
        return NULL;
    if (!*needle)
        return text;
    size_t text_length = strlen(text);
    size_t needle_length = strlen(needle);
    if (needle_length > text_length)
        return NULL;
    for (size_t i = 0; i + needle_length <= text_length; ++i) {
        if (memcmp(text + i, needle, needle_length) == 0)
            return text + i;
    }
    return NULL;
}

static uint32_t wasm_string_upper_cp(uint32_t cp) {
    if (cp >= 'a' && cp <= 'z')
        return cp - ('a' - 'A');
    if (cp >= 0x0430 && cp <= 0x044f)
        return cp - 32;
    return cp == 0x0451 ? 0x0401 : cp;
}

static uint32_t wasm_string_lower_cp(uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z')
        return cp + ('a' - 'A');
    if (cp >= 0x0410 && cp <= 0x042f)
        return cp + 32;
    return cp == 0x0401 ? 0x0451 : cp;
}

static const char* wasm_string_map_case(const char* text, int upper) {
    if (!text)
        return wasm_string_copy("");
    size_t capacity = strlen(text) + 1;
    char* result = (char*)heap_alloc(capacity);
    if (!result)
        return "";
    const char* cursor = text;
    size_t length = 0;
    while (*cursor) {
        uint32_t cp = wasm_utf8_decode(&cursor);
        cp = upper ? wasm_string_upper_cp(cp) : wasm_string_lower_cp(cp);
        char encoded[4];
        size_t encoded_length = wasm_utf8_encode(cp, encoded);
        if (length + encoded_length + 1 > capacity) {
            size_t next_capacity = (length + encoded_length + 1) * 2;
            char* next = (char*)heap_alloc(next_capacity);
            if (!next) {
                free(result);
                return "";
            }
            memcpy(next, result, length);
            free(result);
            result = next;
            capacity = next_capacity;
        }
        memcpy(result + length, encoded, encoded_length);
        length += encoded_length;
    }
    result[length] = '\0';
    return result;
}

static int wasm_string_is_space(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static int wasm_builder_reserve(WasmStringBuilder* builder, size_t required) {
    if (!builder)
        return 0;
    if (required <= builder->capacity)
        return 1;
    size_t capacity = builder->capacity ? builder->capacity : 32;
    while (capacity < required)
        capacity *= 2;
    char* next = (char*)heap_alloc(capacity);
    if (!next)
        return 0;
    if (builder->data && builder->length)
        memcpy(next, builder->data, builder->length);
    if (builder->data)
        free(builder->data);
    builder->data = next;
    builder->capacity = capacity;
    builder->data[builder->length] = '\0';
    return 1;
}

static void wasm_builder_append_bytes(WasmStringBuilder* builder, const char* text, size_t length) {
    if (!builder || !text || !wasm_builder_reserve(builder, builder->length + length + 1))
        return;
    memcpy(builder->data + builder->length, text, length);
    builder->length += length;
    builder->data[builder->length] = '\0';
}

static size_t wasm_i64_text(int64_t value, char out[32]) {
    char reverse[32];
    size_t count = 0;
    uint64_t magnitude = value < 0
        ? (uint64_t)(-(value + 1)) + 1u
        : (uint64_t)value;
    do {
        reverse[count++] = (char)('0' + magnitude % 10u);
        magnitude /= 10u;
    } while (magnitude);
    size_t length = 0;
    if (value < 0)
        out[length++] = '-';
    while (count)
        out[length++] = reverse[--count];
    out[length] = '\0';
    return length;
}

int32_t absolute_string_code_point_count(const char* text) {
    int32_t count = 0;
    const char* cursor = text;
    while (cursor && *cursor) {
        wasm_utf8_decode(&cursor);
        ++count;
    }
    return count;
}

int32_t absolute_string_utf8_byte_count(const char* text) {
    return text ? (int32_t)strlen(text) : 0;
}

int32_t absolute_string_code_point_at(const char* text, int32_t index) {
    if (!text || index < 0)
        return -1;
    const char* cursor = text;
    int32_t current = 0;
    while (*cursor) {
        uint32_t cp = wasm_utf8_decode(&cursor);
        if (current++ == index)
            return (int32_t)cp;
    }
    return -1;
}

const char* absolute_string_from_code_point(int32_t code_point) {
    char encoded[4];
    size_t length = code_point < 0 ? 0 : wasm_utf8_encode((uint32_t)code_point, encoded);
    return wasm_string_copy_range(encoded, length);
}

const char* absolute_string_to_upper(const char* text) {
    return wasm_string_map_case(text, 1);
}

const char* absolute_string_to_lower(const char* text) {
    return wasm_string_map_case(text, 0);
}

const char* absolute_string_trim_start(const char* text) {
    if (!text)
        return wasm_string_copy("");
    while (*text && wasm_string_is_space(*text))
        ++text;
    return wasm_string_copy(text);
}

const char* absolute_string_trim_end(const char* text) {
    if (!text)
        return wasm_string_copy("");
    size_t length = strlen(text);
    while (length && wasm_string_is_space(text[length - 1]))
        --length;
    return wasm_string_copy_range(text, length);
}

const char* absolute_string_trim(const char* text) {
    if (!text)
        return wasm_string_copy("");
    while (*text && wasm_string_is_space(*text))
        ++text;
    size_t length = strlen(text);
    while (length && wasm_string_is_space(text[length - 1]))
        --length;
    return wasm_string_copy_range(text, length);
}

int32_t absolute_string_starts_with(const char* text, const char* prefix) {
    if (!text || !prefix)
        return 0;
    size_t length = strlen(prefix);
    return strlen(text) >= length && memcmp(text, prefix, length) == 0;
}

int32_t absolute_string_ends_with(const char* text, const char* suffix) {
    if (!text || !suffix)
        return 0;
    size_t text_length = strlen(text);
    size_t suffix_length = strlen(suffix);
    return text_length >= suffix_length &&
        memcmp(text + text_length - suffix_length, suffix, suffix_length) == 0;
}

const char* absolute_string_concat(const char* left, const char* right) {
    size_t left_length = left ? strlen(left) : 0;
    size_t right_length = right ? strlen(right) : 0;
    char* result = (char*)heap_alloc(left_length + right_length + 1);
    if (!result)
        return "";
    if (left_length)
        memcpy(result, left, left_length);
    if (right_length)
        memcpy(result + left_length, right, right_length);
    result[left_length + right_length] = '\0';
    return result;
}

int32_t absolute_string_parse_int(const char* text) {
    if (!text)
        return 0;
    while (wasm_string_is_space(*text))
        ++text;
    int sign = 1;
    if (*text == '-' || *text == '+')
        sign = *text++ == '-' ? -1 : 1;
    int32_t value = 0;
    int found = 0;
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (*text++ - '0');
        found = 1;
    }
    return found ? value * sign : 0;
}

int32_t absolute_string_contains(const char* text, const char* sub) {
    return wasm_string_find(text, sub) ? 1 : 0;
}

int32_t absolute_string_index_of(const char* text, const char* sub) {
    const char* found = wasm_string_find(text, sub);
    return found ? wasm_utf8_index_at_byte(text, (size_t)(found - text)) : -1;
}

int32_t absolute_string_last_index_of(const char* text, const char* sub) {
    if (!text || !sub || !*sub)
        return -1;
    const char* last = NULL;
    const char* cursor = text;
    while ((cursor = wasm_string_find(cursor, sub)) != NULL) {
        last = cursor;
        ++cursor;
    }
    return last ? wasm_utf8_index_at_byte(text, (size_t)(last - text)) : -1;
}

const char* absolute_string_replace(const char* text, const char* from, const char* to) {
    if (!text)
        return wasm_string_copy("");
    if (!from || !*from)
        return wasm_string_copy(text);
    if (!to)
        to = "";
    size_t from_length = strlen(from);
    size_t to_length = strlen(to);
    size_t matches = 0;
    const char* cursor = text;
    const char* found;
    while ((found = wasm_string_find(cursor, from)) != NULL) {
        ++matches;
        cursor = found + from_length;
    }
    size_t result_length = strlen(text);
    if (to_length >= from_length)
        result_length += matches * (to_length - from_length);
    else
        result_length -= matches * (from_length - to_length);
    char* result = (char*)heap_alloc(result_length + 1);
    if (!result)
        return "";
    cursor = text;
    size_t written = 0;
    while ((found = wasm_string_find(cursor, from)) != NULL) {
        size_t prefix = (size_t)(found - cursor);
        memcpy(result + written, cursor, prefix);
        written += prefix;
        memcpy(result + written, to, to_length);
        written += to_length;
        cursor = found + from_length;
    }
    size_t tail = strlen(cursor);
    memcpy(result + written, cursor, tail);
    result[written + tail] = '\0';
    return result;
}

const char* absolute_string_substring(const char* text, int32_t start, int32_t length) {
    if (!text || start < 0 || length <= 0)
        return wasm_string_copy("");
    int32_t count = absolute_string_code_point_count(text);
    if (start >= count)
        return wasm_string_copy("");
    int32_t remaining = count - start;
    int32_t end = length > remaining ? count : start + length;
    size_t start_byte = wasm_utf8_byte_offset(text, start);
    size_t end_byte = wasm_utf8_byte_offset(text, end);
    return wasm_string_copy_range(text + start_byte, end_byte - start_byte);
}

void* absolute_string_builder_create(void) {
    WasmStringBuilder* builder = (WasmStringBuilder*)heap_alloc(sizeof(WasmStringBuilder));
    if (!builder)
        return NULL;
    builder->data = NULL;
    builder->length = 0;
    builder->capacity = 0;
    wasm_builder_reserve(builder, 1);
    return builder;
}

void absolute_string_builder_free(void* handle) {
    WasmStringBuilder* builder = (WasmStringBuilder*)handle;
    if (!builder)
        return;
    if (builder->data)
        free(builder->data);
    free(builder);
}

void absolute_string_builder_append(void* handle, const char* text) {
    wasm_builder_append_bytes((WasmStringBuilder*)handle, text, text ? strlen(text) : 0);
}

void absolute_string_builder_append_char(void* handle, int32_t code_point) {
    if (code_point < 0)
        return;
    char encoded[4];
    size_t length = wasm_utf8_encode((uint32_t)code_point, encoded);
    wasm_builder_append_bytes((WasmStringBuilder*)handle, encoded, length);
}

void absolute_string_builder_append_int(void* handle, int64_t value) {
    char text[32];
    size_t length = wasm_i64_text(value, text);
    wasm_builder_append_bytes((WasmStringBuilder*)handle, text, length);
}

void absolute_string_builder_append_double(void* handle, double value) {
    char text[64];
    size_t length = 0;
    if (value != value) {
        wasm_builder_append_bytes((WasmStringBuilder*)handle, "nan", 3);
        return;
    }
    if (value < 0) {
        text[length++] = '-';
        value = -value;
    }
    int64_t whole = (int64_t)value;
    length += wasm_i64_text(whole, text + length);
    double fraction = value - (double)whole;
    if (fraction > 0.0000005) {
        text[length++] = '.';
        size_t fraction_start = length;
        for (int i = 0; i < 6; ++i) {
            fraction *= 10.0;
            int digit = (int)fraction;
            text[length++] = (char)('0' + digit);
            fraction -= digit;
        }
        while (length > fraction_start && text[length - 1] == '0')
            --length;
    }
    text[length] = '\0';
    wasm_builder_append_bytes((WasmStringBuilder*)handle, text, length);
}

void absolute_string_builder_append_bool(void* handle, int32_t value) {
    wasm_builder_append_bytes((WasmStringBuilder*)handle,
        value ? "true" : "false", value ? 4 : 5);
}

void absolute_string_builder_clear(void* handle) {
    WasmStringBuilder* builder = (WasmStringBuilder*)handle;
    if (!builder)
        return;
    builder->length = 0;
    if (builder->data)
        builder->data[0] = '\0';
}

int32_t absolute_string_builder_length(void* handle) {
    WasmStringBuilder* builder = (WasmStringBuilder*)handle;
    return builder ? absolute_string_code_point_count(builder->data) : 0;
}

const char* absolute_string_builder_to_string(void* handle) {
    WasmStringBuilder* builder = (WasmStringBuilder*)handle;
    return wasm_string_copy(builder && builder->data ? builder->data : "");
}

/* ---------- virtual filesystem ---------- */

enum {
    ABSOLUTE_VFS_MAX_FILES = 64,
    ABSOLUTE_VFS_MAX_PATH = 256,
    ABSOLUTE_VFS_MAX_OPEN = 16
};

typedef struct VfsNode {
    int used;
    int is_dir;
    char path[ABSOLUTE_VFS_MAX_PATH];
    char* data;
    size_t length;
    size_t capacity;
} VfsNode;

typedef struct VfsHandle {
    int used;
    int node;
    size_t pos;
    int append;
    int writable;
    int eof;
} VfsHandle;

static VfsNode g_vfs[ABSOLUTE_VFS_MAX_FILES];
static VfsHandle g_vfs_handles[ABSOLUTE_VFS_MAX_OPEN];
static char g_fs_error[128];
static char g_fs_cwd[ABSOLUTE_VFS_MAX_PATH] = "/";
static char g_fs_string_scratch[8192];
static char g_fs_path_scratch[ABSOLUTE_VFS_MAX_PATH];

static void fs_set_error(const char* message) {
    size_t i = 0;
    if (!message)
        message = "filesystem error";
    while (message[i] && i + 1 < sizeof(g_fs_error)) {
        g_fs_error[i] = message[i];
        ++i;
    }
    g_fs_error[i] = '\0';
}

static void fs_clear_error(void) {
    g_fs_error[0] = '\0';
}

static int fs_streq(const char* a, const char* b) {
    if (!a || !b)
        return a == b;
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static void fs_copy_path(char* dest, size_t cap, const char* src) {
    size_t i = 0;
    if (!src)
        src = "";
    while (src[i] && i + 1 < cap) {
        dest[i] = src[i];
        ++i;
    }
    dest[i] = '\0';
}

static int fs_is_separator(char value) {
    return value == '/' || value == '\\';
}

static void fs_path_normalize_into(
    const char* path, char* output, size_t capacity, int force_absolute) {
    if (!output || capacity == 0)
        return;

    char source[ABSOLUTE_VFS_MAX_PATH * 2];
    size_t source_length = 0;
    if (!path)
        path = "";
    if (force_absolute && !fs_is_separator(path[0])) {
        while (g_fs_cwd[source_length] &&
            source_length + 1 < sizeof(source)) {
            source[source_length] = g_fs_cwd[source_length];
            ++source_length;
        }
        if (source_length && !fs_is_separator(source[source_length - 1]) &&
            source_length + 1 < sizeof(source)) {
            source[source_length++] = '/';
        }
    }
    for (size_t i = 0; path[i] && source_length + 1 < sizeof(source); ++i)
        source[source_length++] = path[i];
    source[source_length] = '\0';

    const int absolute = fs_is_separator(source[0]);
    size_t segment_positions[64];
    int segment_is_parent[64];
    size_t depth = 0;
    size_t length = 0;
    if (absolute && length + 1 < capacity)
        output[length++] = '/';

    size_t index = 0;
    while (source[index]) {
        while (fs_is_separator(source[index]))
            ++index;
        if (!source[index])
            break;
        size_t start = index;
        while (source[index] && !fs_is_separator(source[index]))
            ++index;
        size_t segment_length = index - start;
        const int is_dot = segment_length == 1 && source[start] == '.';
        const int is_parent = segment_length == 2 &&
            source[start] == '.' && source[start + 1] == '.';
        if (is_dot)
            continue;
        if (is_parent && depth > 0 && !segment_is_parent[depth - 1]) {
            length = segment_positions[--depth];
            continue;
        }
        if (is_parent && absolute && depth == 0)
            continue;

        size_t before = length;
        if (length && output[length - 1] != '/' && length + 1 < capacity)
            output[length++] = '/';
        for (size_t i = 0; i < segment_length && length + 1 < capacity; ++i)
            output[length++] = source[start + i];
        if (depth < sizeof(segment_positions) / sizeof(segment_positions[0])) {
            segment_positions[depth] = before;
            segment_is_parent[depth] = is_parent;
            ++depth;
        }
    }

    if (length == 0 && source_length > 0 && !absolute && capacity > 1)
        output[length++] = '.';
    if (length == 0 && absolute && capacity > 1)
        output[length++] = '/';
    output[length] = '\0';
}

static void fs_path_join_into(
    const char* left, const char* right, char* output, size_t capacity) {
    if (!left)
        left = "";
    if (!right)
        right = "";
    if (fs_is_separator(right[0])) {
        fs_path_normalize_into(right, output, capacity, 0);
        return;
    }
    char combined[ABSOLUTE_VFS_MAX_PATH * 2];
    size_t length = 0;
    for (size_t i = 0; left[i] && length + 1 < sizeof(combined); ++i)
        combined[length++] = left[i];
    if (length && !fs_is_separator(combined[length - 1]) &&
        length + 1 < sizeof(combined)) {
        combined[length++] = '/';
    }
    for (size_t i = 0; right[i] && length + 1 < sizeof(combined); ++i)
        combined[length++] = right[i];
    combined[length] = '\0';
    fs_path_normalize_into(combined, output, capacity, 0);
}

static const char* fs_normalize(const char* path) {
    fs_path_normalize_into(path, g_fs_path_scratch,
        sizeof(g_fs_path_scratch), 1);
    return g_fs_path_scratch;
}

static int fs_find(const char* path) {
    const char* normalized = fs_normalize(path);
    for (int i = 0; i < ABSOLUTE_VFS_MAX_FILES; ++i) {
        if (g_vfs[i].used && fs_streq(g_vfs[i].path, normalized))
            return i;
    }
    return -1;
}

static int fs_alloc_node(void) {
    for (int i = 0; i < ABSOLUTE_VFS_MAX_FILES; ++i) {
        if (!g_vfs[i].used) {
            g_vfs[i].used = 1;
            g_vfs[i].is_dir = 0;
            g_vfs[i].data = NULL;
            g_vfs[i].length = 0;
            g_vfs[i].capacity = 0;
            g_vfs[i].path[0] = '\0';
            return i;
        }
    }
    return -1;
}

static int fs_ensure_parent_dirs(const char* path) {
    /* Treat missing parents as auto-created directories for write_text. */
    (void)path;
    return 1;
}

int32_t absolute_fs_exists(const char* path) {
    fs_clear_error();
    return fs_find(path) >= 0 ? 1 : 0;
}

int32_t absolute_fs_is_file(const char* path) {
    fs_clear_error();
    int id = fs_find(path);
    return (id >= 0 && !g_vfs[id].is_dir) ? 1 : 0;
}

int32_t absolute_fs_is_directory(const char* path) {
    fs_clear_error();
    int id = fs_find(path);
    if (id < 0) {
        /* cwd always exists */
        if (path && (fs_streq(path, "/") || fs_streq(path, g_fs_cwd)))
            return 1;
        return 0;
    }
    return g_vfs[id].is_dir ? 1 : 0;
}

int64_t absolute_fs_file_size(const char* path) {
    int id = fs_find(path);
    if (id < 0 || g_vfs[id].is_dir) {
        fs_set_error("file not found");
        return -1;
    }
    fs_clear_error();
    return (int64_t)g_vfs[id].length;
}

int32_t absolute_fs_create_directories(const char* path) {
    if (!path || !*path) {
        fs_set_error("directory path is empty");
        return 0;
    }
    int existing = fs_find(path);
    if (existing >= 0) {
        fs_clear_error();
        return g_vfs[existing].is_dir ? 1 : 0;
    }
    int id = fs_alloc_node();
    if (id < 0) {
        fs_set_error("vfs full");
        return 0;
    }
    fs_copy_path(g_vfs[id].path, sizeof(g_vfs[id].path), fs_normalize(path));
    g_vfs[id].is_dir = 1;
    fs_clear_error();
    return 1;
}

int32_t absolute_fs_remove(const char* path) {
    int id = fs_find(path);
    if (id < 0) {
        fs_set_error("path not found");
        return 0;
    }
    if (g_vfs[id].data)
        free(g_vfs[id].data);
    g_vfs[id].used = 0;
    g_vfs[id].data = NULL;
    fs_clear_error();
    return 1;
}

int32_t absolute_fs_rename(const char* source, const char* destination) {
    int id = fs_find(source);
    if (id < 0 || !destination) {
        fs_set_error("rename path missing");
        return 0;
    }
    int dest = fs_find(destination);
    if (dest >= 0)
        absolute_fs_remove(destination);
    fs_copy_path(g_vfs[id].path, sizeof(g_vfs[id].path), fs_normalize(destination));
    fs_clear_error();
    return 1;
}

int32_t absolute_fs_copy_file(const char* source, const char* destination, int32_t overwrite) {
    int id = fs_find(source);
    if (id < 0 || g_vfs[id].is_dir || !destination) {
        fs_set_error("copy source missing");
        return 0;
    }
    int dest = fs_find(destination);
    if (dest >= 0 && !overwrite) {
        fs_set_error("destination exists");
        return 0;
    }
    if (dest < 0) {
        dest = fs_alloc_node();
        if (dest < 0) {
            fs_set_error("vfs full");
            return 0;
        }
        fs_copy_path(g_vfs[dest].path, sizeof(g_vfs[dest].path), fs_normalize(destination));
    } else if (g_vfs[dest].data) {
        free(g_vfs[dest].data);
        g_vfs[dest].data = NULL;
    }
    g_vfs[dest].is_dir = 0;
    g_vfs[dest].length = g_vfs[id].length;
    g_vfs[dest].capacity = g_vfs[id].length + 1;
    g_vfs[dest].data = (char*)heap_alloc(g_vfs[dest].capacity);
    if (!g_vfs[dest].data) {
        fs_set_error("out of memory");
        g_vfs[dest].used = 0;
        return 0;
    }
    for (size_t i = 0; i < g_vfs[id].length; ++i)
        g_vfs[dest].data[i] = g_vfs[id].data[i];
    g_vfs[dest].data[g_vfs[id].length] = '\0';
    fs_clear_error();
    return 1;
}

const char* absolute_fs_current_directory(void) {
    fs_clear_error();
    return g_fs_cwd;
}

const char* absolute_fs_absolute(const char* path) {
    fs_clear_error();
    fs_path_normalize_into(path, g_fs_string_scratch,
        sizeof(g_fs_string_scratch), 1);
    return g_fs_string_scratch;
}

const char* absolute_fs_path_separator(void) {
    return "/";
}

const char* absolute_fs_path_join(const char* left, const char* right) {
    fs_clear_error();
    fs_path_join_into(left, right, g_fs_string_scratch,
        sizeof(g_fs_string_scratch));
    return g_fs_string_scratch;
}

const char* absolute_fs_path_normalize(const char* path) {
    fs_clear_error();
    fs_path_normalize_into(path, g_fs_string_scratch,
        sizeof(g_fs_string_scratch), 0);
    return g_fs_string_scratch;
}

const char* absolute_fs_path_parent(const char* path) {
    char normalized[ABSOLUTE_VFS_MAX_PATH];
    fs_path_normalize_into(path, normalized, sizeof(normalized), 0);
    size_t length = strlen(normalized);
    while (length > 1 && normalized[length - 1] == '/')
        --length;
    while (length > 0 && normalized[length - 1] != '/')
        --length;
    if (length > 1)
        --length;
    if (length == 0 && normalized[0] == '/')
        length = 1;
    fs_clear_error();
    return wasm_string_copy_range(normalized, length);
}

const char* absolute_fs_path_filename(const char* path) {
    char normalized[ABSOLUTE_VFS_MAX_PATH];
    fs_path_normalize_into(path, normalized, sizeof(normalized), 0);
    size_t length = strlen(normalized);
    size_t start = length;
    while (start > 0 && normalized[start - 1] != '/')
        --start;
    fs_clear_error();
    return wasm_string_copy_range(normalized + start, length - start);
}

const char* absolute_fs_path_stem(const char* path) {
    const char* name = absolute_fs_path_filename(path);
    if (fs_streq(name, ".") || fs_streq(name, ".."))
        return wasm_string_copy(name);
    size_t length = strlen(name);
    size_t dot = length;
    while (dot > 0 && name[dot - 1] != '.')
        --dot;
    if (dot <= 1)
        dot = length + 1;
    fs_clear_error();
    return wasm_string_copy_range(name, dot - 1);
}

const char* absolute_fs_path_extension(const char* path) {
    const char* name = absolute_fs_path_filename(path);
    if (fs_streq(name, ".") || fs_streq(name, ".."))
        return wasm_string_copy("");
    size_t length = strlen(name);
    size_t dot = length;
    while (dot > 0 && name[dot - 1] != '.')
        --dot;
    fs_clear_error();
    if (dot <= 1)
        return wasm_string_copy("");
    return wasm_string_copy_range(name + dot - 1, length - dot + 1);
}

int32_t absolute_fs_path_is_absolute(const char* path) {
    fs_clear_error();
    return path && fs_is_separator(path[0]) ? 1 : 0;
}

const char* absolute_fs_read_text(const char* path) {
    int id = fs_find(path);
    if (id < 0 || g_vfs[id].is_dir || !g_vfs[id].data) {
        fs_set_error("file not found");
        return "";
    }
    fs_clear_error();
    return g_vfs[id].data;
}

int32_t absolute_fs_write_text(const char* path, const char* text, int32_t append) {
    if (!path || !*path) {
        fs_set_error("path is empty");
        return 0;
    }
    if (!text)
        text = "";
    fs_ensure_parent_dirs(path);
    int id = fs_find(path);
    if (id < 0) {
        id = fs_alloc_node();
        if (id < 0) {
            fs_set_error("vfs full");
            return 0;
        }
        fs_copy_path(g_vfs[id].path, sizeof(g_vfs[id].path), fs_normalize(path));
        g_vfs[id].is_dir = 0;
    } else if (g_vfs[id].is_dir) {
        fs_set_error("path is a directory");
        return 0;
    }
    size_t text_len = 0;
    while (text[text_len])
        ++text_len;
    size_t base = (append && g_vfs[id].data) ? g_vfs[id].length : 0;
    size_t need = base + text_len + 1;
    char* buffer = (char*)heap_alloc(need);
    if (!buffer) {
        fs_set_error("out of memory");
        return 0;
    }
    for (size_t i = 0; i < base; ++i)
        buffer[i] = g_vfs[id].data[i];
    for (size_t i = 0; i < text_len; ++i)
        buffer[base + i] = text[i];
    buffer[base + text_len] = '\0';
    if (g_vfs[id].data)
        free(g_vfs[id].data);
    g_vfs[id].data = buffer;
    g_vfs[id].length = base + text_len;
    g_vfs[id].capacity = need;
    fs_clear_error();
    return 1;
}

void* absolute_fs_file_open(const char* path, const char* mode) {
    if (!path || !mode) {
        fs_set_error("open arguments null");
        return NULL;
    }
    int writable = 0;
    int append = 0;
    int create = 0;
    if (mode[0] == 'w') {
        writable = 1;
        create = 1;
    } else if (mode[0] == 'a') {
        writable = 1;
        append = 1;
        create = 1;
    } else if (mode[0] != 'r') {
        fs_set_error("unsupported mode");
        return NULL;
    }
    int id = fs_find(path);
    if (id < 0) {
        if (!create) {
            fs_set_error("file not found");
            return NULL;
        }
        if (!absolute_fs_write_text(path, "", 0))
            return NULL;
        id = fs_find(path);
    }
    if (id < 0 || g_vfs[id].is_dir) {
        fs_set_error("not a file");
        return NULL;
    }
    for (int h = 0; h < ABSOLUTE_VFS_MAX_OPEN; ++h) {
        if (!g_vfs_handles[h].used) {
            g_vfs_handles[h].used = 1;
            g_vfs_handles[h].node = id;
            g_vfs_handles[h].pos = append ? g_vfs[id].length : 0;
            g_vfs_handles[h].append = append;
            g_vfs_handles[h].writable = writable;
            g_vfs_handles[h].eof = 0;
            fs_clear_error();
            return &g_vfs_handles[h];
        }
    }
    fs_set_error("too many open files");
    return NULL;
}

void absolute_fs_file_close(void* handle) {
    VfsHandle* file = (VfsHandle*)handle;
    if (!file)
        return;
    file->used = 0;
}

const char* absolute_fs_file_read_line(void* handle) {
    VfsHandle* file = (VfsHandle*)handle;
    if (!file || !file->used) {
        fs_set_error("invalid handle");
        return "";
    }
    VfsNode* node = &g_vfs[file->node];
    if (!node->data || file->pos >= node->length) {
        file->eof = 1;
        fs_clear_error();
        return "";
    }
    size_t start = file->pos;
    size_t end = start;
    while (end < node->length && node->data[end] != '\n')
        ++end;
    size_t n = end - start;
    if (n >= sizeof(g_fs_string_scratch))
        n = sizeof(g_fs_string_scratch) - 1;
    for (size_t i = 0; i < n; ++i)
        g_fs_string_scratch[i] = node->data[start + i];
    g_fs_string_scratch[n] = '\0';
    file->pos = end < node->length ? end + 1 : end;
    if (file->pos >= node->length)
        file->eof = 1;
    fs_clear_error();
    return g_fs_string_scratch;
}

const char* absolute_fs_file_read_all(void* handle) {
    VfsHandle* file = (VfsHandle*)handle;
    if (!file || !file->used) {
        fs_set_error("invalid handle");
        return "";
    }
    VfsNode* node = &g_vfs[file->node];
    fs_clear_error();
    file->pos = node->length;
    file->eof = 1;
    return node->data ? node->data : "";
}

int32_t absolute_fs_file_write(void* handle, const char* text) {
    VfsHandle* file = (VfsHandle*)handle;
    if (!file || !file->used || !file->writable) {
        fs_set_error("invalid handle");
        return 0;
    }
    VfsNode* node = &g_vfs[file->node];
    if (!text)
        text = "";
    size_t text_len = 0;
    while (text[text_len])
        ++text_len;
    size_t base = file->append ? node->length : file->pos;
    size_t need = base + text_len + 1;
    char* buffer = (char*)heap_alloc(need > node->length ? need : node->length + 1);
    if (!buffer) {
        fs_set_error("out of memory");
        return 0;
    }
    size_t copy_len = base < node->length ? base : node->length;
    for (size_t i = 0; i < copy_len; ++i)
        buffer[i] = node->data ? node->data[i] : 0;
    for (size_t i = 0; i < text_len; ++i)
        buffer[base + i] = text[i];
    buffer[base + text_len] = '\0';
    if (node->data)
        free(node->data);
    node->data = buffer;
    node->length = base + text_len;
    node->capacity = need;
    file->pos = node->length;
    fs_clear_error();
    return 1;
}

int32_t absolute_fs_file_flush(void* handle) {
    (void)handle;
    fs_clear_error();
    return 1;
}

int32_t absolute_fs_file_eof(void* handle) {
    VfsHandle* file = (VfsHandle*)handle;
    if (!file || !file->used)
        return 1;
    return file->eof ? 1 : 0;
}

const char* absolute_fs_error(void) {
    return g_fs_error;
}

/* ---------- env ---------- */

enum { ABSOLUTE_ENV_MAX = 128, ABSOLUTE_ENV_KEY = 96, ABSOLUTE_ENV_VAL = 512 };

#if defined(ABSOLUTE_WASM_USE_WASI)
static void wasi_seed_environ(void);
#endif

static struct {
    int used;
    char key[ABSOLUTE_ENV_KEY];
    char value[ABSOLUTE_ENV_VAL];
} g_env[ABSOLUTE_ENV_MAX];
static char g_env_error[64];
static char g_env_scratch[ABSOLUTE_ENV_VAL];

static void env_clear_error(void) { g_env_error[0] = '\0'; }
static void env_set_error(const char* m) {
    size_t i = 0;
    while (m && m[i] && i + 1 < sizeof(g_env_error)) {
        g_env_error[i] = m[i];
        ++i;
    }
    g_env_error[i] = '\0';
}

const char* absolute_env_error(void) { return g_env_error; }

int32_t absolute_env_has(const char* name) {
#if defined(ABSOLUTE_WASM_USE_WASI)
    wasi_seed_environ();
#endif
    if (!name)
        return 0;
    for (int i = 0; i < ABSOLUTE_ENV_MAX; ++i)
        if (g_env[i].used && fs_streq(g_env[i].key, name))
            return 1;
    return 0;
}

const char* absolute_env_get(const char* name) {
#if defined(ABSOLUTE_WASM_USE_WASI)
    wasi_seed_environ();
#endif
    env_clear_error();
    if (!name) {
        env_set_error("null name");
        return "";
    }
    for (int i = 0; i < ABSOLUTE_ENV_MAX; ++i) {
        if (g_env[i].used && fs_streq(g_env[i].key, name)) {
            fs_copy_path(g_env_scratch, sizeof(g_env_scratch), g_env[i].value);
            return g_env_scratch;
        }
    }
    env_set_error("not found");
    return "";
}

int32_t absolute_env_set(const char* name, const char* value) {
    if (!name || !*name) {
        env_set_error("null name");
        return 0;
    }
    if (!value)
        value = "";
    for (int i = 0; i < ABSOLUTE_ENV_MAX; ++i) {
        if (g_env[i].used && fs_streq(g_env[i].key, name)) {
            fs_copy_path(g_env[i].value, sizeof(g_env[i].value), value);
            env_clear_error();
            return 1;
        }
    }
    for (int i = 0; i < ABSOLUTE_ENV_MAX; ++i) {
        if (!g_env[i].used) {
            g_env[i].used = 1;
            fs_copy_path(g_env[i].key, sizeof(g_env[i].key), name);
            fs_copy_path(g_env[i].value, sizeof(g_env[i].value), value);
            env_clear_error();
            return 1;
        }
    }
    env_set_error("env full");
    return 0;
}

int32_t absolute_env_remove(const char* name) {
    if (!name)
        return 0;
    for (int i = 0; i < ABSOLUTE_ENV_MAX; ++i) {
        if (g_env[i].used && fs_streq(g_env[i].key, name)) {
            g_env[i].used = 0;
            env_clear_error();
            return 1;
        }
    }
    env_set_error("not found");
    return 0;
}

/* ---------- process ---------- */

static char g_process_error[64];
static char g_process_exe[] = "absolutec-wasm";
static char g_process_host[] = "wasm";
static char g_process_arg_scratch[1024];

#if !defined(ABSOLUTE_WASM_USE_WASI)
__attribute__((import_module("env"), import_name("absolute_time_unix_nanos")))
int64_t absolute_host_time_unix_nanos(void);
__attribute__((import_module("env"), import_name("absolute_time_monotonic_nanos")))
int64_t absolute_host_time_monotonic_nanos(void);
__attribute__((import_module("env"), import_name("absolute_random_entropy")))
uint64_t absolute_host_random_entropy(void);
__attribute__((import_module("env"), import_name("absolute_process_args_count")))
int32_t absolute_host_process_args_count(void);
__attribute__((import_module("env"), import_name("absolute_process_arg_copy")))
int32_t absolute_host_process_arg_copy(int32_t index, char* output, int32_t capacity);
#endif

#if defined(ABSOLUTE_WASM_USE_WASI)
enum { ABSOLUTE_WASI_MAX_ARGS = 64, ABSOLUTE_WASI_ARGV_BUF = 4096 };
static int g_wasi_args_ready = 0;
static int32_t g_wasi_argc = 0;
static char g_wasi_argv_storage[ABSOLUTE_WASI_ARGV_BUF];
static char* g_wasi_argv[ABSOLUTE_WASI_MAX_ARGS];

static void wasi_load_args(void) {
    if (g_wasi_args_ready)
        return;
    g_wasi_args_ready = 1;
    g_wasi_argc = 0;
    size_t argc = 0;
    size_t buf_size = 0;
    if (absolute_wasi_args_sizes_get(&argc, &buf_size) != 0)
        return;
    if (argc == 0 || buf_size == 0)
        return;
    if (argc > (size_t)ABSOLUTE_WASI_MAX_ARGS)
        argc = ABSOLUTE_WASI_MAX_ARGS;
    if (buf_size >= sizeof(g_wasi_argv_storage))
        buf_size = sizeof(g_wasi_argv_storage) - 1;
    for (size_t i = 0; i < argc; ++i)
        g_wasi_argv[i] = NULL;
    if (absolute_wasi_args_get((uint8_t**)g_wasi_argv, (uint8_t*)g_wasi_argv_storage) != 0) {
        g_wasi_argc = 0;
        return;
    }
    g_wasi_argc = (int32_t)argc;
}

static void wasi_seed_environ(void) {
    static int seeded = 0;
    if (seeded)
        return;
    seeded = 1;
    size_t count = 0;
    size_t buf_size = 0;
    if (absolute_wasi_environ_sizes_get(&count, &buf_size) != 0 || count == 0 || buf_size == 0)
        return;
    /* Cap to Absolute env table + buffer limits. */
    if (count > (size_t)ABSOLUTE_ENV_MAX)
        count = (size_t)ABSOLUTE_ENV_MAX;
    if (buf_size > 65536)
        buf_size = 65536;
    uint8_t* buf = (uint8_t*)heap_alloc(buf_size + 8);
    uint8_t** envp = (uint8_t**)heap_alloc(count * sizeof(uint8_t*));
    if (!buf || !envp) {
        if (buf) free(buf);
        if (envp) free(envp);
        return;
    }
    if (absolute_wasi_environ_get(envp, buf) == 0) {
        for (size_t i = 0; i < count; ++i) {
            if (!envp[i])
                continue;
            const char* entry = (const char*)envp[i];
            size_t eq = 0;
            while (entry[eq] && entry[eq] != '=')
                ++eq;
            if (!entry[eq])
                continue;
            char key[ABSOLUTE_ENV_KEY];
            char value[ABSOLUTE_ENV_VAL];
            size_t k = 0;
            while (k < eq && k + 1 < sizeof(key)) {
                key[k] = entry[k];
                ++k;
            }
            key[k] = '\0';
            fs_copy_path(value, sizeof(value), entry + eq + 1);
            if (!absolute_env_set(key, value))
                break; /* table full */
        }
    }
    free(buf);
    free(envp);
}
#endif

const char* absolute_process_error(void) { return g_process_error; }
int32_t absolute_process_pid(void) { return 1; }
void absolute_process_exit(int32_t code) {
#if defined(ABSOLUTE_WASM_USE_WASI)
    absolute_wasi_proc_exit(code);
#else
    (void)code;
    abort();
#endif
}
void absolute_process_abort(void) {
#if defined(ABSOLUTE_WASM_USE_WASI)
    absolute_wasi_proc_exit(1);
#else
    abort();
#endif
}
const char* absolute_process_executable_path(void) {
#if defined(ABSOLUTE_WASM_USE_WASI)
    wasi_load_args();
    if (g_wasi_argc > 0 && g_wasi_argv[0] && g_wasi_argv[0][0])
        return g_wasi_argv[0];
#else
    if (absolute_host_process_arg_copy(0, g_process_arg_scratch,
            (int32_t)sizeof(g_process_arg_scratch)) >= 0)
        return g_process_arg_scratch;
#endif
    return g_process_exe;
}
const char* absolute_process_hostname(void) { return g_process_host; }
int32_t absolute_process_run(const char* command) {
    (void)command;
    g_process_error[0] = '\0';
    fs_copy_path(g_process_error, sizeof(g_process_error), "process run unavailable on wasm");
    return -1;
}
const char* absolute_process_run_capture(const char* command) {
    (void)command;
    return "";
}
int32_t absolute_process_args_count(void) {
#if defined(ABSOLUTE_WASM_USE_WASI)
    wasi_load_args();
    return g_wasi_argc;
#else
    return absolute_host_process_args_count();
#endif
}
const char* absolute_process_arg_at(int32_t index) {
#if defined(ABSOLUTE_WASM_USE_WASI)
    wasi_load_args();
    if (index < 0 || index >= g_wasi_argc || !g_wasi_argv[index])
        return "";
    return g_wasi_argv[index];
#else
    if (index < 0 || absolute_host_process_arg_copy(index, g_process_arg_scratch,
            (int32_t)sizeof(g_process_arg_scratch)) < 0)
        return "";
    return g_process_arg_scratch;
#endif
}

/* ---------- network (host-backed TCP when not pure WASI) ---------- */

static char g_net_error[96];
static char g_net_recv_scratch[8192];
static char g_net_resolve_scratch[256];

#if !defined(ABSOLUTE_WASM_USE_WASI)
/* Host socket table lives in tools/absolute-wasm-host.js. Handles are positive ids. */
__attribute__((import_module("env"), import_name("absolute_tcp_connect")))
int32_t absolute_host_tcp_connect(const char* host, int32_t port);
__attribute__((import_module("env"), import_name("absolute_tcp_listen")))
int32_t absolute_host_tcp_listen(const char* host, int32_t port, int32_t backlog);
__attribute__((import_module("env"), import_name("absolute_tcp_accept")))
int32_t absolute_host_tcp_accept(int32_t handle);
__attribute__((import_module("env"), import_name("absolute_tcp_send")))
int32_t absolute_host_tcp_send(int32_t handle, const char* text);
__attribute__((import_module("env"), import_name("absolute_tcp_receive")))
int32_t absolute_host_tcp_receive(int32_t handle, uint8_t* out, int32_t max_bytes);
__attribute__((import_module("env"), import_name("absolute_tcp_close")))
void absolute_host_tcp_close(int32_t handle);
__attribute__((import_module("env"), import_name("absolute_tcp_port")))
int32_t absolute_host_tcp_port(int32_t handle);
#else
static int32_t absolute_host_tcp_connect(const char* host, int32_t port) {
    (void)host; (void)port; return -1;
}
static int32_t absolute_host_tcp_listen(const char* host, int32_t port, int32_t backlog) {
    (void)host; (void)port; (void)backlog; return -1;
}
static int32_t absolute_host_tcp_accept(int32_t handle) {
    (void)handle; return -1;
}
static int32_t absolute_host_tcp_send(int32_t handle, const char* text) {
    (void)handle; (void)text; return 0;
}
static int32_t absolute_host_tcp_receive(int32_t handle, uint8_t* out, int32_t max_bytes) {
    (void)handle; (void)out; (void)max_bytes; return -1;
}
static void absolute_host_tcp_close(int32_t handle) { (void)handle; }
static int32_t absolute_host_tcp_port(int32_t handle) {
    (void)handle; return -1;
}
#endif

typedef struct NetHandle {
    int32_t id;
} NetHandle;

static void net_set_error(const char* message) {
    size_t i = 0;
    if (!message) message = "network error";
    while (message[i] && i + 1 < sizeof(g_net_error)) {
        g_net_error[i] = message[i];
        ++i;
    }
    g_net_error[i] = '\0';
}

const char* absolute_net_error(void) { return g_net_error; }

void* absolute_net_tcp_connect(const char* host, int32_t port) {
    int32_t id = absolute_host_tcp_connect(host, port);
    if (id < 0) {
        net_set_error("tcp connect failed");
        return NULL;
    }
    NetHandle* handle = (NetHandle*)heap_alloc(sizeof(NetHandle));
    if (!handle) {
        absolute_host_tcp_close(id);
        net_set_error("out of memory");
        return NULL;
    }
    handle->id = id;
    g_net_error[0] = '\0';
    return handle;
}

void* absolute_net_tcp_listen(const char* host, int32_t port, int32_t backlog) {
    int32_t id = absolute_host_tcp_listen(host ? host : "127.0.0.1", port, backlog);
    if (id < 0) {
        net_set_error("tcp listen failed");
        return NULL;
    }
    NetHandle* handle = (NetHandle*)heap_alloc(sizeof(NetHandle));
    if (!handle) {
        absolute_host_tcp_close(id);
        net_set_error("out of memory");
        return NULL;
    }
    handle->id = id;
    g_net_error[0] = '\0';
    return handle;
}

void* absolute_net_tcp_accept(void* handle) {
    NetHandle* server = (NetHandle*)handle;
    if (!server) {
        net_set_error("null listen handle");
        return NULL;
    }
    int32_t id = absolute_host_tcp_accept(server->id);
    if (id < 0) {
        net_set_error("tcp accept failed");
        return NULL;
    }
    NetHandle* client = (NetHandle*)heap_alloc(sizeof(NetHandle));
    if (!client) {
        absolute_host_tcp_close(id);
        net_set_error("out of memory");
        return NULL;
    }
    client->id = id;
    g_net_error[0] = '\0';
    return client;
}

int32_t absolute_net_tcp_send(void* handle, const char* text) {
    NetHandle* socket = (NetHandle*)handle;
    if (!socket) {
        net_set_error("null socket");
        return 0;
    }
    int32_t n = absolute_host_tcp_send(socket->id, text ? text : "");
    if (n <= 0) {
        net_set_error("tcp send failed");
        return 0;
    }
    g_net_error[0] = '\0';
    return n;
}

const char* absolute_net_tcp_receive(void* handle, int32_t maximumBytes) {
    NetHandle* socket = (NetHandle*)handle;
    if (!socket) {
        net_set_error("null socket");
        return "";
    }
    int32_t cap = maximumBytes;
    if (cap <= 0 || cap >= (int32_t)sizeof(g_net_recv_scratch))
        cap = (int32_t)sizeof(g_net_recv_scratch) - 1;
    int32_t n = absolute_host_tcp_receive(socket->id, (uint8_t*)g_net_recv_scratch, cap);
    if (n < 0) {
        net_set_error("tcp receive failed");
        g_net_recv_scratch[0] = '\0';
        return g_net_recv_scratch;
    }
    g_net_recv_scratch[n] = '\0';
    g_net_error[0] = '\0';
    return g_net_recv_scratch;
}

int32_t absolute_net_tcp_set_timeout(void* handle, int32_t milliseconds) {
    (void)handle;
    (void)milliseconds;
    g_net_error[0] = '\0';
    return 1;
}

int32_t absolute_net_tcp_port(void* handle) {
    NetHandle* socket = (NetHandle*)handle;
    if (!socket)
        return -1;
    return absolute_host_tcp_port(socket->id);
}

void absolute_net_tcp_close(void* handle);

void absolute_net_tcp_shutdown(void* handle) {
    absolute_net_tcp_close(handle);
}

void absolute_net_tcp_close(void* handle) {
    NetHandle* socket = (NetHandle*)handle;
    if (!socket)
        return;
    absolute_host_tcp_close(socket->id);
    free(socket);
}

const char* absolute_net_resolve_host(const char* hostname) {
    if (!hostname) {
        net_set_error("null hostname");
        return "";
    }
    /* Host may rewrite; default echo the name for mock hosts. */
    fs_copy_path(g_net_resolve_scratch, sizeof(g_net_resolve_scratch), hostname);
    g_net_error[0] = '\0';
    return g_net_resolve_scratch;
}

void* absolute_net_udp_bind(const char* host, int32_t port) {
    (void)host;
    (void)port;
    net_set_error("udp is not available on wasm");
    return NULL;
}
int32_t absolute_net_udp_send_to(void* handle, const char* host, int32_t port, const char* text) {
    (void)handle; (void)host; (void)port; (void)text;
    net_set_error("udp is not available on wasm");
    return 0;
}
const char* absolute_net_udp_receive_from(void* handle, int32_t maxBytes) {
    (void)handle; (void)maxBytes;
    return "";
}
void absolute_net_udp_close(void* handle) { (void)handle; }

/* ---------- channels ---------- */

typedef struct ChannelNode {
    int64_t value;
    struct ChannelNode* next;
} ChannelNode;

typedef struct ChannelImpl {
    ChannelNode* first;
    ChannelNode* last;
    int32_t count;
    int32_t capacity;
    uint8_t closed;
} ChannelImpl;

void* absolute_channel_create(int32_t capacity) {
    ChannelImpl* channel = (ChannelImpl*)heap_alloc(sizeof(ChannelImpl));
    if (!channel)
        abort();
    memset(channel, 0, sizeof(ChannelImpl));
    channel->capacity = capacity > 0 ? capacity : 0;
    return channel;
}

int32_t absolute_channel_try_send(void* channelPtr, int64_t value) {
    ChannelImpl* channel = (ChannelImpl*)channelPtr;
    if (!channel || channel->closed)
        return 0;
    if (channel->capacity > 0 && channel->count >= channel->capacity)
        return 0;
    ChannelNode* node = (ChannelNode*)heap_alloc(sizeof(ChannelNode));
    if (!node)
        abort();
    node->value = value;
    node->next = NULL;
    if (channel->last)
        channel->last->next = node;
    else
        channel->first = node;
    channel->last = node;
    channel->count += 1;
    return 1;
}

int32_t absolute_channel_send(void* channelPtr, int64_t value) {
    /* A wasm instance cannot synchronously wait for another worker instance. */
    return absolute_channel_try_send(channelPtr, value);
}

int32_t absolute_channel_try_receive(void* channelPtr, int64_t* outValue) {
    ChannelImpl* channel = (ChannelImpl*)channelPtr;
    if (!channel || !outValue || !channel->first)
        return 0;
    ChannelNode* node = channel->first;
    *outValue = node->value;
    channel->first = node->next;
    if (!channel->first)
        channel->last = NULL;
    channel->count -= 1;
    free(node);
    return 1;
}

int32_t absolute_channel_receive_checked(void* channelPtr, int64_t* outValue) {
    return absolute_channel_try_receive(channelPtr, outValue);
}

int64_t absolute_channel_receive(void* channelPtr) {
    int64_t value = 0;
    absolute_channel_receive_checked(channelPtr, &value);
    return value;
}

void absolute_channel_close(void* channelPtr) {
    ChannelImpl* channel = (ChannelImpl*)channelPtr;
    if (channel)
        channel->closed = 1;
}

int32_t absolute_channel_is_closed(void* channelPtr) {
    ChannelImpl* channel = (ChannelImpl*)channelPtr;
    return !channel || channel->closed;
}

int32_t absolute_channel_count(void* channelPtr) {
    ChannelImpl* channel = (ChannelImpl*)channelPtr;
    return channel ? channel->count : 0;
}

void absolute_channel_destroy(void* channelPtr) {
    ChannelImpl* channel = (ChannelImpl*)channelPtr;
    if (!channel)
        return;
    ChannelNode* node = channel->first;
    while (node) {
        ChannelNode* next = node->next;
        free(node);
        node = next;
    }
    free(channel);
}

/* ---------- transfer capsules ---------- */

typedef struct CapsuleImpl {
    uint64_t handle;
    uint8_t transferred;
    void (*deleter_fn)(void*);
} CapsuleImpl;

void* absolute_capsule_create_typed(
    uint64_t handle, void (*deleter)(void*)) {
    CapsuleImpl* capsule = (CapsuleImpl*)heap_alloc(sizeof(CapsuleImpl));
    if (!capsule)
        abort();
    capsule->handle = absolute_managed_transfer(handle);
    capsule->transferred = 0;
    capsule->deleter_fn = deleter;
    return capsule;
}

void* absolute_capsule_create(uint64_t handle) {
    return absolute_capsule_create_typed(handle, NULL);
}

uint64_t absolute_capsule_unwrap(void* capsulePtr) {
    if (!capsulePtr)
        abort();
    CapsuleImpl* capsule = (CapsuleImpl*)capsulePtr;
    if (capsule->transferred)
        abort();
    capsule->transferred = 1;
    uint64_t handle = capsule->handle;
    free(capsule);
    return handle;
}

void absolute_capsule_destroy(void* capsulePtr) {
    if (!capsulePtr)
        return;
    CapsuleImpl* capsule = (CapsuleImpl*)capsulePtr;
    if (!capsule->transferred && capsule->handle) {
        if (capsule->deleter_fn) {
            void* rawObj = absolute_managed_get(capsule->handle);
            if (rawObj)
                capsule->deleter_fn(rawObj);
        }
        absolute_managed_destroy(capsule->handle);
    }
    free(capsule);
}

/* ---------- ownership-transfer channels ---------- */

typedef struct TransferChannelNode {
    void* capsule;
    struct TransferChannelNode* next;
} TransferChannelNode;

typedef struct TransferChannelImpl {
    TransferChannelNode* first;
    TransferChannelNode* last;
    int32_t count;
    int32_t capacity;
    uint8_t closed;
} TransferChannelImpl;

void* absolute_transfer_channel_create(int32_t capacity) {
    TransferChannelImpl* channel =
        (TransferChannelImpl*)heap_alloc(sizeof(TransferChannelImpl));
    if (!channel)
        abort();
    memset(channel, 0, sizeof(TransferChannelImpl));
    channel->capacity = capacity > 0 ? capacity : 0;
    return channel;
}

int32_t absolute_transfer_channel_try_send(void* channelPtr, void* capsule) {
    TransferChannelImpl* channel = (TransferChannelImpl*)channelPtr;
    if (!channel || !capsule || channel->closed)
        return 0;
    if (channel->capacity > 0 && channel->count >= channel->capacity)
        return 0;
    TransferChannelNode* node =
        (TransferChannelNode*)heap_alloc(sizeof(TransferChannelNode));
    if (!node)
        abort();
    node->capsule = capsule;
    node->next = NULL;
    if (channel->last)
        channel->last->next = node;
    else
        channel->first = node;
    channel->last = node;
    channel->count += 1;
    return 1;
}

int32_t absolute_transfer_channel_send(void* channelPtr, void* capsule) {
    return absolute_transfer_channel_try_send(channelPtr, capsule);
}

void* absolute_transfer_channel_try_receive(void* channelPtr) {
    TransferChannelImpl* channel = (TransferChannelImpl*)channelPtr;
    if (!channel || !channel->first)
        return NULL;
    TransferChannelNode* node = channel->first;
    void* capsule = node->capsule;
    channel->first = node->next;
    if (!channel->first)
        channel->last = NULL;
    channel->count -= 1;
    free(node);
    return capsule;
}

void* absolute_transfer_channel_receive(void* channelPtr) {
    return absolute_transfer_channel_try_receive(channelPtr);
}

void absolute_transfer_channel_close(void* channelPtr) {
    TransferChannelImpl* channel = (TransferChannelImpl*)channelPtr;
    if (channel)
        channel->closed = 1;
}

int32_t absolute_transfer_channel_is_closed(void* channelPtr) {
    TransferChannelImpl* channel = (TransferChannelImpl*)channelPtr;
    return !channel || channel->closed;
}

int32_t absolute_transfer_channel_count(void* channelPtr) {
    TransferChannelImpl* channel = (TransferChannelImpl*)channelPtr;
    return channel ? channel->count : 0;
}

void absolute_transfer_channel_destroy(void* channelPtr) {
    TransferChannelImpl* channel = (TransferChannelImpl*)channelPtr;
    if (!channel)
        return;
    TransferChannelNode* node = channel->first;
    while (node) {
        TransferChannelNode* next = node->next;
        absolute_capsule_destroy(node->capsule);
        free(node);
        node = next;
    }
    free(channel);
}

/* ---------- time / random (WASI-backed when available) ---------- */

int64_t absolute_time_unix_nanos(void) {
#if defined(ABSOLUTE_WASM_USE_WASI)
    uint64_t ts = 0;
    if (absolute_wasi_clock_time_get(ABSOLUTE_WASI_CLOCK_REALTIME, 1, &ts) != 0)
        return 0;
    return (int64_t)ts;
#else
    return absolute_host_time_unix_nanos();
#endif
}

int64_t absolute_time_unix_millis(void) {
    return absolute_time_unix_nanos() / 1000000;
}

int64_t absolute_time_unix_seconds(void) {
    return absolute_time_unix_nanos() / 1000000000;
}

int64_t absolute_time_monotonic_nanos(void) {
#if defined(ABSOLUTE_WASM_USE_WASI)
    uint64_t ts = 0;
    if (absolute_wasi_clock_time_get(ABSOLUTE_WASI_CLOCK_MONOTONIC, 1, &ts) != 0)
        return 0;
    return (int64_t)ts;
#else
    return absolute_host_time_monotonic_nanos();
#endif
}

void absolute_time_sleep_nanos(int64_t nanoseconds) {
    (void)nanoseconds; /* cooperative sleep not available on wasm reactor */
}

void absolute_time_sleep_micros(int64_t microseconds) {
    (void)microseconds;
}

void absolute_time_sleep_millis(int64_t milliseconds) {
    (void)milliseconds;
}

void absolute_time_sleep_seconds(int64_t seconds) {
    (void)seconds;
}

void absolute_time_sleep(int32_t milliseconds) {
    (void)milliseconds;
}

void absolute_time_local_str(char* buffer, int32_t bufferSize) {
    if (!buffer || bufferSize <= 0)
        return;
    buffer[0] = '\0';
}

void absolute_time_date_str(char* buffer, int32_t bufferSize) {
    if (!buffer || bufferSize <= 0)
        return;
    buffer[0] = '\0';
}

typedef struct WasmRng {
    uint64_t state;
} WasmRng;

static uint64_t wasm_splitmix64(uint64_t* state) {
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

uint64_t absolute_random_entropy(void) {
#if defined(ABSOLUTE_WASM_USE_WASI)
    uint64_t value = 0;
    if (absolute_wasi_random_get((uint8_t*)&value, sizeof(value)) == 0 && value != 0)
        return value;
#endif
#if !defined(ABSOLUTE_WASM_USE_WASI)
    {
        uint64_t value = absolute_host_random_entropy();
        if (value != 0) return value;
    }
#endif
    return 0xA5A5F00D1234C0DEULL ^ (uint64_t)(uintptr_t)&absolute_random_entropy;
}

uint64_t* absolute_random_create(uint64_t seed) {
    WasmRng* rng = (WasmRng*)heap_alloc(sizeof(WasmRng));
    if (!rng)
        return NULL;
    if (seed == 0)
        seed = absolute_random_entropy();
    rng->state = seed;
    return &rng->state;
}

void absolute_random_destroy(uint64_t* handle) {
    if (handle)
        free(handle);
}

uint64_t absolute_random_u64(uint64_t* handle) {
    if (!handle)
        return 0;
    return wasm_splitmix64(handle);
}

int32_t absolute_random_i32(uint64_t* handle) {
    return (int32_t)(absolute_random_u64(handle) >> 32);
}

int32_t absolute_random_range(uint64_t* handle, int32_t lo, int32_t hi) {
    if (hi <= lo)
        return lo;
    uint64_t span = (uint64_t)(hi - lo);
    return lo + (int32_t)(absolute_random_u64(handle) % span);
}

double absolute_random_real(uint64_t* handle) {
    const uint64_t bits = absolute_random_u64(handle) >> 11;
    return (double)bits / (double)(1ULL << 53);
}
