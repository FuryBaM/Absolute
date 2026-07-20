#include "codegen_internal.h"

namespace Absolute {
    void CodeGenerator::Visit(SingleStatement* stmt) {
        if (impl->phase == Impl::Phase::EmitBodies && stmt->expr) stmt->expr->Accept(*this);
    }

    void CodeGenerator::Visit(CompoundStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        impl->PushScope();
        for (const auto& statement : stmt->statements) {
            if (!statement || impl->builder.GetInsertBlock()->getTerminator()) break;
            statement->Accept(*this);
        }
        impl->PopScope(true);
    }

    void CodeGenerator::Visit(FunctionCallStmt* stmt) {
        if (impl->phase == Impl::Phase::EmitBodies && stmt->value) stmt->value->Accept(*this);
    }

    void CodeGenerator::Visit(FunctionDeclStmt* stmt) {
        if (impl->phase == Impl::Phase::DeclareFunctions) impl->DeclareFunction(*stmt);
        else if (!stmt->IsExternal()) impl->EmitFunction(*stmt);
    }

    void CodeGenerator::Visit(ReturnStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("return outside a function");
        if (function->getReturnType()->isVoidTy()) {
            impl->EmitCleanupsFrom(0);
            impl->builder.CreateRetVoid();
            return;
        }
        llvm::Value* result = nullptr;
        if (ArrayRankName(impl->currentReturnTypeName) > 0) {
            Impl::ArrayView source = impl->ArrayViewFromValue(
                impl->Evaluate(stmt->expr.get()), impl->currentReturnTypeName);
            llvm::Value* elementCount = impl->builder.getInt64(1);
            for (llvm::Value* dimension : source.dimensions)
                elementCount = impl->builder.CreateMul(elementCount, dimension, "return.array.element.count");
            llvm::Value* byteCount = impl->builder.CreateMul(elementCount,
                impl->builder.getInt64(impl->SizeOfTypeName(
                    ArrayElementTypeName(impl->currentReturnTypeName,
                        ArrayRankName(impl->currentReturnTypeName)))),
                "return.array.byte.count");
            llvm::Value* copiedData = impl->builder.CreateCall(
                impl->Malloc(), {byteCount}, "return.array.data");
            impl->builder.CreateMemCpy(copiedData, llvm::MaybeAlign(16),
                source.address, llvm::MaybeAlign(1), byteCount);
            source.address = copiedData;
            result = impl->BuildArrayDescriptor(source);
        }
        else result = impl->Coerce(impl->Evaluate(stmt->expr.get()), function->getReturnType());
        SymbolId transferredOwner = InvalidSymbolId;
        if (IsManagedPointerTypeName(impl->currentReturnTypeName)) {
            const auto* returnedIdentifier = dynamic_cast<IdentifierExpr*>(stmt->expr.get());
            if (returnedIdentifier) {
                Impl::Variable& returned = impl->RequireVariable(returnedIdentifier->name);
                if (returned.managedOwner) transferredOwner = returned.symbol;
            }
        }
        impl->EmitCleanupsFrom(0, transferredOwner);
        impl->builder.CreateRet(result);
    }

    void CodeGenerator::Visit(AssignmentStmt* stmt) {
        if (impl->phase == Impl::Phase::EmitBodies && stmt->expr) stmt->expr->Accept(*this);
    }

    void CodeGenerator::Visit(VarDeclStmt* stmt) {
        if (!stmt->expr) return;
        if (impl->phase == Impl::Phase::DeclareFunctions) {
            if (ArrayRankName(impl->DeclaredTypeName(*stmt->expr)) > 0)
                impl->DeclareGlobalArray(*stmt->expr);
            return;
        }
        if (!impl->CurrentFunction()) return;
        stmt->expr->Accept(*this);
    }

    void CodeGenerator::Visit(StructDeclStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        const std::string name = impl->Qualify(stmt->name);
        auto found = impl->structs.find(name);
        if (found == impl->structs.end()) impl->Fail("unregistered struct '" + name + "'");
        impl->EmitStructBodies(found->second);
    }

    void CodeGenerator::Visit(ClassDeclStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        const std::string name = impl->Qualify(stmt->name);
        auto found = impl->classes.find(name);
        if (found == impl->classes.end()) impl->Fail("unregistered class '" + name + "'");
        impl->EmitClassBodies(found->second);
    }

    void CodeGenerator::Visit(InterfaceDeclStmt* stmt) {
        (void)stmt;
    }

    void CodeGenerator::Visit(ConstructorDeclStmt* stmt) {
        (void)stmt;
    }

    void CodeGenerator::Visit(EnumDeclStmt* stmt) {
        (void)stmt;
    }

    void CodeGenerator::Visit(GroupDeclStmt* stmt) {
        (void)stmt;
    }

    void CodeGenerator::Visit(IfStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("if outside a function");

        llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(impl->context, "if.end", function);
        for (size_t index = 0; index < stmt->branches.size(); ++index) {
            auto& branch = stmt->branches[index];
            llvm::Value* condition = impl->AsCondition(impl->Evaluate(branch.condition.get()));
            llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(impl->context, "if.body", function);
            const bool hasNext = index + 1 < stmt->branches.size() || stmt->elseBranch;
            llvm::BasicBlock* nextBlock = hasNext
                ? llvm::BasicBlock::Create(impl->context, "if.next", function)
                : mergeBlock;
            impl->builder.CreateCondBr(condition, bodyBlock, nextBlock);

            impl->builder.SetInsertPoint(bodyBlock);
            if (branch.body) branch.body->Accept(*this);
            impl->BranchIfNeeded(mergeBlock);
            impl->builder.SetInsertPoint(nextBlock);
        }

        if (stmt->elseBranch) {
            stmt->elseBranch->Accept(*this);
            impl->BranchIfNeeded(mergeBlock);
        }
        impl->builder.SetInsertPoint(mergeBlock);
    }

    void CodeGenerator::Visit(SwitchStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("switch outside a function");

        llvm::Value* selector = impl->Evaluate(stmt->value.get());
        llvm::Type* selectorType = selector->getType();
        if (!selectorType->isIntegerTy())
            impl->Fail("switch value must lower to an integer");

        llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(
            impl->context, stmt->exhaustive ? "match.end" : "switch.end", function);
        for (auto& branch : stmt->cases) {
            llvm::Value* label = impl->Coerce(impl->Evaluate(branch.label.get()), selectorType);
            llvm::Value* equal = impl->builder.CreateICmpEQ(selector, label, "switch.equal");
            llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(
                impl->context, "switch.case", function);
            llvm::BasicBlock* nextBlock = llvm::BasicBlock::Create(
                impl->context, "switch.next", function);
            impl->builder.CreateCondBr(equal, bodyBlock, nextBlock);

            impl->builder.SetInsertPoint(bodyBlock);
            if (branch.body) branch.body->Accept(*this);
            impl->BranchIfNeeded(endBlock);
            impl->builder.SetInsertPoint(nextBlock);
        }

        if (stmt->defaultCase) stmt->defaultCase->Accept(*this);
        impl->BranchIfNeeded(endBlock);
        impl->builder.SetInsertPoint(endBlock);
    }

    void CodeGenerator::Visit(ForStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("for outside a function");

        impl->PushScope();
        for (const auto& expression : stmt->init) if (expression) expression->Accept(*this);

        llvm::BasicBlock* conditionBlock = llvm::BasicBlock::Create(impl->context, "for.condition", function);
        llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(impl->context, "for.body", function);
        llvm::BasicBlock* updateBlock = llvm::BasicBlock::Create(impl->context, "for.update", function);
        llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(impl->context, "for.end", function);
        impl->builder.CreateBr(conditionBlock);

        impl->builder.SetInsertPoint(conditionBlock);
        llvm::Value* condition = stmt->condition ? impl->AsCondition(impl->Evaluate(stmt->condition.get())) : impl->builder.getTrue();
        impl->builder.CreateCondBr(condition, bodyBlock, endBlock);

        impl->loops.push_back({updateBlock, endBlock, impl->scopes.size()});
        impl->builder.SetInsertPoint(bodyBlock);
        if (stmt->body) stmt->body->Accept(*this);
        impl->BranchIfNeeded(updateBlock);

        impl->builder.SetInsertPoint(updateBlock);
        for (const auto& expression : stmt->update) if (expression) expression->Accept(*this);
        impl->BranchIfNeeded(conditionBlock);
        impl->loops.pop_back();

        impl->builder.SetInsertPoint(endBlock);
        impl->PopScope(true);
    }

    void CodeGenerator::Visit(WhileStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("while outside a function");

        llvm::BasicBlock* conditionBlock = llvm::BasicBlock::Create(impl->context, "while.condition", function);
        llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(impl->context, "while.body", function);
        llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(impl->context, "while.end", function);
        impl->builder.CreateBr(conditionBlock);

        impl->builder.SetInsertPoint(conditionBlock);
        impl->builder.CreateCondBr(impl->AsCondition(impl->Evaluate(stmt->condition.get())), bodyBlock, endBlock);

        impl->loops.push_back({conditionBlock, endBlock, impl->scopes.size()});
        impl->builder.SetInsertPoint(bodyBlock);
        if (stmt->body) stmt->body->Accept(*this);
        impl->BranchIfNeeded(conditionBlock);
        impl->loops.pop_back();

        impl->builder.SetInsertPoint(endBlock);
    }

    void CodeGenerator::Visit(DoWhileStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("do-while outside a function");

        llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(impl->context, "do.body", function);
        llvm::BasicBlock* conditionBlock = llvm::BasicBlock::Create(impl->context, "do.condition", function);
        llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(impl->context, "do.end", function);
        impl->builder.CreateBr(bodyBlock);

        impl->loops.push_back({conditionBlock, endBlock, impl->scopes.size()});
        impl->builder.SetInsertPoint(bodyBlock);
        if (stmt->body) stmt->body->Accept(*this);
        impl->BranchIfNeeded(conditionBlock);

        impl->builder.SetInsertPoint(conditionBlock);
        impl->builder.CreateCondBr(impl->AsCondition(impl->Evaluate(stmt->condition.get())), bodyBlock, endBlock);
        impl->loops.pop_back();
        impl->builder.SetInsertPoint(endBlock);
    }

    void CodeGenerator::Visit(ForEachStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("foreach outside a function");
        Impl::ArrayView source = impl->ViewOfArray(stmt->iterable.get());
        if (source.dimensions.size() != 1)
            impl->Fail("foreach requires a one-dimensional array or slice");

        impl->PushScope();
        if (!stmt->var) impl->Fail("foreach requires an iteration variable");
        stmt->var->Accept(*this);
        const std::string variableName = IdentifierName(stmt->var->name.get());
        Impl::Variable& iterationVariable = impl->RequireVariable(variableName);
        llvm::AllocaInst* index = impl->CreateEntryAlloca(
            *function, impl->builder.getInt64Ty(), "foreach.index");
        impl->builder.CreateStore(impl->builder.getInt64(0), index);

        llvm::BasicBlock* conditionBlock = llvm::BasicBlock::Create(
            impl->context, "foreach.condition", function);
        llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(
            impl->context, "foreach.body", function);
        llvm::BasicBlock* updateBlock = llvm::BasicBlock::Create(
            impl->context, "foreach.update", function);
        llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(
            impl->context, "foreach.end", function);
        impl->builder.CreateBr(conditionBlock);

        impl->builder.SetInsertPoint(conditionBlock);
        llvm::Value* current = impl->builder.CreateLoad(
            impl->builder.getInt64Ty(), index, "foreach.index.value");
        impl->builder.CreateCondBr(impl->builder.CreateICmpULT(
            current, source.dimensions.front(), "foreach.has.next"), bodyBlock, endBlock);

        impl->loops.push_back({updateBlock, endBlock, impl->scopes.size()});
        impl->builder.SetInsertPoint(bodyBlock);
        llvm::Value* elementAddress = impl->builder.CreateInBoundsGEP(
            source.elementType, source.address, current, "foreach.element.address");
        llvm::Value* element = impl->builder.CreateLoad(
            source.elementType, elementAddress, "foreach.element");
        impl->builder.CreateStore(impl->Coerce(element, iterationVariable.type), iterationVariable.address);
        if (stmt->body) stmt->body->Accept(*this);
        impl->BranchIfNeeded(updateBlock);

        impl->builder.SetInsertPoint(updateBlock);
        llvm::Value* next = impl->builder.CreateAdd(
            impl->builder.CreateLoad(impl->builder.getInt64Ty(), index),
            impl->builder.getInt64(1), "foreach.next");
        impl->builder.CreateStore(next, index);
        impl->BranchIfNeeded(conditionBlock);
        impl->loops.pop_back();

        impl->builder.SetInsertPoint(endBlock);
        impl->PopScope(true);
    }

    void CodeGenerator::Visit(ContinueStmt* stmt) {
        (void)stmt;
        if (impl->phase != Impl::Phase::EmitBodies) return;
        if (impl->loops.empty()) impl->Fail("continue outside a loop");
        impl->EmitCleanupsFrom(impl->loops.back().scopeCount);
        impl->builder.CreateBr(impl->loops.back().continueBlock);
    }

    void CodeGenerator::Visit(BreakStmt* stmt) {
        (void)stmt;
        if (impl->phase != Impl::Phase::EmitBodies) return;
        if (impl->loops.empty()) impl->Fail("break outside a loop");
        impl->EmitCleanupsFrom(impl->loops.back().scopeCount);
        impl->builder.CreateBr(impl->loops.back().breakBlock);
    }

    void CodeGenerator::Visit(ImportStmt* stmt) {
        (void)stmt;
    }

    void CodeGenerator::Visit(NamespaceDeclStmt* stmt) {
        const std::string oldNamespace = impl->currentNamespace;
        impl->currentNamespace = impl->Qualify(stmt->name);
        if (stmt->body) {
            for (const auto& statement : stmt->body->statements) {
                if (statement) statement->Accept(*this);
            }
        }
        impl->currentNamespace = oldNamespace;
    }

    void CodeGenerator::Visit(OpaquePluginStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies || !stmt || !stmt->node.vtable ||
            !stmt->node.vtable->emit_llvm) return;
        const std::string moduleName = impl->module->getName().str();
        const std::string triple = impl->module->getTargetTriple();
        const std::string dataLayout = impl->module->getDataLayoutStr();
        AbsoluteOpaqueLlvmContextV1 context{
            ABSOLUTE_SYNTAX_PLUGIN_ABI_VERSION,
            moduleName.c_str(),
            triple.c_str(),
            dataLayout.c_str()
        };
        const char* fragmentText = nullptr;
        const char* errorMessage = nullptr;
        int32_t emitted = 0;
        try {
            emitted = stmt->node.vtable->emit_llvm(
                stmt->node.payload, &context, &fragmentText, &errorMessage);
        }
        catch (const std::exception& error) {
            impl->Fail("opaque plugin '" + stmt->pluginName + "' emitter threw: " + error.what());
        }
        catch (...) {
            impl->Fail("opaque plugin '" + stmt->pluginName + "' emitter threw an unknown exception");
        }
        if (!emitted || !fragmentText)
            impl->Fail("opaque plugin '" + stmt->pluginName + "' failed to emit LLVM IR: " +
                (errorMessage ? errorMessage : "emitter returned no module"));

        llvm::SMDiagnostic diagnostic;
        std::unique_ptr<llvm::Module> fragment = llvm::parseAssemblyString(
            fragmentText, diagnostic, impl->context);
        if (!fragment) {
            std::string detail;
            llvm::raw_string_ostream stream(detail);
            diagnostic.print(stmt->pluginName.c_str(), stream);
            stream.flush();
            impl->Fail("opaque plugin '" + stmt->pluginName + "' returned invalid LLVM IR: " + detail);
        }
        if (fragment->getTargetTriple().empty()) fragment->setTargetTriple(triple);
        if (fragment->getDataLayoutStr().empty() && !dataLayout.empty()) fragment->setDataLayout(dataLayout);
        if (llvm::Linker::linkModules(*impl->module, std::move(fragment)))
            impl->Fail("opaque plugin '" + stmt->pluginName + "' LLVM module could not be linked");
    }
}
