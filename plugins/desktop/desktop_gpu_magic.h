#pragma once

#include <cstdint>

// First field of every Gpu device handle for multi-backend dispatch.
constexpr uint32_t kAbsoluteGpuMagicGL = 0x314C4731u;    // '1GL1'
constexpr uint32_t kAbsoluteGpuMagicD3D11 = 0x31443344u; // 'D3D1'
constexpr uint32_t kAbsoluteGpuMagicD3D12 = 0x32443344u; // 'D3D2'
constexpr uint32_t kAbsoluteGpuMagicVK = 0x314B5631u;    // '1VK1'

inline uint32_t AbsoluteGpuPeekMagic(int64_t handle) {
    if (!handle) return 0;
    return *reinterpret_cast<const uint32_t*>(static_cast<intptr_t>(handle));
}

inline bool AbsoluteGpuIsD3D11(int64_t handle) {
    return AbsoluteGpuPeekMagic(handle) == kAbsoluteGpuMagicD3D11;
}

inline bool AbsoluteGpuIsD3D12(int64_t handle) {
    return AbsoluteGpuPeekMagic(handle) == kAbsoluteGpuMagicD3D12;
}

inline bool AbsoluteGpuIsVK(int64_t handle) {
    return AbsoluteGpuPeekMagic(handle) == kAbsoluteGpuMagicVK;
}

inline bool AbsoluteGpuIsGL(int64_t handle) {
    return AbsoluteGpuPeekMagic(handle) == kAbsoluteGpuMagicGL;
}

// Direct3D device (11 or 12) — shared early-outs for unsupported resource paths.
inline bool AbsoluteGpuIsD3D(int64_t handle) {
    return AbsoluteGpuIsD3D11(handle) || AbsoluteGpuIsD3D12(handle);
}
