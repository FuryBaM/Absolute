#pragma once

#include <llvm/AsmParser/Parser.h>
#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/DebugLoc.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Linker/Linker.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Transforms/Instrumentation/AddressSanitizer.h>
#include <llvm/Transforms/Instrumentation/ThreadSanitizer.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/FileSystem.h>
#if LLVM_VERSION_MAJOR >= 21
#include <llvm/Support/ModRef.h>
#endif
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Triple.h>
#if LLVM_VERSION_MAJOR >= 18
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/SubtargetFeature.h>
#else
#include <llvm/Support/Host.h>
#include <llvm/MC/SubtargetFeature.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// LLVM 20 added an LTO-phase argument to the optimizer extension point
// callbacks. Adapt the two-argument form the call sites use, so they stay
// readable and version independent.
#if LLVM_VERSION_MAJOR >= 20
namespace Absolute::LlvmCompat {
    template <typename Callback>
    auto OptimizerLastEPCallback(Callback callback) {
        return [callback = std::move(callback)](
            llvm::ModulePassManager& modulePasses,
            llvm::OptimizationLevel level,
            llvm::ThinOrFullLTOPhase) mutable {
            callback(modulePasses, level);
        };
    }
}

#define registerOptimizerLastEPCallback(callback) \
    registerOptimizerLastEPCallback(::Absolute::LlvmCompat::OptimizerLastEPCallback(callback))
#endif

// LLVM 21 changed Module target triples from strings to llvm::Triple. Keep the
// LLVM 18 call sites readable while selecting the new overloads during
// preprocessing.
#if LLVM_VERSION_MAJOR >= 21
namespace Absolute::LlvmCompat {
    inline llvm::Triple TargetTriple(const llvm::Triple& triple) {
        return triple;
    }

    inline llvm::Triple TargetTriple(const std::string& triple) {
        return llvm::Triple(triple);
    }

    inline llvm::Triple TargetTriple(llvm::StringRef triple) {
        return llvm::Triple(triple);
    }
}

#define getTargetTriple() getTargetTriple().str()
#define setTargetTriple(value) setTargetTriple(::Absolute::LlvmCompat::TargetTriple(value))
#define createTargetMachine(triple, ...) \
    createTargetMachine(::Absolute::LlvmCompat::TargetTriple(triple), __VA_ARGS__)
#endif
