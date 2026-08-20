#include "codegen_internal.h"

namespace Absolute {
    llvm::FunctionCallee CodeGenerator::Impl::ErrorSet() {
        return module->getOrInsertFunction("absolute_error_set",
            llvm::FunctionType::get(builder.getVoidTy(),
                {builder.getInt64Ty(), builder.getInt64Ty()}, false));
    }

    llvm::FunctionCallee CodeGenerator::Impl::ErrorSetDetails() {
        return module->getOrInsertFunction("absolute_error_set_details",
            llvm::FunctionType::get(builder.getVoidTy(),
                {builder.getPtrTy(), builder.getPtrTy()}, false));
    }

    void CodeGenerator::Impl::EmitErrorDetails(
        llvm::Value* handle, const std::string& typeName) {
        if (!handle || typeName.empty()) return;
        llvm::Value* name = builder.CreateGlobalStringPtr(typeName, "exception.type.name");
        // Every thrown object derives from Error, whose first declared field is
        // the message, and base fields are laid out first, so the field of the
        // static type is the field of the object. A type the backend does not
        // know about reports its name alone rather than nothing.
        llvm::Value* message = llvm::ConstantPointerNull::get(builder.getPtrTy());
        if (const auto found = classes.find(typeName); found != classes.end()) {
            if (const auto field = found->second.fieldByName.find("message");
                field != found->second.fieldByName.end() &&
                field->second.typeName == "string") {
                llvm::Value* object = EmitManagedGet(handle);
                message = builder.CreateLoad(builder.getPtrTy(),
                    FieldAddress(object, found->second, field->second),
                    "exception.message");
            }
        }
        builder.CreateCall(ErrorSetDetails(), {name, message});
    }

    llvm::FunctionCallee CodeGenerator::Impl::ErrorPending() {
        llvm::FunctionCallee callee = module->getOrInsertFunction(
            "absolute_error_pending",
            llvm::FunctionType::get(builder.getInt1Ty(), {}, false));
        // It reads one thread-local flag and returns. Saying so is what lets a
        // field stay in a register across it: this call sits after every call
        // to a function that can throw, and undescribed it reads and writes all
        // memory, so every such call was a barrier that forced everything
        // reachable back to memory and reloaded it afterwards. No other thread
        // can change what it reads, which is what makes the claim safe.
        if (auto* function = llvm::dyn_cast<llvm::Function>(callee.getCallee())) {
            function->setOnlyReadsMemory();
            function->setDoesNotThrow();
            function->setWillReturn();
        }
        return callee;
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

    void CodeGenerator::Impl::EmitTransferCleanups(
        size_t destinationScope, bool includeEqualFinalizer, SymbolId transferredOwner) {
        for (size_t count = scopes.size(); count > destinationScope; --count) {
            const size_t scopeIndex = count - 1;
            EmitScopeCleanup(scopeIndex, transferredOwner);
            if (!builder.GetInsertBlock() || builder.GetInsertBlock()->getTerminator()) return;
            if (emittingFinally) continue;

            for (auto target = finallyTargets.rbegin(); target != finallyTargets.rend(); ++target) {
                if (!target->statement || !target->statement->finallyBody ||
                    target->scopeCount != scopeIndex) continue;
                const bool exitsTry = includeEqualFinalizer
                    ? target->scopeCount >= destinationScope
                    : target->scopeCount > destinationScope;
                if (!exitsTry) continue;
                const bool old = emittingFinally;
                emittingFinally = true;
                target->statement->finallyBody->Accept(visitor);
                emittingFinally = old;
                if (!builder.GetInsertBlock() || builder.GetInsertBlock()->getTerminator()) return;
            }
        }
    }

    void CodeGenerator::Impl::EmitExceptionPropagation(SymbolId transferredOwner) {
        const size_t destinationScope = exceptionTargets.empty()
            ? 0 : exceptionTargets.back().scopeCount;
        EmitTransferCleanups(destinationScope, exceptionTargets.empty(), transferredOwner);
        if (!builder.GetInsertBlock() || builder.GetInsertBlock()->getTerminator()) return;
        if (!exceptionTargets.empty()) {
            builder.CreateBr(exceptionTargets.back().handlerBlock);
            return;
        }

        llvm::Function* function = CurrentFunction();
        if (!function) Fail("exception propagation outside a function");

        // Leaving a constructor through an exception. Scope unwinding above has
        // released the locals, but the fields already stored into `this` are not
        // a scope and no caller has a name for them, so they would leak.
        //
        // Fields are released directly rather than through the class destructor,
        // because that destructor first calls the type's own destroy() hook. A
        // constructor that threw never established the invariants such a hook
        // assumes, so running it here would execute user cleanup on an object
        // that was never built.
        //
        // Only fields are touched, and only once each: currentConstructorClass is
        // set after the base constructor has succeeded, so a failing base cleans
        // its own fields on its own path and this frame never repeats that work.
        // Object storage is zero-initialized before a constructor runs, so fields
        // the constructor had not reached yet are null and cost a no-op.
        if (!currentConstructorClass.empty() && currentThis) {
            if (const auto found = classes.find(currentConstructorClass);
                found != classes.end()) {
                ClassInfo& partial = found->second;
                for (auto field = partial.fields.rbegin();
                    field != partial.fields.rend(); ++field) {
                    if (!TypeNeedsCleanup(field->typeName)) continue;
                    EmitValueCleanup(
                        FieldAddress(currentThis, partial, *field), field->typeName);
                }
            }
        }

        if (function->getName() == "main") {
            builder.CreateCall(ErrorReport());
            // An unhandled exception leaves the process with a failing status,
            // and the status is an int32 whatever `main` was declared to
            // return. Returning the 1 was right only for `int32 main()`: a
            // `void main()` with a `try` in it -- five of them are in tests/ --
            // emitted a value from a function that returns none, and any other
            // width emitted the wrong one. Exiting says what was meant and
            // says it the same way for every signature.
            builder.CreateCall(ExitFailure(), {builder.getInt32(1)});
            builder.CreateUnreachable();
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
        // The statement that owns these temporaries is being left through its
        // exception path, so the release it ends with never runs. They are
        // released here instead, without being taken off the ledger: the
        // ordinary path still has its own release to emit.
        for (size_t index = temporaryManagedOwners.size(); index > 0; --index) {
            const TemporaryOwner& temporary = temporaryManagedOwners[index - 1];
            if (temporary.kind == TemporaryOwner::Kind::ArrayBuffer) {
                builder.CreateCall(Free(), {temporary.handle});
                continue;
            }
            if (temporary.kind == TemporaryOwner::Kind::StringStorage) {
                builder.CreateCall(StringRelease(), {temporary.handle});
                continue;
            }
            if (temporary.kind == TemporaryOwner::Kind::AggregateValue) {
                EmitValueCleanup(temporary.handle, temporary.typeName);
                continue;
            }
            llvm::Value* pointee = EmitManagedGet(temporary.handle, false);
            EmitPointeeCleanup(pointee, temporary.typeName);
            builder.CreateCall(ManagedDestroy(), {temporary.handle});
        }
        EmitExceptionPropagation();
        builder.SetInsertPoint(continueBlock);
    }
}
