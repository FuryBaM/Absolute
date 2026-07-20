#include "codegen_internal.h"

namespace Absolute {
    namespace {
        struct OpaqueAttributeViews {
            std::vector<std::vector<AbsoluteAttributeArgumentV1>> argumentStorage;
            std::vector<AbsoluteAttributeV1> attributes;

            explicit OpaqueAttributeViews(const Statement& statement) {
                argumentStorage.resize(statement.attributes.size());
                attributes.reserve(statement.attributes.size());
                for (size_t index = 0; index < statement.attributes.size(); ++index) {
                    const Attribute& source = statement.attributes[index];
                    auto& arguments = argumentStorage[index];
                    arguments.reserve(source.arguments.size());
                    for (const AttributeArgument& argument : source.arguments) {
                        arguments.push_back({
                            argument.name.empty() ? nullptr : argument.name.c_str(),
                            argument.name.size(),
                            static_cast<uint32_t>(argument.value.kind),
                            argument.value.text.c_str(),
                            argument.value.text.size()
                        });
                    }
                    attributes.push_back({source.name.c_str(), source.name.size(),
                        arguments.size(), arguments.empty() ? nullptr : arguments.data()});
                }
            }
        };
    }

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
            impl->EmitTransferCleanups(0, true);
            if (impl->builder.GetInsertBlock()->getTerminator()) return;
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
        impl->EmitTransferCleanups(0, true, transferredOwner);
        if (impl->builder.GetInsertBlock()->getTerminator()) return;
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

    void CodeGenerator::Visit(ThrowStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        llvm::Value* handle = nullptr;
        llvm::Value* type = nullptr;
        SymbolId transferredOwner = InvalidSymbolId;
        if (stmt->value) {
            const std::string exceptionType = impl->SemanticType(stmt->value.get());
            handle = impl->Evaluate(stmt->value.get());
            llvm::Value* dynamicType = impl->builder.CreateCall(
                impl->ManagedType(), {handle}, "exception.dynamic.type");
            llvm::Value* staticType = impl->builder.getInt64(
                impl->ExceptionTypeId(PointerPointeeName(exceptionType)));
            type = impl->builder.CreateSelect(
                impl->builder.CreateICmpNE(dynamicType, impl->builder.getInt64(0)),
                dynamicType, staticType, "exception.effective.type");
            transferredOwner = impl->SemanticSymbol(stmt->value.get());
        }
        else {
            if (impl->caughtExceptions.empty()) impl->Fail("rethrow outside catch");
            const Impl::CaughtException& caught = impl->caughtExceptions.back();
            handle = impl->builder.CreateLoad(
                impl->builder.getInt64Ty(), caught.address, "rethrow.handle");
            type = caught.type;
            transferredOwner = caught.symbol;
        }
        impl->builder.CreateCall(impl->ErrorSet(), {handle, type});
        impl->EmitExceptionPropagation(transferredOwner);
    }

    void CodeGenerator::Visit(TryStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("try outside a function");

        llvm::BasicBlock* handlerBlock = llvm::BasicBlock::Create(
            impl->context, "try.handler", function);
        llvm::BasicBlock* finallyBlock = stmt->finallyBody
            ? llvm::BasicBlock::Create(impl->context, "try.finally", function) : nullptr;
        llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(
            impl->context, "try.end", function);
        llvm::BasicBlock* completionBlock = finallyBlock ? finallyBlock : endBlock;
        const size_t tryScope = impl->scopes.size();

        if (stmt->finallyBody) impl->finallyTargets.push_back({stmt, tryScope});
        impl->exceptionTargets.push_back({handlerBlock, tryScope});
        if (stmt->body) stmt->body->Accept(*this);
        impl->exceptionTargets.pop_back();
        impl->BranchIfNeeded(completionBlock);

        impl->builder.SetInsertPoint(handlerBlock);
        llvm::Value* actualType = impl->builder.CreateCall(
            impl->ErrorType(), {}, "exception.type");
        for (auto& clause : stmt->catches) {
            const ExpressionInfo* info = clause.parameter && impl->analyzer
                ? impl->analyzer->GetExpressionInfo(*clause.parameter) : nullptr;
            const std::string parameterType = info
                ? info->type : impl->DeclaredTypeName(*clause.parameter);
            const std::string catchType = PointerPointeeName(parameterType);
            llvm::Value* matches = impl->builder.getFalse();
            bool foundMatchableClass = false;
            for (const auto& [className, classInfo] : impl->classes) {
                (void)classInfo;
                if (!impl->IsClassDerivedFrom(className, catchType)) continue;
                foundMatchableClass = true;
                llvm::Value* equal = impl->builder.CreateICmpEQ(actualType,
                    impl->builder.getInt64(impl->ExceptionTypeId(className)), "catch.type.equal");
                matches = impl->builder.CreateOr(matches, equal, "catch.matches");
            }
            if (!foundMatchableClass) {
                matches = impl->builder.CreateICmpEQ(actualType,
                    impl->builder.getInt64(impl->ExceptionTypeId(catchType)), "catch.type.equal");
            }

            llvm::BasicBlock* catchBlock = llvm::BasicBlock::Create(
                impl->context, "catch.body", function);
            llvm::BasicBlock* nextBlock = llvm::BasicBlock::Create(
                impl->context, "catch.next", function);
            impl->builder.CreateCondBr(matches, catchBlock, nextBlock);
            impl->builder.SetInsertPoint(catchBlock);

            impl->PushScope();
            const std::string name = IdentifierName(clause.parameter->name.get());
            llvm::AllocaInst* address = impl->CreateEntryAlloca(
                *function, impl->builder.getInt64Ty(), name);
            llvm::Value* handle = impl->builder.CreateCall(
                impl->ErrorTake(), {}, "exception.handle");
            impl->builder.CreateStore(handle, address);
            const SymbolId symbol = impl->SemanticSymbol(clause.parameter.get());
            if (!impl->scopes.back().emplace(name,
                Impl::Variable{address, impl->builder.getInt64Ty(), parameterType,
                    true, false, nullptr, {}, nullptr, symbol}).second) {
                impl->Fail("duplicate catch parameter '" + name + "'");
            }
            impl->caughtExceptions.push_back({address, actualType, symbol});
            if (clause.body) clause.body->Accept(*this);
            impl->caughtExceptions.pop_back();
            impl->PopScope(true);
            impl->BranchIfNeeded(completionBlock);
            impl->builder.SetInsertPoint(nextBlock);
        }

        if (stmt->finallyBody) impl->builder.CreateBr(finallyBlock);
        else impl->EmitExceptionPropagation();

        if (stmt->finallyBody) {
            impl->builder.SetInsertPoint(finallyBlock);
            const bool oldEmittingFinally = impl->emittingFinally;
            impl->emittingFinally = true;
            stmt->finallyBody->Accept(*this);
            impl->emittingFinally = oldEmittingFinally;
            if (impl->builder.GetInsertBlock() &&
                !impl->builder.GetInsertBlock()->getTerminator()) {
                llvm::Value* pending = impl->builder.CreateCall(
                    impl->ErrorPending(), {}, "finally.error.pending");
                llvm::BasicBlock* propagateBlock = llvm::BasicBlock::Create(
                    impl->context, "finally.error", function);
                impl->builder.CreateCondBr(pending, propagateBlock, endBlock);
                impl->builder.SetInsertPoint(propagateBlock);
                impl->EmitExceptionPropagation();
            }
        }
        if (stmt->finallyBody) impl->finallyTargets.pop_back();
        impl->builder.SetInsertPoint(endBlock);
    }

    void CodeGenerator::Visit(DeferStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        if (!impl->CurrentFunction() || impl->deferredScopes.empty())
            impl->Fail("defer outside a function scope");
        impl->deferredScopes.back().push_back(stmt);
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
        impl->EmitTransferCleanups(impl->loops.back().scopeCount, true);
        if (impl->builder.GetInsertBlock()->getTerminator()) return;
        impl->builder.CreateBr(impl->loops.back().continueBlock);
    }

    void CodeGenerator::Visit(BreakStmt* stmt) {
        (void)stmt;
        if (impl->phase != Impl::Phase::EmitBodies) return;
        if (impl->loops.empty()) impl->Fail("break outside a loop");
        impl->EmitTransferCleanups(impl->loops.back().scopeCount, true);
        if (impl->builder.GetInsertBlock()->getTerminator()) return;
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
        OpaqueAttributeViews attributeViews(*stmt);
        AbsoluteOpaqueLlvmContextV1 context{
            ABSOLUTE_SYNTAX_PLUGIN_ABI_VERSION,
            moduleName.c_str(),
            triple.c_str(),
            dataLayout.c_str(),
            attributeViews.attributes.size(),
            attributeViews.attributes.empty() ? nullptr : attributeViews.attributes.data()
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
