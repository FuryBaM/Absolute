// Minimal OpenGL RHI for absolute.desktop.
// Model: Shader + VertexBuffer + VertexLayout + Pipeline;
// frame: beginFrame / clear / bind / draw / endFrame / present.
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
#define GL_BLEND 0x0BE2
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#endif

namespace {
    using PFNWGLCREATECONTEXTATTRIBSARB = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
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
    using PFNGLUNIFORM2F = void(APIENTRY*)(GLint, GLfloat, GLfloat);
    using PFNGLGENBUFFERS = void(APIENTRY*)(GLsizei, GLuint*);
    using PFNGLBINDBUFFER = void(APIENTRY*)(GLenum, GLuint);
    using PFNGLBUFFERDATA = void(APIENTRY*)(GLenum, GLsizeiptr, const void*, GLenum);
    using PFNGLDELETEBUFFERS = void(APIENTRY*)(GLsizei, const GLuint*);
    using PFNGLGENVERTEXARRAYS = void(APIENTRY*)(GLsizei, GLuint*);
    using PFNGLBINDVERTEXARRAY = void(APIENTRY*)(GLuint);
    using PFNGLDELETEVERTEXARRAYS = void(APIENTRY*)(GLsizei, const GLuint*);
    using PFNGLENABLEVERTEXATTRIBARRAY = void(APIENTRY*)(GLuint);
    using PFNGLDISABLEVERTEXATTRIBARRAY = void(APIENTRY*)(GLuint);
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
    using PFNGLENABLE = void(APIENTRY*)(GLenum);
    using PFNGLDISABLE = void(APIENTRY*)(GLenum);
    using PFNGLBLENDFUNC = void(APIENTRY*)(GLenum, GLenum);

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
        PFNGLUNIFORM2F Uniform2f = nullptr;
        PFNGLGENBUFFERS GenBuffers = nullptr;
        PFNGLBINDBUFFER BindBuffer = nullptr;
        PFNGLBUFFERDATA BufferData = nullptr;
        PFNGLDELETEBUFFERS DeleteBuffers = nullptr;
        PFNGLGENVERTEXARRAYS GenVertexArrays = nullptr;
        PFNGLBINDVERTEXARRAY BindVertexArray = nullptr;
        PFNGLDELETEVERTEXARRAYS DeleteVertexArrays = nullptr;
        PFNGLENABLEVERTEXATTRIBARRAY EnableVertexAttribArray = nullptr;
        PFNGLDISABLEVERTEXATTRIBARRAY DisableVertexAttribArray = nullptr;
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
        PFNGLENABLE Enable = nullptr;
        PFNGLDISABLE Disable = nullptr;
        PFNGLBLENDFUNC BlendFunc = nullptr;
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
        gl.Uniform2f = LoadProc<PFNGLUNIFORM2F>("glUniform2f");
        gl.GenBuffers = LoadProc<PFNGLGENBUFFERS>("glGenBuffers");
        gl.BindBuffer = LoadProc<PFNGLBINDBUFFER>("glBindBuffer");
        gl.BufferData = LoadProc<PFNGLBUFFERDATA>("glBufferData");
        gl.DeleteBuffers = LoadProc<PFNGLDELETEBUFFERS>("glDeleteBuffers");
        gl.GenVertexArrays = LoadProc<PFNGLGENVERTEXARRAYS>("glGenVertexArrays");
        gl.BindVertexArray = LoadProc<PFNGLBINDVERTEXARRAY>("glBindVertexArray");
        gl.DeleteVertexArrays = LoadProc<PFNGLDELETEVERTEXARRAYS>("glDeleteVertexArrays");
        gl.EnableVertexAttribArray = LoadProc<PFNGLENABLEVERTEXATTRIBARRAY>("glEnableVertexAttribArray");
        gl.DisableVertexAttribArray = LoadProc<PFNGLDISABLEVERTEXATTRIBARRAY>("glDisableVertexAttribArray");
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
        gl.Enable = LoadProc<PFNGLENABLE>("glEnable");
        gl.Disable = LoadProc<PFNGLDISABLE>("glDisable");
        gl.BlendFunc = LoadProc<PFNGLBLENDFUNC>("glBlendFunc");
        gl.SwapIntervalEXT = LoadProc<PFNWGLSWAPINTERVALEXT>("wglSwapIntervalEXT");

        return gl.CreateShader && gl.ShaderSource && gl.CompileShader && gl.GetShaderiv
            && gl.CreateProgram && gl.AttachShader && gl.LinkProgram && gl.GetProgramiv
            && gl.UseProgram && gl.GenBuffers && gl.BindBuffer && gl.BufferData
            && gl.GenVertexArrays && gl.BindVertexArray
            && gl.EnableVertexAttribArray && gl.VertexAttribPointer && gl.DrawArrays
            && gl.Viewport && gl.ClearColor && gl.Clear
            && gl.Enable && gl.BlendFunc && gl.Uniform1i && gl.Uniform2f;
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
            if (gl.GetShaderInfoLog) gl.GetShaderInfoLog(shader, len, nullptr, log.data());
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
            if (gl.GetProgramInfoLog) gl.GetProgramInfoLog(program, len, nullptr, log.data());
            gl.DeleteProgram(program);
            SetError(std::string("shader link failed: ") + log.c_str());
            return 0;
        }
        return program;
    }

    struct VertexAttr {
        int32_t location = 0;
        int32_t components = 0;
        int32_t offsetBytes = 0;
    };

    struct GpuLayout {
        int32_t strideBytes = 0;
        std::vector<VertexAttr> attrs;
    };

    struct GpuShader {
        GLuint program = 0;
    };

    struct GpuBuffer {
        GLuint vbo = 0;
        int32_t byteSize = 0;
        int32_t floatCount = 0;
    };

    struct GpuPipeline {
        GLuint program = 0;
        int32_t strideBytes = 0;
        std::vector<VertexAttr> attrs;
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
        GLuint defaultVao = 0;
        bool valid = false;
        bool inFrame = false;
        int64_t boundPipeline = 0;
        int64_t boundBuffer = 0;
        bool attrsDirty = true;

        void ReleaseGlObjects() {
            if (!hglrc || !hdc) return;
            wglMakeCurrent(hdc, hglrc);
            if (defaultVao) {
                gl.DeleteVertexArrays(1, &defaultVao);
                defaultVao = 0;
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
            inFrame = false;
            boundPipeline = 0;
            boundBuffer = 0;
        }
    };

    GpuDevice* DeviceFromHandle(int64_t handle) {
        return reinterpret_cast<GpuDevice*>(static_cast<intptr_t>(handle));
    }

    int64_t DeviceToHandle(GpuDevice* device) {
        return static_cast<int64_t>(reinterpret_cast<intptr_t>(device));
    }

    GpuLayout* LayoutFromHandle(int64_t handle) {
        return reinterpret_cast<GpuLayout*>(static_cast<intptr_t>(handle));
    }

    GpuShader* ShaderFromHandle(int64_t handle) {
        return reinterpret_cast<GpuShader*>(static_cast<intptr_t>(handle));
    }

    GpuBuffer* BufferFromHandle(int64_t handle) {
        return reinterpret_cast<GpuBuffer*>(static_cast<intptr_t>(handle));
    }

    GpuPipeline* PipelineFromHandle(int64_t handle) {
        return reinterpret_cast<GpuPipeline*>(static_cast<intptr_t>(handle));
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

    void ApplyVertexState(GpuDevice& device) {
        if (!device.attrsDirty) return;
        device.attrsDirty = false;

        GlFns& gl = device.gl;
        gl.BindVertexArray(device.defaultVao);

        GpuPipeline* pipeline = PipelineFromHandle(device.boundPipeline);
        GpuBuffer* buffer = BufferFromHandle(device.boundBuffer);

        // Disable a reasonable location range, then re-enable from layout.
        for (int32_t loc = 0; loc < 16; ++loc) {
            if (gl.DisableVertexAttribArray) gl.DisableVertexAttribArray(static_cast<GLuint>(loc));
        }

        if (!pipeline || !buffer || !pipeline->program || !buffer->vbo) {
            gl.BindBuffer(GL_ARRAY_BUFFER, 0);
            return;
        }

        gl.UseProgram(pipeline->program);
        gl.BindBuffer(GL_ARRAY_BUFFER, buffer->vbo);
        for (const VertexAttr& attr : pipeline->attrs) {
            if (attr.components <= 0 || attr.location < 0) continue;
            gl.EnableVertexAttribArray(static_cast<GLuint>(attr.location));
            gl.VertexAttribPointer(
                static_cast<GLuint>(attr.location),
                attr.components,
                GL_FLOAT,
                GL_FALSE,
                pipeline->strideBytes,
                reinterpret_cast<void*>(static_cast<intptr_t>(attr.offsetBytes)));
        }
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

    device->gl.GenVertexArrays(1, &device->defaultVao);
    if (!device->defaultVao) {
        SetError("failed to create default VAO");
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

extern "C" void absolute_desktop_gpu_begin_frame(int64_t handle) {
    GpuDevice* device = DeviceFromHandle(handle);
    if (!device || !MakeCurrent(*device)) return;
    device->inFrame = true;
    device->boundPipeline = 0;
    device->boundBuffer = 0;
    device->attrsDirty = true;
    device->gl.BindVertexArray(device->defaultVao);
    // Premultiplied-friendly alpha for soft sprites / BMP color keys.
    device->gl.Enable(GL_BLEND);
    device->gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

extern "C" void absolute_desktop_gpu_end_frame(int64_t handle) {
    GpuDevice* device = DeviceFromHandle(handle);
    if (!device || !device->valid) return;
    if (MakeCurrent(*device)) {
        device->gl.UseProgram(0);
        device->gl.BindBuffer(GL_ARRAY_BUFFER, 0);
        device->gl.BindVertexArray(0);
    }
    device->inFrame = false;
    device->boundPipeline = 0;
    device->boundBuffer = 0;
    device->attrsDirty = true;
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

extern "C" int64_t absolute_desktop_gpu_layout_create(int32_t strideBytes) {
    if (strideBytes <= 0) {
        SetError("VertexLayout strideBytes must be > 0");
        return 0;
    }
    auto* layout = new GpuLayout();
    layout->strideBytes = strideBytes;
    layout->attrs.reserve(8);
    return static_cast<int64_t>(reinterpret_cast<intptr_t>(layout));
}

extern "C" void absolute_desktop_gpu_layout_add(
    int64_t layoutHandle, int32_t location, int32_t components, int32_t offsetBytes) {
    GpuLayout* layout = LayoutFromHandle(layoutHandle);
    if (!layout) return;
    if (location < 0 || components <= 0 || components > 4 || offsetBytes < 0) {
        SetError("invalid VertexLayout attribute");
        return;
    }
    if (static_cast<int32_t>(layout->attrs.size()) >= 16) {
        SetError("VertexLayout attribute limit (16) exceeded");
        return;
    }
    VertexAttr attr{};
    attr.location = location;
    attr.components = components;
    attr.offsetBytes = offsetBytes;
    layout->attrs.push_back(attr);
}

extern "C" void absolute_desktop_gpu_layout_destroy(int64_t layoutHandle) {
    delete LayoutFromHandle(layoutHandle);
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

extern "C" int64_t absolute_desktop_gpu_buffer_create(
    int64_t gpuHandle, const float* data, int32_t floatCount) {
    GpuDevice* device = DeviceFromHandle(gpuHandle);
    if (!device || !MakeCurrent(*device) || !data || floatCount <= 0) {
        SetError("vertex buffer requires non-empty float data");
        return 0;
    }
    GlFns& gl = device->gl;
    auto* buffer = new GpuBuffer();
    buffer->floatCount = floatCount;
    buffer->byteSize = floatCount * static_cast<int32_t>(sizeof(float));
    gl.GenBuffers(1, &buffer->vbo);
    gl.BindBuffer(GL_ARRAY_BUFFER, buffer->vbo);
    gl.BufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(buffer->byteSize),
        data, GL_STATIC_DRAW);
    gl.BindBuffer(GL_ARRAY_BUFFER, 0);
    return static_cast<int64_t>(reinterpret_cast<intptr_t>(buffer));
}

extern "C" void absolute_desktop_gpu_buffer_destroy(int64_t gpuHandle, int64_t bufferHandle) {
    GpuDevice* device = DeviceFromHandle(gpuHandle);
    GpuBuffer* buffer = BufferFromHandle(bufferHandle);
    if (!device || !buffer) return;
    if (device->boundBuffer == bufferHandle) {
        device->boundBuffer = 0;
        device->attrsDirty = true;
    }
    if (MakeCurrent(*device) && buffer->vbo) {
        device->gl.DeleteBuffers(1, &buffer->vbo);
    }
    delete buffer;
}

extern "C" int32_t absolute_desktop_gpu_buffer_float_count(int64_t bufferHandle) {
    const GpuBuffer* buffer = BufferFromHandle(bufferHandle);
    return buffer ? buffer->floatCount : 0;
}

extern "C" int64_t absolute_desktop_gpu_pipeline_create(
    int64_t gpuHandle, int64_t shaderHandle, int64_t layoutHandle) {
    GpuDevice* device = DeviceFromHandle(gpuHandle);
    GpuShader* shader = ShaderFromHandle(shaderHandle);
    GpuLayout* layout = LayoutFromHandle(layoutHandle);
    if (!device || !shader || !layout || !shader->program) {
        SetError("createPipeline requires valid gpu, shader, and layout");
        return 0;
    }
    if (layout->strideBytes <= 0 || layout->attrs.empty()) {
        SetError("createPipeline layout needs stride and at least one attribute");
        return 0;
    }
    auto* pipeline = new GpuPipeline();
    pipeline->program = shader->program; // borrowed; shader must outlive pipeline draws
    pipeline->strideBytes = layout->strideBytes;
    pipeline->attrs = layout->attrs;
    return static_cast<int64_t>(reinterpret_cast<intptr_t>(pipeline));
}

extern "C" void absolute_desktop_gpu_pipeline_destroy(int64_t gpuHandle, int64_t pipelineHandle) {
    GpuDevice* device = DeviceFromHandle(gpuHandle);
    GpuPipeline* pipeline = PipelineFromHandle(pipelineHandle);
    if (!device || !pipeline) return;
    if (device->boundPipeline == pipelineHandle) {
        device->boundPipeline = 0;
        device->attrsDirty = true;
    }
    // Program is owned by GpuShader — do not delete here.
    delete pipeline;
}

extern "C" void absolute_desktop_gpu_bind_pipeline(int64_t gpuHandle, int64_t pipelineHandle) {
    GpuDevice* device = DeviceFromHandle(gpuHandle);
    if (!device) return;
    device->boundPipeline = pipelineHandle;
    device->attrsDirty = true;
}

extern "C" void absolute_desktop_gpu_bind_buffer(int64_t gpuHandle, int64_t bufferHandle) {
    GpuDevice* device = DeviceFromHandle(gpuHandle);
    if (!device) return;
    device->boundBuffer = bufferHandle;
    device->attrsDirty = true;
}

extern "C" void absolute_desktop_gpu_draw(int64_t gpuHandle, int32_t vertexCount) {
    GpuDevice* device = DeviceFromHandle(gpuHandle);
    if (!device || vertexCount <= 0 || !MakeCurrent(*device)) return;
    ApplyVertexState(*device);
    GpuPipeline* pipeline = PipelineFromHandle(device->boundPipeline);
    GpuBuffer* buffer = BufferFromHandle(device->boundBuffer);
    if (!pipeline || !buffer || !pipeline->program || !buffer->vbo) {
        SetError("draw requires bound pipeline and vertex buffer");
        return;
    }
    device->gl.DrawArrays(GL_TRIANGLES, 0, vertexCount);
}

extern "C" void absolute_desktop_gpu_set_uniform_f(
    int64_t gpuHandle, const char* name, float value) {
    GpuDevice* device = DeviceFromHandle(gpuHandle);
    GpuPipeline* pipeline = device ? PipelineFromHandle(device->boundPipeline) : nullptr;
    if (!device || !pipeline || !name || !MakeCurrent(*device) || !pipeline->program) return;
    device->gl.UseProgram(pipeline->program);
    const GLint loc = device->gl.GetUniformLocation(pipeline->program, name);
    if (loc >= 0) device->gl.Uniform1f(loc, value);
}

extern "C" void absolute_desktop_gpu_set_uniform_i(
    int64_t gpuHandle, const char* name, int32_t value) {
    GpuDevice* device = DeviceFromHandle(gpuHandle);
    GpuPipeline* pipeline = device ? PipelineFromHandle(device->boundPipeline) : nullptr;
    if (!device || !pipeline || !name || !MakeCurrent(*device) || !pipeline->program) return;
    if (!device->gl.Uniform1i) return;
    device->gl.UseProgram(pipeline->program);
    const GLint loc = device->gl.GetUniformLocation(pipeline->program, name);
    if (loc >= 0) device->gl.Uniform1i(loc, value);
}

extern "C" void absolute_desktop_gpu_set_uniform_2f(
    int64_t gpuHandle, const char* name, float x, float y) {
    GpuDevice* device = DeviceFromHandle(gpuHandle);
    GpuPipeline* pipeline = device ? PipelineFromHandle(device->boundPipeline) : nullptr;
    if (!device || !pipeline || !name || !MakeCurrent(*device) || !pipeline->program) return;
    if (!device->gl.Uniform2f) return;
    device->gl.UseProgram(pipeline->program);
    const GLint loc = device->gl.GetUniformLocation(pipeline->program, name);
    if (loc >= 0) device->gl.Uniform2f(loc, x, y);
}

// Upload soft sprite 0x00RRGGBB as RGBA8 (0 = transparent). Flip rows for GL origin.
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
    for (int32_t y = 0; y < h; ++y) {
        const int32_t srcY = h - 1 - y;
        for (int32_t x = 0; x < w; ++x) {
            const uint32_t c = sprite->pixels[
                static_cast<std::size_t>(srcY) * static_cast<std::size_t>(w)
                + static_cast<std::size_t>(x)];
            const std::size_t di = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w)
                + static_cast<std::size_t>(x)) * 4u;
            rgba[di + 0] = static_cast<uint8_t>((c >> 16) & 0xFF);
            rgba[di + 1] = static_cast<uint8_t>((c >> 8) & 0xFF);
            rgba[di + 2] = static_cast<uint8_t>(c & 0xFF);
            rgba[di + 3] = c == 0 ? 0 : 255;
        }
    }

    auto* texture = new GpuTexture();
    texture->width = w;
    texture->height = h;
    GlFns& gl = device->gl;
    gl.GenTextures(1, &texture->id);
    gl.BindTexture(GL_TEXTURE_2D, texture->id);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    gl.BindTexture(GL_TEXTURE_2D, 0);
    return static_cast<int64_t>(reinterpret_cast<intptr_t>(texture));
}

extern "C" int32_t absolute_desktop_gpu_texture_width(int64_t textureHandle) {
    const GpuTexture* texture = TextureFromHandle(textureHandle);
    return texture ? texture->width : 0;
}

extern "C" int32_t absolute_desktop_gpu_texture_height(int64_t textureHandle) {
    const GpuTexture* texture = TextureFromHandle(textureHandle);
    return texture ? texture->height : 0;
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

extern "C" int64_t absolute_desktop_gpu_create(int64_t) { return 0; }
extern "C" void absolute_desktop_gpu_destroy(int64_t) {}
extern "C" int32_t absolute_desktop_gpu_is_valid(int64_t) { return 0; }
extern "C" const char* absolute_desktop_gpu_backend() { return "none"; }
extern "C" const char* absolute_desktop_gpu_last_error() { return g_lastError.c_str(); }
extern "C" void absolute_desktop_gpu_begin_frame(int64_t) {}
extern "C" void absolute_desktop_gpu_end_frame(int64_t) {}
extern "C" void absolute_desktop_gpu_clear(int64_t, float, float, float, float) {}
extern "C" void absolute_desktop_gpu_present(int64_t) {}
extern "C" int64_t absolute_desktop_gpu_layout_create(int32_t) { return 0; }
extern "C" void absolute_desktop_gpu_layout_add(int64_t, int32_t, int32_t, int32_t) {}
extern "C" void absolute_desktop_gpu_layout_destroy(int64_t) {}
extern "C" int64_t absolute_desktop_gpu_shader_create(int64_t, const char*, const char*) { return 0; }
extern "C" void absolute_desktop_gpu_shader_destroy(int64_t, int64_t) {}
extern "C" int64_t absolute_desktop_gpu_buffer_create(int64_t, const float*, int32_t) { return 0; }
extern "C" void absolute_desktop_gpu_buffer_destroy(int64_t, int64_t) {}
extern "C" int32_t absolute_desktop_gpu_buffer_float_count(int64_t) { return 0; }
extern "C" int64_t absolute_desktop_gpu_pipeline_create(int64_t, int64_t, int64_t) { return 0; }
extern "C" void absolute_desktop_gpu_pipeline_destroy(int64_t, int64_t) {}
extern "C" void absolute_desktop_gpu_bind_pipeline(int64_t, int64_t) {}
extern "C" void absolute_desktop_gpu_bind_buffer(int64_t, int64_t) {}
extern "C" void absolute_desktop_gpu_draw(int64_t, int32_t) {}
extern "C" void absolute_desktop_gpu_set_uniform_f(int64_t, const char*, float) {}
extern "C" void absolute_desktop_gpu_set_uniform_i(int64_t, const char*, int32_t) {}
extern "C" void absolute_desktop_gpu_set_uniform_2f(int64_t, const char*, float, float) {}
extern "C" int64_t absolute_desktop_gpu_texture_from_sprite(int64_t, int64_t) { return 0; }
extern "C" void absolute_desktop_gpu_texture_destroy(int64_t, int64_t) {}
extern "C" void absolute_desktop_gpu_bind_texture(int64_t, int64_t, int32_t) {}
extern "C" int32_t absolute_desktop_gpu_texture_width(int64_t) { return 0; }
extern "C" int32_t absolute_desktop_gpu_texture_height(int64_t) { return 0; }

#endif
