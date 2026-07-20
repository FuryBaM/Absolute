#include "codegen_internal.h"

namespace Absolute {
    llvm::FunctionCallee CodeGenerator::Impl::ErrorSet() {
        return module->getOrInsertFunction("absolute_error_set",
            llvm::FunctionType::get(builder.getVoidTy(),
                {builder.getInt64Ty(), builder.getInt64Ty()}, false));
    }

    llvm::FunctionCallee CodeGenerator::Impl::ErrorPending() {
        return module->getOrInsertFunction("absolute_error_pending",
            llvm::FunctionType::get(builder.getInt1Ty(), {}, false));
    }

    llvm::FunctionCallee CodeGenerator::Impl::ErrorType() {
        return module->getOrInsertFunction("absolute_error_type",
            llvm::FunctionType::get(builder.getInt64Ty(), {}, false));
    }

    llvm::FunctionCallee CodeGenerator::Impl::ErrorTake() {
        return module->getOrInsertFunction("absolute_error_take",
            llvm::FunctionType::get(builder.getInt64Ty(), {}, false));
    }

    llvm::FunctionCallee CodeGenerator::Impl::ErrorReport() {
        return module->getOrInsertFunction("absolute_error_report",
            llvm::FunctionType::get(builder.getVoidTy(), {}, false));
    }

    std::uint64_t CodeGenerator::Impl::ExceptionTypeId(const std::string& name) const {
        std::uint64_t hash = 14695981039346656037ULL;
        for (const unsigned char character : name) {
            hash ^= character;
            hash *= 1099511628211ULL;
        }
        return hash == 0 ? 1 : hash;
    }

    bool CodeGenerator::Impl::IsClassDerivedFrom(
        const std::string& type, const std::string& base) const {
        if (type == base) return true;
        const auto found = classes.find(type);
        if (found == classes.end()) return false;
        for (const std::string& parent : found->second.parents)
            if (parent == base || IsClassDerivedFrom(parent, base)) return true;
        return false;
    }

    void CodeGenerator::Impl::EmitActiveFinalizers(
        size_t destinationScope, bool includeEqual) {
        if (emittingFinally) return;
        const bool old = emittingFinally;
        emittingFinally = true;
        for (auto target = finallyTargets.rbegin(); target != finallyTargets.rend(); ++target) {
            const bool exitsTry = includeEqual
                ? target->scopeCount >= destinationScope
                : target->scopeCount > destinationScope;
            if (!exitsTry || !target->statement || !target->statement->finallyBody) continue;
            target->statement->finallyBody->Accept(visitor);
            if (!builder.GetInsertBlock() || builder.GetInsertBlock()->getTerminator()) break;
        }
        emittingFinally = old;
    }

    void CodeGenerator::Impl::EmitExceptionPropagation(SymbolId transferredOwner) {
        const size_t destinationScope = exceptionTargets.empty()
            ? 0 : exceptionTargets.back().scopeCount;
        EmitActiveFinalizers(destinationScope, exceptionTargets.empty());
        if (!builder.GetInsertBlock() || builder.GetInsertBlock()->getTerminator()) return;
        EmitCleanupsFrom(destinationScope, transferredOwner);
        if (!exceptionTargets.empty()) {
            builder.CreateBr(exceptionTargets.back().handlerBlock);
            return;
        }

        llvm::Function* function = CurrentFunction();
        if (!function) Fail("exception propagation outside a function");
        if (function->getName() == "main") {
            builder.CreateCall(ErrorReport());
            builder.CreateRet(builder.getInt32(1));
        }
        else if (function->getReturnType()->isVoidTy()) builder.CreateRetVoid();
        else builder.CreateRet(llvm::Constant::getNullValue(function->getReturnType()));
    }

    void CodeGenerator::Impl::EmitExceptionCheck(
        llvm::Value* temporaryManagedOwner, llvm::Value* temporaryRawOwner) {
        if (!exceptionsEnabled) return;
        llvm::Function* function = CurrentFunction();
        if (!function || !builder.GetInsertBlock() || builder.GetInsertBlock()->getTerminator()) return;
        llvm::Value* pending = builder.CreateCall(ErrorPending(), {}, "error.pending");
        llvm::BasicBlock* errorBlock = llvm::BasicBlock::Create(context, "error.propagate", function);
        llvm::BasicBlock* continueBlock = llvm::BasicBlock::Create(context, "error.continue", function);
        builder.CreateCondBr(pending, errorBlock, continueBlock);
        builder.SetInsertPoint(errorBlock);
        if (temporaryManagedOwner)
            builder.CreateCall(ManagedDestroy(), {temporaryManagedOwner});
        if (temporaryRawOwner)
            builder.CreateCall(Free(), {temporaryRawOwner});
        EmitExceptionPropagation();
        builder.SetInsertPoint(continueBlock);
    }
}
