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
extern "C" void absolute_desktop_draw_line(int64 handle, int32 x0, int32 y0, int32 x1, int32 y1, uint32 color);
extern "C" void absolute_desktop_fill_circle(int64 handle, int32 cx, int32 cy, int32 radius, uint32 color);
extern "C" void absolute_desktop_blit(int64 handle, int32 destX, int32 destY, int32 width, int32 height, raw uint32* pixels);
extern "C" void absolute_desktop_present(int64 handle);
extern "C" int32 absolute_desktop_key_down(int64 handle, int32 key);
extern "C" int32 absolute_desktop_key_pressed(int64 handle, int32 key);
extern "C" int32 absolute_desktop_key_released(int64 handle, int32 key);
extern "C" int32 absolute_desktop_mouse_x(int64 handle);
extern "C" int32 absolute_desktop_mouse_y(int64 handle);
extern "C" int32 absolute_desktop_mouse_down(int64 handle, int32 button);
extern "C" int32 absolute_desktop_mouse_pressed(int64 handle, int32 button);
extern "C" int32 absolute_desktop_mouse_released(int64 handle, int32 button);
extern "C" double absolute_desktop_delta_time(int64 handle);
extern "C" uint32 absolute_desktop_rgb(int32 red, int32 green, int32 blue);
extern "C" double absolute_desktop_time();
extern "C" void absolute_desktop_sleep(int32 milliseconds);
extern "C" int64 absolute_desktop_sprite_create(int32 width, int32 height);
extern "C" void absolute_desktop_sprite_destroy(int64 handle);
extern "C" int32 absolute_desktop_sprite_width(int64 handle);
extern "C" int32 absolute_desktop_sprite_height(int64 handle);
extern "C" void absolute_desktop_sprite_clear(int64 handle, uint32 color);
extern "C" void absolute_desktop_sprite_pixel(int64 handle, int32 x, int32 y, uint32 color);
extern "C" void absolute_desktop_sprite_fill_rect(int64 handle, int32 x, int32 y, int32 width, int32 height, uint32 color);
extern "C" void absolute_desktop_sprite_fill_circle(int64 handle, int32 cx, int32 cy, int32 radius, uint32 color);
extern "C" void absolute_desktop_sprite_draw(int64 windowHandle, int64 spriteHandle, int32 x, int32 y);

namespace Desktop {
    // Virtual-key style codes (Win32 / mapped X11).
    int32 KeyEscape = 27;
    int32 KeySpace = 32;
    int32 KeyEnter = 13;
    int32 KeyLeft = 37;
    int32 KeyUp = 38;
    int32 KeyRight = 39;
    int32 KeyDown = 40;
    int32 KeyW = 87;
    int32 KeyA = 65;
    int32 KeyS = 83;
    int32 KeyD = 68;
    int32 MouseLeft = 0;
    int32 MouseRight = 1;
    int32 MouseMiddle = 2;

    uint32 rgb(int32 red, int32 green, int32 blue) {
        return absolute_desktop_rgb(red, green, blue);
    }

    double time() {
        return absolute_desktop_time();
    }

    void sleep(int32 milliseconds) {
        absolute_desktop_sleep(milliseconds);
    }

    // Fixed timestep accumulator for deterministic simulation.
    // Typical usage after window.poll():
    //   fixed.add(window.deltaTime());
    //   while (fixed.shouldUpdate()) { simulate(fixed.step); fixed.consume(); }
    //   render(fixed.alpha());
    class FixedStep {
        public double step;
        public double accumulator;
        public double maxFrame;

        public FixedStep(double updatesPerSecond) {
            if (updatesPerSecond <= 0.0) {
                updatesPerSecond = 60.0;
            }
            step = 1.0 / updatesPerSecond;
            accumulator = 0.0;
            maxFrame = 0.1;
        }

        public void add(double dt) {
            if (dt < 0.0) {
                dt = 0.0;
            }
            if (dt > maxFrame) {
                dt = maxFrame;
            }
            accumulator = accumulator + dt;
        }

        public bool shouldUpdate() {
            return accumulator >= step;
        }

        public void consume() {
            accumulator = accumulator - step;
            if (accumulator < 0.0) {
                accumulator = 0.0;
            }
        }

        // Interpolation factor in [0, 1) for rendering between fixed steps.
        public double alpha() {
            if (step <= 0.0) {
                return 0.0;
            }
            double a = accumulator / step;
            if (a < 0.0) {
                a = 0.0;
            }
            if (a > 1.0) {
                a = 1.0;
            }
            return a;
        }
    }

    // Soft sprite: offscreen ARGB buffer (0 = transparent when drawn).
    class Sprite {
        public int64 handle;

        public Sprite(int32 width, int32 height) {
            handle = absolute_desktop_sprite_create(width, height);
        }

        public void destroy() {
            if (handle != 0) {
                absolute_desktop_sprite_destroy(handle);
                handle = 0;
            }
        }

        public int32 width() {
            return absolute_desktop_sprite_width(handle);
        }

        public int32 height() {
            return absolute_desktop_sprite_height(handle);
        }

        public void clear(uint32 color) {
            absolute_desktop_sprite_clear(handle, color);
        }

        public void pixel(int32 x, int32 y, uint32 color) {
            absolute_desktop_sprite_pixel(handle, x, y, color);
        }

        public void fillRect(int32 x, int32 y, int32 width, int32 height, uint32 color) {
            absolute_desktop_sprite_fill_rect(handle, x, y, width, height, color);
        }

        public void fillCircle(int32 cx, int32 cy, int32 radius, uint32 color) {
            absolute_desktop_sprite_fill_circle(handle, cx, cy, radius, color);
        }
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

        public void drawLine(int32 x0, int32 y0, int32 x1, int32 y1, uint32 color) {
            absolute_desktop_draw_line(handle, x0, y0, x1, y1, color);
        }

        public void fillCircle(int32 cx, int32 cy, int32 radius, uint32 color) {
            absolute_desktop_fill_circle(handle, cx, cy, radius, color);
        }

        public void blit(int32 destX, int32 destY, int32 width, int32 height, raw uint32* pixels) {
            absolute_desktop_blit(handle, destX, destY, width, height, pixels);
        }

        public void drawSprite(Sprite sprite, int32 x, int32 y) {
            absolute_desktop_sprite_draw(handle, sprite.handle, x, y);
        }

        public void present() {
            absolute_desktop_present(handle);
        }

        public bool keyDown(int32 key) {
            return absolute_desktop_key_down(handle, key) != 0;
        }

        public bool keyPressed(int32 key) {
            return absolute_desktop_key_pressed(handle, key) != 0;
        }

        public bool keyReleased(int32 key) {
            return absolute_desktop_key_released(handle, key) != 0;
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

        public bool mousePressed(int32 button) {
            return absolute_desktop_mouse_pressed(handle, button) != 0;
        }

        public bool mouseReleased(int32 button) {
            return absolute_desktop_mouse_released(handle, button) != 0;
        }

        // Seconds since previous poll(); 0 on the first frame.
        public double deltaTime() {
            return absolute_desktop_delta_time(handle);
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
