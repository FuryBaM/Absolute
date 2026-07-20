#include "plugin_api.h"

namespace {
    const AbsoluteSyntaxPluginV1 plugin = {
        ABSOLUTE_SYNTAX_PLUGIN_ABI_VERSION,
        "absolute.desktop",
        0,
        nullptr
    };

    constexpr const char* prelude = R"ABSOLUTE(
extern "C" int64 absolute_desktop_create(string title, int32 width, int32 height, int32 resizable);
extern "C" void absolute_desktop_destroy(int64 handle);
extern "C" int32 absolute_desktop_poll(int64 handle);
extern "C" int32 absolute_desktop_is_open(int64 handle);
extern "C" void absolute_desktop_set_title(int64 handle, string title);
extern "C" int32 absolute_desktop_width(int64 handle);
extern "C" int32 absolute_desktop_height(int64 handle);
extern "C" void absolute_desktop_clear(int64 handle, uint32 color);
extern "C" void absolute_desktop_pixel(int64 handle, int32 x, int32 y, uint32 color);
extern "C" void absolute_desktop_fill_rect(int64 handle, int32 x, int32 y, int32 width, int32 height, uint32 color);
extern "C" void absolute_desktop_present(int64 handle);
extern "C" int32 absolute_desktop_key_down(int64 handle, int32 key);
extern "C" int32 absolute_desktop_mouse_x(int64 handle);
extern "C" int32 absolute_desktop_mouse_y(int64 handle);
extern "C" int32 absolute_desktop_mouse_down(int64 handle, int32 button);
extern "C" uint32 absolute_desktop_rgb(int32 red, int32 green, int32 blue);
extern "C" double absolute_desktop_time();
extern "C" void absolute_desktop_sleep(int32 milliseconds);

namespace Desktop {
    uint32 rgb(int32 red, int32 green, int32 blue) {
        return absolute_desktop_rgb(red, green, blue);
    }

    double time() {
        return absolute_desktop_time();
    }

    void sleep(int32 milliseconds) {
        absolute_desktop_sleep(milliseconds);
    }

    class Window {
        public int64 handle;

        public Window(string title, int32 width, int32 height, bool resizable) {
            handle = absolute_desktop_create(title, width, height, resizable ? 1 : 0);
        }

        public bool poll() {
            return handle != 0 && absolute_desktop_poll(handle) != 0;
        }

        public bool isOpen() {
            return handle != 0 && absolute_desktop_is_open(handle) != 0;
        }

        public void close() {
            if (handle != 0) {
                absolute_desktop_destroy(handle);
                handle = 0;
            }
        }

        public void setTitle(string title) {
            if (handle != 0) { absolute_desktop_set_title(handle, title); }
        }

        public int32 width() {
            return absolute_desktop_width(handle);
        }

        public int32 height() {
            return absolute_desktop_height(handle);
        }

        public void clear(uint32 color) {
            absolute_desktop_clear(handle, color);
        }

        public void pixel(int32 x, int32 y, uint32 color) {
            absolute_desktop_pixel(handle, x, y, color);
        }

        public void fillRect(int32 x, int32 y, int32 width, int32 height, uint32 color) {
            absolute_desktop_fill_rect(handle, x, y, width, height, color);
        }

        public void present() {
            absolute_desktop_present(handle);
        }

        public bool keyDown(int32 key) {
            return absolute_desktop_key_down(handle, key) != 0;
        }

        public int32 mouseX() {
            return absolute_desktop_mouse_x(handle);
        }

        public int32 mouseY() {
            return absolute_desktop_mouse_y(handle);
        }

        public bool mouseDown(int32 button) {
            return absolute_desktop_mouse_down(handle, button) != 0;
        }
    }
}
)ABSOLUTE";
}

extern "C" ABSOLUTE_PLUGIN_EXPORT const AbsoluteSyntaxPluginV1* absolute_syntax_plugin_init_v1() {
    return &plugin;
}

extern "C" ABSOLUTE_PLUGIN_EXPORT const char* absolute_syntax_plugin_prelude_v1() {
    return prelude;
}

