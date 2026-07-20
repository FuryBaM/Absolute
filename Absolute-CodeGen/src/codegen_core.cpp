#include "codegen_internal.h"

namespace Absolute {
    CodeGenerator::Impl::Impl(CodeGenerator& visitor, const Analyzer* analyzer)
        : visitor(visitor), analyzer(analyzer), builder(context) {
    }

    [[noreturn]] void CodeGenerator::Impl::Fail(const std::string& message) const {
        throw std::runtime_error("LLVM codegen: " + message);
    }
}
