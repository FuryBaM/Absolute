#include "codegen_internal.h"

namespace Absolute {
    CodeGenerator::CodeGenerator(const Analyzer* analyzer)
        : impl(std::make_unique<Impl>(*this, analyzer)) {
    }

    CodeGenerator::~CodeGenerator() = default;

    std::string CodeGenerator::Generate(Program& program, const std::string& moduleName) {
        return impl->Generate(program, moduleName);
    }

    void CodeGenerator::GenerateObject(
        Program& program, const std::string& moduleName, const std::string& outputPath) {
        impl->GenerateObject(program, moduleName, outputPath);
    }

}
