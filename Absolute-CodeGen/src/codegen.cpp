#include "codegen_internal.h"

namespace Absolute {
    CodeGenerator::CodeGenerator(const Analyzer* analyzer)
        : impl(std::make_unique<Impl>(*this, analyzer)) {
    }

    CodeGenerator::~CodeGenerator() = default;

    std::string CodeGenerator::Generate(Program& program, const std::string& moduleName,
        const std::string& targetTriple) {
        return impl->Generate(program, moduleName, targetTriple);
    }

    void CodeGenerator::GenerateObject(
        Program& program, const std::string& moduleName, const std::string& outputPath,
        bool sanitizeAddress, const std::string& targetTriple) {
        impl->GenerateObject(program, moduleName, outputPath, sanitizeAddress, targetTriple);
    }

}
