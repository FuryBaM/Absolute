/*
 * Virtual FS + env/process stubs + network stubs for Absolute wasm runtime.
 * Included from absolute_wasm_runtime.c (shares heap_alloc / host log).
 */

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

static const char* fs_normalize(const char* path) {
    if (!path || !*path)
        return "/";
    if (path[0] == '/')
        return path;
    /* relative -> cwd + "/" + path */
    size_t n = 0;
    const char* cwd = g_fs_cwd;
    while (cwd[n] && n + 1 < sizeof(g_fs_path_scratch)) {
        g_fs_path_scratch[n] = cwd[n];
        ++n;
    }
    if (n > 0 && g_fs_path_scratch[n - 1] != '/' && n + 1 < sizeof(g_fs_path_scratch))
        g_fs_path_scratch[n++] = '/';
    size_t i = 0;
    while (path[i] && n + 1 < sizeof(g_fs_path_scratch)) {
        g_fs_path_scratch[n++] = path[i++];
    }
    g_fs_path_scratch[n] = '\0';
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
    fs_copy_path(g_fs_string_scratch, sizeof(g_fs_string_scratch), fs_normalize(path));
    return g_fs_string_scratch;
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

enum { ABSOLUTE_ENV_MAX = 32, ABSOLUTE_ENV_KEY = 64, ABSOLUTE_ENV_VAL = 256 };

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
    if (!name)
        return 0;
    for (int i = 0; i < ABSOLUTE_ENV_MAX; ++i)
        if (g_env[i].used && fs_streq(g_env[i].key, name))
            return 1;
    return 0;
}

const char* absolute_env_get(const char* name) {
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

const char* absolute_process_error(void) { return g_process_error; }
int32_t absolute_process_pid(void) { return 1; }
void absolute_process_exit(int32_t code) { (void)code; abort(); }
void absolute_process_abort(void) { abort(); }
const char* absolute_process_executable_path(void) { return g_process_exe; }
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
int32_t absolute_process_args_count(void) { return 0; }
const char* absolute_process_arg_at(int32_t index) {
    (void)index;
    return "";
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

/* ---------- transfer capsules ---------- */

typedef struct CapsuleImpl {
    uint64_t handle;
    uint8_t transferred;
} CapsuleImpl;

void* absolute_capsule_create(uint64_t handle) {
    CapsuleImpl* capsule = (CapsuleImpl*)heap_alloc(sizeof(CapsuleImpl));
    if (!capsule)
        abort();
    capsule->handle = handle;
    capsule->transferred = 0;
    return capsule;
}

uint64_t absolute_capsule_unwrap(void* capsulePtr) {
    if (!capsulePtr)
        return 0;
    CapsuleImpl* capsule = (CapsuleImpl*)capsulePtr;
    if (capsule->transferred)
        abort();
    capsule->transferred = 1;
    return capsule->handle;
}

void absolute_capsule_destroy(void* capsulePtr) {
    if (!capsulePtr)
        return;
    CapsuleImpl* capsule = (CapsuleImpl*)capsulePtr;
    if (!capsule->transferred && capsule->handle)
        absolute_managed_destroy(capsule->handle);
    free(capsule);
}
