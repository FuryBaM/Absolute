#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

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
        std::array<bool, 3> mouseButtons{};
        int32_t mouseX = 0;
        int32_t mouseY = 0;

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
    };

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

extern "C" int32_t absolute_desktop_poll(int64_t handle) {
    DesktopWindow* state = FromHandle(handle);
    if (!state) return 0;
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
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
    if (!state || x < 0 || y < 0 || x >= state->width || y >= state->height) return;
    state->pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(state->width) + static_cast<std::size_t>(x)] = color;
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

extern "C" uint32_t absolute_desktop_rgb(int32_t red, int32_t green, int32_t blue) {
    const uint32_t r = static_cast<uint32_t>(std::clamp(red, 0, 255));
    const uint32_t g = static_cast<uint32_t>(std::clamp(green, 0, 255));
    const uint32_t b = static_cast<uint32_t>(std::clamp(blue, 0, 255));
    return (r << 16U) | (g << 8U) | b;
}

extern "C" double absolute_desktop_time() {
    LARGE_INTEGER frequency{};
    LARGE_INTEGER counter{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
}

extern "C" void absolute_desktop_sleep(int32_t milliseconds) {
    if (milliseconds > 0) Sleep(static_cast<DWORD>(milliseconds));
}

