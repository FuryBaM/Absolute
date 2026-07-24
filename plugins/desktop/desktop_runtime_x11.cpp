#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <thread>
#include <vector>

namespace {
    struct DesktopWindow {
        Display* display = nullptr;
        ::Window window = 0;
        GC graphics = nullptr;
        Atom closeMessage = 0;
        int32_t width = 1;
        int32_t height = 1;
        bool open = true;
        std::vector<uint32_t> pixels;
        std::array<bool, 256> keys{};
        std::array<bool, 256> keysPrev{};
        std::array<bool, 256> keysPressed{};
        std::array<bool, 256> keysReleased{};
        std::array<bool, 3> mouseButtons{};
        std::array<bool, 3> mousePrev{};
        std::array<bool, 3> mousePressed{};
        std::array<bool, 3> mouseReleased{};
        int32_t mouseX = 0;
        int32_t mouseY = 0;
        double lastTime = -1.0;
        double deltaTime = 0.0;
        std::deque<int32_t> textQueue;

        void Resize(int32_t newWidth, int32_t newHeight) {
            width = std::max<int32_t>(1, newWidth);
            height = std::max<int32_t>(1, newHeight);
            pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
        }

        void PutPixel(int32_t x, int32_t y, uint32_t color) {
            if (x < 0 || y < 0 || x >= width || y >= height) return;
            pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = color;
        }
    };

    void UpdateInputEdges(DesktopWindow& state) {
        for (std::size_t i = 0; i < state.keys.size(); ++i) {
            state.keysPressed[i] = state.keys[i] && !state.keysPrev[i];
            state.keysReleased[i] = !state.keys[i] && state.keysPrev[i];
            state.keysPrev[i] = state.keys[i];
        }
        for (std::size_t i = 0; i < state.mouseButtons.size(); ++i) {
            state.mousePressed[i] = state.mouseButtons[i] && !state.mousePrev[i];
            state.mouseReleased[i] = !state.mouseButtons[i] && state.mousePrev[i];
            state.mousePrev[i] = state.mouseButtons[i];
        }
    }

    void UpdateDelta(DesktopWindow& state) {
        const double now = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (state.lastTime < 0.0) state.deltaTime = 0.0;
        else state.deltaTime = std::max(0.0, now - state.lastTime);
        state.lastTime = now;
    }

    DesktopWindow* FromHandle(int64_t handle) {
        return reinterpret_cast<DesktopWindow*>(static_cast<intptr_t>(handle));
    }

    int32_t KeyCode(KeySym key) {
        if (key >= XK_a && key <= XK_z) return static_cast<int32_t>('A' + (key - XK_a));
        if (key >= XK_A && key <= XK_Z) return static_cast<int32_t>(key);
        if (key >= XK_0 && key <= XK_9) return static_cast<int32_t>(key);
        switch (key) {
        case XK_Escape: return 27;
        case XK_space: return 32;
        case XK_Left: return 37;
        case XK_Up: return 38;
        case XK_Right: return 39;
        case XK_Down: return 40;
        case XK_Return: return 13;
        case XK_Tab: return 9;
        case XK_BackSpace: return 8;
        default: return -1;
        }
    }

    void Present(DesktopWindow& state) {
        if (!state.display || !state.window || state.pixels.empty()) return;
        const int screen = DefaultScreen(state.display);
        const int depth = DefaultDepth(state.display, screen);
        const std::size_t bytes = state.pixels.size() * sizeof(uint32_t);
        char* imageData = static_cast<char*>(std::malloc(bytes));
        if (!imageData) return;
        std::memcpy(imageData, state.pixels.data(), bytes);
        XImage* image = XCreateImage(state.display, DefaultVisual(state.display, screen),
            static_cast<unsigned>(depth), ZPixmap, 0, imageData,
            static_cast<unsigned>(state.width), static_cast<unsigned>(state.height), 32, 0);
        if (!image) { std::free(imageData); return; }
        XPutImage(state.display, state.window, state.graphics, image, 0, 0, 0, 0,
            static_cast<unsigned>(state.width), static_cast<unsigned>(state.height));
        XDestroyImage(image);
        XFlush(state.display);
    }
}

extern "C" int64_t absolute_desktop_create(const char* title, int32_t width, int32_t height, int32_t resizable) {
    if (width <= 0 || height <= 0) return 0;
    Display* display = XOpenDisplay(nullptr);
    if (!display) return 0;
    auto* state = new DesktopWindow();
    state->display = display;
    state->Resize(width, height);
    const int screen = DefaultScreen(display);
    state->window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0,
        static_cast<unsigned>(width), static_cast<unsigned>(height), 0,
        BlackPixel(display, screen), BlackPixel(display, screen));
    if (!state->window) { XCloseDisplay(display); delete state; return 0; }
    XStoreName(display, state->window, title ? title : "Absolute");
    XSelectInput(display, state->window, ExposureMask | StructureNotifyMask |
        KeyPressMask | KeyReleaseMask | PointerMotionMask |
        ButtonPressMask | ButtonReleaseMask | FocusChangeMask);
    state->closeMessage = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, state->window, &state->closeMessage, 1);
    if (resizable == 0) {
        XSizeHints hints{};
        hints.flags = PMinSize | PMaxSize;
        hints.min_width = hints.max_width = width;
        hints.min_height = hints.max_height = height;
        XSetWMNormalHints(display, state->window, &hints);
    }
    state->graphics = XCreateGC(display, state->window, 0, nullptr);
    XMapWindow(display, state->window);
    XFlush(display);
    return static_cast<int64_t>(reinterpret_cast<intptr_t>(state));
}

extern "C" void absolute_desktop_destroy(int64_t handle) {
    DesktopWindow* state = FromHandle(handle);
    if (!state) return;
    if (state->display) {
        if (state->graphics) XFreeGC(state->display, state->graphics);
        if (state->window) XDestroyWindow(state->display, state->window);
        XCloseDisplay(state->display);
    }
    delete state;
}

extern "C" int32_t absolute_desktop_poll(int64_t handle) {
    DesktopWindow* state = FromHandle(handle);
    if (!state || !state->display) return 0;
    while (XPending(state->display) > 0) {
        XEvent event{};
        XNextEvent(state->display, &event);
        switch (event.type) {
        case ClientMessage:
            if (static_cast<Atom>(event.xclient.data.l[0]) == state->closeMessage) state->open = false;
            break;
        case DestroyNotify: state->open = false; state->window = 0; break;
        case ConfigureNotify: state->Resize(event.xconfigure.width, event.xconfigure.height); break;
        case Expose: Present(*state); break;
        case KeyPress: case KeyRelease: {
            const int32_t key = KeyCode(XLookupKeysym(&event.xkey, 0));
            if (key >= 0 && key < static_cast<int32_t>(state->keys.size()))
                state->keys[key] = event.type == KeyPress;
            if (event.type == KeyPress) {
                char buffer[8]{};
                KeySym sym = 0;
                const int n = XLookupString(&event.xkey, buffer, sizeof(buffer), &sym, nullptr);
                if (n > 0) {
                    const unsigned char ch = static_cast<unsigned char>(buffer[0]);
                    if (ch == 8 || ch == 9 || ch == 10 || ch == 13 || ch >= 32) {
                        if (state->textQueue.size() < 256)
                            state->textQueue.push_back(static_cast<int32_t>(ch));
                    }
                }
            }
            break;
        }
        case FocusOut: state->keys.fill(false); state->mouseButtons.fill(false); break;
        case MotionNotify: state->mouseX = event.xmotion.x; state->mouseY = event.xmotion.y; break;
        case ButtonPress: case ButtonRelease: {
            int button = -1;
            if (event.xbutton.button == Button1) button = 0;
            else if (event.xbutton.button == Button3) button = 1;
            else if (event.xbutton.button == Button2) button = 2;
            if (button >= 0) state->mouseButtons[button] = event.type == ButtonPress;
            break;
        }
        default: break;
        }
    }
    UpdateInputEdges(*state);
    UpdateDelta(*state);
    return state->open && state->window ? 1 : 0;
}

extern "C" int32_t absolute_desktop_is_open(int64_t handle) {
    const DesktopWindow* state = FromHandle(handle);
    return state && state->open && state->window ? 1 : 0;
}

// Native OS window id for GLX/Vulkan backends (X11 Window as void*).
extern "C" void* absolute_desktop_native_window(int64_t handle) {
    DesktopWindow* state = FromHandle(handle);
    if (!state) return nullptr;
    return reinterpret_cast<void*>(static_cast<uintptr_t>(state->window));
}

// X11 Display* for GLX context creation.
extern "C" void* absolute_desktop_native_display(int64_t handle) {
    DesktopWindow* state = FromHandle(handle);
    return state ? static_cast<void*>(state->display) : nullptr;
}

extern "C" void absolute_desktop_set_title(int64_t handle, const char* title) {
    DesktopWindow* state = FromHandle(handle);
    if (state && state->display && state->window)
        XStoreName(state->display, state->window, title ? title : "Absolute");
}

extern "C" int32_t absolute_desktop_width(int64_t handle) {
    const DesktopWindow* state = FromHandle(handle);
    return state ? state->width : 0;
}

extern "C" int32_t absolute_desktop_height(int64_t handle) {
    const DesktopWindow* state = FromHandle(handle);
    return state ? state->height : 0;
}

extern "C" void absolute_desktop_clear(int64_t handle, uint32_t color) {
    DesktopWindow* state = FromHandle(handle);
    if (state) std::fill(state->pixels.begin(), state->pixels.end(), color);
}

extern "C" void absolute_desktop_pixel(int64_t handle, int32_t x, int32_t y, uint32_t color) {
    DesktopWindow* state = FromHandle(handle);
    if (state) state->PutPixel(x, y, color);
}

extern "C" void absolute_desktop_fill_rect(
    int64_t handle, int32_t x, int32_t y, int32_t width, int32_t height, uint32_t color) {
    DesktopWindow* state = FromHandle(handle);
    if (!state || width <= 0 || height <= 0) return;
    const int32_t left = std::clamp<int32_t>(x, 0, state->width);
    const int32_t top = std::clamp<int32_t>(y, 0, state->height);
    const int32_t right = std::clamp<int32_t>(x + width, 0, state->width);
    const int32_t bottom = std::clamp<int32_t>(y + height, 0, state->height);
    for (int32_t row = top; row < bottom; ++row) {
        auto begin = state->pixels.begin() + static_cast<std::size_t>(row) * state->width + left;
        std::fill(begin, begin + (right - left), color);
    }
}

extern "C" void absolute_desktop_draw_line(
    int64_t handle, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
    DesktopWindow* state = FromHandle(handle);
    if (!state) return;
    int32_t dx = std::abs(x1 - x0);
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t dy = -std::abs(y1 - y0);
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy;
    for (;;) {
        state->PutPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        const int32_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

extern "C" void absolute_desktop_fill_circle(
    int64_t handle, int32_t cx, int32_t cy, int32_t radius, uint32_t color) {
    DesktopWindow* state = FromHandle(handle);
    if (!state || radius <= 0) return;
    const int32_t r2 = radius * radius;
    const int32_t top = std::max(0, cy - radius);
    const int32_t bottom = std::min(state->height - 1, cy + radius);
    for (int32_t y = top; y <= bottom; ++y) {
        const int32_t dy = y - cy;
        const int32_t span2 = r2 - dy * dy;
        if (span2 < 0) continue;
        int32_t span = 0;
        while ((span + 1) * (span + 1) <= span2) ++span;
        const int32_t left = std::max(0, cx - span);
        const int32_t right = std::min(state->width - 1, cx + span);
        auto begin = state->pixels.begin() + static_cast<std::size_t>(y) * state->width + left;
        std::fill(begin, begin + (right - left + 1), color);
    }
}

extern "C" void absolute_desktop_blit_strided(
    int64_t handle, int32_t destX, int32_t destY, int32_t width, int32_t height,
    const uint32_t* pixels, int32_t pitch) {
    DesktopWindow* state = FromHandle(handle);
    if (!state || !pixels || width <= 0 || height <= 0 || pitch < width) return;
    for (int32_t row = 0; row < height; ++row) {
        const int32_t y = destY + row;
        if (y < 0 || y >= state->height) continue;
        const uint32_t* srcRow = pixels + static_cast<std::size_t>(row) * static_cast<std::size_t>(pitch);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t x = destX + col;
            if (x < 0 || x >= state->width) continue;
            const uint32_t color = srcRow[static_cast<std::size_t>(col)];
            if (color == 0) continue;
            state->PutPixel(x, y, color & 0x00FFFFFFu);
        }
    }
}

extern "C" void absolute_desktop_blit(
    int64_t handle, int32_t destX, int32_t destY, int32_t width, int32_t height, const uint32_t* pixels) {
    absolute_desktop_blit_strided(handle, destX, destY, width, height, pixels, width);
}

extern "C" void absolute_desktop_present(int64_t handle) {
    DesktopWindow* state = FromHandle(handle);
    if (state) Present(*state);
}

extern "C" int32_t absolute_desktop_key_down(int64_t handle, int32_t key) {
    const DesktopWindow* state = FromHandle(handle);
    return state && key >= 0 && key < static_cast<int32_t>(state->keys.size()) && state->keys[key] ? 1 : 0;
}

extern "C" int32_t absolute_desktop_key_pressed(int64_t handle, int32_t key) {
    const DesktopWindow* state = FromHandle(handle);
    return state && key >= 0 && key < static_cast<int32_t>(state->keysPressed.size()) && state->keysPressed[key] ? 1 : 0;
}

extern "C" int32_t absolute_desktop_key_released(int64_t handle, int32_t key) {
    const DesktopWindow* state = FromHandle(handle);
    return state && key >= 0 && key < static_cast<int32_t>(state->keysReleased.size()) && state->keysReleased[key] ? 1 : 0;
}

extern "C" int32_t absolute_desktop_mouse_x(int64_t handle) {
    const DesktopWindow* state = FromHandle(handle);
    return state ? state->mouseX : 0;
}

extern "C" int32_t absolute_desktop_mouse_y(int64_t handle) {
    const DesktopWindow* state = FromHandle(handle);
    return state ? state->mouseY : 0;
}

extern "C" int32_t absolute_desktop_mouse_down(int64_t handle, int32_t button) {
    const DesktopWindow* state = FromHandle(handle);
    return state && button >= 0 && button < static_cast<int32_t>(state->mouseButtons.size()) &&
        state->mouseButtons[button] ? 1 : 0;
}

extern "C" int32_t absolute_desktop_mouse_pressed(int64_t handle, int32_t button) {
    const DesktopWindow* state = FromHandle(handle);
    return state && button >= 0 && button < static_cast<int32_t>(state->mousePressed.size()) &&
        state->mousePressed[button] ? 1 : 0;
}

extern "C" int32_t absolute_desktop_mouse_released(int64_t handle, int32_t button) {
    const DesktopWindow* state = FromHandle(handle);
    return state && button >= 0 && button < static_cast<int32_t>(state->mouseReleased.size()) &&
        state->mouseReleased[button] ? 1 : 0;
}

extern "C" double absolute_desktop_delta_time(int64_t handle) {
    const DesktopWindow* state = FromHandle(handle);
    return state ? state->deltaTime : 0.0;
}

extern "C" uint32_t absolute_desktop_rgb(int32_t red, int32_t green, int32_t blue) {
    const uint32_t r = static_cast<uint32_t>(std::clamp(red, 0, 255));
    const uint32_t g = static_cast<uint32_t>(std::clamp(green, 0, 255));
    const uint32_t b = static_cast<uint32_t>(std::clamp(blue, 0, 255));
    return (r << 16U) | (g << 8U) | b;
}

extern "C" double absolute_desktop_time() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration<double>(now).count();
}

extern "C" void absolute_desktop_sleep(int32_t milliseconds) {
    if (milliseconds > 0) std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

extern "C" int32_t absolute_desktop_text_count(int64_t handle) {
    const DesktopWindow* state = FromHandle(handle);
    return state ? static_cast<int32_t>(state->textQueue.size()) : 0;
}

extern "C" int32_t absolute_desktop_text_pop(int64_t handle) {
    DesktopWindow* state = FromHandle(handle);
    if (!state || state->textQueue.empty()) return -1;
    const int32_t ch = state->textQueue.front();
    state->textQueue.pop_front();
    return ch;
}

extern "C" void absolute_desktop_text_clear(int64_t handle) {
    DesktopWindow* state = FromHandle(handle);
    if (state) state->textQueue.clear();
}

// Gamepad: no XInput on Linux in this backend yet.
extern "C" int32_t absolute_desktop_gamepad_connected(int32_t) { return 0; }
extern "C" int32_t absolute_desktop_gamepad_button(int32_t, int32_t) { return 0; }
extern "C" double absolute_desktop_gamepad_axis(int32_t, int32_t) { return 0.0; }

