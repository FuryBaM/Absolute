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
extern "C" int64 absolute_desktop_sprite_load_bmp(string path);
extern "C" void absolute_desktop_sprite_destroy(int64 handle);
extern "C" int32 absolute_desktop_sprite_width(int64 handle);
extern "C" int32 absolute_desktop_sprite_height(int64 handle);
extern "C" void absolute_desktop_sprite_clear(int64 handle, uint32 color);
extern "C" void absolute_desktop_sprite_pixel(int64 handle, int32 x, int32 y, uint32 color);
extern "C" void absolute_desktop_sprite_fill_rect(int64 handle, int32 x, int32 y, int32 width, int32 height, uint32 color);
extern "C" void absolute_desktop_sprite_fill_circle(int64 handle, int32 cx, int32 cy, int32 radius, uint32 color);
extern "C" void absolute_desktop_sprite_color_key(int64 handle, uint32 color);
extern "C" void absolute_desktop_sprite_draw(int64 windowHandle, int64 spriteHandle, int32 x, int32 y);
extern "C" void absolute_desktop_sprite_draw_rect(int64 windowHandle, int64 spriteHandle, int32 destX, int32 destY, int32 srcX, int32 srcY, int32 srcW, int32 srcH);
extern "C" void absolute_desktop_sprite_draw_text(int64 spriteHandle, int32 x, int32 y, string text, uint32 color, int32 scale);
extern "C" int64 absolute_desktop_batch_create();
extern "C" void absolute_desktop_batch_destroy(int64 handle);
extern "C" void absolute_desktop_batch_begin(int64 handle, int64 windowHandle, int64 atlasHandle);
extern "C" void absolute_desktop_batch_set_atlas(int64 handle, int64 atlasHandle);
extern "C" void absolute_desktop_batch_draw(int64 handle, int32 x, int32 y);
extern "C" void absolute_desktop_batch_draw_rect(int64 handle, int32 destX, int32 destY, int32 srcX, int32 srcY, int32 srcW, int32 srcH);
extern "C" void absolute_desktop_batch_draw_sprite(int64 handle, int64 spriteHandle, int32 x, int32 y);
extern "C" void absolute_desktop_batch_draw_sprite_rect(int64 handle, int64 spriteHandle, int32 destX, int32 destY, int32 srcX, int32 srcY, int32 srcW, int32 srcH);
extern "C" void absolute_desktop_batch_flush(int64 handle);
extern "C" void absolute_desktop_batch_end(int64 handle);
extern "C" int32 absolute_desktop_batch_count(int64 handle);
extern "C" void absolute_desktop_draw_text(int64 windowHandle, int32 x, int32 y, string text, uint32 color, int32 scale);
extern "C" int32 absolute_desktop_measure_text(string text, int32 scale);
extern "C" int32 absolute_desktop_measure_text_height(string text, int32 scale);
extern "C" int32 absolute_desktop_font_glyph_width();
extern "C" int32 absolute_desktop_font_glyph_height();
extern "C" int32 absolute_desktop_text_count(int64 handle);
extern "C" int32 absolute_desktop_text_pop(int64 handle);
extern "C" void absolute_desktop_text_clear(int64 handle);
extern "C" int32 absolute_desktop_gamepad_connected(int32 index);
extern "C" int32 absolute_desktop_gamepad_button(int32 index, int32 button);
extern "C" double absolute_desktop_gamepad_axis(int32 index, int32 axis);

namespace Desktop {
    // Input codes as static fields (namespace vars are not LLVM-codegen'd yet).
    class Codes {
        // Virtual-key style (Win32 / mapped X11).
        public static int32 KeyEscape = 27;
        public static int32 KeySpace = 32;
        public static int32 KeyEnter = 13;
        public static int32 KeyLeft = 37;
        public static int32 KeyUp = 38;
        public static int32 KeyRight = 39;
        public static int32 KeyDown = 40;
        public static int32 KeyW = 87;
        public static int32 KeyA = 65;
        public static int32 KeyS = 83;
        public static int32 KeyD = 68;
        public static int32 MouseLeft = 0;
        public static int32 MouseRight = 1;
        public static int32 MouseMiddle = 2;

        // XInput-style gamepad (index 0..3). Linux backend returns disconnected.
        public static int32 PadA = 0;
        public static int32 PadB = 1;
        public static int32 PadX = 2;
        public static int32 PadY = 3;
        public static int32 PadLB = 4;
        public static int32 PadRB = 5;
        public static int32 PadBack = 6;
        public static int32 PadStart = 7;
        public static int32 PadLS = 8;
        public static int32 PadRS = 9;
        public static int32 PadDUp = 10;
        public static int32 PadDDown = 11;
        public static int32 PadDLeft = 12;
        public static int32 PadDRight = 13;
        public static int32 AxisLX = 0;
        public static int32 AxisLY = 1;
        public static int32 AxisRX = 2;
        public static int32 AxisRY = 3;
        public static int32 AxisLT = 4;
        public static int32 AxisRT = 5;
    }

    // Convenience aliases as zero-arg functions (namespace fields are not codegen'd).
    int32 KeyEscape() { return Codes.KeyEscape; }
    int32 KeySpace() { return Codes.KeySpace; }
    int32 KeyEnter() { return Codes.KeyEnter; }
    int32 KeyLeft() { return Codes.KeyLeft; }
    int32 KeyUp() { return Codes.KeyUp; }
    int32 KeyRight() { return Codes.KeyRight; }
    int32 KeyDown() { return Codes.KeyDown; }
    int32 KeyW() { return Codes.KeyW; }
    int32 KeyA() { return Codes.KeyA; }
    int32 KeyS() { return Codes.KeyS; }
    int32 KeyD() { return Codes.KeyD; }
    int32 MouseLeft() { return Codes.MouseLeft; }
    int32 MouseRight() { return Codes.MouseRight; }
    int32 MouseMiddle() { return Codes.MouseMiddle; }
    int32 PadA() { return Codes.PadA; }
    int32 PadB() { return Codes.PadB; }
    int32 PadX() { return Codes.PadX; }
    int32 PadY() { return Codes.PadY; }
    int32 PadLB() { return Codes.PadLB; }
    int32 PadRB() { return Codes.PadRB; }
    int32 PadBack() { return Codes.PadBack; }
    int32 PadStart() { return Codes.PadStart; }
    int32 PadLS() { return Codes.PadLS; }
    int32 PadRS() { return Codes.PadRS; }
    int32 PadDUp() { return Codes.PadDUp; }
    int32 PadDDown() { return Codes.PadDDown; }
    int32 PadDLeft() { return Codes.PadDLeft; }
    int32 PadDRight() { return Codes.PadDRight; }
    int32 AxisLX() { return Codes.AxisLX; }
    int32 AxisLY() { return Codes.AxisLY; }
    int32 AxisRX() { return Codes.AxisRX; }
    int32 AxisRY() { return Codes.AxisRY; }
    int32 AxisLT() { return Codes.AxisLT; }
    int32 AxisRT() { return Codes.AxisRT; }

    uint32 rgb(int32 red, int32 green, int32 blue) {
        return absolute_desktop_rgb(red, green, blue);
    }

    double time() {
        return absolute_desktop_time();
    }

    void sleep(int32 milliseconds) {
        absolute_desktop_sleep(milliseconds);
    }

    // Built-in 8x8 monospace soft font (ASCII 32..126). Scale is clamped to 1..16.
    int32 fontGlyphWidth() {
        return absolute_desktop_font_glyph_width();
    }

    int32 fontGlyphHeight() {
        return absolute_desktop_font_glyph_height();
    }

    int32 measureText(string text, int32 scale) {
        return absolute_desktop_measure_text(text, scale);
    }

    int32 measureTextHeight(string text, int32 scale) {
        return absolute_desktop_measure_text_height(text, scale);
    }

    bool gamepadConnected(int32 index) {
        return absolute_desktop_gamepad_connected(index) != 0;
    }

    bool gamepadButton(int32 index, int32 button) {
        return absolute_desktop_gamepad_button(index, button) != 0;
    }

    double gamepadAxis(int32 index, int32 axis) {
        return absolute_desktop_gamepad_axis(index, axis);
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

    // Soft sprite: offscreen 0x00RRGGBB buffer (0 = transparent when drawn).
    class Sprite {
        public int64 handle;

        public Sprite(int32 width, int32 height) {
            handle = absolute_desktop_sprite_create(width, height);
        }

        public bool isValid() {
            return handle != 0;
        }

        public void destroy() {
            if (handle != 0) {
                absolute_desktop_sprite_destroy(handle);
                handle = 0;
            }
        }

        // Replace buffer with 24/32-bit uncompressed BMP. Returns false on failure.
        public bool loadBmp(string path) {
            if (handle != 0) {
                absolute_desktop_sprite_destroy(handle);
                handle = 0;
            }
            handle = absolute_desktop_sprite_load_bmp(path);
            return handle != 0;
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

        // Treat exact RGB matches as transparent (set to 0). e.g. magenta Desktop.rgb(255, 0, 255).
        public void colorKey(uint32 color) {
            absolute_desktop_sprite_color_key(handle, color);
        }

        // Draw built-in 8x8 font into this sprite (opaque pixels; 0 background left alone).
        public void drawText(int32 x, int32 y, string text, uint32 color, int32 scale) {
            absolute_desktop_sprite_draw_text(handle, x, y, text, color, scale);
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

        public void drawSprite(Sprite* sprite, int32 x, int32 y) {
            absolute_desktop_sprite_draw(handle, sprite.handle, x, y);
        }

        // Draw a source rectangle from a sprite/atlas (soft blit with 0 = transparent).
        public void drawSpriteRect(Sprite* sprite, int32 destX, int32 destY, int32 srcX, int32 srcY, int32 srcW, int32 srcH) {
            absolute_desktop_sprite_draw_rect(handle, sprite.handle, destX, destY, srcX, srcY, srcW, srcH);
        }

        // Built-in soft font (ASCII). Supports '\\n'. scale 1..16. Transparent background.
        public void drawText(int32 x, int32 y, string text, uint32 color, int32 scale) {
            absolute_desktop_draw_text(handle, x, y, text, color, scale);
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

        // UTF code units / ASCII from keyboard (BMP). -1 when empty.
        public int32 textCount() {
            return absolute_desktop_text_count(handle);
        }

        public int32 textPop() {
            return absolute_desktop_text_pop(handle);
        }

        public void textClear() {
            absolute_desktop_text_clear(handle);
        }
    }

    // Soft sprite batch: queue many atlas/sprite draws, then flush once.
    // begin(window, atlas) binds the default atlas; draw/drawRect use it.
    // drawSprite / drawSpriteRect draw other sprites without changing the atlas.
    // Auto-flushes at 8192 pending entries. Call end() (or flush) before present.
    class SpriteBatch {
        public int64 handle;

        public SpriteBatch() {
            handle = absolute_desktop_batch_create();
        }

        public bool isValid() {
            return handle != 0;
        }

        public void destroy() {
            if (handle != 0) {
                absolute_desktop_batch_destroy(handle);
                handle = 0;
            }
        }

        public void begin(Window* window, Sprite* atlas) {
            absolute_desktop_batch_begin(handle, window.handle, atlas.handle);
        }

        // Switch default atlas; flushes pending draws first.
        public void setAtlas(Sprite* atlas) {
            absolute_desktop_batch_set_atlas(handle, atlas.handle);
        }

        // Full default atlas at (x, y).
        public void draw(int32 x, int32 y) {
            absolute_desktop_batch_draw(handle, x, y);
        }

        // Sub-rect of the default atlas (atlas / tile).
        public void drawRect(int32 destX, int32 destY, int32 srcX, int32 srcY, int32 srcW, int32 srcH) {
            absolute_desktop_batch_draw_rect(handle, destX, destY, srcX, srcY, srcW, srcH);
        }

        // One-off full sprite without changing the default atlas.
        public void drawSprite(Sprite* sprite, int32 x, int32 y) {
            absolute_desktop_batch_draw_sprite(handle, sprite.handle, x, y);
        }

        public void drawSpriteRect(Sprite* sprite, int32 destX, int32 destY, int32 srcX, int32 srcY, int32 srcW, int32 srcH) {
            absolute_desktop_batch_draw_sprite_rect(handle, sprite.handle, destX, destY, srcX, srcY, srcW, srcH);
        }

        public void flush() {
            absolute_desktop_batch_flush(handle);
        }

        public void end() {
            absolute_desktop_batch_end(handle);
        }

        // Pending draws since last flush (0 after flush/end).
        public int32 count() {
            return absolute_desktop_batch_count(handle);
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
