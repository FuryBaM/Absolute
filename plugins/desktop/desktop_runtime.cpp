#define NOMINMAX
#include <Windows.h>
#include <Xinput.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#pragma comment(lib, "Xinput9_1_0.lib")

namespace {
    constexpr wchar_t WindowClassName[] = L"AbsoluteDesktopWindowV1";

    struct DesktopWindow {
        HWND window = nullptr;
        int32_t width = 1;
        int32_t height = 1;
        bool open = true;
        std::vector<uint32_t> pixels;
        BITMAPINFO bitmap{};
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
            bitmap = {};
            bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bitmap.bmiHeader.biWidth = width;
            bitmap.bmiHeader.biHeight = -height;
            bitmap.bmiHeader.biPlanes = 1;
            bitmap.bmiHeader.biBitCount = 32;
            bitmap.bmiHeader.biCompression = BI_RGB;
        }

        void PutPixel(int32_t x, int32_t y, uint32_t color) {
            if (x < 0 || y < 0 || x >= width || y >= height) return;
            pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = color;
        }
    };

    double NowSeconds() {
        LARGE_INTEGER frequency{};
        LARGE_INTEGER counter{};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&counter);
        return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
    }

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
        const double now = NowSeconds();
        if (state.lastTime < 0.0) state.deltaTime = 0.0;
        else state.deltaTime = std::max(0.0, now - state.lastTime);
        state.lastTime = now;
    }

    DesktopWindow* FromHandle(int64_t handle) {
        return reinterpret_cast<DesktopWindow*>(static_cast<intptr_t>(handle));
    }

    std::wstring ToWide(const char* text) {
        if (!text) return L"Absolute";
        const int size = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
        if (size <= 0) return L"Absolute";
        std::wstring result(static_cast<std::size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), size);
        if (!result.empty() && result.back() == L'\0') result.pop_back();
        return result;
    }

    void Draw(DesktopWindow& state, HDC device, int targetWidth, int targetHeight) {
        if (state.pixels.empty()) return;
        StretchDIBits(device, 0, 0, targetWidth, targetHeight,
            0, 0, state.width, state.height, state.pixels.data(), &state.bitmap,
            DIB_RGB_COLORS, SRCCOPY);
    }

    LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* state = reinterpret_cast<DesktopWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            state = static_cast<DesktopWindow*>(create->lpCreateParams);
            state->window = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        }
        if (!state) return DefWindowProcW(window, message, wParam, lParam);

        switch (message) {
        case WM_CLOSE:
            state->open = false;
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            state->open = false;
            state->window = nullptr;
            return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            return DefWindowProcW(window, message, wParam, lParam);
        case WM_SIZE:
            state->Resize(static_cast<int32_t>(LOWORD(lParam)), static_cast<int32_t>(HIWORD(lParam)));
            return 0;
        case WM_KEYDOWN: case WM_SYSKEYDOWN:
            if (wParam < state->keys.size()) state->keys[wParam] = true;
            return 0;
        case WM_KEYUP: case WM_SYSKEYUP:
            if (wParam < state->keys.size()) state->keys[wParam] = false;
            return 0;
        case WM_CHAR: {
            // Store Unicode code points (BMP). Ignore control chars except tab/newline/backspace.
            const auto ch = static_cast<int32_t>(wParam);
            if (ch == 8 || ch == 9 || ch == 10 || ch == 13 || ch >= 32) {
                if (state->textQueue.size() < 256) state->textQueue.push_back(ch);
            }
            return 0;
        }
        case WM_KILLFOCUS:
            state->keys.fill(false);
            state->mouseButtons.fill(false);
            return 0;
        case WM_MOUSEMOVE:
            state->mouseX = static_cast<int16_t>(LOWORD(lParam));
            state->mouseY = static_cast<int16_t>(HIWORD(lParam));
            return 0;
        case WM_LBUTTONDOWN: state->mouseButtons[0] = true; return 0;
        case WM_LBUTTONUP: state->mouseButtons[0] = false; return 0;
        case WM_RBUTTONDOWN: state->mouseButtons[1] = true; return 0;
        case WM_RBUTTONUP: state->mouseButtons[1] = false; return 0;
        case WM_MBUTTONDOWN: state->mouseButtons[2] = true; return 0;
        case WM_MBUTTONUP: state->mouseButtons[2] = false; return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC device = BeginPaint(window, &paint);
            RECT client{};
            GetClientRect(window, &client);
            Draw(*state, device, client.right - client.left, client.bottom - client.top);
            EndPaint(window, &paint);
            return 0;
        }
        default:
            return DefWindowProcW(window, message, wParam, lParam);
        }
    }

    bool RegisterWindowClass() {
        static std::once_flag registration;
        static bool registered = false;
        std::call_once(registration, [] {
            SetProcessDPIAware();
            WNDCLASSEXW type{};
            type.cbSize = sizeof(type);
            type.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
            type.lpfnWndProc = WindowProcedure;
            type.hInstance = GetModuleHandleW(nullptr);
            type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            type.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
            type.hbrBackground = nullptr;
            type.lpszClassName = WindowClassName;
            registered = RegisterClassExW(&type) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        });
        return registered;
    }
}

extern "C" int64_t absolute_desktop_create(const char* title, int32_t width, int32_t height, int32_t resizable) {
    if (!RegisterWindowClass() || width <= 0 || height <= 0) return 0;
    auto* state = new DesktopWindow();
    state->Resize(width, height);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    if (resizable != 0) style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
    RECT bounds{0, 0, width, height};
    AdjustWindowRect(&bounds, style, FALSE);
    const std::wstring wideTitle = ToWide(title);
    HWND window = CreateWindowExW(0, WindowClassName, wideTitle.c_str(), style,
        CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, GetModuleHandleW(nullptr), state);
    if (!window) { delete state; return 0; }
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    return static_cast<int64_t>(reinterpret_cast<intptr_t>(state));
}

extern "C" void absolute_desktop_destroy(int64_t handle) {
    DesktopWindow* state = FromHandle(handle);
    if (!state) return;
    if (state->window) DestroyWindow(state->window);
    state->open = false;
    delete state;
}

// Forward-declared; defined with gamepad state below.
static void PollGamepads();

extern "C" int32_t absolute_desktop_poll(int64_t handle) {
    DesktopWindow* state = FromHandle(handle);
    if (!state) return 0;
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    UpdateInputEdges(*state);
    UpdateDelta(*state);
    PollGamepads();
    return state->open && state->window ? 1 : 0;
}

extern "C" int32_t absolute_desktop_is_open(int64_t handle) {
    const DesktopWindow* state = FromHandle(handle);
    return state && state->open && state->window ? 1 : 0;
}

extern "C" void absolute_desktop_set_title(int64_t handle, const char* title) {
    DesktopWindow* state = FromHandle(handle);
    if (state && state->window) SetWindowTextW(state->window, ToWide(title).c_str());
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

extern "C" void absolute_desktop_blit(
    int64_t handle, int32_t destX, int32_t destY, int32_t width, int32_t height, const uint32_t* pixels) {
    DesktopWindow* state = FromHandle(handle);
    if (!state || !pixels || width <= 0 || height <= 0) return;
    for (int32_t row = 0; row < height; ++row) {
        const int32_t y = destY + row;
        if (y < 0 || y >= state->height) continue;
        for (int32_t col = 0; col < width; ++col) {
            const int32_t x = destX + col;
            if (x < 0 || x >= state->width) continue;
            const uint32_t color = pixels[static_cast<std::size_t>(row) * static_cast<std::size_t>(width)
                + static_cast<std::size_t>(col)];
            // Treat 0xFF000000-style fully transparent black as skip only if alpha high-nibble zero-ish:
            // our soft framebuffer is 0x00RRGGBB; 0 means transparent for blits.
            if (color == 0) continue;
            state->PutPixel(x, y, color & 0x00FFFFFFu);
        }
    }
}

extern "C" void absolute_desktop_present(int64_t handle) {
    DesktopWindow* state = FromHandle(handle);
    if (!state || !state->window) return;
    HDC device = GetDC(state->window);
    RECT client{};
    GetClientRect(state->window, &client);
    Draw(*state, device, client.right - client.left, client.bottom - client.top);
    ReleaseDC(state->window, device);
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
    return NowSeconds();
}

extern "C" void absolute_desktop_sleep(int32_t milliseconds) {
    if (milliseconds > 0) Sleep(static_cast<DWORD>(milliseconds));
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

namespace {
    struct GamepadState {
        bool connected = false;
        std::array<bool, 16> buttons{};
        std::array<float, 6> axes{};
    };

    std::array<GamepadState, 4> g_gamepads{};

    float NormalizeStick(SHORT value, SHORT deadzone) {
        if (value > -deadzone && value < deadzone) return 0.0f;
        const float scaled = static_cast<float>(value) / 32767.0f;
        return std::clamp(scaled, -1.0f, 1.0f);
    }

    float NormalizeTrigger(BYTE value) {
        if (value < XINPUT_GAMEPAD_TRIGGER_THRESHOLD) return 0.0f;
        return static_cast<float>(value) / 255.0f;
    }
}

static void PollGamepads() {
    for (DWORD i = 0; i < g_gamepads.size(); ++i) {
        XINPUT_STATE xstate{};
        const DWORD result = XInputGetState(i, &xstate);
        GamepadState& pad = g_gamepads[i];
        if (result != ERROR_SUCCESS) {
            pad = {};
            continue;
        }
        pad.connected = true;
        const WORD b = xstate.Gamepad.wButtons;
        pad.buttons[0] = (b & XINPUT_GAMEPAD_A) != 0;
        pad.buttons[1] = (b & XINPUT_GAMEPAD_B) != 0;
        pad.buttons[2] = (b & XINPUT_GAMEPAD_X) != 0;
        pad.buttons[3] = (b & XINPUT_GAMEPAD_Y) != 0;
        pad.buttons[4] = (b & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
        pad.buttons[5] = (b & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
        pad.buttons[6] = (b & XINPUT_GAMEPAD_BACK) != 0;
        pad.buttons[7] = (b & XINPUT_GAMEPAD_START) != 0;
        pad.buttons[8] = (b & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
        pad.buttons[9] = (b & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
        pad.buttons[10] = (b & XINPUT_GAMEPAD_DPAD_UP) != 0;
        pad.buttons[11] = (b & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
        pad.buttons[12] = (b & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
        pad.buttons[13] = (b & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
        pad.axes[0] = NormalizeStick(xstate.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        pad.axes[1] = -NormalizeStick(xstate.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        pad.axes[2] = NormalizeStick(xstate.Gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
        pad.axes[3] = -NormalizeStick(xstate.Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
        pad.axes[4] = NormalizeTrigger(xstate.Gamepad.bLeftTrigger);
        pad.axes[5] = NormalizeTrigger(xstate.Gamepad.bRightTrigger);
    }
}

extern "C" int32_t absolute_desktop_gamepad_connected(int32_t index) {
    if (index < 0 || index >= static_cast<int32_t>(g_gamepads.size())) return 0;
    return g_gamepads[static_cast<std::size_t>(index)].connected ? 1 : 0;
}

extern "C" int32_t absolute_desktop_gamepad_button(int32_t index, int32_t button) {
    if (index < 0 || index >= static_cast<int32_t>(g_gamepads.size())) return 0;
    if (button < 0 || button >= 16) return 0;
    const auto& pad = g_gamepads[static_cast<std::size_t>(index)];
    return pad.connected && pad.buttons[static_cast<std::size_t>(button)] ? 1 : 0;
}

extern "C" double absolute_desktop_gamepad_axis(int32_t index, int32_t axis) {
    if (index < 0 || index >= static_cast<int32_t>(g_gamepads.size())) return 0.0;
    if (axis < 0 || axis >= 6) return 0.0;
    const auto& pad = g_gamepads[static_cast<std::size_t>(index)];
    if (!pad.connected) return 0.0;
    return static_cast<double>(pad.axes[static_cast<std::size_t>(axis)]);
}

