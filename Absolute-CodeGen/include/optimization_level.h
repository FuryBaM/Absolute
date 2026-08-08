#pragma once

#include <cstdint>

// The optimization level is part of the compiler driver's command line, which
// exists in every configuration. Keep it free of LLVM dependencies so frontend
// only builds (ABSOLUTE_ENABLE_LLVM=OFF) can parse -O0..-O3 without pulling in
// codegen.h and the LLVM headers behind it.
namespace Absolute {
    enum class OptimizationLevel : std::uint8_t {
        O0,
        O1,
        O2,
        O3
    };
}
