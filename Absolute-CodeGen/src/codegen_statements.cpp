#include "codegen_internal.h"

namespace Absolute {
    namespace {
        bool ContainsDirectNestedLoop(const Statement* statement) {
            const auto* compound = dynamic_cast<const CompoundStmt*>(statement);
            if (!compound) return false;
            for (const auto& child : compound->statements) {
                if (dynamic_cast<const ForStmt*>(child.get()) ||
                    dynamic_cast<const WhileStmt*>(child.get()) ||
                    dynamic_cast<const DoWhileStmt*>(child.get()) ||
                    dynamic_cast<const ForEachStmt*>(child.get())) {
                    return true;
                }
            }
            return false;
        }

        bool ExpressionMayWriteVariable(const Expression* expression, const std::string& variable) {
            if (!expression) return false;
            if (const auto* assignment = dynamic_cast<const AssignmentExpr*>(expression)) {
                if (IdentifierName(assignment->target.get()) == variable) return true;
                return ExpressionMayWriteVariable(assignment->target.get(), variable) ||
                    ExpressionMayWriteVariable(assignment->value.get(), variable);
            }
            if (const auto* binary = dynamic_cast<const BinaryExpr*>(expression)) {
                return ExpressionMayWriteVariable(binary->left.get(), variable) ||
                    ExpressionMayWriteVariable(binary->right.get(), variable);
            }
            if (const auto* access = dynamic_cast<const ArrayAccessExpr*>(expression)) {
                if (ExpressionMayWriteVariable(access->base.get(), variable)) return true;
                for (const auto& index : access->indexes) {
                    if (ExpressionMayWriteVariable(index.get(), variable)) return true;
                }
                return false;
            }
            if (dynamic_cast<const IdentifierExpr*>(expression) ||
                dynamic_cast<const NumberLiteralExpr*>(expression) ||
                dynamic_cast<const BooleanLiteralExpr*>(expression) ||
                dynamic_cast<const StringLiteralExpr*>(expression) ||
                dynamic_cast<const CharLiteralExpr*>(expression) ||
                dynamic_cast<const NullExpr*>(expression)) {
                return false;
            }
            // Reject unfamiliar expressions rather than deriving an unsafe range fact.
            return true;
        }

        bool StatementMayWriteVariable(const Statement* statement, const std::string& variable) {
            if (!statement) return false;
            if (const auto* assignment = dynamic_cast<const AssignmentStmt*>(statement)) {
                return ExpressionMayWriteVariable(assignment->expr.get(), variable);
            }
            if (const auto* single = dynamic_cast<const SingleStatement*>(statement)) {
                return ExpressionMayWriteVariable(single->expr.get(), variable);
            }
            if (const auto* compound = dynamic_cast<const CompoundStmt*>(statement)) {
                for (const auto& child : compound->statements) {
                    if (StatementMayWriteVariable(child.get(), variable)) return true;
                }
                return false;
            }
            if (const auto* conditional = dynamic_cast<const IfStmt*>(statement)) {
                for (const IfStmt::Branch& branch : conditional->branches) {
                    if (ExpressionMayWriteVariable(branch.condition.get(), variable) ||
                        StatementMayWriteVariable(branch.body.get(), variable)) {
                        return true;
                    }
                }
                return StatementMayWriteVariable(conditional->elseBranch.get(), variable);
            }
            return dynamic_cast<const BreakStmt*>(statement) == nullptr &&
                dynamic_cast<const ContinueStmt*>(statement) == nullptr;
        }

        std::optional<std::string> UnitDecrementLoopVariable(const WhileStmt& statement) {
            const auto* condition = dynamic_cast<const BinaryExpr*>(statement.condition.get());
            const auto* lowerBound = condition
                ? dynamic_cast<const NumberLiteralExpr*>(condition->right.get()) : nullptr;
            const std::string variable = condition
                ? IdentifierName(condition->left.get()) : std::string{};
            if (!condition || condition->op != ">=" || variable.empty() ||
                !lowerBound || lowerBound->value != "0") {
                return std::nullopt;
            }

            const auto* body = dynamic_cast<const CompoundStmt*>(statement.body.get());
            if (!body || body->statements.empty()) return std::nullopt;
            const Statement* last = body->statements.back().get();
            const auto* decrement = dynamic_cast<const AssignmentStmt*>(last);
            const auto* single = dynamic_cast<const SingleStatement*>(last);
            const AssignmentExpr* expression = decrement
                ? decrement->expr.get()
                : (single ? dynamic_cast<const AssignmentExpr*>(single->expr.get()) : nullptr);
            const auto* amount = expression
                ? dynamic_cast<const NumberLiteralExpr*>(expression->value.get()) : nullptr;
            if (!expression || expression->op != "-=" ||
                IdentifierName(expression->target.get()) != variable ||
                !amount || amount->value != "1") {
                return std::nullopt;
            }

            for (size_t index = 0; index + 1 < body->statements.size(); ++index) {
                if (StatementMayWriteVariable(body->statements[index].get(), variable))
                    return std::nullopt;
            }
            return variable;
        }

        void SetNestedLoopUnrollCount(llvm::BranchInst* backEdge) {
            if (!backEdge) return;
            llvm::LLVMContext& context = backEdge->getContext();
            llvm::Metadata* operands[] = {
                nullptr,
                llvm::MDNode::get(context,
                    {llvm::MDString::get(context, "llvm.loop.unroll.count"),
                     llvm::ConstantAsMetadata::get(
                        llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 8))})
            };
            llvm::MDNode* loopId = llvm::MDNode::getDistinct(context, operands);
            loopId->replaceOperandWith(0, loopId);
            backEdge->setMetadata(llvm::LLVMContext::MD_loop, loopId);
        }

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
        if (impl->phase != Impl::Phase::EmitBodies || !stmt->expr) return;
        impl->SetDebugLocation(stmt);
        // An owner produced inside this statement that nothing took is
        // destroyed when the statement ends.
        Impl::TemporaryOwnerScope temporaries(*impl);
        impl->valueCreatesArrayOwner = false;
        impl->valueArrayOwner = nullptr;
        if (impl->SemanticType(stmt->expr.get()) == "void") stmt->expr->Accept(*this);
        else impl->Evaluate(stmt->expr.get());
        if (impl->valueCreatesArrayOwner)
            impl->builder.CreateCall(impl->Free(), {impl->valueArrayOwner});
    }

    void CodeGenerator::Visit(CompoundStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        impl->SetDebugLocation(stmt);
        impl->PushDebugScope(stmt);
        impl->PushScope();
        for (const auto& statement : stmt->statements) {
            if (!statement || impl->builder.GetInsertBlock()->getTerminator()) break;
            statement->Accept(*this);
        }
        impl->PopScope(true);
        impl->PopDebugScope();
    }

    void CodeGenerator::Visit(FunctionCallStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies || !stmt->value) return;
        impl->SetDebugLocation(stmt);
        Impl::TemporaryOwnerScope temporaries(*impl);
        impl->valueCreatesArrayOwner = false;
        impl->valueArrayOwner = nullptr;
        if (impl->SemanticType(stmt->value.get()) == "void") stmt->value->Accept(*this);
        else impl->Evaluate(stmt->value.get());
        if (impl->valueCreatesArrayOwner)
            impl->builder.CreateCall(impl->Free(), {impl->valueArrayOwner});
    }

    void CodeGenerator::Visit(FunctionDeclStmt* stmt) {
        if (stmt->IsGeneric()) return;
        if (impl->phase == Impl::Phase::DeclareFunctions) impl->DeclareFunction(*stmt);
        else if (!stmt->IsExternal()) impl->EmitFunction(*stmt);
    }

    void CodeGenerator::Visit(PropertyDeclStmt* stmt) {
        (void)stmt;
    }

    void CodeGenerator::Visit(IndexerDeclStmt* stmt) {
        (void)stmt;
    }

    void CodeGenerator::Visit(ReturnStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        impl->SetDebugLocation(stmt);
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("return outside a function");
        if (function->getReturnType()->isVoidTy() && !impl->currentReturnStorage) {
            impl->EmitTransferCleanups(0, true);
            if (impl->builder.GetInsertBlock()->getTerminator()) return;
            impl->builder.CreateRetVoid();
            return;
        }
        // A `void main()` is emitted returning an int, because the C `main`
        // returns one and the process exit status is it. A bare `return;`
        // inside it is a successful exit rather than a value.
        if (impl->currentReturnTypeName == "void" && !stmt->expr) {
            impl->EmitTransferCleanups(0, true);
            if (impl->builder.GetInsertBlock()->getTerminator()) return;
            impl->builder.CreateRet(
                llvm::Constant::getNullValue(function->getReturnType()));
            return;
        }
        // C ABI bool returns i8 while Absolute evaluation produces i1.
        llvm::Type* expectedReturn = impl->currentReturnStorage
            ? impl->TypeFromName(impl->currentReturnTypeName)
            : function->getReturnType();
        if (auto* returnedIdentifier =
            dynamic_cast<IdentifierExpr*>(stmt->expr.get())) {
            if (Impl::Variable* returned =
                impl->FindVariable(returnedIdentifier->name);
                returned && returned->ownershipFlagStorage &&
                impl->TypeNeedsCleanup(impl->currentReturnTypeName)) {
                llvm::Value* owns = impl->builder.CreateLoad(
                    impl->builder.getInt1Ty(),
                    returned->ownershipFlagStorage,
                    "return.is_owner");
                impl->EmitOrExit(owns, "return.requires.owner");
            }
        }
        // Released by hand rather than by a scope: the statement ends with a
        // terminator, and nothing may be emitted after one.
        const size_t temporaryMark = impl->temporaryManagedOwners.size();
        llvm::Value* result = impl->Coerce(
            impl->Evaluate(stmt->expr.get()), expectedReturn,
            impl->SemanticType(stmt->expr.get()), impl->currentReturnTypeName);
        // A returned string is handed to the caller, who releases it. What is
        // returned is usually a local or a field -- storage something here
        // still names -- and the cleanup below is about to let that name go,
        // so the value has to be held once more on its way out. A string the
        // return expression made itself is already held once and needs no
        // second count.
        if (impl->currentReturnTypeName == "string")
            result = impl->RetainStoredString(stmt->expr.get(), result);
        // The same for an aggregate whose parts are counted. A getter that
        // hands back a struct read out of a container -- `return
        // unsafeArrayGet(items, index);` -- copies the bytes and not what they
        // hold, so the caller's copy named the container's strings without
        // saying so and released them when it went out of scope.
        else if (const TypeSemantics returned =
                impl->SemanticsOfTypeName(impl->currentReturnTypeName);
            returned.needsDrop && returned.copyable && result &&
            !impl->CreatesFreshString(stmt->expr.get())) {
            llvm::AllocaInst* carried = impl->CreateEntryAlloca(
                *function, result->getType(), "return.retained");
            impl->builder.CreateStore(result, carried);
            impl->EmitValueRetain(carried, impl->currentReturnTypeName);
            result = impl->builder.CreateLoad(result->getType(), carried, "return.counted");
        }
        impl->ReleaseTemporaryOwners(temporaryMark);
        SymbolId transferredOwner = InvalidSymbolId;
        if (IsStrongManagedPointerTypeName(impl->currentReturnTypeName)) {
            const auto* returnedIdentifier = dynamic_cast<IdentifierExpr*>(stmt->expr.get());
            if (returnedIdentifier) {
                Impl::Variable& returned = impl->RequireVariable(returnedIdentifier->name);
                if (returned.managedOwner) transferredOwner = returned.symbol;
            }
        }
        else if (ArrayRankName(impl->currentReturnTypeName) > 0) {
            if (Impl::Variable* returned = impl->FindVariable(
                impl->SemanticSymbol(stmt->expr.get()))) {
                transferredOwner = returned->ownsArrayStorage
                    ? returned->symbol : returned->arrayOwnerSymbol;
            }
        }
        // A returned aggregate is not transferred out of its local: it is
        // copied, and the copy counts the parts it now names just above. The
        // local keeps its own count and gives it back the ordinary way. Doing
        // both -- suppressing the cleanup *and* counting -- leaves two counts
        // and one release, which is a leak rather than the use-after-free the
        // suppression was added to stop.
        impl->EmitTransferCleanups(0, true, transferredOwner);
        if (impl->builder.GetInsertBlock()->getTerminator()) return;
        if (impl->currentReturnStorage) {
            impl->builder.CreateStore(result, impl->currentReturnStorage);
            impl->builder.CreateRetVoid();
        }
        else impl->builder.CreateRet(result);
    }

    void CodeGenerator::Visit(AssignmentStmt* stmt) {
        impl->SetDebugLocation(stmt);
        if (impl->phase != Impl::Phase::EmitBodies || !stmt->expr) return;
        Impl::TemporaryOwnerScope temporaries(*impl);
        stmt->expr->Accept(*this);
    }

    void CodeGenerator::Visit(VarDeclStmt* stmt) {
        if (!stmt->expr) return;
        if (impl->phase == Impl::Phase::DeclareFunctions) {
            // Module scope. Arrays already had storage emitted here; scalars
            // fell through silently and were then reported as an unknown
            // variable by whichever expression first read them, which named
            // the use rather than the declaration that was never emitted.
            if (ArrayRankName(impl->DeclaredTypeName(*stmt->expr)) > 0)
                impl->DeclareGlobalArray(*stmt->expr);
            else
                impl->DeclareGlobalScalar(*stmt->expr);
            return;
        }
        if (!impl->CurrentFunction()) return;
        impl->SetDebugLocation(stmt);
        Impl::TemporaryOwnerScope temporaries(*impl);
        const Attribute* previousSpawnAttribute = impl->currentSpawnAttribute;
        impl->currentSpawnAttribute = stmt->FindAttribute("spawn");
        stmt->expr->Accept(*this);
        impl->currentSpawnAttribute = previousSpawnAttribute;
    }

    void CodeGenerator::Visit(StructDeclStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        if (!stmt->templateParams.empty()) {
            for (const std::string& specialization : impl->structOrder) {
                auto found = impl->structs.find(specialization);
                if (found != impl->structs.end() && found->second.statement == stmt)
                    impl->EmitStructBodies(found->second);
            }
            return;
        }
        const std::string name = impl->Qualify(stmt->name);
        auto found = impl->structs.find(name);
        if (found == impl->structs.end()) impl->Fail("unregistered struct '" + name + "'");
        impl->EmitStructBodies(found->second);
    }

    void CodeGenerator::Visit(ClassDeclStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        if (!stmt->templateParams.empty()) {
            for (const std::string& specialization : impl->classOrder) {
                auto found = impl->classes.find(specialization);
                if (found != impl->classes.end() && found->second.statement == stmt)
                    impl->EmitClassBodies(found->second);
            }
            return;
        }
        const std::string name = impl->Qualify(stmt->name);
        auto found = impl->classes.find(name);
        if (found == impl->classes.end()) impl->Fail("unregistered class '" + name + "'");
        impl->EmitClassBodies(found->second);
    }

    void CodeGenerator::Visit(InterfaceDeclStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        if (!stmt->templateParams.empty()) {
            for (const std::string& specialization : impl->interfaceOrder) {
                auto found = impl->interfaces.find(specialization);
                if (found != impl->interfaces.end() && found->second.statement == stmt)
                    impl->EmitInterfaceBodies(found->second);
            }
            return;
        }
        const std::string name = impl->Qualify(stmt->name);
        auto found = impl->interfaces.find(name);
        if (found == impl->interfaces.end()) impl->Fail("unregistered interface '" + name + "'");
        impl->EmitInterfaceBodies(found->second);
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
        impl->SetDebugLocation(stmt);
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("if outside a function");

        llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(impl->context, "if.end", function);
        for (size_t index = 0; index < stmt->branches.size(); ++index) {
            auto& branch = stmt->branches[index];
            llvm::Value* condition = nullptr;
            {
                // Released inside the block that tested it, so a condition on
                // one path does not name a value another path never produced.
                Impl::TemporaryOwnerScope temporaries(*impl);
                condition = impl->AsCondition(impl->EvaluateBorrowed(branch.condition.get()));
            }
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
        impl->SetDebugLocation(stmt);
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("switch outside a function");

        llvm::Value* selector = nullptr;
        {
            Impl::TemporaryOwnerScope temporaries(*impl);
            selector = impl->EvaluateBorrowed(stmt->value.get());
        }
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
        impl->SetDebugLocation(stmt);
        llvm::Value* handle = nullptr;
        llvm::Value* type = nullptr;
        SymbolId transferredOwner = InvalidSymbolId;
        const size_t temporaryMark = impl->temporaryManagedOwners.size();
        if (stmt->value) {
            const std::string exceptionType = impl->SemanticType(stmt->value.get());
            handle = impl->Evaluate(stmt->value.get());
            // Before the throw leaves: nothing may be emitted after it. The
            // thrown owner itself is transferred, not registered.
            impl->ReleaseTemporaryOwners(temporaryMark);
            llvm::Value* dynamicType = impl->builder.CreateCall(
                impl->ManagedType(), {handle}, "exception.dynamic.type");
            llvm::Value* staticType = impl->builder.getInt64(
                impl->ExceptionTypeId(PointerPointeeName(exceptionType)));
            type = impl->builder.CreateSelect(
                impl->builder.CreateICmpNE(dynamicType, impl->builder.getInt64(0)),
                dynamicType, staticType, "exception.effective.type");
            transferredOwner = impl->SemanticSymbol(stmt->value.get());
            impl->EmitErrorDetails(handle, PointerPointeeName(exceptionType));
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
        impl->SetDebugLocation(stmt);
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
        impl->SetDebugLocation(stmt);
        if (!impl->CurrentFunction() || impl->deferredScopes.empty())
            impl->Fail("defer outside a function scope");
        impl->deferredScopes.back().push_back(stmt);
    }

    void CodeGenerator::Visit(ForStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        impl->SetDebugLocation(stmt);
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
        llvm::Value* condition = impl->builder.getTrue();
        if (stmt->condition) {
            // Inside the condition block: the condition runs once per
            // iteration, so what it produces is released once per iteration.
            Impl::TemporaryOwnerScope temporaries(*impl);
            condition = impl->AsCondition(impl->EvaluateBorrowed(stmt->condition.get()));
        }
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
        impl->SetDebugLocation(stmt);
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("while outside a function");

        const std::optional<std::string> unitDecrementVariable = UnitDecrementLoopVariable(*stmt);
        llvm::BasicBlock* conditionBlock = llvm::BasicBlock::Create(impl->context, "while.condition", function);
        llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(impl->context, "while.body", function);
        llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(impl->context, "while.end", function);
        impl->builder.CreateBr(conditionBlock);

        impl->builder.SetInsertPoint(conditionBlock);
        llvm::Value* condition = nullptr;
        {
            Impl::TemporaryOwnerScope temporaries(*impl);
            condition = impl->AsCondition(impl->EvaluateBorrowed(stmt->condition.get()));
        }
        impl->builder.CreateCondBr(condition, bodyBlock, endBlock);

        impl->loops.push_back({conditionBlock, endBlock, impl->scopes.size()});
        impl->builder.SetInsertPoint(bodyBlock);
        if (stmt->body) stmt->body->Accept(*this);
        llvm::BasicBlock* backEdgeBlock = impl->builder.GetInsertBlock();
        impl->BranchIfNeeded(conditionBlock);
        // Eight copies amortize outer-loop control without the code-size and
        // register-pressure regression caused by unrestricted forced unrolling.
        if (ContainsDirectNestedLoop(stmt->body.get())) {
            SetNestedLoopUnrollCount(llvm::dyn_cast_or_null<llvm::BranchInst>(
                backEdgeBlock ? backEdgeBlock->getTerminator() : nullptr));
        }
        impl->loops.pop_back();

        impl->builder.SetInsertPoint(endBlock);
        if (unitDecrementVariable) {
            Impl::Variable& variable = impl->RequireVariable(*unitDecrementVariable);
            if (variable.type->isIntegerTy() && !variable.typeName.starts_with("uint")) {
                llvm::Value* afterLoop = impl->builder.CreateLoad(
                    variable.type, variable.address, *unitDecrementVariable + ".after.loop");
                llvm::Value* lowerBound = llvm::ConstantInt::getSigned(variable.type, -1);
                impl->builder.CreateAssumption(impl->builder.CreateICmpSGE(
                    afterLoop, lowerBound, *unitDecrementVariable + ".after.loop.in.range"));
            }
        }
    }

    void CodeGenerator::Visit(DoWhileStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        impl->SetDebugLocation(stmt);
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
        llvm::Value* condition = nullptr;
        {
            Impl::TemporaryOwnerScope temporaries(*impl);
            condition = impl->AsCondition(impl->EvaluateBorrowed(stmt->condition.get()));
        }
        impl->builder.CreateCondBr(condition, bodyBlock, endBlock);
        impl->loops.pop_back();
        impl->builder.SetInsertPoint(endBlock);
    }

    void CodeGenerator::Visit(ForEachStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        impl->SetDebugLocation(stmt);
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("foreach outside a function");
        // The iterable is evaluated once, before the loop, and read on every
        // iteration, so a temporary source -- `for (x in makeList())` -- has to
        // outlive the loop and is released after it.
        Impl::TemporaryOwnerScope temporaries(*impl);
        const std::string iterableType = impl->SemanticType(stmt->iterable.get());
        const bool isArray = iterableType.ends_with("[]");

        impl->PushScope();
        if (!stmt->var) impl->Fail("foreach requires an iteration variable");
        stmt->var->Accept(*this);
        const std::string variableName = IdentifierName(stmt->var->name.get());
        Impl::Variable& iterationVariable = impl->RequireVariable(variableName);

        llvm::BasicBlock* conditionBlock = llvm::BasicBlock::Create(
            impl->context, "foreach.condition", function);
        llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(
            impl->context, "foreach.body", function);
        llvm::BasicBlock* updateBlock = llvm::BasicBlock::Create(
            impl->context, "foreach.update", function);
        llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(
            impl->context, "foreach.end", function);

        if (isArray) {
            Impl::ArrayView source = impl->ViewOfArray(stmt->iterable.get());
            if (source.dimensions.size() != 1)
                impl->Fail("foreach requires a one-dimensional array or slice");

            llvm::AllocaInst* index = impl->CreateEntryAlloca(
                *function, impl->builder.getInt64Ty(), "foreach.index");
            impl->builder.CreateStore(impl->builder.getInt64(0), index);
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
        } else {
            llvm::Value* iterable = impl->Evaluate(stmt->iterable.get());
            llvm::Value* iterablePtr = impl->ObjectPointer(stmt->iterable.get(), iterableType);

            auto callMethod = [&](llvm::Value* obj, const std::string& typeName, const std::string& methodName, const std::string& propertyName = "") -> std::pair<llvm::Value*, std::string> {
                const std::string aggregateName = impl->ClassNameFromType(typeName);
                std::string methodKey = CallableKey(methodName, {});
                if (!propertyName.empty()) {
                    methodKey = CallableKey(PropertyGetterName(propertyName), {});
                }
                
                auto invoke = [&](const auto& info) -> std::pair<llvm::Value*, std::string> {
                    auto method = info.methods.find(methodKey);
                    if (method == info.methods.end()) return {nullptr, ""};
                    llvm::Value* callee = nullptr;
                    if (method->second.virtualSlot) {
                        llvm::Value* vtableAddress = impl->builder.CreateStructGEP(info.llvmType, obj, 0);
                        llvm::Value* vtable = impl->builder.CreateLoad(impl->builder.getPtrTy(), vtableAddress);
                        llvm::Value* slot = impl->builder.CreateGEP(impl->builder.getPtrTy(), vtable, impl->builder.getInt64(*method->second.virtualSlot));
                        callee = impl->builder.CreateLoad(impl->builder.getPtrTy(), slot);
                    } else {
                        callee = impl->module->getFunction(method->second.linkName);
                    }
                    llvm::Value* result = impl->EmitAbiCall(impl->MethodFunctionType(method->second), callee, method->second.returnType, {obj}, method->second.parameterTypes, {}, "foreach.call");
                    impl->EmitExceptionCheck();
                    return {result, method->second.returnType};
                };

                if (auto found = impl->classes.find(aggregateName); found != impl->classes.end()) {
                    return invoke(found->second);
                }
                if (auto found = impl->interfaces.find(aggregateName); found != impl->interfaces.end()) {
                    auto method = found->second.methods.find(methodKey);
                    if (method == found->second.methods.end()) return {nullptr, ""};
                    llvm::Value* vtable = impl->builder.CreateLoad(impl->builder.getPtrTy(), obj);
                    llvm::Value* slot = impl->builder.CreateGEP(impl->builder.getPtrTy(), vtable, impl->builder.getInt64(*method->second.virtualSlot));
                    llvm::Value* callee = impl->builder.CreateLoad(impl->builder.getPtrTy(), slot);
                    llvm::Value* result = impl->EmitAbiCall(impl->MethodFunctionType(method->second), callee, method->second.returnType, {obj}, method->second.parameterTypes, {}, "foreach.call");
                    impl->EmitExceptionCheck();
                    return {result, method->second.returnType};
                }
                if (auto found = impl->structs.find(aggregateName); found != impl->structs.end()) {
                    return invoke(found->second);
                }
                impl->Fail("type '" + typeName + "' does not support custom iterators");
                return {nullptr, ""};
            };

            auto iterateResult = callMethod(iterablePtr, iterableType, "iterate");
            llvm::Value* iteratorValue = iterateResult.first;
            std::string iteratorType = iterateResult.second;
            if (!iteratorValue) impl->Fail("missing iterate method on " + iterableType);

            llvm::Type* iteratorLlvmType = impl->TypeFromName(iteratorType);
            llvm::AllocaInst* iteratorAddress = impl->CreateEntryAlloca(*function, iteratorLlvmType, "foreach.iterator");
            impl->builder.CreateStore(impl->Coerce(iteratorValue, iteratorLlvmType), iteratorAddress);

            llvm::Value* iteratorObjPtr = iteratorAddress;
            const bool managedIterator = IsStrongManagedPointerTypeName(iteratorType);
            if (managedIterator) {
                iteratorObjPtr = impl->EmitManagedGet(iteratorValue, true);
                Impl::Variable owner;
                owner.address = iteratorAddress;
                owner.type = iteratorLlvmType;
                owner.typeName = iteratorType;
                owner.managedOwner = true;
                impl->scopes.back().emplace(
                    "__foreach.iterator." + std::to_string(impl->scopes.back().size()),
                    std::move(owner));
            }
            else if (IsRawPointerTypeName(iteratorType)) {
                iteratorObjPtr = iteratorValue;
            }
            else if (impl->TypeNeedsCleanup(iteratorType)) {
                Impl::Variable owner;
                owner.address = iteratorAddress;
                owner.type = iteratorLlvmType;
                owner.typeName = iteratorType;
                owner.ownsAggregateResources = true;
                impl->scopes.back().emplace(
                    "__foreach.iterator." + std::to_string(impl->scopes.back().size()),
                    std::move(owner));
            }

            impl->builder.CreateBr(conditionBlock);
            
            impl->builder.SetInsertPoint(conditionBlock);
            auto nextResult = callMethod(iteratorObjPtr, iteratorType, "next");
            llvm::Value* hasNext = nextResult.first;
            if (!hasNext) impl->Fail("missing next method on iterator " + iteratorType);
            impl->builder.CreateCondBr(impl->AsCondition(hasNext), bodyBlock, endBlock);

            impl->loops.push_back({updateBlock, endBlock, impl->scopes.size()});
            impl->builder.SetInsertPoint(bodyBlock);
            
            auto elementResult = callMethod(iteratorObjPtr, iteratorType, "value");
            if (!elementResult.first) elementResult = callMethod(iteratorObjPtr, iteratorType, "", "value");
            if (!elementResult.first) impl->Fail("missing value method or property on iterator " + iteratorType);
            
            impl->builder.CreateStore(impl->Coerce(elementResult.first, iterationVariable.type), iterationVariable.address);
            
            if (stmt->body) stmt->body->Accept(*this);
            impl->BranchIfNeeded(updateBlock);

            impl->builder.SetInsertPoint(updateBlock);
            impl->BranchIfNeeded(conditionBlock);
            impl->loops.pop_back();
        }

        impl->builder.SetInsertPoint(endBlock);
        impl->PopScope(true);
    }

    void CodeGenerator::Visit(ContinueStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        impl->SetDebugLocation(stmt);
        if (impl->loops.empty()) impl->Fail("continue outside a loop");
        impl->EmitTransferCleanups(impl->loops.back().scopeCount, true);
        if (impl->builder.GetInsertBlock()->getTerminator()) return;
        impl->builder.CreateBr(impl->loops.back().continueBlock);
    }

    void CodeGenerator::Visit(BreakStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        impl->SetDebugLocation(stmt);
        if (impl->loops.empty()) impl->Fail("break outside a loop");
        impl->EmitTransferCleanups(impl->loops.back().scopeCount, true);
        if (impl->builder.GetInsertBlock()->getTerminator()) return;
        impl->builder.CreateBr(impl->loops.back().breakBlock);
    }

    void CodeGenerator::Visit(TypeAliasStmt* stmt) {
        (void)stmt;
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
