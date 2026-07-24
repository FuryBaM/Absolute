#pragma once

#include <cstdint>

// First field of every Gpu device handle (OpenGL / D3D11) for multi-backend dispatch.
constexpr uint32_t kAbsoluteGpuMagicGL = 0x314C4731u;    // '1GL1'
constexpr uint32_t kAbsoluteGpuMagicD3D11 = 0x31443344u; // 'D3D1'

inline uint32_t AbsoluteGpuPeekMagic(int64_t handle) {
    if (!handle) return 0;
    return *reinterpret_cast<const uint32_t*>(static_cast<intptr_t>(handle));
}

inline bool AbsoluteGpuIsD3D11(int64_t handle) {
    return AbsoluteGpuPeekMagic(handle) == kAbsoluteGpuMagicD3D11;
}

inline bool AbsoluteGpuIsGL(int64_t handle) {
    return AbsoluteGpuPeekMagic(handle) == kAbsoluteGpuMagicGL;
}
