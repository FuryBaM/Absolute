// Vulkan backend for Desktop.Gpu (Windows primary; stubs elsewhere).
// HLSL → SPIR-V via portable DXC (.absolute/toolchains/dxc-spirv or ABSOLUTE_DXC).
// Mesh RHI: VB/IB, pipeline, draw/drawIndexed, UBO b0, texture t0 + sampler s0.

#if !defined(_WIN32) || !defined(ABSOLUTE_DESKTOP_HAS_VULKAN)

#include <cstdint>
#include <string>

namespace {
    thread_local std::string g_lastError = "Vulkan backend unavailable";
}

extern "C" int64_t absolute_desktop_gpu_vk_create(int64_t) { return 0; }
extern "C" void absolute_desktop_gpu_vk_destroy(int64_t) {}
extern "C" int32_t absolute_desktop_gpu_vk_is_valid(int64_t) { return 0; }
extern "C" const char* absolute_desktop_gpu_vk_backend() { return "none"; }
extern "C" const char* absolute_desktop_gpu_vk_last_error() { return g_lastError.c_str(); }
extern "C" void absolute_desktop_gpu_vk_begin_frame(int64_t) {}
extern "C" void absolute_desktop_gpu_vk_end_frame(int64_t) {}
extern "C" void absolute_desktop_gpu_vk_clear(int64_t, float, float, float, float) {}
extern "C" void absolute_desktop_gpu_vk_present(int64_t) {}
extern "C" void absolute_desktop_gpu_vk_unsupported(const char* what) {
    g_lastError = std::string("Vulkan backend: ") + (what ? what : "unsupported");
}
extern "C" int64_t absolute_desktop_gpu_vk_shader_create(int64_t, const char*, const char*) { return 0; }
extern "C" void absolute_desktop_gpu_vk_shader_destroy(int64_t, int64_t) {}
extern "C" int64_t absolute_desktop_gpu_vk_buffer_create(int64_t, const float*, int32_t) { return 0; }
extern "C" void absolute_desktop_gpu_vk_buffer_destroy(int64_t, int64_t) {}
extern "C" int32_t absolute_desktop_gpu_vk_buffer_float_count(int64_t) { return 0; }
extern "C" int64_t absolute_desktop_gpu_vk_index_buffer_create(int64_t, const int32_t*, int32_t) { return 0; }
extern "C" void absolute_desktop_gpu_vk_index_buffer_destroy(int64_t, int64_t) {}
extern "C" int32_t absolute_desktop_gpu_vk_index_buffer_count(int64_t) { return 0; }
extern "C" int64_t absolute_desktop_gpu_vk_pipeline_create(
    int64_t, int64_t, int32_t, const int32_t*, const int32_t*, const int32_t*, int32_t) {
    return 0;
}
extern "C" void absolute_desktop_gpu_vk_pipeline_destroy(int64_t, int64_t) {}
extern "C" void absolute_desktop_gpu_vk_bind_pipeline(int64_t, int64_t) {}
extern "C" void absolute_desktop_gpu_vk_bind_buffer(int64_t, int64_t) {}
extern "C" void absolute_desktop_gpu_vk_bind_index_buffer(int64_t, int64_t) {}
extern "C" void absolute_desktop_gpu_vk_draw(int64_t, int32_t) {}
extern "C" void absolute_desktop_gpu_vk_draw_indexed(int64_t, int32_t) {}
extern "C" void absolute_desktop_gpu_vk_set_uniform_f(int64_t, const char*, float) {}
extern "C" void absolute_desktop_gpu_vk_set_uniform_i(int64_t, const char*, int32_t) {}
extern "C" void absolute_desktop_gpu_vk_set_uniform_2f(int64_t, const char*, float, float) {}
extern "C" int32_t absolute_desktop_gpu_vk_is_resource(int64_t) { return 0; }
extern "C" int64_t absolute_desktop_gpu_vk_texture_from_sprite(int64_t, int64_t) { return 0; }
extern "C" void absolute_desktop_gpu_vk_texture_destroy(int64_t, int64_t) {}
extern "C" int32_t absolute_desktop_gpu_vk_texture_width(int64_t) { return 0; }
extern "C" int32_t absolute_desktop_gpu_vk_texture_height(int64_t) { return 0; }
extern "C" void absolute_desktop_gpu_vk_bind_texture(int64_t, int64_t, int32_t) {}
extern "C" int64_t absolute_desktop_gpu_vk_sampler_create(int64_t, int32_t, int32_t) { return 0; }
extern "C" void absolute_desktop_gpu_vk_sampler_destroy(int64_t, int64_t) {}
extern "C" void absolute_desktop_gpu_vk_bind_sampler(int64_t, int64_t, int32_t) {}

#else

#define NOMINMAX
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <Windows.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "desktop_gpu_magic.h"

extern "C" void* absolute_desktop_native_window(int64_t handle);
extern "C" int32_t absolute_desktop_width(int64_t handle);
extern "C" int32_t absolute_desktop_height(int64_t handle);

namespace {

thread_local std::string g_lastError;

void SetError(std::string message) { g_lastError = std::move(message); }

constexpr uint32_t kResShader = 0x564B5348u;   // 'VKSH'
constexpr uint32_t kResBuffer = 0x564B5642u;   // 'VKVB'
constexpr uint32_t kResIndex = 0x564B4942u;    // 'VKIB'
constexpr uint32_t kResPipeline = 0x564B504Cu; // 'VKPL'
constexpr uint32_t kResTexture = 0x564B5445u;  // 'VKTE'
constexpr uint32_t kResSampler = 0x564B5341u;  // 'VKSA'
constexpr UINT kUboSize = 256;

uint32_t PeekRes(int64_t handle) {
    if (!handle) return 0;
    return *reinterpret_cast<const uint32_t*>(static_cast<intptr_t>(handle));
}
int64_t PtrToHandle(void* p) { return static_cast<int64_t>(reinterpret_cast<intptr_t>(p)); }

// ---- dynamic Vulkan loader ----
HMODULE g_vkLib = nullptr;
PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;

#define ABS_VK_FN_LIST \
    X(vkCreateInstance) \
    X(vkDestroyInstance) \
    X(vkEnumeratePhysicalDevices) \
    X(vkGetPhysicalDeviceProperties) \
    X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetPhysicalDeviceMemoryProperties) \
    X(vkGetPhysicalDeviceSurfaceSupportKHR) \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR) \
    X(vkCreateDevice) \
    X(vkDestroyDevice) \
    X(vkGetDeviceQueue) \
    X(vkCreateWin32SurfaceKHR) \
    X(vkDestroySurfaceKHR) \
    X(vkCreateSwapchainKHR) \
    X(vkDestroySwapchainKHR) \
    X(vkGetSwapchainImagesKHR) \
    X(vkCreateImageView) \
    X(vkDestroyImageView) \
    X(vkCreateRenderPass) \
    X(vkDestroyRenderPass) \
    X(vkCreateFramebuffer) \
    X(vkDestroyFramebuffer) \
    X(vkCreateCommandPool) \
    X(vkDestroyCommandPool) \
    X(vkAllocateCommandBuffers) \
    X(vkFreeCommandBuffers) \
    X(vkBeginCommandBuffer) \
    X(vkEndCommandBuffer) \
    X(vkCmdBeginRenderPass) \
    X(vkCmdEndRenderPass) \
    X(vkCmdBindPipeline) \
    X(vkCmdBindVertexBuffers) \
    X(vkCmdBindIndexBuffer) \
    X(vkCmdBindDescriptorSets) \
    X(vkCmdDraw) \
    X(vkCmdDrawIndexed) \
    X(vkCmdSetViewport) \
    X(vkCmdSetScissor) \
    X(vkCmdPipelineBarrier) \
    X(vkCmdCopyBuffer) \
    X(vkCmdCopyBufferToImage) \
    X(vkCmdClearAttachments) \
    X(vkCreateSemaphore) \
    X(vkDestroySemaphore) \
    X(vkCreateFence) \
    X(vkDestroyFence) \
    X(vkWaitForFences) \
    X(vkResetFences) \
    X(vkAcquireNextImageKHR) \
    X(vkQueueSubmit) \
    X(vkQueuePresentKHR) \
    X(vkQueueWaitIdle) \
    X(vkDeviceWaitIdle) \
    X(vkCreateShaderModule) \
    X(vkDestroyShaderModule) \
    X(vkCreatePipelineLayout) \
    X(vkDestroyPipelineLayout) \
    X(vkCreateGraphicsPipelines) \
    X(vkDestroyPipeline) \
    X(vkCreateDescriptorSetLayout) \
    X(vkDestroyDescriptorSetLayout) \
    X(vkCreateDescriptorPool) \
    X(vkDestroyDescriptorPool) \
    X(vkAllocateDescriptorSets) \
    X(vkUpdateDescriptorSets) \
    X(vkCreateBuffer) \
    X(vkDestroyBuffer) \
    X(vkGetBufferMemoryRequirements) \
    X(vkAllocateMemory) \
    X(vkFreeMemory) \
    X(vkBindBufferMemory) \
    X(vkMapMemory) \
    X(vkUnmapMemory) \
    X(vkCreateImage) \
    X(vkDestroyImage) \
    X(vkGetImageMemoryRequirements) \
    X(vkBindImageMemory) \
    X(vkCreateSampler) \
    X(vkDestroySampler) \
    X(vkEnumerateInstanceExtensionProperties) \
    X(vkResetCommandBuffer)

#define X(name) PFN_##name name = nullptr;
ABS_VK_FN_LIST
#undef X

bool LoadVulkanLibrary() {
    if (g_vkLib) return true;
    g_vkLib = LoadLibraryA("vulkan-1.dll");
    if (!g_vkLib) {
        SetError("LoadLibrary(vulkan-1.dll) failed — install a Vulkan-capable GPU driver");
        return false;
    }
    vkGetInstanceProcAddr =
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(g_vkLib, "vkGetInstanceProcAddr"));
    if (!vkGetInstanceProcAddr) {
        SetError("vkGetInstanceProcAddr missing");
        return false;
    }
    return true;
}

void* LoadInst(VkInstance inst, const char* name) {
    return reinterpret_cast<void*>(vkGetInstanceProcAddr(inst, name));
}

bool LoadInstanceFns(VkInstance inst) {
#define X(name) name = reinterpret_cast<PFN_##name>(LoadInst(inst, #name));
    ABS_VK_FN_LIST
#undef X
    // Core createInstance is special — already used via global gipa.
    if (!vkDestroyInstance || !vkEnumeratePhysicalDevices || !vkCreateDevice) {
        SetError("failed to load Vulkan instance functions");
        return false;
    }
    return true;
}

bool LoadDeviceFns(VkDevice device) {
    auto gdpa = reinterpret_cast<PFN_vkGetDeviceProcAddr>(LoadInst(VK_NULL_HANDLE, "vkGetDeviceProcAddr"));
    // Prefer device-level resolution when available.
    auto load = [&](const char* n) -> PFN_vkVoidFunction {
        if (gdpa) {
            if (auto p = gdpa(device, n)) return p;
        }
        return vkGetInstanceProcAddr(VK_NULL_HANDLE, n);
    };
    // Re-bind device entry points that may be device-dispatch only.
    // Most loaders still expose them via instance gipa after device create.
    (void)load;
    return vkCreateSwapchainKHR && vkQueueSubmit && vkCreateGraphicsPipelines;
}

struct UniformVar {
    UINT offset = 0;
    UINT size = 0;
};

void ParseUniforms(const char* source, std::unordered_map<std::string, UniformVar>& out, UINT& total) {
    out.clear();
    total = 0;
    if (!source) return;
    // Minimal HLSL cbuffer scanner: float / float2 / float3 / float4 / int.
    const char* p = source;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
        const char* line = p;
        while (*p && *p != '\n') ++p;
        std::string s(line, p);
        if (*p == '\n') ++p;
        auto trim = [](std::string& t) {
            while (!t.empty() && (t.back() == '\r' || t.back() == ' ' || t.back() == ';')) t.pop_back();
            size_t i = 0;
            while (i < t.size() && (t[i] == ' ' || t[i] == '\t')) ++i;
            t = t.substr(i);
        };
        trim(s);
        UINT size = 0;
        std::string rest;
        if (s.rfind("float4 ", 0) == 0) {
            size = 16;
            rest = s.substr(7);
        } else if (s.rfind("float3 ", 0) == 0) {
            size = 12;
            rest = s.substr(7);
        } else if (s.rfind("float2 ", 0) == 0) {
            size = 8;
            rest = s.substr(7);
        } else if (s.rfind("float ", 0) == 0) {
            size = 4;
            rest = s.substr(6);
        } else if (s.rfind("int ", 0) == 0) {
            size = 4;
            rest = s.substr(4);
        } else {
            continue;
        }
        trim(rest);
        // name only (ignore array syntax)
        size_t end = 0;
        while (end < rest.size() && (isalnum(static_cast<unsigned char>(rest[end])) || rest[end] == '_')) {
            ++end;
        }
        if (end == 0) continue;
        std::string name = rest.substr(0, end);
        if (name == "register" || name.rfind("_pad", 0) == 0) continue;
        // Align to 4 bytes; HLSL packoffset-ish: 16-byte rules simplified to sequential 4-align.
        total = (total + 3u) & ~3u;
        if (size == 8) total = (total + 7u) & ~7u;
        if (size == 12 || size == 16) total = (total + 15u) & ~15u;
        UniformVar u;
        u.offset = total;
        u.size = size;
        out[name] = u;
        total += size;
    }
    if (total < 16) total = 16;
    total = (total + 15u) & ~15u;
    if (total > kUboSize) total = kUboSize;
}

struct VkGpuShader {
    uint32_t magic = kResShader;
    std::vector<uint32_t> vsSpv;
    std::vector<uint32_t> psSpv;
    std::unordered_map<std::string, UniformVar> uniforms;
    UINT cbufferSize = 16;
    std::vector<uint8_t> cbufferCpu;
    bool cbufferDirty = true;
};

struct VkGpuBuffer {
    uint32_t magic = kResBuffer;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    int32_t floatCount = 0;
    VkDeviceSize size = 0;
};

struct VkGpuIndexBuffer {
    uint32_t magic = kResIndex;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    int32_t indexCount = 0;
};

struct VkGpuTexture {
    uint32_t magic = kResTexture;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    int32_t width = 0;
    int32_t height = 0;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct VkGpuSampler {
    uint32_t magic = kResSampler;
    VkSampler sampler = VK_NULL_HANDLE;
};

struct VkGpuPipeline {
    uint32_t magic = kResPipeline;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorSet descSet = VK_NULL_HANDLE;
    VkBuffer ubo = VK_NULL_HANDLE;
    VkDeviceMemory uboMem = VK_NULL_HANDLE;
    void* uboMapped = nullptr;
    VkGpuShader* shader = nullptr;
    int32_t strideBytes = 0;
};

struct DesktopSpriteView {
    int32_t width = 1;
    int32_t height = 1;
    std::vector<uint32_t> pixels;
};

struct VkGpuDevice {
    uint32_t magic = kAbsoluteGpuMagicVK;
    int64_t windowHandle = 0;
    HWND hwnd = nullptr;

    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapFormat = VK_FORMAT_B8G8R8A8_UNORM;
    std::vector<VkImage> swapImages;
    std::vector<VkImageView> swapViews;
    std::vector<VkFramebuffer> framebuffers;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
    VkFence inFlight = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorPool descPool = VK_NULL_HANDLE;

    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t imageIndex = 0;
    bool valid = false;
    bool inFrame = false;
    bool cmdOpen = false;
    float clearColor[4] = {0.06f, 0.07f, 0.12f, 1.0f};

    int64_t boundPipeline = 0;
    int64_t boundBuffer = 0;
    int64_t boundIndexBuffer = 0;
    int64_t boundTexture = 0;
    int64_t boundSampler = 0;

    VkGpuTexture* defaultTexture = nullptr;
    VkGpuSampler* defaultSampler = nullptr;

    uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const {
        VkPhysicalDeviceMemoryProperties mp{};
        vkGetPhysicalDeviceMemoryProperties(phys, &mp);
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
            if ((typeBits & (1u << i))
                && (mp.memoryTypes[i].propertyFlags & props) == props) {
                return i;
            }
        }
        return UINT32_MAX;
    }

    bool CreateBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags memProps,
        VkBuffer* outBuf,
        VkDeviceMemory* outMem,
        void** mapped) {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = size;
        bi.usage = usage;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bi, nullptr, outBuf) != VK_SUCCESS) return false;
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(device, *outBuf, &req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, memProps);
        if (ai.memoryTypeIndex == UINT32_MAX) return false;
        if (vkAllocateMemory(device, &ai, nullptr, outMem) != VK_SUCCESS) return false;
        vkBindBufferMemory(device, *outBuf, *outMem, 0);
        if (mapped) {
            if (vkMapMemory(device, *outMem, 0, size, 0, mapped) != VK_SUCCESS) return false;
        }
        return true;
    }

    void DestroyBuffer(VkBuffer& b, VkDeviceMemory& m) {
        if (b) {
            vkDestroyBuffer(device, b, nullptr);
            b = VK_NULL_HANDLE;
        }
        if (m) {
            vkFreeMemory(device, m, nullptr);
            m = VK_NULL_HANDLE;
        }
    }

    void DestroySwapchain() {
        for (auto fb : framebuffers) {
            if (fb) vkDestroyFramebuffer(device, fb, nullptr);
        }
        framebuffers.clear();
        for (auto v : swapViews) {
            if (v) vkDestroyImageView(device, v, nullptr);
        }
        swapViews.clear();
        swapImages.clear();
        if (swapchain) {
            vkDestroySwapchainKHR(device, swapchain, nullptr);
            swapchain = VK_NULL_HANDLE;
        }
    }

    bool CreateSwapchain() {
        DestroySwapchain();
        VkSurfaceCapabilitiesKHR caps{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps);
        uint32_t fmtCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmtCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(fmtCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fmtCount, formats.data());
        VkSurfaceFormatKHR chosen = formats[0];
        for (const auto& f : formats) {
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM
                || f.format == VK_FORMAT_R8G8B8A8_UNORM) {
                chosen = f;
                break;
            }
        }
        swapFormat = chosen.format;

        uint32_t w = caps.currentExtent.width;
        uint32_t h = caps.currentExtent.height;
        if (w == UINT32_MAX) {
            w = static_cast<uint32_t>(absolute_desktop_width(windowHandle));
            h = static_cast<uint32_t>(absolute_desktop_height(windowHandle));
        }
        if (w == 0) w = 1;
        if (h == 0) h = 1;
        width = w;
        height = h;

        uint32_t imgCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount) imgCount = caps.maxImageCount;

        VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        sci.surface = surface;
        sci.minImageCount = imgCount;
        sci.imageFormat = swapFormat;
        sci.imageColorSpace = chosen.colorSpace;
        sci.imageExtent = {w, h};
        sci.imageArrayLayers = 1;
        sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        sci.preTransform = caps.currentTransform;
        sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        sci.clipped = VK_TRUE;
        if (vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain) != VK_SUCCESS) {
            SetError("vkCreateSwapchainKHR failed");
            return false;
        }

        uint32_t count = 0;
        vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr);
        swapImages.resize(count);
        vkGetSwapchainImagesKHR(device, swapchain, &count, swapImages.data());
        swapViews.resize(count);
        framebuffers.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vi.image = swapImages[i];
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = swapFormat;
            vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vi.subresourceRange.levelCount = 1;
            vi.subresourceRange.layerCount = 1;
            if (vkCreateImageView(device, &vi, nullptr, &swapViews[i]) != VK_SUCCESS) {
                SetError("vkCreateImageView (swapchain) failed");
                return false;
            }
            VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fi.renderPass = renderPass;
            fi.attachmentCount = 1;
            fi.pAttachments = &swapViews[i];
            fi.width = w;
            fi.height = h;
            fi.layers = 1;
            if (vkCreateFramebuffer(device, &fi, nullptr, &framebuffers[i]) != VK_SUCCESS) {
                SetError("vkCreateFramebuffer failed");
                return false;
            }
        }
        return true;
    }

    void Destroy() {
        if (device) vkDeviceWaitIdle(device);
        if (defaultTexture) {
            if (defaultTexture->view) vkDestroyImageView(device, defaultTexture->view, nullptr);
            if (defaultTexture->image) vkDestroyImage(device, defaultTexture->image, nullptr);
            if (defaultTexture->memory) vkFreeMemory(device, defaultTexture->memory, nullptr);
            delete defaultTexture;
            defaultTexture = nullptr;
        }
        if (defaultSampler) {
            if (defaultSampler->sampler) vkDestroySampler(device, defaultSampler->sampler, nullptr);
            delete defaultSampler;
            defaultSampler = nullptr;
        }
        DestroySwapchain();
        if (imageAvailable) vkDestroySemaphore(device, imageAvailable, nullptr);
        if (renderFinished) vkDestroySemaphore(device, renderFinished, nullptr);
        if (inFlight) vkDestroyFence(device, inFlight, nullptr);
        if (cmdPool) vkDestroyCommandPool(device, cmdPool, nullptr);
        if (descPool) vkDestroyDescriptorPool(device, descPool, nullptr);
        if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        if (setLayout) vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
        if (renderPass) vkDestroyRenderPass(device, renderPass, nullptr);
        if (device) vkDestroyDevice(device, nullptr);
        if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
        device = VK_NULL_HANDLE;
        valid = false;
        inFrame = false;
        cmdOpen = false;
    }
};

VkGpuDevice* DeviceFrom(int64_t h) {
    if (!AbsoluteGpuIsVK(h)) return nullptr;
    return reinterpret_cast<VkGpuDevice*>(static_cast<intptr_t>(h));
}
VkGpuShader* ShaderFrom(int64_t h) {
    return PeekRes(h) == kResShader ? reinterpret_cast<VkGpuShader*>(static_cast<intptr_t>(h)) : nullptr;
}
VkGpuBuffer* BufferFrom(int64_t h) {
    return PeekRes(h) == kResBuffer ? reinterpret_cast<VkGpuBuffer*>(static_cast<intptr_t>(h)) : nullptr;
}
VkGpuIndexBuffer* IndexFrom(int64_t h) {
    return PeekRes(h) == kResIndex ? reinterpret_cast<VkGpuIndexBuffer*>(static_cast<intptr_t>(h)) : nullptr;
}
VkGpuPipeline* PipelineFrom(int64_t h) {
    return PeekRes(h) == kResPipeline ? reinterpret_cast<VkGpuPipeline*>(static_cast<intptr_t>(h)) : nullptr;
}
VkGpuTexture* TextureFrom(int64_t h) {
    return PeekRes(h) == kResTexture ? reinterpret_cast<VkGpuTexture*>(static_cast<intptr_t>(h)) : nullptr;
}
VkGpuSampler* SamplerFrom(int64_t h) {
    return PeekRes(h) == kResSampler ? reinterpret_cast<VkGpuSampler*>(static_cast<intptr_t>(h)) : nullptr;
}
DesktopSpriteView* SpriteFrom(int64_t h) {
    return h ? reinterpret_cast<DesktopSpriteView*>(static_cast<intptr_t>(h)) : nullptr;
}

VkFormat FormatForComponents(int32_t c) {
    switch (c) {
    case 1: return VK_FORMAT_R32_SFLOAT;
    case 2: return VK_FORMAT_R32G32_SFLOAT;
    case 3: return VK_FORMAT_R32G32B32_SFLOAT;
    case 4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    default: return VK_FORMAT_UNDEFINED;
    }
}

std::string FindDxcPath() {
    if (const char* env = std::getenv("ABSOLUTE_DXC")) {
        if (GetFileAttributesA(env) != INVALID_FILE_ATTRIBUTES) return env;
    }
    char modulePath[MAX_PATH]{};
    HMODULE hm = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&FindDxcPath),
        &hm);
    if (hm) GetModuleFileNameA(hm, modulePath, MAX_PATH);
    std::vector<std::string> seeds;
    if (modulePath[0]) seeds.emplace_back(modulePath);
    char cwd[MAX_PATH]{};
    GetCurrentDirectoryA(MAX_PATH, cwd);
    if (cwd[0]) seeds.emplace_back(std::string(cwd) + "\\.");
    for (std::string base : seeds) {
        for (int up = 0; up < 10; ++up) {
            const std::string candidate =
                base + "\\.absolute\\toolchains\\dxc-spirv\\bin\\x64\\dxc.exe";
            // When base is a file path, strip filename first on first iteration.
            std::string dir = base;
            if (up == 0) {
                const size_t slash = dir.find_last_of("\\/");
                if (slash != std::string::npos) dir = dir.substr(0, slash);
            }
            std::string tryPath = dir;
            for (int i = 0; i < up; ++i) {
                const size_t slash = tryPath.find_last_of("\\/");
                if (slash == std::string::npos) break;
                tryPath = tryPath.substr(0, slash);
            }
            const std::string dxc = tryPath + "\\.absolute\\toolchains\\dxc-spirv\\bin\\x64\\dxc.exe";
            if (GetFileAttributesA(dxc.c_str()) != INVALID_FILE_ATTRIBUTES) return dxc;
            (void)candidate;
            if (up == 0) base = dir;
            else {
                const size_t slash = base.find_last_of("\\/");
                if (slash == std::string::npos) break;
                base = base.substr(0, slash);
            }
        }
    }
    return {};
}

bool CompileHlslToSpirv(
    const char* source, const char* profile, std::vector<uint32_t>& outSpv) {
    outSpv.clear();
    if (!source || !source[0]) {
        SetError("empty HLSL source");
        return false;
    }
    if (std::strncmp(source, "#version", 8) == 0) {
        SetError("Vulkan createShader expects HLSL (got GLSL #version); use HLSL like D3D backends");
        return false;
    }
    const std::string dxc = FindDxcPath();
    if (dxc.empty()) {
        SetError("DXC with SPIR-V not found. Set ABSOLUTE_DXC or place portable DXC at "
                 ".absolute/toolchains/dxc-spirv/bin/x64/dxc.exe");
        return false;
    }

    char tempPath[MAX_PATH]{};
    GetTempPathA(MAX_PATH, tempPath);
    char hlslFile[MAX_PATH]{};
    char spvFile[MAX_PATH]{};
    GetTempFileNameA(tempPath, "ahl", 0, hlslFile);
    std::string spv = std::string(hlslFile) + ".spv";
    // GetTempFileName creates the file; write HLSL into it.
    {
        std::ofstream f(hlslFile, std::ios::binary);
        f.write(source, static_cast<std::streamsize>(std::strlen(source)));
    }

    std::string cmd = "\"";
    cmd += dxc;
    cmd += "\" -T ";
    cmd += profile;
    cmd += " -E main -spirv -fspv-target-env=vulkan1.1 -fvk-use-dx-layout "
           "-fvk-b-shift 0 0 -fvk-t-shift 1 0 -fvk-s-shift 2 0 -Fo \"";
    cmd += spv;
    cmd += "\" \"";
    cmd += hlslFile;
    cmd += "\"";

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<char> cmdline(cmd.begin(), cmd.end());
    cmdline.push_back('\0');
    if (!CreateProcessA(
            nullptr, cmdline.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        SetError("failed to launch DXC for SPIR-V");
        DeleteFileA(hlslFile);
        return false;
    }
    WaitForSingleObject(pi.hProcess, 60000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    DeleteFileA(hlslFile);
    if (code != 0) {
        SetError(std::string("DXC SPIR-V compile failed for ") + profile
            + " (exit " + std::to_string(code) + ")");
        DeleteFileA(spv.c_str());
        return false;
    }
    std::ifstream in(spv, std::ios::binary);
    if (!in) {
        SetError("failed to read SPIR-V output");
        return false;
    }
    in.seekg(0, std::ios::end);
    const auto bytes = static_cast<size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    if (bytes < 4 || (bytes % 4) != 0) {
        SetError("invalid SPIR-V size");
        DeleteFileA(spv.c_str());
        return false;
    }
    outSpv.resize(bytes / 4);
    in.read(reinterpret_cast<char*>(outSpv.data()), static_cast<std::streamsize>(bytes));
    DeleteFileA(spv.c_str());
    return true;
}

bool CreateDefaultTexture(VkGpuDevice* dev) {
    auto* tex = new VkGpuTexture();
    tex->width = 1;
    tex->height = 1;
    const uint8_t white[4] = {255, 255, 255, 255};

    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent = {1, 1, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_LINEAR;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
    if (vkCreateImage(dev->device, &ii, nullptr, &tex->image) != VK_SUCCESS) {
        delete tex;
        return false;
    }
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(dev->device, tex->image, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = dev->FindMemoryType(
        req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX
        || vkAllocateMemory(dev->device, &ai, nullptr, &tex->memory) != VK_SUCCESS) {
        vkDestroyImage(dev->device, tex->image, nullptr);
        delete tex;
        return false;
    }
    vkBindImageMemory(dev->device, tex->image, tex->memory, 0);
    void* mapped = nullptr;
    vkMapMemory(dev->device, tex->memory, 0, req.size, 0, &mapped);
    std::memcpy(mapped, white, 4);
    vkUnmapMemory(dev->device, tex->memory);

    // Transition PREINITIALIZED → SHADER_READ_ONLY via barrier on a one-shot command buffer.
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkResetCommandBuffer(dev->cmd, 0);
    vkBeginCommandBuffer(dev->cmd, &bi);
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = tex->image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(
        dev->cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    vkEndCommandBuffer(dev->cmd);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &dev->cmd;
    vkQueueSubmit(dev->queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(dev->queue);
    tex->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = tex->image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(dev->device, &vi, nullptr, &tex->view) != VK_SUCCESS) {
        delete tex;
        return false;
    }
    dev->defaultTexture = tex;

    auto* samp = new VkGpuSampler();
    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = VK_FILTER_NEAREST;
    sci.minFilter = VK_FILTER_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod = 0;
    if (vkCreateSampler(dev->device, &sci, nullptr, &samp->sampler) != VK_SUCCESS) {
        delete samp;
        return false;
    }
    dev->defaultSampler = samp;
    return true;
}

} // namespace

extern "C" int64_t absolute_desktop_gpu_vk_create(int64_t windowHandle) {
    g_lastError.clear();
    if (!LoadVulkanLibrary()) return 0;

    void* hwndPtr = absolute_desktop_native_window(windowHandle);
    HWND hwnd = static_cast<HWND>(hwndPtr);
    if (!hwnd) {
        SetError("Vulkan: missing native HWND");
        return 0;
    }

    // Load global createInstance
    auto createInstance =
        reinterpret_cast<PFN_vkCreateInstance>(vkGetInstanceProcAddr(nullptr, "vkCreateInstance"));
    auto enumExt = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
        vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceExtensionProperties"));
    if (!createInstance || !enumExt) {
        SetError("vkCreateInstance unavailable");
        return 0;
    }

    const char* exts[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
    };

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Absolute";
    app.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = exts;

    auto* dev = new VkGpuDevice();
    dev->windowHandle = windowHandle;
    dev->hwnd = hwnd;

    if (createInstance(&ici, nullptr, &dev->instance) != VK_SUCCESS) {
        SetError("vkCreateInstance failed");
        delete dev;
        return 0;
    }
    if (!LoadInstanceFns(dev->instance)) {
        delete dev;
        return 0;
    }

    VkWin32SurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    sci.hinstance = GetModuleHandleW(nullptr);
    sci.hwnd = hwnd;
    if (vkCreateWin32SurfaceKHR(dev->instance, &sci, nullptr, &dev->surface) != VK_SUCCESS) {
        SetError("vkCreateWin32SurfaceKHR failed");
        dev->Destroy();
        delete dev;
        return 0;
    }

    uint32_t physCount = 0;
    vkEnumeratePhysicalDevices(dev->instance, &physCount, nullptr);
    if (physCount == 0) {
        SetError("no Vulkan physical devices");
        dev->Destroy();
        delete dev;
        return 0;
    }
    std::vector<VkPhysicalDevice> phys(physCount);
    vkEnumeratePhysicalDevices(dev->instance, &physCount, phys.data());
    dev->phys = phys[0];

    uint32_t qCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev->phys, &qCount, nullptr);
    std::vector<VkQueueFamilyProperties> qprops(qCount);
    vkGetPhysicalDeviceQueueFamilyProperties(dev->phys, &qCount, qprops.data());
    dev->queueFamily = UINT32_MAX;
    for (uint32_t i = 0; i < qCount; ++i) {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(dev->phys, i, dev->surface, &present);
        if ((qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
            dev->queueFamily = i;
            break;
        }
    }
    if (dev->queueFamily == UINT32_MAX) {
        SetError("no graphics+present queue family");
        dev->Destroy();
        delete dev;
        return 0;
    }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = dev->queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    const char* devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = devExts;
    if (vkCreateDevice(dev->phys, &dci, nullptr, &dev->device) != VK_SUCCESS) {
        SetError("vkCreateDevice failed");
        dev->Destroy();
        delete dev;
        return 0;
    }
    LoadDeviceFns(dev->device);
    vkGetDeviceQueue(dev->device, dev->queueFamily, 0, &dev->queue);

    VkAttachmentDescription color{};
    color.format = VK_FORMAT_B8G8R8A8_UNORM; // updated after swapchain format known — recreated below
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 1;
    rpci.pAttachments = &color;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;

    // Probe surface format first for render pass
    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev->phys, dev->surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev->phys, dev->surface, &fmtCount, formats.data());
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) {
            dev->swapFormat = f.format;
            break;
        }
    }
    color.format = dev->swapFormat;
    if (vkCreateRenderPass(dev->device, &rpci, nullptr, &dev->renderPass) != VK_SUCCESS) {
        SetError("vkCreateRenderPass failed");
        dev->Destroy();
        delete dev;
        return 0;
    }

    if (!dev->CreateSwapchain()) {
        dev->Destroy();
        delete dev;
        return 0;
    }

    VkDescriptorSetLayoutBinding binds[3]{};
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[1].binding = 1;
    binds[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[2].binding = 2;
    binds[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    binds[2].descriptorCount = 1;
    binds[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dsl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dsl.bindingCount = 3;
    dsl.pBindings = binds;
    if (vkCreateDescriptorSetLayout(dev->device, &dsl, nullptr, &dev->setLayout) != VK_SUCCESS) {
        SetError("vkCreateDescriptorSetLayout failed");
        dev->Destroy();
        delete dev;
        return 0;
    }
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &dev->setLayout;
    if (vkCreatePipelineLayout(dev->device, &plci, nullptr, &dev->pipelineLayout) != VK_SUCCESS) {
        SetError("vkCreatePipelineLayout failed");
        dev->Destroy();
        delete dev;
        return 0;
    }

    VkDescriptorPoolSize sizes[3]{};
    sizes[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 64};
    sizes[1] = {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 64};
    sizes[2] = {VK_DESCRIPTOR_TYPE_SAMPLER, 64};
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets = 64;
    dpci.poolSizeCount = 3;
    dpci.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(dev->device, &dpci, nullptr, &dev->descPool) != VK_SUCCESS) {
        SetError("vkCreateDescriptorPool failed");
        dev->Destroy();
        delete dev;
        return 0;
    }

    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.queueFamilyIndex = dev->queueFamily;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(dev->device, &cpci, nullptr, &dev->cmdPool) != VK_SUCCESS) {
        SetError("vkCreateCommandPool failed");
        dev->Destroy();
        delete dev;
        return 0;
    }
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = dev->cmdPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(dev->device, &cbai, &dev->cmd) != VK_SUCCESS) {
        SetError("vkAllocateCommandBuffers failed");
        dev->Destroy();
        delete dev;
        return 0;
    }

    VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateSemaphore(dev->device, &sem, nullptr, &dev->imageAvailable);
    vkCreateSemaphore(dev->device, &sem, nullptr, &dev->renderFinished);
    vkCreateFence(dev->device, &fci, nullptr, &dev->inFlight);

    if (!CreateDefaultTexture(dev)) {
        SetError("Vulkan default texture/sampler failed");
        dev->Destroy();
        delete dev;
        return 0;
    }

    dev->valid = true;
    return PtrToHandle(dev);
}

extern "C" void absolute_desktop_gpu_vk_destroy(int64_t handle) {
    VkGpuDevice* dev = DeviceFrom(handle);
    if (!dev) return;
    dev->Destroy();
    delete dev;
}

extern "C" int32_t absolute_desktop_gpu_vk_is_valid(int64_t handle) {
    const VkGpuDevice* dev = DeviceFrom(handle);
    return dev && dev->valid ? 1 : 0;
}

extern "C" const char* absolute_desktop_gpu_vk_backend() { return "vulkan"; }
extern "C" const char* absolute_desktop_gpu_vk_last_error() { return g_lastError.c_str(); }

extern "C" void absolute_desktop_gpu_vk_unsupported(const char* what) {
    SetError(std::string("Vulkan backend: ") + (what ? what : "unsupported")
        + " (HLSL via DXC→SPIR-V; Texture2D t0 + SamplerState s0)");
}

extern "C" int32_t absolute_desktop_gpu_vk_is_resource(int64_t handle) {
    const uint32_t m = PeekRes(handle);
    return (m == kResShader || m == kResBuffer || m == kResIndex || m == kResPipeline
        || m == kResTexture || m == kResSampler)
        ? 1
        : 0;
}

extern "C" void absolute_desktop_gpu_vk_begin_frame(int64_t handle) {
    VkGpuDevice* dev = DeviceFrom(handle);
    if (!dev || !dev->valid) return;

    // Resize if needed
    const uint32_t w = static_cast<uint32_t>(absolute_desktop_width(dev->windowHandle));
    const uint32_t h = static_cast<uint32_t>(absolute_desktop_height(dev->windowHandle));
    if (w > 0 && h > 0 && (w != dev->width || h != dev->height)) {
        vkDeviceWaitIdle(dev->device);
        if (!dev->CreateSwapchain()) return;
    }

    vkWaitForFences(dev->device, 1, &dev->inFlight, VK_TRUE, UINT64_MAX);
    vkResetFences(dev->device, 1, &dev->inFlight);

    VkResult ar = vkAcquireNextImageKHR(
        dev->device, dev->swapchain, UINT64_MAX, dev->imageAvailable, VK_NULL_HANDLE, &dev->imageIndex);
    if (ar == VK_ERROR_OUT_OF_DATE_KHR) {
        vkDeviceWaitIdle(dev->device);
        dev->CreateSwapchain();
        return;
    }

    vkResetCommandBuffer(dev->cmd, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(dev->cmd, &bi);

    VkClearValue clear{};
    clear.color.float32[0] = dev->clearColor[0];
    clear.color.float32[1] = dev->clearColor[1];
    clear.color.float32[2] = dev->clearColor[2];
    clear.color.float32[3] = dev->clearColor[3];
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = dev->renderPass;
    rp.framebuffer = dev->framebuffers[dev->imageIndex];
    rp.renderArea.extent = {dev->width, dev->height};
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(dev->cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{};
    vp.width = static_cast<float>(dev->width);
    vp.height = static_cast<float>(dev->height);
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(dev->cmd, 0, 1, &vp);
    VkRect2D sc{{0, 0}, {dev->width, dev->height}};
    vkCmdSetScissor(dev->cmd, 0, 1, &sc);

    dev->boundPipeline = 0;
    dev->boundBuffer = 0;
    dev->boundIndexBuffer = 0;
    dev->boundTexture = 0;
    dev->boundSampler = 0;
    dev->cmdOpen = true;
    dev->inFrame = true;
}

extern "C" void absolute_desktop_gpu_vk_clear(
    int64_t handle, float r, float g, float b, float a) {
    VkGpuDevice* dev = DeviceFrom(handle);
    if (!dev) return;
    dev->clearColor[0] = r;
    dev->clearColor[1] = g;
    dev->clearColor[2] = b;
    dev->clearColor[3] = a;
    if (dev->cmdOpen && vkCmdClearAttachments) {
        VkClearAttachment ca{};
        ca.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ca.colorAttachment = 0;
        ca.clearValue.color.float32[0] = r;
        ca.clearValue.color.float32[1] = g;
        ca.clearValue.color.float32[2] = b;
        ca.clearValue.color.float32[3] = a;
        VkClearRect rect{};
        rect.layerCount = 1;
        rect.rect.extent = {dev->width, dev->height};
        vkCmdClearAttachments(dev->cmd, 1, &ca, 1, &rect);
    }
}

extern "C" void absolute_desktop_gpu_vk_end_frame(int64_t handle) {
    VkGpuDevice* dev = DeviceFrom(handle);
    if (!dev || !dev->cmdOpen) return;
    vkCmdEndRenderPass(dev->cmd);
    vkEndCommandBuffer(dev->cmd);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &dev->imageAvailable;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &dev->cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &dev->renderFinished;
    vkQueueSubmit(dev->queue, 1, &si, dev->inFlight);
    dev->cmdOpen = false;
    dev->inFrame = false;
}

extern "C" void absolute_desktop_gpu_vk_present(int64_t handle) {
    VkGpuDevice* dev = DeviceFrom(handle);
    if (!dev || !dev->valid) return;
    if (dev->cmdOpen) absolute_desktop_gpu_vk_end_frame(handle);

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &dev->renderFinished;
    pi.swapchainCount = 1;
    pi.pSwapchains = &dev->swapchain;
    pi.pImageIndices = &dev->imageIndex;
    vkQueuePresentKHR(dev->queue, &pi);
}

extern "C" int64_t absolute_desktop_gpu_vk_shader_create(
    int64_t gpuHandle, const char* vertexSource, const char* fragmentSource) {
    g_lastError.clear();
    VkGpuDevice* dev = DeviceFrom(gpuHandle);
    if (!dev || !vertexSource || !fragmentSource) {
        SetError("Vulkan createShader: invalid arguments");
        return 0;
    }
    auto* shader = new VkGpuShader();
    if (!CompileHlslToSpirv(vertexSource, "vs_6_0", shader->vsSpv)) {
        delete shader;
        return 0;
    }
    if (!CompileHlslToSpirv(fragmentSource, "ps_6_0", shader->psSpv)) {
        delete shader;
        return 0;
    }
    ParseUniforms(vertexSource, shader->uniforms, shader->cbufferSize);
    // Merge fragment uniforms if any
    std::unordered_map<std::string, UniformVar> fsU;
    UINT fsSize = 0;
    ParseUniforms(fragmentSource, fsU, fsSize);
    for (const auto& kv : fsU) {
        if (!shader->uniforms.count(kv.first)) {
            shader->uniforms[kv.first] = kv.second;
        }
    }
    if (shader->cbufferSize < fsSize) shader->cbufferSize = fsSize;
    shader->cbufferCpu.assign(kUboSize, 0);
    return PtrToHandle(shader);
}

extern "C" void absolute_desktop_gpu_vk_shader_destroy(int64_t, int64_t shaderHandle) {
    VkGpuShader* s = ShaderFrom(shaderHandle);
    if (!s) return;
    delete s;
}

extern "C" int64_t absolute_desktop_gpu_vk_buffer_create(
    int64_t gpuHandle, const float* data, int32_t floatCount) {
    g_lastError.clear();
    VkGpuDevice* dev = DeviceFrom(gpuHandle);
    if (!dev || !data || floatCount <= 0) {
        SetError("Vulkan VB requires data");
        return 0;
    }
    auto* buf = new VkGpuBuffer();
    buf->floatCount = floatCount;
    buf->size = static_cast<VkDeviceSize>(floatCount) * sizeof(float);
    void* mapped = nullptr;
    if (!dev->CreateBuffer(
            buf->size,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &buf->buffer,
            &buf->memory,
            &mapped)) {
        SetError("Vulkan CreateBuffer (VB) failed");
        delete buf;
        return 0;
    }
    std::memcpy(mapped, data, static_cast<size_t>(buf->size));
    vkUnmapMemory(dev->device, buf->memory);
    return PtrToHandle(buf);
}

extern "C" void absolute_desktop_gpu_vk_buffer_destroy(int64_t gpuHandle, int64_t bufferHandle) {
    VkGpuDevice* dev = DeviceFrom(gpuHandle);
    VkGpuBuffer* buf = BufferFrom(bufferHandle);
    if (!buf) return;
    if (dev && dev->boundBuffer == bufferHandle) dev->boundBuffer = 0;
    if (dev) {
        vkDeviceWaitIdle(dev->device);
        dev->DestroyBuffer(buf->buffer, buf->memory);
    }
    delete buf;
}

extern "C" int32_t absolute_desktop_gpu_vk_buffer_float_count(int64_t bufferHandle) {
    const VkGpuBuffer* b = BufferFrom(bufferHandle);
    return b ? b->floatCount : 0;
}

extern "C" int64_t absolute_desktop_gpu_vk_index_buffer_create(
    int64_t gpuHandle, const int32_t* indices, int32_t indexCount) {
    g_lastError.clear();
    VkGpuDevice* dev = DeviceFrom(gpuHandle);
    if (!dev || !indices || indexCount <= 0) {
        SetError("Vulkan IB requires data");
        return 0;
    }
    auto* buf = new VkGpuIndexBuffer();
    buf->indexCount = indexCount;
    const VkDeviceSize size = static_cast<VkDeviceSize>(indexCount) * sizeof(uint32_t);
    void* mapped = nullptr;
    if (!dev->CreateBuffer(
            size,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &buf->buffer,
            &buf->memory,
            &mapped)) {
        SetError("Vulkan CreateBuffer (IB) failed");
        delete buf;
        return 0;
    }
    auto* dst = static_cast<uint32_t*>(mapped);
    for (int32_t i = 0; i < indexCount; ++i) {
        dst[i] = indices[i] < 0 ? 0u : static_cast<uint32_t>(indices[i]);
    }
    vkUnmapMemory(dev->device, buf->memory);
    return PtrToHandle(buf);
}

extern "C" void absolute_desktop_gpu_vk_index_buffer_destroy(int64_t gpuHandle, int64_t bufferHandle) {
    VkGpuDevice* dev = DeviceFrom(gpuHandle);
    VkGpuIndexBuffer* buf = IndexFrom(bufferHandle);
    if (!buf) return;
    if (dev && dev->boundIndexBuffer == bufferHandle) dev->boundIndexBuffer = 0;
    if (dev) {
        vkDeviceWaitIdle(dev->device);
        dev->DestroyBuffer(buf->buffer, buf->memory);
    }
    delete buf;
}

extern "C" int32_t absolute_desktop_gpu_vk_index_buffer_count(int64_t bufferHandle) {
    const VkGpuIndexBuffer* b = IndexFrom(bufferHandle);
    return b ? b->indexCount : 0;
}

extern "C" int64_t absolute_desktop_gpu_vk_pipeline_create(
    int64_t gpuHandle,
    int64_t shaderHandle,
    int32_t strideBytes,
    const int32_t* locations,
    const int32_t* components,
    const int32_t* offsets,
    int32_t attrCount) {
    g_lastError.clear();
    VkGpuDevice* dev = DeviceFrom(gpuHandle);
    VkGpuShader* shader = ShaderFrom(shaderHandle);
    if (!dev || !shader || strideBytes <= 0 || attrCount <= 0) {
        SetError("Vulkan createPipeline requires gpu, shader, layout");
        return 0;
    }

    VkShaderModuleCreateInfo vsci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    vsci.codeSize = shader->vsSpv.size() * 4;
    vsci.pCode = shader->vsSpv.data();
    VkShaderModule vsMod = VK_NULL_HANDLE;
    VkShaderModule psMod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(dev->device, &vsci, nullptr, &vsMod) != VK_SUCCESS) {
        SetError("vkCreateShaderModule VS failed");
        return 0;
    }
    VkShaderModuleCreateInfo psci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    psci.codeSize = shader->psSpv.size() * 4;
    psci.pCode = shader->psSpv.data();
    if (vkCreateShaderModule(dev->device, &psci, nullptr, &psMod) != VK_SUCCESS) {
        vkDestroyShaderModule(dev->device, vsMod, nullptr);
        SetError("vkCreateShaderModule PS failed");
        return 0;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vsMod;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = psMod;
    stages[1].pName = "main";

    std::vector<VkVertexInputAttributeDescription> attrs(static_cast<size_t>(attrCount));
    for (int32_t i = 0; i < attrCount; ++i) {
        attrs[static_cast<size_t>(i)].location = static_cast<uint32_t>(locations[i]);
        attrs[static_cast<size_t>(i)].binding = 0;
        attrs[static_cast<size_t>(i)].format = FormatForComponents(components[i]);
        attrs[static_cast<size_t>(i)].offset = static_cast<uint32_t>(offsets[i]);
    }
    VkVertexInputBindingDescription bind{};
    bind.binding = 0;
    bind.stride = static_cast<uint32_t>(strideBytes);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
    vi.pVertexAttributeDescriptions = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blendAtt{};
    blendAtt.blendEnable = VK_TRUE;
    blendAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAtt.colorBlendOp = VK_BLEND_OP_ADD;
    blendAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAtt.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAtt.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &blendAtt;
    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;

    VkGraphicsPipelineCreateInfo gpci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gpci.stageCount = 2;
    gpci.pStages = stages;
    gpci.pVertexInputState = &vi;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState = &vp;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState = &ms;
    gpci.pColorBlendState = &cb;
    gpci.pDynamicState = &dyn;
    gpci.layout = dev->pipelineLayout;
    gpci.renderPass = dev->renderPass;
    gpci.subpass = 0;

    auto* pipe = new VkGpuPipeline();
    pipe->shader = shader;
    pipe->strideBytes = strideBytes;
    if (vkCreateGraphicsPipelines(dev->device, VK_NULL_HANDLE, 1, &gpci, nullptr, &pipe->pipeline)
        != VK_SUCCESS) {
        SetError("vkCreateGraphicsPipelines failed");
        vkDestroyShaderModule(dev->device, vsMod, nullptr);
        vkDestroyShaderModule(dev->device, psMod, nullptr);
        delete pipe;
        return 0;
    }
    vkDestroyShaderModule(dev->device, vsMod, nullptr);
    vkDestroyShaderModule(dev->device, psMod, nullptr);

    void* mapped = nullptr;
    if (!dev->CreateBuffer(
            kUboSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &pipe->ubo,
            &pipe->uboMem,
            &mapped)) {
        SetError("Vulkan UBO create failed");
        vkDestroyPipeline(dev->device, pipe->pipeline, nullptr);
        delete pipe;
        return 0;
    }
    pipe->uboMapped = mapped;
    std::memset(pipe->uboMapped, 0, kUboSize);

    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = dev->descPool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &dev->setLayout;
    if (vkAllocateDescriptorSets(dev->device, &dsai, &pipe->descSet) != VK_SUCCESS) {
        SetError("vkAllocateDescriptorSets failed");
        vkUnmapMemory(dev->device, pipe->uboMem);
        dev->DestroyBuffer(pipe->ubo, pipe->uboMem);
        vkDestroyPipeline(dev->device, pipe->pipeline, nullptr);
        delete pipe;
        return 0;
    }

    return PtrToHandle(pipe);
}

extern "C" void absolute_desktop_gpu_vk_pipeline_destroy(int64_t gpuHandle, int64_t pipelineHandle) {
    VkGpuDevice* dev = DeviceFrom(gpuHandle);
    VkGpuPipeline* pipe = PipelineFrom(pipelineHandle);
    if (!pipe) return;
    if (dev && dev->boundPipeline == pipelineHandle) dev->boundPipeline = 0;
    if (dev) {
        vkDeviceWaitIdle(dev->device);
        if (pipe->pipeline) vkDestroyPipeline(dev->device, pipe->pipeline, nullptr);
        if (pipe->uboMapped && pipe->uboMem) vkUnmapMemory(dev->device, pipe->uboMem);
        dev->DestroyBuffer(pipe->ubo, pipe->uboMem);
    }
    delete pipe;
}

extern "C" void absolute_desktop_gpu_vk_bind_pipeline(int64_t gpuHandle, int64_t pipelineHandle) {
    VkGpuDevice* dev = DeviceFrom(gpuHandle);
    if (dev) dev->boundPipeline = pipelineHandle;
}
extern "C" void absolute_desktop_gpu_vk_bind_buffer(int64_t gpuHandle, int64_t bufferHandle) {
    VkGpuDevice* dev = DeviceFrom(gpuHandle);
    if (dev) dev->boundBuffer = bufferHandle;
}
extern "C" void absolute_desktop_gpu_vk_bind_index_buffer(int64_t gpuHandle, int64_t bufferHandle) {
    VkGpuDevice* dev = DeviceFrom(gpuHandle);
    if (dev) dev->boundIndexBuffer = bufferHandle;
}
extern "C" void absolute_desktop_gpu_vk_bind_texture(int64_t gpuHandle, int64_t textureHandle, int32_t) {
    VkGpuDevice* dev = DeviceFrom(gpuHandle);
    if (dev) dev->boundTexture = textureHandle;
}
extern "C" void absolute_desktop_gpu_vk_bind_sampler(int64_t gpuHandle, int64_t samplerHandle, int32_t) {
    VkGpuDevice* dev = DeviceFrom(gpuHandle);
    if (dev) dev->boundSampler = samplerHandle;
}

static void FlushUniformsAndDescriptors(VkGpuDevice* dev, VkGpuPipeline* pipe) {
    if (pipe->shader && pipe->uboMapped) {
        if (pipe->shader->cbufferDirty) {
            std::memcpy(pipe->uboMapped, pipe->shader->cbufferCpu.data(), kUboSize);
            pipe->shader->cbufferDirty = false;
        }
    }
    VkGpuTexture* tex = TextureFrom(dev->boundTexture);
    if (!tex) tex = dev->defaultTexture;
    VkGpuSampler* samp = SamplerFrom(dev->boundSampler);
    if (!samp) samp = dev->defaultSampler;

    VkDescriptorBufferInfo ubi{};
    ubi.buffer = pipe->ubo;
    ubi.range = kUboSize;
    VkDescriptorImageInfo iii{};
    iii.imageView = tex ? tex->view : VK_NULL_HANDLE;
    iii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorImageInfo sii{};
    sii.sampler = samp ? samp->sampler : VK_NULL_HANDLE;

    VkWriteDescriptorSet writes[3]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = pipe->descSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &ubi;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = pipe->descSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[1].pImageInfo = &iii;
    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = pipe->descSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    writes[2].pImageInfo = &sii;
    vkUpdateDescriptorSets(dev->device, 3, writes, 0, nullptr);
}

static bool BindDrawState(VkGpuDevice* dev, VkGpuPipeline* pipe, VkGpuBuffer* vb) {
    if (!dev->cmdOpen || !pipe || !pipe->pipeline || !vb || !vb->buffer) {
        SetError("Vulkan draw requires open frame, pipeline, VB");
        return false;
    }
    FlushUniformsAndDescriptors(dev, pipe);
    vkCmdBindPipeline(dev->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe->pipeline);
    vkCmdBindDescriptorSets(
        dev->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, dev->pipelineLayout, 0, 1, &pipe->descSet, 0, nullptr);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(dev->cmd, 0, 1, &vb->buffer, &offset);
    return true;
}

extern "C" void absolute_desktop_gpu_vk_draw(int64_t gpuHandle, int32_t vertexCount) {
    VkGpuDevice* dev = DeviceFrom(gpuHandle);
    if (!dev || vertexCount <= 0) return;
    VkGpuPipeline* pipe = PipelineFrom(dev->boundPipeline);
    VkGpuBuffer* vb = BufferFrom(dev->boundBuffer);
    if (!BindDrawState(dev, pipe, vb)) return;
    vkCmdDraw(dev->cmd, static_cast<uint32_t>(vertexCount), 1, 0, 0);
}

extern "C" void absolute_desktop_gpu_vk_draw_indexed(int64_t gpuHandle, int32_t indexCount) {
    VkGpuDevice* dev = DeviceFrom(gpuHandle);
    if (!dev || indexCount <= 0) return;
    VkGpuPipeline* pipe = PipelineFrom(dev->boundPipeline);
    VkGpuBuffer* vb = BufferFrom(dev->boundBuffer);
    VkGpuIndexBuffer* ib = IndexFrom(dev->boundIndexBuffer);
    if (!ib || !ib->buffer) {
        SetError("Vulkan drawIndexed requires IB");
        return;
    }
    if (!BindDrawState(dev, pipe, vb)) return;
    vkCmdBindIndexBuffer(dev->cmd, ib->buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(dev->cmd, static_cast<uint32_t>(indexCount), 1, 0, 0, 0);
}

static void WriteUniform(VkGpuDevice* dev, const char* name, const void* data, size_t size) {
    if (!dev || !name) return;
    VkGpuPipeline* pipe = PipelineFrom(dev->boundPipeline);
    if (!pipe || !pipe->shader) return;
    auto it = pipe->shader->uniforms.find(name);
    if (it == pipe->shader->uniforms.end()) return;
    if (it->second.offset + size > pipe->shader->cbufferCpu.size()) return;
    std::memcpy(pipe->shader->cbufferCpu.data() + it->second.offset, data, size);
    pipe->shader->cbufferDirty = true;
}

extern "C" void absolute_desktop_gpu_vk_set_uniform_f(int64_t gpuHandle, const char* name, float value) {
    WriteUniform(DeviceFrom(gpuHandle), name, &value, sizeof(value));
}
extern "C" void absolute_desktop_gpu_vk_set_uniform_i(int64_t gpuHandle, const char* name, int32_t value) {
    WriteUniform(DeviceFrom(gpuHandle), name, &value, sizeof(value));
}
extern "C" void absolute_desktop_gpu_vk_set_uniform_2f(
    int64_t gpuHandle, const char* name, float x, float y) {
    const float v[2] = {x, y};
    WriteUniform(DeviceFrom(gpuHandle), name, v, sizeof(v));
}

extern "C" int64_t absolute_desktop_gpu_vk_texture_from_sprite(int64_t gpuHandle, int64_t spriteHandle) {
    g_lastError.clear();
    VkGpuDevice* dev = DeviceFrom(gpuHandle);
    DesktopSpriteView* sprite = SpriteFrom(spriteHandle);
    if (!dev || !sprite || sprite->pixels.empty()) {
        SetError("Vulkan invalid sprite for texture");
        return 0;
    }
    const int32_t w = sprite->width;
    const int32_t h = sprite->height;
    if (w <= 0 || h <= 0) {
        SetError("Vulkan texture size invalid");
        return 0;
    }

    std::vector<uint8_t> rgba(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
    for (int32_t y = 0; y < h; ++y) {
        const int32_t srcY = h - 1 - y;
        for (int32_t x = 0; x < w; ++x) {
            const uint32_t c = sprite->pixels[
                static_cast<size_t>(srcY) * static_cast<size_t>(w) + static_cast<size_t>(x)];
            const size_t di =
                (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u;
            rgba[di + 0] = static_cast<uint8_t>((c >> 16) & 0xFF);
            rgba[di + 1] = static_cast<uint8_t>((c >> 8) & 0xFF);
            rgba[di + 2] = static_cast<uint8_t>(c & 0xFF);
            rgba[di + 3] = c == 0 ? 0 : 255;
        }
    }

    auto* tex = new VkGpuTexture();
    tex->width = w;
    tex->height = h;

    // Host-visible linear image (simple path).
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_LINEAR;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
    if (vkCreateImage(dev->device, &ii, nullptr, &tex->image) != VK_SUCCESS) {
        SetError("vkCreateImage failed");
        delete tex;
        return 0;
    }
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(dev->device, tex->image, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = dev->FindMemoryType(
        req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX
        || vkAllocateMemory(dev->device, &ai, nullptr, &tex->memory) != VK_SUCCESS) {
        SetError("texture memory alloc failed");
        vkDestroyImage(dev->device, tex->image, nullptr);
        delete tex;
        return 0;
    }
    vkBindImageMemory(dev->device, tex->image, tex->memory, 0);
    void* mapped = nullptr;
    vkMapMemory(dev->device, tex->memory, 0, req.size, 0, &mapped);
    // Assume tightly packed row pitch for linear images on most drivers for small sprites.
    std::memcpy(mapped, rgba.data(), rgba.size());
    vkUnmapMemory(dev->device, tex->memory);

    if (!dev->inFrame) {
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkResetCommandBuffer(dev->cmd, 0);
        vkBeginCommandBuffer(dev->cmd, &bi);
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = tex->image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(
            dev->cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
        vkEndCommandBuffer(dev->cmd);
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &dev->cmd;
        vkQueueSubmit(dev->queue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(dev->queue);
    }
    tex->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = tex->image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(dev->device, &vi, nullptr, &tex->view) != VK_SUCCESS) {
        SetError("texture image view failed");
        vkDestroyImage(dev->device, tex->image, nullptr);
        vkFreeMemory(dev->device, tex->memory, nullptr);
        delete tex;
        return 0;
    }
    return PtrToHandle(tex);
}

extern "C" void absolute_desktop_gpu_vk_texture_destroy(int64_t gpuHandle, int64_t textureHandle) {
    VkGpuDevice* dev = DeviceFrom(gpuHandle);
    VkGpuTexture* tex = TextureFrom(textureHandle);
    if (!tex || (dev && tex == dev->defaultTexture)) return;
    if (dev && dev->boundTexture == textureHandle) dev->boundTexture = 0;
    if (dev) {
        vkDeviceWaitIdle(dev->device);
        if (tex->view) vkDestroyImageView(dev->device, tex->view, nullptr);
        if (tex->image) vkDestroyImage(dev->device, tex->image, nullptr);
        if (tex->memory) vkFreeMemory(dev->device, tex->memory, nullptr);
    }
    delete tex;
}

extern "C" int32_t absolute_desktop_gpu_vk_texture_width(int64_t textureHandle) {
    const VkGpuTexture* t = TextureFrom(textureHandle);
    return t ? t->width : 0;
}
extern "C" int32_t absolute_desktop_gpu_vk_texture_height(int64_t textureHandle) {
    const VkGpuTexture* t = TextureFrom(textureHandle);
    return t ? t->height : 0;
}

extern "C" int64_t absolute_desktop_gpu_vk_sampler_create(
    int64_t gpuHandle, int32_t filter, int32_t wrap) {
    g_lastError.clear();
    VkGpuDevice* dev = DeviceFrom(gpuHandle);
    if (!dev) {
        SetError("Vulkan sampler requires GPU");
        return 0;
    }
    auto* samp = new VkGpuSampler();
    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = filter == 1 ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    sci.minFilter = sci.magFilter;
    auto mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (wrap == 1) mode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    if (wrap == 2) mode = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    sci.addressModeU = mode;
    sci.addressModeV = mode;
    sci.addressModeW = mode;
    if (vkCreateSampler(dev->device, &sci, nullptr, &samp->sampler) != VK_SUCCESS) {
        SetError("vkCreateSampler failed");
        delete samp;
        return 0;
    }
    return PtrToHandle(samp);
}

extern "C" void absolute_desktop_gpu_vk_sampler_destroy(int64_t gpuHandle, int64_t samplerHandle) {
    VkGpuDevice* dev = DeviceFrom(gpuHandle);
    VkGpuSampler* samp = SamplerFrom(samplerHandle);
    if (!samp || (dev && samp == dev->defaultSampler)) return;
    if (dev && dev->boundSampler == samplerHandle) dev->boundSampler = 0;
    if (dev) {
        vkDeviceWaitIdle(dev->device);
        if (samp->sampler) vkDestroySampler(dev->device, samp->sampler, nullptr);
    }
    delete samp;
}

#endif
