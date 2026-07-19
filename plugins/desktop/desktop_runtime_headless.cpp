#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>

extern "C" int64_t absolute_desktop_create(const char*, int32_t, int32_t, int32_t) { return 0; }
extern "C" void absolute_desktop_destroy(int64_t) {}
extern "C" int32_t absolute_desktop_poll(int64_t) { return 0; }
extern "C" int32_t absolute_desktop_is_open(int64_t) { return 0; }
extern "C" void absolute_desktop_set_title(int64_t, const char*) {}
extern "C" int32_t absolute_desktop_width(int64_t) { return 0; }
extern "C" int32_t absolute_desktop_height(int64_t) { return 0; }
extern "C" void absolute_desktop_clear(int64_t, uint32_t) {}
extern "C" void absolute_desktop_pixel(int64_t, int32_t, int32_t, uint32_t) {}
extern "C" void absolute_desktop_fill_rect(int64_t, int32_t, int32_t, int32_t, int32_t, uint32_t) {}
extern "C" void absolute_desktop_present(int64_t) {}
extern "C" int32_t absolute_desktop_key_down(int64_t, int32_t) { return 0; }
extern "C" int32_t absolute_desktop_mouse_x(int64_t) { return 0; }
extern "C" int32_t absolute_desktop_mouse_y(int64_t) { return 0; }
extern "C" int32_t absolute_desktop_mouse_down(int64_t, int32_t) { return 0; }

extern "C" uint32_t absolute_desktop_rgb(int32_t red, int32_t green, int32_t blue) {
    const uint32_t r = static_cast<uint32_t>(std::clamp(red, 0, 255));
    const uint32_t g = static_cast<uint32_t>(std::clamp(green, 0, 255));
    const uint32_t b = static_cast<uint32_t>(std::clamp(blue, 0, 255));
    return (r << 16U) | (g << 8U) | b;
}

extern "C" double absolute_desktop_time() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

extern "C" void absolute_desktop_sleep(int32_t milliseconds) {
    if (milliseconds > 0) std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

