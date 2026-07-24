// Minimal OpenGL RHI for absolute.desktop.
// Windows: WGL + OpenGL 3.3 core. Other platforms: stub (isValid = false).

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

extern "C" void* absolute_desktop_native_window(int64_t handle);
extern "C" int32_t absolute_desktop_width(int64_t handle);
extern "C" int32_t absolute_desktop_height(int64_t handle);

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#include <GL/gl.h>

#pragma comment(lib, "opengl32.lib")

// --- WGL / OpenGL types not always in system gl.h ---
using GLchar = char;
using GLsizeiptr = ptrdiff_t;
using GLintptr = ptrdiff_t;

#ifndef GL_VERTEX_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_FLOAT 0x1406
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_TRIANGLES 0x0004
#define GL_TEXTURE_2D 0x0DE1
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TEXTURE0 0x84C0
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#endif

namespace {
    using PFNWGLCREATECONTEXTATTRIBSARB = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
    using PFNWGLCHOOSEPIXELFORMATARB = BOOL(WINAPI*)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);
    using PFNWGLSWAPINTERVALEXT = BOOL(WINAPI*)(int);

    using PFNGLCREATESHADER = GLuint(APIENTRY*)(GLenum);
    using PFNGLSHADERSOURCE = void(APIENTRY*)(GLuint, GLsizei, const GLchar* const*, const GLint*);
    using PFNGLCOMPILESHADER = void(APIENTRY*)(GLuint);
    using PFNGLGETSHADERIV = void(APIENTRY*)(GLuint, GLenum, GLint*);
    using PFNGLGETSHADERINFOLOG = void(APIENTRY*)(GLuint, GLsizei, GLsizei*, GLchar*);
    using PFNGLDELETESHADER = void(APIENTRY*)(GLuint);
    using PFNGLCREATEPROGRAM = GLuint(APIENTRY*)();
    using PFNGLATTACHSHADER = void(APIENTRY*)(GLuint, GLuint);
    using PFNGLLINKPROGRAM = void(APIENTRY*)(GLuint);
    using PFNGLGETPROGRAMIV = void(APIENTRY*)(GLuint, GLenum, GLint*);
    using PFNGLGETPROGRAMINFOLOG = void(APIENTRY*)(GLuint, GLsizei, GLsizei*, GLchar*);
    using PFNGLDELETEPROGRAM = void(APIENTRY*)(GLuint);
    using PFNGLUSEPROGRAM = void(APIENTRY*)(GLuint);
    using PFNGLGETUNIFORMLOCATION = GLint(APIENTRY*)(GLuint, const GLchar*);
    using PFNGLUNIFORM1F = void(APIENTRY*)(GLint, GLfloat);
    using PFNGLUNIFORM1I = void(APIENTRY*)(GLint, GLint);
    using PFNGLGENBUFFERS = void(APIENTRY*)(GLsizei, GLuint*);
    using PFNGLBINDBUFFER = void(APIENTRY*)(GLenum, GLuint);
    using PFNGLBUFFERDATA = void(APIENTRY*)(GLenum, GLsizeiptr, const void*, GLenum);
    using PFNGLDELETEBUFFERS = void(APIENTRY*)(GLsizei, const GLuint*);
    using PFNGLGENVERTEXARRAYS = void(APIENTRY*)(GLsizei, GLuint*);
    using PFNGLBINDVERTEXARRAY = void(APIENTRY*)(GLuint);
    using PFNGLDELETEVERTEXARRAYS = void(APIENTRY*)(GLsizei, const GLuint*);
    using PFNGLENABLEVERTEXATTRIBARRAY = void(APIENTRY*)(GLuint);
    using PFNGLVERTEXATTRIBPOINTER = void(APIENTRY*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
    using PFNGLDRAWARRAYS = void(APIENTRY*)(GLenum, GLint, GLsizei);
    using PFNGLVIEWPORT = void(APIENTRY*)(GLint, GLint, GLsizei, GLsizei);
    using PFNGLCLEARCOLOR = void(APIENTRY*)(GLfloat, GLfloat, GLfloat, GLfloat);
    using PFNGLCLEAR = void(APIENTRY*)(GLbitfield);
    using PFNGLGENTEXTURES = void(APIENTRY*)(GLsizei, GLuint*);
    using PFNGLBINDTEXTURE = void(APIENTRY*)(GLenum, GLuint);
    using PFNGLTEXIMAGE2D = void(APIENTRY*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    using PFNGLTEXPARAMETERI = void(APIENTRY*)(GLenum, GLenum, GLint);
    using PFNGLDELETETEXTURES = void(APIENTRY*)(GLsizei, const GLuint*);
    using PFNGLACTIVETEXTURE = void(APIENTRY*)(GLenum);
    using PFNGLGETERROR = GLenum(APIENTRY*)();

    struct GlFns {
        PFNGLCREATESHADER CreateShader = nullptr;
        PFNGLSHADERSOURCE ShaderSource = nullptr;
        PFNGLCOMPILESHADER CompileShader = nullptr;
        PFNGLGETSHADERIV GetShaderiv = nullptr;
        PFNGLGETSHADERINFOLOG GetShaderInfoLog = nullptr;
        PFNGLDELETESHADER DeleteShader = nullptr;
        PFNGLCREATEPROGRAM CreateProgram = nullptr;
        PFNGLATTACHSHADER AttachShader = nullptr;
        PFNGLLINKPROGRAM LinkProgram = nullptr;
        PFNGLGETPROGRAMIV GetProgramiv = nullptr;
        PFNGLGETPROGRAMINFOLOG GetProgramInfoLog = nullptr;
        PFNGLDELETEPROGRAM DeleteProgram = nullptr;
        PFNGLUSEPROGRAM UseProgram = nullptr;
        PFNGLGETUNIFORMLOCATION GetUniformLocation = nullptr;
        PFNGLUNIFORM1F Uniform1f = nullptr;
        PFNGLUNIFORM1I Uniform1i = nullptr;
        PFNGLGENBUFFERS GenBuffers = nullptr;
        PFNGLBINDBUFFER BindBuffer = nullptr;
        PFNGLBUFFERDATA BufferData = nullptr;
        PFNGLDELETEBUFFERS DeleteBuffers = nullptr;
        PFNGLGENVERTEXARRAYS GenVertexArrays = nullptr;
        PFNGLBINDVERTEXARRAY BindVertexArray = nullptr;
        PFNGLDELETEVERTEXARRAYS DeleteVertexArrays = nullptr;
        PFNGLENABLEVERTEXATTRIBARRAY EnableVertexAttribArray = nullptr;
        PFNGLVERTEXATTRIBPOINTER VertexAttribPointer = nullptr;
        PFNGLDRAWARRAYS DrawArrays = nullptr;
        PFNGLVIEWPORT Viewport = nullptr;
        PFNGLCLEARCOLOR ClearColor = nullptr;
        PFNGLCLEAR Clear = nullptr;
        PFNGLGENTEXTURES GenTextures = nullptr;
        PFNGLBINDTEXTURE BindTexture = nullptr;
        PFNGLTEXIMAGE2D TexImage2D = nullptr;
        PFNGLTEXPARAMETERI TexParameteri = nullptr;
        PFNGLDELETETEXTURES DeleteTextures = nullptr;
        PFNGLACTIVETEXTURE ActiveTexture = nullptr;
        PFNWGLSWAPINTERVALEXT SwapIntervalEXT = nullptr;
    };

    thread_local std::string g_lastError;

    void SetError(std::string message) {
        g_lastError = std::move(message);
    }

    template <typename T>
    T LoadProc(const char* name) {
        PROC proc = wglGetProcAddress(name);
        if (!proc) {
            HMODULE module = GetModuleHandleA("opengl32.dll");
            if (module) proc = GetProcAddress(module, name);
        }
        return reinterpret_cast<T>(proc);
    }

    bool LoadGl(GlFns& gl) {
        gl.CreateShader = LoadProc<PFNGLCREATESHADER>("glCreateShader");
        gl.ShaderSource = LoadProc<PFNGLSHADERSOURCE>("glShaderSource");
        gl.CompileShader = LoadProc<PFNGLCOMPILESHADER>("glCompileShader");
        gl.GetShaderiv = LoadProc<PFNGLGETSHADERIV>("glGetShaderiv");
        gl.GetShaderInfoLog = LoadProc<PFNGLGETSHADERINFOLOG>("glGetShaderInfoLog");
        gl.DeleteShader = LoadProc<PFNGLDELETESHADER>("glDeleteShader");
        gl.CreateProgram = LoadProc<PFNGLCREATEPROGRAM>("glCreateProgram");
        gl.AttachShader = LoadProc<PFNGLATTACHSHADER>("glAttachShader");
        gl.LinkProgram = LoadProc<PFNGLLINKPROGRAM>("glLinkProgram");
        gl.GetProgramiv = LoadProc<PFNGLGETPROGRAMIV>("glGetProgramiv");
        gl.GetProgramInfoLog = LoadProc<PFNGLGETPROGRAMINFOLOG>("glGetProgramInfoLog");
        gl.DeleteProgram = LoadProc<PFNGLDELETEPROGRAM>("glDeleteProgram");
        gl.UseProgram = LoadProc<PFNGLUSEPROGRAM>("glUseProgram");
        gl.GetUniformLocation = LoadProc<PFNGLGETUNIFORMLOCATION>("glGetUniformLocation");
        gl.Uniform1f = LoadProc<PFNGLUNIFORM1F>("glUniform1f");
        gl.Uniform1i = LoadProc<PFNGLUNIFORM1I>("glUniform1i");
        gl.GenBuffers = LoadProc<PFNGLGENBUFFERS>("glGenBuffers");
        gl.BindBuffer = LoadProc<PFNGLBINDBUFFER>("glBindBuffer");
        gl.BufferData = LoadProc<PFNGLBUFFERDATA>("glBufferData");
        gl.DeleteBuffers = LoadProc<PFNGLDELETEBUFFERS>("glDeleteBuffers");
        gl.GenVertexArrays = LoadProc<PFNGLGENVERTEXARRAYS>("glGenVertexArrays");
        gl.BindVertexArray = LoadProc<PFNGLBINDVERTEXARRAY>("glBindVertexArray");
        gl.DeleteVertexArrays = LoadProc<PFNGLDELETEVERTEXARRAYS>("glDeleteVertexArrays");
        gl.EnableVertexAttribArray = LoadProc<PFNGLENABLEVERTEXATTRIBARRAY>("glEnableVertexAttribArray");
        gl.VertexAttribPointer = LoadProc<PFNGLVERTEXATTRIBPOINTER>("glVertexAttribPointer");
        gl.DrawArrays = LoadProc<PFNGLDRAWARRAYS>("glDrawArrays");
        gl.Viewport = LoadProc<PFNGLVIEWPORT>("glViewport");
        gl.ClearColor = LoadProc<PFNGLCLEARCOLOR>("glClearColor");
        gl.Clear = LoadProc<PFNGLCLEAR>("glClear");
        gl.GenTextures = LoadProc<PFNGLGENTEXTURES>("glGenTextures");
        gl.BindTexture = LoadProc<PFNGLBINDTEXTURE>("glBindTexture");
        gl.TexImage2D = LoadProc<PFNGLTEXIMAGE2D>("glTexImage2D");
        gl.TexParameteri = LoadProc<PFNGLTEXPARAMETERI>("glTexParameteri");
        gl.DeleteTextures = LoadProc<PFNGLDELETETEXTURES>("glDeleteTextures");
        gl.ActiveTexture = LoadProc<PFNGLACTIVETEXTURE>("glActiveTexture");
        gl.SwapIntervalEXT = LoadProc<PFNWGLSWAPINTERVALEXT>("wglSwapIntervalEXT");

        return gl.CreateShader && gl.ShaderSource && gl.CompileShader && gl.GetShaderiv
            && gl.CreateProgram && gl.AttachShader && gl.LinkProgram && gl.GetProgramiv
            && gl.UseProgram && gl.GenBuffers && gl.BindBuffer && gl.BufferData
            && gl.GenVertexArrays && gl.BindVertexArray
            && gl.EnableVertexAttribArray && gl.VertexAttribPointer && gl.DrawArrays
            && gl.Viewport && gl.ClearColor && gl.Clear;
    }

    GLuint CompileShader(const GlFns& gl, GLenum type, const char* source) {
        GLuint shader = gl.CreateShader(type);
        gl.ShaderSource(shader, 1, &source, nullptr);
        gl.CompileShader(shader);
        GLint ok = 0;
        gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            GLint len = 0;
            gl.GetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
            std::string log(static_cast<std::size_t>(std::max(len, 1)), '\0');
            if (gl.GetShaderInfoLog) {
                gl.GetShaderInfoLog(shader, len, nullptr, log.data());
            }
            gl.DeleteShader(shader);
            SetError(std::string("shader compile failed: ") + log.c_str());
            return 0;
        }
        return shader;
    }

    GLuint LinkProgram(const GlFns& gl, GLuint vs, GLuint fs) {
        GLuint program = gl.CreateProgram();
        gl.AttachShader(program, vs);
        gl.AttachShader(program, fs);
        gl.LinkProgram(program);
        GLint ok = 0;
        gl.GetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) {
            GLint len = 0;
            gl.GetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
            std::string log(static_cast<std::size_t>(std::max(len, 1)), '\0');
            if (gl.GetProgramInfoLog) {
                gl.GetProgramInfoLog(program, len, nullptr, log.data());
            }
            gl.DeleteProgram(program);
            SetError(std::string("shader link failed: ") + log.c_str());
            return 0;
        }
        return program;
    }

    constexpr const char* kDemoVs = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform float uTime;
out vec3 vColor;
void main() {
    float c = cos(uTime);
    float s = sin(uTime);
    vec2 p = vec2(aPos.x * c - aPos.y * s, aPos.x * s + aPos.y * c);
    gl_Position = vec4(p * 0.7, aPos.z, 1.0);
    vColor = aColor;
}
)GLSL";

    constexpr const char* kDemoFs = R"GLSL(
#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(vColor, 1.0);
}
)GLSL";

    // Layout: pos.xyz + color.rgb (6 floats per vertex).
    constexpr float kDemoVerts[] = {
        0.0f,  0.55f, 0.0f,  1.0f, 0.35f, 0.25f,
       -0.55f, -0.45f, 0.0f,  0.25f, 0.85f, 0.35f,
        0.55f, -0.45f, 0.0f,  0.30f, 0.45f, 1.0f,
    };

    struct GpuShader {
        GLuint program = 0;
    };

    struct GpuBuffer {
        GLuint vbo = 0;
        GLuint vao = 0;
        int32_t vertexCount = 0; // when layout is known (6 floats/vert)
    };

    struct GpuTexture {
        GLuint id = 0;
        int32_t width = 0;
        int32_t height = 0;
    };

    struct GpuDevice {
        int64_t windowHandle = 0;
        HWND hwnd = nullptr;
        HDC hdc = nullptr;
        HGLRC hglrc = nullptr;
        GlFns gl{};
        GLuint demoProgram = 0;
        GLuint demoVao = 0;
        GLuint demoVbo = 0;
        bool valid = false;

        void ReleaseGlObjects() {
            if (!hglrc || !hdc) return;
            wglMakeCurrent(hdc, hglrc);
            if (demoProgram) {
                gl.DeleteProgram(demoProgram);
                demoProgram = 0;
            }
            if (demoVao) {
                gl.DeleteVertexArrays(1, &demoVao);
                demoVao = 0;
            }
            if (demoVbo) {
                gl.DeleteBuffers(1, &demoVbo);
                demoVbo = 0;
            }
        }

        void Destroy() {
            if (hglrc) {
                ReleaseGlObjects();
                wglMakeCurrent(nullptr, nullptr);
                wglDeleteContext(hglrc);
                hglrc = nullptr;
            }
            if (hdc && hwnd) {
                ReleaseDC(hwnd, hdc);
                hdc = nullptr;
            }
            hwnd = nullptr;
            valid = false;
        }
    };

    GpuDevice* DeviceFromHandle(int64_t handle) {
        return reinterpret_cast<GpuDevice*>(static_cast<intptr_t>(handle));
    }

    int64_t DeviceToHandle(GpuDevice* device) {
        return static_cast<int64_t>(reinterpret_cast<intptr_t>(device));
    }

    GpuShader* ShaderFromHandle(int64_t handle) {
        return reinterpret_cast<GpuShader*>(static_cast<intptr_t>(handle));
    }

    GpuBuffer* BufferFromHandle(int64_t handle) {
        return reinterpret_cast<GpuBuffer*>(static_cast<intptr_t>(handle));
    }

    GpuTexture* TextureFromHandle(int64_t handle) {
        return reinterpret_cast<GpuTexture*>(static_cast<intptr_t>(handle));
    }

    bool MakeCurrent(GpuDevice& device) {
        if (!device.valid || !device.hdc || !device.hglrc) return false;
        if (!wglMakeCurrent(device.hdc, device.hglrc)) {
            SetError("wglMakeCurrent failed");
            return false;
        }
        const int32_t w = absolute_desktop_width(device.windowHandle);
        const int32_t h = absolute_desktop_height(device.windowHandle);
        if (device.gl.Viewport && w > 0 && h > 0) {
            device.gl.Viewport(0, 0, w, h);
        }
        return true;
    }

    bool CreateModernContext(HDC hdc, HGLRC* out) {
        *out = nullptr;
        // Temporary context to load WGL extensions.
        PIXELFORMATDESCRIPTOR pfd{};
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cDepthBits = 24;
        pfd.iLayerType = PFD_MAIN_PLANE;
        const int format = ChoosePixelFormat(hdc, &pfd);
        if (format == 0 || !SetPixelFormat(hdc, format, &pfd)) {
            SetError("SetPixelFormat failed");
            return false;
        }
        HGLRC temp = wglCreateContext(hdc);
        if (!temp || !wglMakeCurrent(hdc, temp)) {
            if (temp) wglDeleteContext(temp);
            SetError("temporary WGL context failed");
            return false;
        }

        auto createAttribs = LoadProc<PFNWGLCREATECONTEXTATTRIBSARB>("wglCreateContextAttribsARB");
        HGLRC modern = nullptr;
        if (createAttribs) {
            const int attribs[] = {
                WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
                WGL_CONTEXT_MINOR_VERSION_ARB, 3,
                WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                0
            };
            modern = createAttribs(hdc, nullptr, attribs);
        }

        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(temp);

        if (!modern) {
            // Fallback: legacy context (may still load many entry points).
            modern = wglCreateContext(hdc);
            if (!modern) {
                SetError("OpenGL 3.3 core context unavailable");
                return false;
            }
        }
        if (!wglMakeCurrent(hdc, modern)) {
            wglDeleteContext(modern);
            SetError("failed to activate OpenGL context");
            return false;
        }
        *out = modern;
        return true;
    }

    bool InitDemoResources(GpuDevice& device) {
        GlFns& gl = device.gl;
        GLuint vs = CompileShader(gl, GL_VERTEX_SHADER, kDemoVs);
        if (!vs) return false;
        GLuint fs = CompileShader(gl, GL_FRAGMENT_SHADER, kDemoFs);
        if (!fs) {
            gl.DeleteShader(vs);
            return false;
        }
        device.demoProgram = LinkProgram(gl, vs, fs);
        gl.DeleteShader(vs);
        gl.DeleteShader(fs);
        if (!device.demoProgram) return false;

        gl.GenVertexArrays(1, &device.demoVao);
        gl.GenBuffers(1, &device.demoVbo);
        gl.BindVertexArray(device.demoVao);
        gl.BindBuffer(GL_ARRAY_BUFFER, device.demoVbo);
        gl.BufferData(GL_ARRAY_BUFFER, sizeof(kDemoVerts), kDemoVerts, GL_STATIC_DRAW);
        const GLsizei stride = static_cast<GLsizei>(6 * sizeof(float));
        gl.EnableVertexAttribArray(0);
        gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
        gl.EnableVertexAttribArray(1);
        gl.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
        gl.BindVertexArray(0);
        return true;
    }

    // Soft sprite layout (must match desktop_soft_sprites.cpp).
    struct DesktopSpriteView {
        int32_t width = 1;
        int32_t height = 1;
        std::vector<uint32_t> pixels;
    };

    DesktopSpriteView* SpriteFromHandle(int64_t handle) {
        return reinterpret_cast<DesktopSpriteView*>(static_cast<intptr_t>(handle));
    }
}

extern "C" int64_t absolute_desktop_gpu_create(int64_t windowHandle) {
    g_lastError.clear();
    HWND hwnd = static_cast<HWND>(absolute_desktop_native_window(windowHandle));
    if (!hwnd) {
        SetError("window has no native handle");
        return 0;
    }

    auto* device = new GpuDevice();
    device->windowHandle = windowHandle;
    device->hwnd = hwnd;
    device->hdc = GetDC(hwnd);
    if (!device->hdc) {
        SetError("GetDC failed");
        delete device;
        return 0;
    }

    if (!CreateModernContext(device->hdc, &device->hglrc)) {
        device->Destroy();
        delete device;
        return 0;
    }

    if (!LoadGl(device->gl)) {
        SetError("failed to load OpenGL entry points");
        device->Destroy();
        delete device;
        return 0;
    }

    if (device->gl.SwapIntervalEXT) {
        device->gl.SwapIntervalEXT(1);
    }

    if (!InitDemoResources(*device)) {
        device->Destroy();
        delete device;
        return 0;
    }

    device->valid = true;
    wglMakeCurrent(nullptr, nullptr);
    return DeviceToHandle(device);
}

extern "C" void absolute_desktop_gpu_destroy(int64_t handle) {
    GpuDevice* device = DeviceFromHandle(handle);
    if (!device) return;
    device->Destroy();
    delete device;
}

extern "C" int32_t absolute_desktop_gpu_is_valid(int64_t handle) {
    const GpuDevice* device = DeviceFromHandle(handle);
    return device && device->valid ? 1 : 0;
}

extern "C" const char* absolute_desktop_gpu_backend() {
    return "opengl";
}

extern "C" const char* absolute_desktop_gpu_last_error() {
    return g_lastError.c_str();
}

extern "C" void absolute_desktop_gpu_make_current(int64_t handle) {
    GpuDevice* device = DeviceFromHandle(handle);
    if (!device) return;
    MakeCurrent(*device);
}

extern "C" void absolute_desktop_gpu_clear(int64_t handle, float r, float g, float b, float a) {
    GpuDevice* device = DeviceFromHandle(handle);
    if (!device || !MakeCurrent(*device)) return;
    device->gl.ClearColor(r, g, b, a);
    device->gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

extern "C" void absolute_desktop_gpu_present(int64_t handle) {
    GpuDevice* device = DeviceFromHandle(handle);
    if (!device || !device->hdc) return;
    if (!MakeCurrent(*device)) return;
    SwapBuffers(device->hdc);
}

extern "C" void absolute_desktop_gpu_draw_demo_triangle(int64_t handle, float timeSeconds) {
    GpuDevice* device = DeviceFromHandle(handle);
    if (!device || !MakeCurrent(*device) || !device->demoProgram) return;
    GlFns& gl = device->gl;
    gl.UseProgram(device->demoProgram);
    const GLint loc = gl.GetUniformLocation(device->demoProgram, "uTime");
    if (loc >= 0) gl.Uniform1f(loc, timeSeconds);
    gl.BindVertexArray(device->demoVao);
    gl.DrawArrays(GL_TRIANGLES, 0, 3);
    gl.BindVertexArray(0);
}

extern "C" int64_t absolute_desktop_gpu_shader_create(
    int64_t gpuHandle, const char* vertexSource, const char* fragmentSource) {
    GpuDevice* device = DeviceFromHandle(gpuHandle);
    if (!device || !MakeCurrent(*device) || !vertexSource || !fragmentSource) {
        SetError("invalid shader create arguments");
        return 0;
    }
    GlFns& gl = device->gl;
    GLuint vs = CompileShader(gl, GL_VERTEX_SHADER, vertexSource);
    if (!vs) return 0;
    GLuint fs = CompileShader(gl, GL_FRAGMENT_SHADER, fragmentSource);
    if (!fs) {
        gl.DeleteShader(vs);
        return 0;
    }
    GLuint program = LinkProgram(gl, vs, fs);
    gl.DeleteShader(vs);
    gl.DeleteShader(fs);
    if (!program) return 0;
    auto* shader = new GpuShader();
    shader->program = program;
    return static_cast<int64_t>(reinterpret_cast<intptr_t>(shader));
}

extern "C" void absolute_desktop_gpu_shader_destroy(int64_t gpuHandle, int64_t shaderHandle) {
    GpuDevice* device = DeviceFromHandle(gpuHandle);
    GpuShader* shader = ShaderFromHandle(shaderHandle);
    if (!device || !shader) return;
    if (MakeCurrent(*device) && shader->program) {
        device->gl.DeleteProgram(shader->program);
    }
    delete shader;
}

// Interleaved float data: [x,y,z,r,g,b] * vertexCount (floatCount must be multiple of 6).
extern "C" int64_t absolute_desktop_gpu_buffer_create(
    int64_t gpuHandle, const float* data, int32_t floatCount) {
    GpuDevice* device = DeviceFromHandle(gpuHandle);
    if (!device || !MakeCurrent(*device) || !data || floatCount < 6 || (floatCount % 6) != 0) {
        SetError("vertex buffer requires floatCount multiple of 6 (pos3+color3)");
        return 0;
    }
    GlFns& gl = device->gl;
    auto* buffer = new GpuBuffer();
    buffer->vertexCount = floatCount / 6;
    gl.GenVertexArrays(1, &buffer->vao);
    gl.GenBuffers(1, &buffer->vbo);
    gl.BindVertexArray(buffer->vao);
    gl.BindBuffer(GL_ARRAY_BUFFER, buffer->vbo);
    gl.BufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(floatCount) * static_cast<GLsizeiptr>(sizeof(float)),
        data, GL_STATIC_DRAW);
    const GLsizei stride = static_cast<GLsizei>(6 * sizeof(float));
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    gl.EnableVertexAttribArray(1);
    gl.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
    gl.BindVertexArray(0);
    return static_cast<int64_t>(reinterpret_cast<intptr_t>(buffer));
}

extern "C" void absolute_desktop_gpu_buffer_destroy(int64_t gpuHandle, int64_t bufferHandle) {
    GpuDevice* device = DeviceFromHandle(gpuHandle);
    GpuBuffer* buffer = BufferFromHandle(bufferHandle);
    if (!device || !buffer) return;
    if (MakeCurrent(*device)) {
        if (buffer->vao) device->gl.DeleteVertexArrays(1, &buffer->vao);
        if (buffer->vbo) device->gl.DeleteBuffers(1, &buffer->vbo);
    }
    delete buffer;
}

extern "C" void absolute_desktop_gpu_draw(
    int64_t gpuHandle, int64_t shaderHandle, int64_t bufferHandle, int32_t vertexCount) {
    GpuDevice* device = DeviceFromHandle(gpuHandle);
    GpuShader* shader = ShaderFromHandle(shaderHandle);
    GpuBuffer* buffer = BufferFromHandle(bufferHandle);
    if (!device || !shader || !buffer || !MakeCurrent(*device)) return;
    const int32_t count = vertexCount > 0 ? vertexCount : buffer->vertexCount;
    if (count <= 0 || !shader->program || !buffer->vao) return;
    GlFns& gl = device->gl;
    gl.UseProgram(shader->program);
    gl.BindVertexArray(buffer->vao);
    gl.DrawArrays(GL_TRIANGLES, 0, count);
    gl.BindVertexArray(0);
}

extern "C" void absolute_desktop_gpu_set_uniform_f(
    int64_t gpuHandle, int64_t shaderHandle, const char* name, float value) {
    GpuDevice* device = DeviceFromHandle(gpuHandle);
    GpuShader* shader = ShaderFromHandle(shaderHandle);
    if (!device || !shader || !name || !MakeCurrent(*device) || !shader->program) return;
    const GLint loc = device->gl.GetUniformLocation(shader->program, name);
    if (loc >= 0) device->gl.Uniform1f(loc, value);
}

// Upload soft sprite 0x00RRGGBB pixels as RGBA8 texture (0 remains transparent black).
extern "C" int64_t absolute_desktop_gpu_texture_from_sprite(int64_t gpuHandle, int64_t spriteHandle) {
    GpuDevice* device = DeviceFromHandle(gpuHandle);
    DesktopSpriteView* sprite = SpriteFromHandle(spriteHandle);
    if (!device || !sprite || sprite->pixels.empty() || !MakeCurrent(*device)) {
        SetError("invalid sprite for texture upload");
        return 0;
    }
    if (!device->gl.GenTextures || !device->gl.TexImage2D) {
        SetError("texture entry points unavailable");
        return 0;
    }

    const int32_t w = sprite->width;
    const int32_t h = sprite->height;
    std::vector<uint8_t> rgba(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u);
    for (int32_t i = 0; i < w * h; ++i) {
        const uint32_t c = sprite->pixels[static_cast<std::size_t>(i)];
        rgba[static_cast<std::size_t>(i) * 4u + 0] = static_cast<uint8_t>((c >> 16) & 0xFF);
        rgba[static_cast<std::size_t>(i) * 4u + 1] = static_cast<uint8_t>((c >> 8) & 0xFF);
        rgba[static_cast<std::size_t>(i) * 4u + 2] = static_cast<uint8_t>(c & 0xFF);
        rgba[static_cast<std::size_t>(i) * 4u + 3] = c == 0 ? 0 : 255;
    }

    auto* texture = new GpuTexture();
    texture->width = w;
    texture->height = h;
    GlFns& gl = device->gl;
    gl.GenTextures(1, &texture->id);
    gl.BindTexture(GL_TEXTURE_2D, texture->id);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    gl.BindTexture(GL_TEXTURE_2D, 0);
    return static_cast<int64_t>(reinterpret_cast<intptr_t>(texture));
}

extern "C" void absolute_desktop_gpu_texture_destroy(int64_t gpuHandle, int64_t textureHandle) {
    GpuDevice* device = DeviceFromHandle(gpuHandle);
    GpuTexture* texture = TextureFromHandle(textureHandle);
    if (!device || !texture) return;
    if (MakeCurrent(*device) && texture->id && device->gl.DeleteTextures) {
        device->gl.DeleteTextures(1, &texture->id);
    }
    delete texture;
}

extern "C" void absolute_desktop_gpu_bind_texture(
    int64_t gpuHandle, int64_t textureHandle, int32_t unit) {
    GpuDevice* device = DeviceFromHandle(gpuHandle);
    GpuTexture* texture = TextureFromHandle(textureHandle);
    if (!device || !texture || !MakeCurrent(*device) || !device->gl.ActiveTexture) return;
    if (unit < 0) unit = 0;
    device->gl.ActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    device->gl.BindTexture(GL_TEXTURE_2D, texture->id);
}

#else // !_WIN32

namespace {
    thread_local std::string g_lastError = "OpenGL RHI is currently implemented on Windows (WGL) only";
}

extern "C" int64_t absolute_desktop_gpu_create(int64_t) {
    return 0;
}
extern "C" void absolute_desktop_gpu_destroy(int64_t) {}
extern "C" int32_t absolute_desktop_gpu_is_valid(int64_t) { return 0; }
extern "C" const char* absolute_desktop_gpu_backend() { return "none"; }
extern "C" const char* absolute_desktop_gpu_last_error() { return g_lastError.c_str(); }
extern "C" void absolute_desktop_gpu_make_current(int64_t) {}
extern "C" void absolute_desktop_gpu_clear(int64_t, float, float, float, float) {}
extern "C" void absolute_desktop_gpu_present(int64_t) {}
extern "C" void absolute_desktop_gpu_draw_demo_triangle(int64_t, float) {}
extern "C" int64_t absolute_desktop_gpu_shader_create(int64_t, const char*, const char*) { return 0; }
extern "C" void absolute_desktop_gpu_shader_destroy(int64_t, int64_t) {}
extern "C" int64_t absolute_desktop_gpu_buffer_create(int64_t, const float*, int32_t) { return 0; }
extern "C" void absolute_desktop_gpu_buffer_destroy(int64_t, int64_t) {}
extern "C" void absolute_desktop_gpu_draw(int64_t, int64_t, int64_t, int32_t) {}
extern "C" void absolute_desktop_gpu_set_uniform_f(int64_t, int64_t, const char*, float) {}
extern "C" int64_t absolute_desktop_gpu_texture_from_sprite(int64_t, int64_t) { return 0; }
extern "C" void absolute_desktop_gpu_texture_destroy(int64_t, int64_t) {}
extern "C" void absolute_desktop_gpu_bind_texture(int64_t, int64_t, int32_t) {}

#endif
