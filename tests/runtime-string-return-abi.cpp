#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

extern "C" {
const char* absolute_string_retain(const char*);
void absolute_string_release(const char*);

const char* absolute_env_error();
const char* absolute_process_error();
const char* absolute_process_hostname();
const char* absolute_fs_path_separator();
const char* absolute_fs_path_join(const char*, const char*);
const char* absolute_fs_error();
const char* absolute_datetime_error();
const char* absolute_datetime_local_zone_name();
const char* absolute_datetime_format_iso(
    std::int32_t, std::int32_t, std::int32_t, std::int32_t,
    std::int32_t, std::int32_t, std::int32_t, std::int32_t);
const char* absolute_json_get_last_error();
const char* absolute_net_error();
const char* absolute_http_tls_error();
const char* absolute_load_error();
const char* absolute_task_current_role();

void* absolute_binary_writer_create();
void absolute_binary_writer_free(void*);
void absolute_binary_writer_write_byte(void*, std::uint8_t);
const char* absolute_binary_writer_to_hex(const void*);
}

namespace {
    void Require(bool condition, const char* message) {
        if (!condition) {
            std::cerr << message << '\n';
            std::abort();
        }
    }

    void Release(const char* text) {
        Require(text != nullptr, "string-return ABI produced null unexpectedly");
        Require(absolute_string_retain(text) == text,
            "retain changed the returned string pointer");
        absolute_string_release(text);
        absolute_string_release(text);
    }
}

int main() {
    // These used to point into one shared thread-local std::string. The second
    // call therefore mutated the first value even though the language had
    // already accepted both as independent string values.
    const char* first = absolute_fs_path_join("alpha", "one");
    const char* second = absolute_fs_path_join("beta", "two");
    Require(std::strstr(first, "alpha") != nullptr,
        "first filesystem string was invalidated by the next call");
    Require(std::strstr(second, "beta") != nullptr,
        "second filesystem string has the wrong value");
    Release(first);
    Release(second);

    void* writer = absolute_binary_writer_create();
    absolute_binary_writer_write_byte(writer, 0xab);
    const char* hex = absolute_binary_writer_to_hex(writer);
    Require(std::strcmp(hex, "ab") == 0, "binary string return mismatch");
    absolute_binary_writer_free(writer);
    Require(std::strcmp(hex, "ab") == 0,
        "binary string borrowed destroyed producer storage");
    Release(hex);

    Release(absolute_env_error());
    Release(absolute_process_error());
    Release(absolute_process_hostname());
    Release(absolute_fs_path_separator());
    Release(absolute_fs_error());
    Release(absolute_datetime_error());
    Release(absolute_datetime_local_zone_name());
    Release(absolute_datetime_format_iso(2026, 9, 28, 12, 34, 56, 0, 0));
    Release(absolute_json_get_last_error());
    Release(absolute_net_error());
    Release(absolute_http_tls_error());
    Release(absolute_load_error());
    Release(absolute_task_current_role());

    std::cout << "runtime-string-return-abi=ok\n";
    return 0;
}
