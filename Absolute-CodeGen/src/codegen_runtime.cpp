#include "codegen_internal.h"
#include <llvm/IR/MDBuilder.h>

namespace Absolute {
    llvm::Value* CodeGenerator::Impl::Evaluate(Expression* expression) {
        if (!expression) Fail("missing expression");
        value = nullptr;
        valueCreatesManagedOwner = false;
        valueManagedPointee = nullptr;
        valueCreatesArrayOwner = false;
        valueArrayOwner = nullptr;
        valueArrayOwnedCount = nullptr;
        valueCreatesClosureOwner = false;
        expression->Accept(visitor);
        if (!value) {
            llvm::Function* function = CurrentFunction();
            Fail("expression does not produce a value in '" +
                (function ? function->getName().str() : std::string("<module>")) +
                "': " + expression->ToString());
        }
        currentValueType = SemanticType(expression);
        if (ArrayRankName(currentValueType) > 0 && !valueCreatesArrayOwner) {
            // An accessor is a call however it is written. Asking for the
            // shape of the expression instead answered "no" for a property
            // and an indexer, so `holder.tags` -- a getter that returns
            // `copy(_tags)`, which the analyzer requires of it -- handed back
            // an owner nobody was recorded as holding, and the storage and
            // everything in it was never released. The analyzer already
            // answers "did this expression produce its value", and `move`
            // reaches here having settled the question itself.
            if (CreatesFreshString(expression)) {
                ArrayView returned = ArrayViewFromValue(value, currentValueType);
                valueCreatesArrayOwner = true;
                valueArrayOwner = returned.owner;
                valueArrayOwnedCount = ArrayElementCount(returned.dimensions);
            }
        }
        return value;
    }

    llvm::Value* CodeGenerator::Impl::EvaluateAddress(Expression* expression) {
        if (!expression) Fail("missing assignable expression");
        const bool oldMode = addressMode;
        llvm::Value* oldAddress = addressValue;
        addressMode = true;
        addressValue = nullptr;
        expression->Accept(visitor);
        llvm::Value* result = addressValue;
        addressMode = oldMode;
        addressValue = oldAddress;
        if (!result) Fail("expression is not assignable");
        return result;
    }

    void CodeGenerator::Impl::PushScope() {
        scopes.emplace_back();
        deferredScopes.emplace_back();
    }

    llvm::FunctionCallee CodeGenerator::Impl::TaskSpawn() {
        llvm::FunctionType* entryType = llvm::FunctionType::get(
            builder.getVoidTy(), {builder.getPtrTy()}, false);
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getPtrTy(), {entryType->getPointerTo(), builder.getPtrTy(),
                builder.getInt32Ty(), builder.getInt32Ty(), builder.getPtrTy()}, false);
        return module->getOrInsertFunction("absolute_task_spawn_config", type);
    }

    llvm::FunctionCallee CodeGenerator::Impl::TaskAwait() {
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getPtrTy(), {builder.getPtrTy()}, false);
        return module->getOrInsertFunction("absolute_task_await", type);
    }

    llvm::FunctionCallee CodeGenerator::Impl::TaskDestroy() {
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getVoidTy(), {builder.getPtrTy()}, false);
        return module->getOrInsertFunction("absolute_task_destroy", type);
    }

    void CodeGenerator::Impl::EmitScopeCleanup(size_t index, SymbolId transferredOwner) {
        if (index >= scopes.size() || !builder.GetInsertBlock() || builder.GetInsertBlock()->getTerminator()) return;
        if (index < deferredScopes.size()) {
            const std::vector<DeferStmt*> deferred = deferredScopes[index];
            const auto active = deferredCleanupCursors.find(index);
            const size_t start = active == deferredCleanupCursors.end() || active->second.empty()
                ? deferred.size() : active->second.back();
            deferredCleanupCursors[index].push_back(start);
            while (deferredCleanupCursors[index].back() > 0) {
                const size_t deferredIndex = --deferredCleanupCursors[index].back();
                DeferStmt* statement = deferred[deferredIndex];
                if (statement && statement->body) statement->body->Accept(visitor);
                if (!builder.GetInsertBlock() || builder.GetInsertBlock()->getTerminator()) break;
            }
            deferredCleanupCursors[index].pop_back();
            if (deferredCleanupCursors[index].empty()) deferredCleanupCursors.erase(index);
            if (!builder.GetInsertBlock() || builder.GetInsertBlock()->getTerminator()) return;
        }
        for (auto& [name, variable] : scopes[index]) {
            (void)name;
            const auto emitWhenOwner = [&](auto&& emitCleanup) {
                if (!variable.ownershipFlagStorage) {
                    emitCleanup();
                    return;
                }
                llvm::Function* function = CurrentFunction();
                llvm::BasicBlock* cleanup = llvm::BasicBlock::Create(
                    context, "role.owner.cleanup", function);
                llvm::BasicBlock* complete = llvm::BasicBlock::Create(
                    context, "role.cleanup.end", function);
                llvm::Value* owns = builder.CreateLoad(
                    builder.getInt1Ty(), variable.ownershipFlagStorage,
                    "role.is.owner");
                builder.CreateCondBr(owns, cleanup, complete);
                builder.SetInsertPoint(cleanup);
                emitCleanup();
                BranchIfNeeded(complete);
                builder.SetInsertPoint(complete);
            };
            if (IsTaskTypeName(variable.typeName)) {
                llvm::Value* handle = builder.CreateLoad(variable.type, variable.address, "cleanup.task");
                builder.CreateCall(TaskDestroy(), {handle});
                builder.CreateStore(llvm::ConstantPointerNull::get(builder.getPtrTy()), variable.address);
                continue;
            }
            const bool transfersThisOwner = transferredOwner != InvalidSymbolId &&
                variable.symbol == transferredOwner;
            if (variable.ownsArrayStorage && !transfersThisOwner) {
                emitWhenOwner([&] {
                    llvm::Value* owner = variable.arrayOwnerStorage
                        ? builder.CreateLoad(
                            builder.getPtrTy(), variable.arrayOwnerStorage,
                            "cleanup.array.owner")
                        : variable.arrayOwner;
                    // What the elements own is released first, and only if the
                    // element type says there is anything there to release.
                    // Guarded by the same owner pointer the free is: `move`
                    // clears that slot, and a moved-from local must not release
                    // what the destination now holds.
                    // Over what the owner covers. A name holding a slice of
                    // an allocation still releases the whole allocation, and
                    // walking the view would leave what is outside it held.
                    llvm::Value* ownedCount = variable.arrayOwnedCountStorage
                        ? builder.CreateLoad(builder.getInt64Ty(),
                            variable.arrayOwnedCountStorage, "cleanup.array.owned.count")
                        : nullptr;
                    EmitArrayElementCleanup(
                        ownedCount ? owner : variable.address,
                        variable.arrayElementType,
                        ownedCount ? std::vector<llvm::Value*>{ownedCount}
                            : variable.arrayDimensions,
                        ArrayElementTypeName(variable.typeName,
                            ArrayRankName(variable.typeName)), owner);
                    builder.CreateCall(Free(), {owner});
                });
                continue;
            }
            // The frame's own array: no storage to free, and the elements
            // released without the owner guard, which is the question a view
            // answers the other way.
            if (variable.ownsArrayElements && !transfersThisOwner) {
                EmitArrayElementCleanup(variable.address, variable.arrayElementType,
                    variable.arrayDimensions,
                    ArrayElementTypeName(variable.typeName,
                        ArrayRankName(variable.typeName)), nullptr);
                continue;
            }
            if (variable.ownsAggregateResources && !transfersThisOwner) {
                emitWhenOwner([&] {
                    EmitValueCleanup(variable.address, variable.typeName);
                });
                continue;
            }
            if (!variable.managedOwner || transfersThisOwner) continue;
            emitWhenOwner([&] {
                llvm::Value* handle = builder.CreateLoad(
                    variable.type, variable.address, "cleanup.handle");
                llvm::Value* pointee = EmitManagedGet(handle, false);
                EmitPointeeCleanup(pointee, variable.typeName);
                builder.CreateCall(ManagedDestroy(), {handle});
                builder.CreateStore(builder.getInt64(0), variable.address);
                if (variable.managedPointee)
                    builder.CreateStore(
                        llvm::ConstantPointerNull::get(builder.getPtrTy()),
                        variable.managedPointee);
            });
        }
    }

    void CodeGenerator::Impl::EmitCleanupsFrom(size_t firstScope, SymbolId transferredOwner) {
        for (size_t index = scopes.size(); index > firstScope; --index)
            EmitScopeCleanup(index - 1, transferredOwner);
    }

    void CodeGenerator::Impl::PopScope(bool cleanup) {
        if (scopes.empty()) return;
        if (cleanup) EmitScopeCleanup(scopes.size() - 1);
        scopes.pop_back();
        deferredScopes.pop_back();
    }

    CodeGenerator::Impl::Variable* CodeGenerator::Impl::FindVariable(const std::string& name) {
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
            auto found = scope->find(name);
            if (found != scope->end()) return &found->second;
        }
        if (const auto found = globals.find(Qualify(name)); found != globals.end())
            return &found->second;
        if (const auto found = globals.find(name); found != globals.end())
            return &found->second;
        return nullptr;
    }

    CodeGenerator::Impl::Variable* CodeGenerator::Impl::FindVariable(SymbolId symbol) {
        if (symbol == InvalidSymbolId) return nullptr;
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
            for (auto& [name, variable] : *scope) {
                (void)name;
                if (variable.symbol == symbol) return &variable;
            }
        }
        for (auto& [name, variable] : globals) {
            (void)name;
            if (variable.symbol == symbol) return &variable;
        }
        return nullptr;
    }

    CodeGenerator::Impl::Variable& CodeGenerator::Impl::RequireVariable(const std::string& name) {
        Variable* variable = FindVariable(name);
        if (!variable) Fail("unknown variable '" + name + "'");
        return *variable;
    }

    CodeGenerator::Impl::Variable& CodeGenerator::Impl::AddressOf(Expression* expression) {
        const std::string name = IdentifierName(expression);
        if (name.empty()) Fail("expected assignable identifier");
        return RequireVariable(name);
    }

    SymbolId CodeGenerator::Impl::SemanticSymbol(Expression* expression) const {
        if (!analyzer || !expression) return InvalidSymbolId;
        const ExpressionInfo* info = analyzer->GetExpressionInfo(*expression);
        return info ? info->symbol : InvalidSymbolId;
    }

    bool CodeGenerator::Impl::StaticManagedOwner(SymbolId symbol) const {
        if (!analyzer || symbol == InvalidSymbolId) return false;
        const Symbol* resolved = analyzer->GetSymbol(symbol);
        return resolved && resolved->managedOwner;
    }

    llvm::Value* CodeGenerator::Impl::ManagedPointee(Expression* expression, llvm::Value* handle) {
        if (auto* identifier = dynamic_cast<IdentifierExpr*>(expression)) {
            Variable* variable = FindVariable(identifier->name);
            const ExpressionInfo* info = analyzer ? analyzer->GetExpressionInfo(*expression) : nullptr;
            if (variable && variable->managedPointee && info &&
                info->pointerOwner == variable->symbol) {
                return builder.CreateLoad(builder.getPtrTy(), variable->managedPointee,
                    identifier->name + ".cached.pointee");
            }
        }
        return EmitManagedGet(handle, true);
    }

    llvm::Value* CodeGenerator::Impl::ArgumentOwnershipFlag(
        Expression* expression, const std::string& parameterType) {
        if (!ParameterSupportsOwnershipName(parameterType) ||
            !analyzer || !expression)
            return builder.getFalse();
        const ExpressionInfo* info = analyzer->GetExpressionInfo(*expression);
        if (!info) return builder.getFalse();
        // A value the expression produced is a value nobody else is holding: a
        // `move`, a `new`, a call's return -- a `T*` return must transfer an
        // owner, which the analyzer enforces -- or a take out of a slot, which
        // clears the slot, and that is exactly what makes it a producer rather
        // than a read.
        //
        // The questions below are all about the shape of the parameter's type,
        // and inside an open generic body none of them can be answered: `T` is
        // not a pointer yet, so `createsManagedOwner` is false for `move(v)`
        // and for `unsafeArrayTake(values, i)` alike. The flag then said
        // "borrowed" about a value the caller had just given up. One hop was
        // fine, because the outermost caller passes a `new` and the analyzer
        // sees that; the second hop is a container wrapping a container, and
        // `Queue<Cell*>` over `Deque<Cell*>` aborted on its first enqueue with
        // "Ownership operation requires an owner argument".
        //
        // Asked after the guard above, so a shared value is unaffected: `move`
        // of a string is a read, and a string parameter carries no ownership
        // role for this to answer about.
        if (info->isMoveResult || CreatesFreshString(expression))
            return builder.getTrue();
        const std::string valueType =
            ValueReferenceBaseTypeName(parameterType);
        bool transfers = false;
        if (IsStrongManagedPointerTypeName(valueType))
            transfers = info->createsManagedOwner;
        else if (ArrayRankName(valueType) > 0)
            transfers = info->createsArrayOwner;
        else
            transfers = info->isMoveResult || !info->isLValue;
        return builder.getInt1(transfers);
    }

    void CodeGenerator::Impl::AppendDefaultArguments(
        std::vector<Expression*>& expressions, const Symbol* callee) {
        if (!callee || callee->variadicParameter) return;
        for (size_t index = expressions.size();
            index < callee->parameterDefaults.size(); ++index) {
            Expression* fallback = callee->parameterDefaults[index];
            if (!fallback) return;
            expressions.push_back(fallback);
        }
    }

    llvm::Value* CodeGenerator::Impl::EvaluateCallArgument(
        Expression* expression, std::vector<llvm::Value*>& temporaryArrayOwners,
        std::vector<llvm::Value*>& temporaryClosureOwners,
        const std::string& parameterType) {
        if (IsValueReferenceTypeName(parameterType)) {
            const ExpressionInfo* info = analyzer && expression
                ? analyzer->GetExpressionInfo(*expression) : nullptr;
            if (info && info->isLValue) return EvaluateAddress(expression);
            if (!IsConstValueReferenceTypeName(parameterType))
                Fail("mutable reference argument requires an addressable lvalue");
            llvm::Value* value = Evaluate(expression);
            llvm::Type* valueType = TypeFromName(ValueReferenceBaseTypeName(parameterType));
            llvm::AllocaInst* temporary = CreateEntryAlloca(
                *CurrentFunction(), valueType, "const.ref.temporary");
            builder.CreateStore(Coerce(value, valueType), temporary);
            return temporary;
        }
        llvm::Value* argument = Evaluate(expression);
        // A string the caller made only in order to pass it in: released with
        // the statement, once the call it was made for is over. The parameter
        // takes its own count on the way in and gives it back on the way out,
        // so the callee never depends on the caller's. That split is what lets
        // an external C function take a string at all -- there is no body to
        // emit a release into, and none is needed.
        RegisterIfFreshString(expression, argument);
        RegisterFreshValueArgument(expression, argument);
        const bool transfersArrayOwner =
            valueCreatesArrayOwner && llvm::cast<llvm::ConstantInt>(
                ArgumentOwnershipFlag(expression, parameterType))->isOne();
        if (valueCreatesArrayOwner && !transfersArrayOwner)
            temporaryArrayOwners.push_back(valueArrayOwner);
        if (valueCreatesClosureOwner) temporaryClosureOwners.push_back(argument);
        if (!parameterType.empty()) {
            const std::string targetType = ValueReferenceBaseTypeName(parameterType);
            argument = Coerce(argument, TypeFromName(targetType),
                SemanticType(expression), targetType);
        }
        ConsumeTaskArgument(expression, parameterType);
        return argument;
    }

    std::vector<llvm::Value*> CodeGenerator::Impl::EvaluateCallArguments(
        const std::vector<Expression*>& expressions,
        std::vector<llvm::Value*>& temporaryArrayOwners,
        std::vector<llvm::Value*>& temporaryClosureOwners,
        const std::vector<std::string>& rawParameterTypes,
        bool variadicParameter,
        std::vector<llvm::Value*>* ownershipFlags) {
        std::vector<std::string> parameterTypes = rawParameterTypes;
        for (std::string& parameter : parameterTypes)
            parameter = SubstituteCodegenType(parameter, currentGenericSubstitutions);

        std::vector<llvm::Value*> arguments;
        if (!variadicParameter) {
            arguments.reserve(expressions.size());
            if (ownershipFlags) ownershipFlags->reserve(expressions.size());
            for (size_t index = 0; index < expressions.size(); ++index) {
                const std::string parameterType = index < parameterTypes.size()
                    ? parameterTypes[index] : std::string{};
                if (ownershipFlags)
                    ownershipFlags->push_back(
                        ArgumentOwnershipFlag(expressions[index], parameterType));
                arguments.push_back(EvaluateCallArgument(
                    expressions[index], temporaryArrayOwners, temporaryClosureOwners,
                    parameterType));
            }
            valueCreatesArrayOwner = false;
            valueArrayOwner = nullptr;
        valueArrayOwnedCount = nullptr;
            valueArrayOwnedCount = nullptr;
            return arguments;
        }
        if (parameterTypes.empty()) Fail("params callable has no array parameter");
        const size_t fixedCount = parameterTypes.size() - 1;
        if (expressions.size() < fixedCount)
            Fail("params call has too few fixed arguments");

        arguments.reserve(parameterTypes.size());
        for (size_t index = 0; index < fixedCount; ++index)
        {
            if (ownershipFlags)
                ownershipFlags->push_back(
                    ArgumentOwnershipFlag(expressions[index], parameterTypes[index]));
            arguments.push_back(EvaluateCallArgument(
                expressions[index], temporaryArrayOwners, temporaryClosureOwners,
                parameterTypes[index]));
        }

        const std::string& arrayType = parameterTypes.back();
        const bool directArray = expressions.size() == parameterTypes.size() &&
            SemanticType(expressions.back()) == ValueReferenceBaseTypeName(arrayType);
        if (directArray) {
            if (ownershipFlags)
                ownershipFlags->push_back(
                    ArgumentOwnershipFlag(expressions.back(), arrayType));
            arguments.push_back(EvaluateCallArgument(
                expressions.back(), temporaryArrayOwners, temporaryClosureOwners,
                arrayType));
            valueCreatesArrayOwner = false;
            valueArrayOwner = nullptr;
        valueArrayOwnedCount = nullptr;
            valueArrayOwnedCount = nullptr;
            return arguments;
        }

        const std::string elementTypeName = ArrayElementTypeName(
            ValueReferenceBaseTypeName(arrayType), 1);
        llvm::Type* elementType = TypeFromName(elementTypeName);
        const size_t count = expressions.size() - fixedCount;
        llvm::AllocaInst* storage = builder.CreateAlloca(
            elementType, builder.getInt64(count), "params.storage");
        storage->setAlignment(llvm::Align(16));
        for (size_t index = 0; index < count; ++index) {
            llvm::Value* item = EvaluateCallArgument(
                expressions[fixedCount + index], temporaryArrayOwners,
                temporaryClosureOwners, elementTypeName);
            llvm::Value* destination = builder.CreateInBoundsGEP(
                elementType, storage, builder.getInt64(index), "params.element");
            builder.CreateStore(item, destination);
        }
        arguments.push_back(BuildArrayDescriptor(
            {storage, elementType, ValueReferenceBaseTypeName(arrayType),
                {builder.getInt64(count)}, nullptr}));
        if (ownershipFlags) ownershipFlags->push_back(builder.getFalse());
        valueCreatesArrayOwner = false;
        valueArrayOwner = nullptr;
        valueArrayOwnedCount = nullptr;
        return arguments;
    }

    void CodeGenerator::Impl::ConsumeTaskArgument(
        Expression* expression, const std::string& parameterType) {
        if (!expression || !IsTaskTypeName(parameterType)) return;
        const ExpressionInfo* info = analyzer
            ? analyzer->GetExpressionInfo(*expression) : nullptr;
        if (!info || !info->isLValue) return;
        llvm::Value* address = EvaluateAddress(expression);
        builder.CreateStore(
            llvm::ConstantPointerNull::get(builder.getPtrTy()), address);
    }

    void CodeGenerator::Impl::ReleaseArrayTemporaries(
        const std::vector<llvm::Value*>& owners) {
        for (llvm::Value* owner : owners) builder.CreateCall(Free(), {owner});
    }

    void CodeGenerator::Impl::ReleaseClosureTemporaries(
        const std::vector<llvm::Value*>& owners) {
        for (llvm::Value* owner : owners) builder.CreateCall(ClosureRelease(), {owner});
    }

    llvm::Value* CodeGenerator::Impl::EvaluateBorrowed(Expression* expression) {
        llvm::Value* value = Evaluate(expression);
        // Only a freshly produced owner is registered. The flag is false for a
        // variable holding one, which is what keeps this from destroying
        // something its scope still owns.
        if (valueCreatesManagedOwner && value)
            RegisterTemporaryOwner(value, SemanticType(expression));
        // An array produced by an expression owns its buffer the same way, and
        // is dropped the same way: `map.toArray().length` allocated a snapshot
        // and read one field of it.
        if (valueCreatesArrayOwner && valueArrayOwner)
            RegisterTemporaryArrayOwner(value, SemanticType(expression),
                valueArrayOwner, valueArrayOwnedCount);
        RegisterIfFreshString(expression, value);
        return value;
    }

    void CodeGenerator::Impl::RegisterTemporaryOwner(
        llvm::Value* handle, const std::string& typeName) {
        if (!handle) return;
        temporaryManagedOwners.push_back({handle, typeName});
        valueCreatesManagedOwner = false;
        valueManagedPointee = nullptr;
    }

    namespace {
        // Only these describe storage that is entirely their own. A field of
        // struct type shares its bytes with the struct around it, and an
        // element of an array of structs shares its bytes with that struct's
        // fields, so neither is described and both keep aliasing everything.
        bool IsPrimitiveStorageType(const std::string& typeName) {
            return typeName == "int8" || typeName == "uint8" || typeName == "char" ||
                typeName == "int16" || typeName == "uint16" ||
                typeName == "int32" || typeName == "uint32" ||
                typeName == "int64" || typeName == "uint64" ||
                typeName == "bool" || typeName == "float" || typeName == "double";
        }
    }

    // One scalar type node per described type, under a single root, and the
    // access tag LLVM wants: {type, type, offset}.
    llvm::MDNode* CodeGenerator::Impl::TbaaNode(const std::string& name) {
        if (const auto found = tbaaTypeNodes.find(name); found != tbaaTypeNodes.end())
            return found->second;
        llvm::MDBuilder builder(context);
        if (!tbaaRoot) tbaaRoot = builder.createTBAARoot("Absolute");
        llvm::MDNode* type = builder.createTBAAScalarTypeNode(name, tbaaRoot);
        llvm::MDNode* tag = builder.createTBAAStructTagNode(type, type, 0);
        tbaaTypeNodes.emplace(name, tag);
        return tag;
    }

    llvm::MDNode* CodeGenerator::Impl::TbaaFieldAccess(const std::string& typeName) {
        // An array-typed field holds a descriptor, which is three words of the
        // object rather than the elements themselves, so it is described like
        // any other field: the elements it points at are somewhere else.
        const bool describable = IsPrimitiveStorageType(typeName) ||
            ArrayRankName(typeName) > 0 || IsPointerTypeName(typeName);
        if (!describable) return nullptr;
        const std::string name = "absolute.field." +
            (ArrayRankName(typeName) > 0 ? std::string("descriptor") : typeName);
        return TbaaNode(name);
    }

    llvm::MDNode* CodeGenerator::Impl::TbaaElementAccess(const std::string& typeName) {
        if (!IsPrimitiveStorageType(typeName)) return nullptr;
        return TbaaNode("absolute.element." + typeName);
    }

    void CodeGenerator::Impl::TagAccess(llvm::Value* instruction, llvm::MDNode* tag) {
        if (!tag || !typeAliasInfoEnabled) return;
        if (auto* memory = llvm::dyn_cast_or_null<llvm::Instruction>(instruction))
            memory->setMetadata(llvm::LLVMContext::MD_tbaa, tag);
    }

    void CodeGenerator::Impl::RequireNoPendingTemporaries(const std::string& where) {
        if (!temporaryManagedOwners.empty())
            Fail("temporary owner left unreleased in " + where);
    }

    // Registered as the value it is, so the walk that releases it is the one
    // the type already describes. It used to be registered as a bare buffer
    // and released with `free`, which let go of the storage and of nothing in
    // it: `snapshot()[0]`, `makeList().length` and a `foreach` over an array
    // its own header built all leaked every string the array held.
    llvm::Value* CodeGenerator::Impl::ArrayElementCount(
        const std::vector<llvm::Value*>& dimensions) {
        llvm::Value* count = builder.getInt64(1);
        for (llvm::Value* dimension : dimensions)
            count = builder.CreateMul(count, dimension, "array.owned.count");
        return count;
    }

    void CodeGenerator::Impl::RegisterTemporaryArrayOwner(
        llvm::Value* descriptor, const std::string& typeName,
        llvm::Value* owner, llvm::Value* ownedCount) {
        if (!descriptor || !CurrentFunction()) return;
        // What is released is what the owner covers, not what the value in
        // hand describes. A slice is a view of part of an allocation, and
        // releasing it through the view left every element outside the view
        // holding what it held: `copy(makeArray()[0:2])` gave back two of
        // three strings and freed the storage under the third.
        //
        // Registered as a run of elements rather than as the array it was
        // sliced from, because a rank the view no longer has is not needed to
        // walk what the buffer holds -- one count and an element type are.
        if (owner && ownedCount && ArrayRankName(typeName) > 0) {
            const size_t rank = ArrayRankName(typeName);
            const std::string elementTypeName = ArrayElementTypeName(typeName, rank);
            const std::string ownedTypeName = elementTypeName + "[]";
            ArrayView owned;
            owned.address = owner;
            owned.elementType = TypeFromName(elementTypeName);
            owned.typeName = ownedTypeName;
            owned.dimensions = {ownedCount};
            owned.owner = owner;
            llvm::Value* ownedDescriptor = BuildArrayDescriptor(owned);
            llvm::AllocaInst* spill = CreateEntryAlloca(
                *CurrentFunction(), ownedDescriptor->getType(), "array.temporary");
            builder.CreateStore(ownedDescriptor, spill);
            temporaryManagedOwners.push_back(
                {spill, ownedTypeName, TemporaryOwner::Kind::AggregateValue});
            valueCreatesArrayOwner = false;
            valueArrayOwner = nullptr;
            valueArrayOwnedCount = nullptr;
            return;
        }
        if (typeName.empty() || ArrayRankName(typeName) == 0) {
            temporaryManagedOwners.push_back(
                {descriptor, {}, TemporaryOwner::Kind::ArrayBuffer});
        }
        else {
            llvm::AllocaInst* spill = CreateEntryAlloca(
                *CurrentFunction(), descriptor->getType(), "array.temporary");
            builder.CreateStore(descriptor, spill);
            temporaryManagedOwners.push_back(
                {spill, typeName, TemporaryOwner::Kind::AggregateValue});
        }
        valueCreatesArrayOwner = false;
        valueArrayOwner = nullptr;
        valueArrayOwnedCount = nullptr;
    }

    // A string an expression produced and nothing kept. `println(format(...))`
    // in a loop is the shape this exists for: the bytes are formatted, printed,
    // and then nobody holds them.
    void CodeGenerator::Impl::RegisterTemporaryStringOwner(llvm::Value* text) {
        if (!text) return;
        temporaryManagedOwners.push_back(
            {text, "string", TemporaryOwner::Kind::StringStorage});
    }

    // Whether this expression made the storage it handed back. The analyzer
    // decides: a call allocates, a literal is static, and reading a name gives
    // back storage something else already holds.
    bool CodeGenerator::Impl::CreatesFreshString(Expression* expression) const {
        if (!expression || !analyzer) return false;
        // `move(x)` is a call, so it says it made its value -- but for a string
        // it hands back the one `x` still names. Counting it as fresh would
        // leave the destination holding a count the source is about to give
        // back. What `move` means for an owner is a transfer; for a shared
        // value it is a read, and a read is not fresh.
        if (auto* call = dynamic_cast<FunctionCallExpr*>(expression)) {
            const std::string callee = IdentifierName(call->base.get());
            // These read rather than produce. Syntactically they are calls, so
            // the analyzer's answer is that they made their value -- but
            // `unsafeArrayGet` hands back what the array already holds, and
            // `move` of a shared value hands back what the source still names.
            // A container's indexer is written on top of the first of them, so
            // treating it as fresh meant the value it returned was never
            // counted and the caller's copy released storage the container
            // still had. `unsafeArrayTake` is not here: it clears the slot,
            // which is exactly what makes it a producer.
            if (callee == "move" || callee == "unsafeArrayGet") return false;
        }
        const ExpressionInfo* info = analyzer->GetExpressionInfo(*expression);
        return info && info->producesFreshValue;
    }

    // A value an expression made and nobody kept, whose parts are counted:
    // `headers[0].name` builds a struct to read one field of. The struct is
    // spilled so there is an address to release, and released with the
    // statement, the same way a string temporary is.
    void CodeGenerator::Impl::RegisterTemporaryAggregate(
        llvm::Value* address, const std::string& typeName) {
        if (!address) return;
        temporaryManagedOwners.push_back(
            {address, typeName, TemporaryOwner::Kind::AggregateValue});
    }

    // Reading one field of a value the expression made -- `entries[0].name` --
    // builds the whole aggregate to get at it, and the aggregate counted its
    // parts on the way out of whatever produced it. Nobody keeps it, so it is
    // released with the statement.
    //
    // Indexing something that is not an array is an indexer call, and a call
    // produces its value. The analyzer's flag answers for a method or a
    // property; it is keyed to the symbol the access resolved to, and an
    // indexer read resolves to the container rather than to the indexer.
    void CodeGenerator::Impl::RegisterAggregateTemporaryBase(
        Expression* base, llvm::Value* object, const std::string& typeName) {
        if (!base || !object) return;
        auto* indexed = dynamic_cast<ArrayAccessExpr*>(base);
        const bool indexerRead = indexed && indexed->base &&
            ArrayRankName(SemanticType(indexed->base.get())) == 0;
        if (!indexerRead && !CreatesFreshString(base)) return;
        const TypeSemantics semantics = SemanticsOfTypeName(typeName);
        if (semantics.needsDrop && semantics.copyable)
            RegisterTemporaryAggregate(object, typeName);
    }

    // The same rule for a value with parts rather than one pointer:
    // `take(make(1))` built a struct only in order to pass it in, and the
    // parameter borrows it -- so nobody but the statement that made it will
    // release what its parts hold. It is spilled to give the walk an address.
    // Only a shared value is registered. One that is not copyable travels with
    // a role instead, and the ownership flag says who releases it.
    void CodeGenerator::Impl::RegisterFreshValueArgument(
        Expression* expression, llvm::Value* value) {
        if (!value || !expression || !CurrentFunction()) return;
        const std::string typeName = SemanticType(expression);
        // A string is one pointer and was registered as it stands.
        if (typeName.empty() || typeName == "string") return;
        if (!CreatesFreshString(expression)) return;
        const TypeSemantics semantics = SemanticsOfTypeName(typeName);
        if (!semantics.needsDrop || !semantics.copyable) return;
        // An indirect value arrives as an address the callee reads through,
        // and whoever made that address is who releases it.
        if (value->getType()->isPointerTy()) return;
        llvm::AllocaInst* spill = CreateEntryAlloca(
            *CurrentFunction(), value->getType(), "argument.temporary");
        builder.CreateStore(value, spill);
        RegisterTemporaryAggregate(spill, typeName);
    }

    std::vector<llvm::Value*> CodeGenerator::Impl::EvaluateIndexArguments(
        const std::vector<std::unique_ptr<Expression>>& indexes) {
        std::vector<llvm::Value*> arguments;
        arguments.reserve(indexes.size());
        for (const auto& index : indexes) {
            llvm::Value* argument = Evaluate(index.get());
            RegisterIfFreshString(index.get(), argument);
            RegisterFreshValueArgument(index.get(), argument);
            arguments.push_back(argument);
        }
        return arguments;
    }

    // A conditional hands back one value from two paths, and the two paths
    // need not agree about who is holding it: `cond ? format(...) : name` is a
    // count on one side and a borrow on the other. Nothing downstream can ask
    // which path ran, so the arms are made to agree here -- an arm that
    // borrowed takes a count of its own -- and the conditional as a whole
    // reports that it produced its value, which is what the analyzer says
    // about it too.
    llvm::Value* CodeGenerator::Impl::CountedArmValue(
        Expression* arm, llvm::Value* value, const std::string& typeName) {
        if (!arm || !value || CreatesFreshString(arm)) return value;
        const TypeSemantics semantics = SemanticsOfTypeName(typeName);
        if (!semantics.needsDrop || !semantics.copyable) return value;
        if (semantics.dropKind == DropKind::StringStorage)
            return builder.CreateCall(StringRetain(), {value}, "arm.retained");
        llvm::AllocaInst* counted = CreateEntryAlloca(
            *CurrentFunction(), value->getType(), "arm.counted");
        builder.CreateStore(value, counted);
        EmitValueRetain(counted, typeName);
        return builder.CreateLoad(value->getType(), counted, "arm.counted.value");
    }

    void CodeGenerator::Impl::RegisterIfFreshString(
        Expression* expression, llvm::Value* value) {
        if (!value || !expression) return;
        // The flag says the expression made its value; only a string is
        // released this way, so the type has to agree as well.
        if (SemanticType(expression) == "string" && CreatesFreshString(expression))
            RegisterTemporaryStringOwner(value);
    }

    // Storing a string into a place that will release it later. A fresh one
    // already counts as held once -- the call that made it said so -- and
    // anything else is another name for storage something already holds, so it
    // has to say so.
    llvm::Value* CodeGenerator::Impl::RetainStoredString(
        Expression* source, llvm::Value* value) {
        if (!value || CreatesFreshString(source)) return value;
        return builder.CreateCall(StringRetain(), {value}, "string.retained");
    }

    llvm::Value* CodeGenerator::Impl::RetainReturnedValue(
        Expression* source, llvm::Value* result) {
        if (!result || !source || !CurrentFunction()) return result;
        if (currentReturnTypeName == "string")
            return RetainStoredString(source, result);
        const TypeSemantics returned = SemanticsOfTypeName(currentReturnTypeName);
        if (!returned.needsDrop || !returned.copyable) return result;
        if (CreatesFreshString(source)) return result;
        llvm::AllocaInst* carried = CreateEntryAlloca(
            *CurrentFunction(), result->getType(), "return.retained");
        builder.CreateStore(result, carried);
        EmitValueRetain(carried, currentReturnTypeName);
        return builder.CreateLoad(result->getType(), carried, "return.counted");
    }

    void CodeGenerator::Impl::EmitCallableReturn(
        Expression* source, llvm::Value* result, size_t temporaryMark) {
        result = RetainReturnedValue(source, result);
        ReleaseTemporaryOwners(temporaryMark);
        // And what every statement still open around this one made. A return
        // leaves them all: `foreach (string t in texts()) { return 1; }` walks
        // an array the loop made and nobody else names, and the loop's own
        // release is on a path this return does not take. Emitted rather than
        // popped, because the paths that do reach the end of that statement
        // still release it there.
        EmitTemporaryOwnerCleanups(0);
        SymbolId transferredOwner = InvalidSymbolId;
        if (IsStrongManagedPointerTypeName(currentReturnTypeName)) {
            if (dynamic_cast<IdentifierExpr*>(source)) {
                Impl::Variable& returned = RequireVariable(
                    IdentifierName(source));
                if (returned.managedOwner) transferredOwner = returned.symbol;
            }
        }
        else if (ArrayRankName(currentReturnTypeName) > 0) {
            if (Impl::Variable* returned = FindVariable(SemanticSymbol(source)))
                transferredOwner = returned->ownsArrayStorage
                    ? returned->symbol : returned->arrayOwnerSymbol;
        }
        // A returned aggregate is not transferred out of its local: it is
        // copied, and the copy counts the parts it now names just above. The
        // local keeps its own count and gives it back the ordinary way. Doing
        // both -- suppressing the cleanup *and* counting -- leaves two counts
        // and one release, which is a leak rather than the use-after-free the
        // suppression was added to stop.
        EmitTransferCleanups(0, true, transferredOwner);
        if (builder.GetInsertBlock()->getTerminator()) return;
        if (currentReturnStorage) {
            builder.CreateStore(result, currentReturnStorage);
            builder.CreateRetVoid();
        }
        else builder.CreateRet(result);
    }

    void CodeGenerator::Impl::ReleaseTemporaryOwners(size_t mark) {
        EmitTemporaryOwnerCleanups(mark);
        temporaryManagedOwners.resize(mark);
    }

    void CodeGenerator::Impl::EmitTemporaryOwnerCleanups(size_t mark) {
        if (temporaryManagedOwners.size() <= mark) return;
        // Nothing may be emitted into a block a terminator has already closed,
        // so a scope has to close before the return or branch its statement
        // ends with. The guard is here to keep malformed IR out if one does
        // not.
        llvm::BasicBlock* block = builder.GetInsertBlock();
        if (block && !block->getTerminator()) {
            // The same two steps `delete` takes, in the same order: what the
            // object owns goes first, then the object. Destroying the handle
            // alone left every managed field of the temporary behind, which is
            // a quieter leak than the one this replaced.
            for (size_t index = temporaryManagedOwners.size(); index > mark; --index) {
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
        }
    }

    llvm::StructType* CodeGenerator::Impl::ClosureObjectType() {
        if (closureObjectType) return closureObjectType;
        closureObjectType = llvm::StructType::create(context, "absolute.closure");
        closureObjectType->setBody({builder.getInt64Ty(), builder.getPtrTy(),
            builder.getPtrTy(), builder.getPtrTy()});
        return closureObjectType;
    }

    llvm::FunctionType* CodeGenerator::Impl::ClosureInvokeType(
        const std::string& returnType,
        const std::vector<std::string>& parameterTypes) {
        std::vector<llvm::Type*> parameters;
        if (AbiReturnOffset(returnType) != 0) parameters.push_back(builder.getPtrTy());
        parameters.push_back(builder.getPtrTy());
        for (const std::string& parameter : parameterTypes) {
            parameters.push_back(AbiParameterType(parameter));
            if (ParameterSupportsOwnershipName(parameter))
                parameters.push_back(builder.getInt1Ty());
        }
        return llvm::FunctionType::get(AbiReturnType(returnType), parameters, false);
    }

    llvm::Function* CodeGenerator::Impl::ClosureRetain() {
        if (llvm::Function* existing = module->getFunction("__absolute.closure.retain"))
            return existing;
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getVoidTy(), {builder.getPtrTy()}, false);
        llvm::Function* function = llvm::Function::Create(type,
            llvm::Function::InternalLinkage, "__absolute.closure.retain", *module);
        const auto saved = builder.saveIP();
        llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
        llvm::BasicBlock* update = llvm::BasicBlock::Create(context, "update", function);
        llvm::BasicBlock* complete = llvm::BasicBlock::Create(context, "complete", function);
        builder.SetInsertPoint(entry);
        llvm::Value* closure = function->getArg(0);
        builder.CreateCondBr(builder.CreateICmpNE(closure,
            llvm::ConstantPointerNull::get(builder.getPtrTy())), update, complete);
        builder.SetInsertPoint(update);
        llvm::Value* countAddress = builder.CreateStructGEP(
            ClosureObjectType(), closure, 0, "closure.count.address");
        llvm::Value* count = builder.CreateLoad(
            builder.getInt64Ty(), countAddress, "closure.count");
        llvm::BasicBlock* increment = llvm::BasicBlock::Create(
            context, "increment", function);
        builder.CreateCondBr(builder.CreateICmpSGE(count, builder.getInt64(0)),
            increment, complete);
        builder.SetInsertPoint(increment);
        builder.CreateStore(builder.CreateAdd(count, builder.getInt64(1)), countAddress);
        builder.CreateBr(complete);
        builder.SetInsertPoint(complete);
        builder.CreateRetVoid();
        builder.restoreIP(saved);
        return function;
    }

    llvm::Function* CodeGenerator::Impl::ClosureRelease() {
        if (llvm::Function* existing = module->getFunction("__absolute.closure.release"))
            return existing;
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getVoidTy(), {builder.getPtrTy()}, false);
        llvm::Function* function = llvm::Function::Create(type,
            llvm::Function::InternalLinkage, "__absolute.closure.release", *module);
        const auto saved = builder.saveIP();
        llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
        llvm::BasicBlock* update = llvm::BasicBlock::Create(context, "update", function);
        llvm::BasicBlock* decrement = llvm::BasicBlock::Create(context, "decrement", function);
        llvm::BasicBlock* destroy = llvm::BasicBlock::Create(context, "destroy", function);
        llvm::BasicBlock* callDestroy = llvm::BasicBlock::Create(context, "destroy.env", function);
        llvm::BasicBlock* freeClosure = llvm::BasicBlock::Create(context, "free", function);
        llvm::BasicBlock* complete = llvm::BasicBlock::Create(context, "complete", function);
        builder.SetInsertPoint(entry);
        llvm::Value* closure = function->getArg(0);
        builder.CreateCondBr(builder.CreateICmpNE(closure,
            llvm::ConstantPointerNull::get(builder.getPtrTy())), update, complete);
        builder.SetInsertPoint(update);
        llvm::Value* countAddress = builder.CreateStructGEP(
            ClosureObjectType(), closure, 0, "closure.count.address");
        llvm::Value* count = builder.CreateLoad(
            builder.getInt64Ty(), countAddress, "closure.count");
        builder.CreateCondBr(builder.CreateICmpSGE(count, builder.getInt64(0)),
            decrement, complete);
        builder.SetInsertPoint(decrement);
        llvm::Value* next = builder.CreateSub(count, builder.getInt64(1), "closure.count.next");
        builder.CreateStore(next, countAddress);
        builder.CreateCondBr(builder.CreateICmpEQ(next, builder.getInt64(0)), destroy, complete);
        builder.SetInsertPoint(destroy);
        llvm::Value* destroyAddress = builder.CreateStructGEP(
            ClosureObjectType(), closure, 3, "closure.destroy.address");
        llvm::Value* destroyFunction = builder.CreateLoad(
            builder.getPtrTy(), destroyAddress, "closure.destroy");
        builder.CreateCondBr(builder.CreateICmpNE(destroyFunction,
            llvm::ConstantPointerNull::get(builder.getPtrTy())), callDestroy, freeClosure);
        builder.SetInsertPoint(callDestroy);
        llvm::Value* environmentAddress = builder.CreateStructGEP(
            ClosureObjectType(), closure, 2, "closure.environment.address");
        llvm::Value* environment = builder.CreateLoad(
            builder.getPtrTy(), environmentAddress, "closure.environment");
        llvm::FunctionType* destroyType = llvm::FunctionType::get(
            builder.getVoidTy(), {builder.getPtrTy()}, false);
        builder.CreateCall(destroyType, destroyFunction, {environment});
        builder.CreateBr(freeClosure);
        builder.SetInsertPoint(freeClosure);
        builder.CreateCall(Free(), {closure});
        builder.CreateBr(complete);
        builder.SetInsertPoint(complete);
        builder.CreateRetVoid();
        builder.restoreIP(saved);
        return function;
    }

    llvm::Value* CodeGenerator::Impl::FunctionClosure(const Symbol& symbol) {
        if (const auto found = functionClosures.find(symbol.id);
            found != functionClosures.end()) return found->second;
        llvm::Function* target = module->getFunction(FunctionLinkName(symbol));
        if (!target) Fail("missing function value '" + symbol.name + "'");
        llvm::FunctionType* wrapperType = ClosureInvokeType(symbol.type, symbol.parameterTypes);
        const std::string wrapperName = "__absolute.closure.invoke." +
            EncodeLinkComponent(FunctionLinkName(symbol));
        llvm::Function* wrapper = llvm::Function::Create(wrapperType,
            llvm::Function::InternalLinkage, wrapperName, *module);
        wrapper->setCallingConv(llvm::CallingConv::C);
        const auto saved = builder.saveIP();
        llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", wrapper);
        builder.SetInsertPoint(entry);
        const unsigned returnOffset = AbiReturnOffset(symbol.type);
        std::vector<llvm::Value*> arguments;
        if (returnOffset != 0) arguments.push_back(wrapper->getArg(0));
        for (unsigned index = returnOffset + 1; index < wrapper->arg_size(); ++index)
            arguments.push_back(wrapper->getArg(index));
        llvm::CallInst* call = builder.CreateCall(target->getFunctionType(), target, arguments);
        if (target->getReturnType()->isVoidTy()) builder.CreateRetVoid();
        else builder.CreateRet(call);
        builder.restoreIP(saved);

        llvm::Constant* initializer = llvm::ConstantStruct::get(ClosureObjectType(), {
            llvm::ConstantInt::getSigned(builder.getInt64Ty(), -1), wrapper,
            llvm::ConstantPointerNull::get(builder.getPtrTy()),
            llvm::ConstantPointerNull::get(builder.getPtrTy())});
        auto* closure = new llvm::GlobalVariable(*module, ClosureObjectType(), true,
            llvm::GlobalValue::InternalLinkage, initializer,
            "__absolute.closure.value." + EncodeLinkComponent(FunctionLinkName(symbol)));
        functionClosures.emplace(symbol.id, closure);
        return closure;
    }

    llvm::Value* CodeGenerator::Impl::BuildArrayDescriptor(const ArrayView& view) {
        llvm::StructType* type = ArrayDescriptorType(view.typeName);
        llvm::Value* descriptor = llvm::UndefValue::get(type);
        descriptor = builder.CreateInsertValue(descriptor, view.address, {0}, "array.data");
        llvm::Value* owner = view.owner ? view.owner
            : llvm::ConstantPointerNull::get(builder.getPtrTy());
        descriptor = builder.CreateInsertValue(descriptor, owner, {1}, "array.owner");
        for (size_t index = 0; index < view.dimensions.size(); ++index)
            descriptor = builder.CreateInsertValue(
                descriptor, view.dimensions[index], {static_cast<unsigned>(index + 2)}, "array.dimension");
        return descriptor;
    }

    CodeGenerator::Impl::ArrayView CodeGenerator::Impl::ArrayViewFromValue(llvm::Value* descriptor, const std::string& typeName) {
        const size_t rank = ArrayRankName(typeName);
        if (rank == 0)
            Fail("array expression does not produce a descriptor");
        if (descriptor->getType()->isPointerTy()) {
            llvm::LoadInst* load = builder.CreateLoad(ArrayDescriptorType(typeName), descriptor, "array.descriptor.loaded");
            load->setMetadata(llvm::LLVMContext::MD_invariant_load, llvm::MDNode::get(context, {}));
            descriptor = load;
        }
        if (!descriptor->getType()->isStructTy())
            Fail("array expression does not produce a descriptor");
        ArrayView view;
        view.address = builder.CreateExtractValue(descriptor, {0}, "array.data");
        view.owner = builder.CreateExtractValue(descriptor, {1}, "array.owner");
        view.elementType = TypeFromName(ArrayElementTypeName(typeName, rank));
        view.typeName = typeName;
        for (size_t index = 0; index < rank; ++index)
            view.dimensions.push_back(builder.CreateExtractValue(
                descriptor, {static_cast<unsigned>(index + 2)}, "array.dimension"));
        return view;
    }

    CodeGenerator::Impl::ArrayView CodeGenerator::Impl::ViewOfArray(Expression* expression) {
        if (auto* identifier = dynamic_cast<IdentifierExpr*>(expression)) {
            if (Variable* variable = FindVariable(identifier->name)) {
                if (!variable->isArray) Fail("object is not an array");
                llvm::Value* owner = variable->arrayOwnerStorage
                    ? builder.CreateLoad(
                        builder.getPtrTy(), variable->arrayOwnerStorage,
                        identifier->name + ".array.owner")
                    : variable->arrayOwner;
                return {variable->address, variable->arrayElementType,
                    variable->typeName, variable->arrayDimensions, owner};
            }
        }
        const std::string typeName = SemanticType(expression);
        const bool outerAddressMode = addressMode;
        addressMode = false;
        llvm::Value* descriptor = Evaluate(expression);
        addressMode = outerAddressMode;
        return ArrayViewFromValue(descriptor, typeName);
    }

    void CodeGenerator::Impl::EmitOrExit(llvm::Value* condition, const std::string& name) {
        llvm::Function* function = CurrentFunction();
        if (!function) Fail("runtime check outside a function");
        if (const auto* constant = llvm::dyn_cast<llvm::ConstantInt>(condition);
            constant && !constant->isZero()) return;
        llvm::BasicBlock* success = llvm::BasicBlock::Create(context, name + ".success", function);
        llvm::BasicBlock* failure = llvm::BasicBlock::Create(context, name + ".failure", function);
        builder.CreateCondBr(condition, success, failure,
            llvm::MDBuilder(context).createBranchWeights(2000, 1));
        builder.SetInsertPoint(failure);
        const std::string message =
            name == "array.size"
                ? "Array size must be greater than zero"
                : name == "array.bounds"
                    ? "Array index out of bounds"
                    : name == "division.by.zero"
                        ? "Division by zero"
                        : name == "division.overflow"
                        ? "Division overflows: the most negative value divided by -1"
                        : name == "shift.amount"
                        ? "Shift amount is out of range: it must be less than the width of the shifted value"
                        : name.ends_with(".requires.owner")
                        ? "Ownership operation requires an owner argument"
                        : "Runtime safety check failed";
        builder.CreateCall(Puts(), {builder.CreateGlobalStringPtr(message, name + ".message")});
        builder.CreateCall(ExitFailure(), {builder.getInt32(1)});
        builder.CreateUnreachable();
        builder.SetInsertPoint(success);
    }

    llvm::Value* CodeGenerator::Impl::ArrayElementAddress(
        ArrayAccessExpr& expression, const ArrayView& view) {
        if (expression.indexes.size() > view.dimensions.size())
            Fail("array access provides too many dimensions");

        llvm::Value* offset = builder.getInt64(0);
        llvm::Value* oneDimensionalIndex = nullptr;
        for (size_t dimension = 0; dimension < expression.indexes.size(); ++dimension) {
            if (!expression.indexes[dimension]) Fail("array access requires an index");
            const bool outerAddressMode = addressMode;
            addressMode = false;
            llvm::Value* index = Evaluate(expression.indexes[dimension].get());
            addressMode = outerAddressMode;
            if (!index->getType()->isIntegerTy()) Fail("array index must be an integer");

            const std::string indexTypeName = SemanticType(expression.indexes[dimension].get());
            const bool unsignedIndex = indexTypeName.starts_with("uint") || indexTypeName == "char";
            llvm::Value* wideIndex = index->getType()->isIntegerTy(64)
                ? index
                : builder.CreateIntCast(index, builder.getInt64Ty(), !unsignedIndex, "array.index.wide");
            llvm::Value* valid = builder.CreateICmpULT(wideIndex, view.dimensions[dimension], "array.index.valid");
            EmitOrExit(valid, "array.bounds");
            if (expression.indexes.size() == 1 && view.dimensions.size() == 1) oneDimensionalIndex = index;
            if (dimension == 0) offset = wideIndex;
            else {
                offset = builder.CreateMul(offset, view.dimensions[dimension], "array.row.offset");
                offset = builder.CreateAdd(offset, wideIndex, "array.linear.offset");
            }
        }
        for (size_t dimension = expression.indexes.size(); dimension < view.dimensions.size(); ++dimension) {
            offset = builder.CreateMul(offset, view.dimensions[dimension], "array.sub.stride");
        }
        return builder.CreateInBoundsGEP(
            view.elementType, view.address,
            oneDimensionalIndex ? oneDimensionalIndex : offset, "array.element.address");
    }

    llvm::AllocaInst* CodeGenerator::Impl::CreateEntryAlloca(llvm::Function& function, llvm::Type* type, const std::string& name) {
        llvm::IRBuilder<> entryBuilder(&function.getEntryBlock(), function.getEntryBlock().begin());
        return entryBuilder.CreateAlloca(type, nullptr, name);
    }

    llvm::Value* CodeGenerator::Impl::Coerce(llvm::Value* source, llvm::Type* target) {
        return Coerce(source, target, {}, {});
    }

    llvm::Value* CodeGenerator::Impl::Coerce(llvm::Value* source, llvm::Type* target,
        const std::string& sourceTypeName, const std::string& targetTypeName) {
        if (!source) Fail("cannot convert an empty value");
        llvm::Type* sourceType = source->getType();
        if (sourceType == target) return source;
        const std::string& semanticSource =
            sourceTypeName.empty() ? currentValueType : sourceTypeName;
        const std::string sourceValueType = ValueReferenceBaseTypeName(semanticSource);
        const std::string targetValueType = ValueReferenceBaseTypeName(targetTypeName);
        const bool sourceUnsigned = sourceValueType.starts_with("uint") ||
            sourceValueType == "char" || sourceValueType == "bool";
        const bool targetUnsigned = targetValueType.starts_with("uint") ||
            targetValueType == "char" || targetValueType == "bool";

        if (target->isIntegerTy(64) && llvm::isa<llvm::ConstantPointerNull>(source))
            return builder.getInt64(0);

        if (sourceType->isIntegerTy() && target->isIntegerTy()) {
            return builder.CreateIntCast(source, target, !sourceUnsigned, "int.cast");
        }
        if (sourceType->isIntegerTy() && target->isFloatingPointTy()) {
            return sourceUnsigned
                ? builder.CreateUIToFP(source, target, "uint.to.fp")
                : builder.CreateSIToFP(source, target, "int.to.fp");
        }
        if (sourceType->isFloatingPointTy() && target->isIntegerTy()) {
            // Saturating, because the plain conversions are poison whenever the
            // value does not fit: `1e18 as int32` gave a different number on
            // each build, `inf as int32` produced a value that broke the
            // formatter it was passed to, and native and wasm disagreed on all
            // of them. The saturating intrinsics are defined everywhere -- NaN
            // becomes 0, and anything past an end clamps to it -- and lower to
            // the same instruction the target already uses for a checked
            // conversion. docs/implicit-conversions.md says so.
            return builder.CreateIntrinsic(
                targetUnsigned ? llvm::Intrinsic::fptoui_sat : llvm::Intrinsic::fptosi_sat,
                {target, sourceType}, {source}, nullptr,
                targetUnsigned ? "fp.to.uint" : "fp.to.int");
        }
        if (sourceType->isFloatingPointTy() && target->isFloatingPointTy()) {
            return builder.CreateFPCast(source, target, "fp.cast");
        }
        if (sourceType->isPointerTy() && target->isPointerTy()) {
            return builder.CreatePointerCast(source, target, "ptr.cast");
        }
        if (sourceType->isPointerTy() && target->isIntegerTy()) {
            return builder.CreatePtrToInt(source, target, "ptr.to.int");
        }
        if (sourceType->isIntegerTy() && target->isPointerTy()) {
            return builder.CreateIntToPtr(source, target, "int.to.ptr");
        }
        std::string sourceText;
        std::string targetText;
        llvm::raw_string_ostream(sourceText) << *sourceType;
        llvm::raw_string_ostream(targetText) << *target;
        Fail("incompatible value conversion from '" + semanticSource + "' (" +
            sourceText + ") to '" + targetValueType + "' (" + targetText + ")");
    }

    llvm::Type* CodeGenerator::Impl::CommonNumericType(llvm::Type* left, llvm::Type* right) {
        if (left->isDoubleTy() || right->isDoubleTy()) return builder.getDoubleTy();
        if (left->isFloatTy() || right->isFloatTy()) return builder.getFloatTy();
        if (left->isIntegerTy() && right->isIntegerTy()) {
            const unsigned bits = std::max(left->getIntegerBitWidth(), right->getIntegerBitWidth());
            return llvm::IntegerType::get(context, std::max(bits, 1U));
        }
        Fail("binary operator requires numeric operands");
    }

    llvm::Value* CodeGenerator::Impl::AsCondition(llvm::Value* condition) {
        llvm::Type* type = condition->getType();
        if (IsManagedPointerTypeName(currentValueType) && type->isIntegerTy(64))
            return builder.CreateCall(ManagedValid(), {condition}, "managed.valid");
        if (type->isIntegerTy(1)) return condition;
        if (type->isIntegerTy()) {
            return builder.CreateICmpNE(condition, llvm::ConstantInt::get(type, 0), "condition");
        }
        if (type->isFloatingPointTy()) {
            return builder.CreateFCmpONE(condition, llvm::ConstantFP::get(type, 0.0), "condition");
        }
        if (type->isPointerTy()) {
            return builder.CreateIsNotNull(condition, "condition");
        }
        Fail("value cannot be used as a condition");
    }

    llvm::Value* CodeGenerator::Impl::ApplyBinary(const std::string& op, llvm::Value* left, llvm::Value* right) {
        return ApplyBinary(op, left, right, {}, {});
    }

    llvm::Value* CodeGenerator::Impl::ApplyBinary(
        const std::string& op, llvm::Value* left, llvm::Value* right,
        const std::string& leftTypeName, const std::string& rightTypeName) {
        if (op == "&&" || op == "||") {
            left = AsCondition(left);
            right = AsCondition(right);
            return op == "&&" ? builder.CreateAnd(left, right, "logical.and")
                                : builder.CreateOr(left, right, "logical.or");
        }

        llvm::Type* type = CommonNumericType(left->getType(), right->getType());
        std::string commonTypeName;
        if (leftTypeName == "double" || rightTypeName == "double") commonTypeName = "double";
        else if (leftTypeName == "float" || rightTypeName == "float") commonTypeName = "float";
        else if (leftTypeName == "int64" || rightTypeName == "int64" ||
            leftTypeName == "uint64" || rightTypeName == "uint64") commonTypeName = "int64";
        else if (!leftTypeName.empty() || !rightTypeName.empty()) commonTypeName = "int32";

        // An LLVM integer type carries no signedness, so the instruction has to
        // be chosen from the declared types. Without this every unsigned value
        // was divided, compared and right-shifted as a signed one, which is not
        // an edge case: 4294967295u > 1u came out false, (uint8)200 > 100 came
        // out false, and 4294967295u / 2 came out zero.
        //
        // The same ladder the analyzer uses, so the two agree on the result
        // type: int64 covers every uint32 and keeps the operation signed,
        // nothing signed covers uint64, and at equal rank unsigned wins.
        if (!commonTypeName.empty() && commonTypeName != "double" &&
            commonTypeName != "float") {
            if (leftTypeName == "uint64" || rightTypeName == "uint64")
                commonTypeName = "uint64";
            else if (leftTypeName == "int64" || rightTypeName == "int64")
                commonTypeName = "int64";
            else if (leftTypeName == "uint32" || rightTypeName == "uint32" ||
                leftTypeName.starts_with("uint") || rightTypeName.starts_with("uint"))
                commonTypeName = "uint32";
        }
        const bool unsignedOperation = commonTypeName.starts_with("uint");
        left = Coerce(left, type, leftTypeName, commonTypeName);
        right = Coerce(right, type, rightTypeName, commonTypeName);
        const bool floating = type->isFloatingPointTy();

        if (op == "+") return floating ? builder.CreateFAdd(left, right, "add") : builder.CreateAdd(left, right, "add");
        if (op == "-") return floating ? builder.CreateFSub(left, right, "sub") : builder.CreateSub(left, right, "sub");
        if (op == "*") return floating ? builder.CreateFMul(left, right, "mul") : builder.CreateMul(left, right, "mul");
        if (op == "/" || op == "%") {
            // Integer division by zero is immediate undefined behavior in LLVM,
            // so the optimizer may fold the result to anything and the program
            // keeps running on a garbage value. Reject a divisor that is
            // provably zero and check the rest at runtime, the way an array
            // index is checked.
            if (!floating) {
                if (const auto* divisor = llvm::dyn_cast<llvm::ConstantInt>(right);
                    divisor && divisor->isZero())
                    Fail(op == "/" ? "division by zero" : "remainder by zero");
                EmitOrExit(
                    builder.CreateICmpNE(right, llvm::ConstantInt::get(type, 0),
                        "divisor.not.zero"),
                    "division.by.zero");

                // The other undefined case: the most negative value divided by
                // -1 has no representable quotient, so LLVM leaves it undefined
                // and the result was observably garbage rather than a wrap.
                // Unsigned division has no such case, so the guard is only
                // emitted where it can happen.
                if (!unsignedOperation && type->isIntegerTy()) {
                    llvm::Constant* mostNegative = llvm::ConstantInt::get(
                        type, llvm::APInt::getSignedMinValue(
                            type->getIntegerBitWidth()));
                    llvm::Value* leftIsMin = builder.CreateICmpEQ(
                        left, mostNegative, "dividend.is.min");
                    llvm::Value* rightIsMinusOne = builder.CreateICmpEQ(
                        right, llvm::ConstantInt::getSigned(type, -1),
                        "divisor.is.minus.one");
                    EmitOrExit(
                        builder.CreateNot(
                            builder.CreateAnd(leftIsMin, rightIsMinusOne,
                                "division.overflows"),
                            "division.in.range"),
                        "division.overflow");
                }
            }
            if (op == "/") {
                if (floating) return builder.CreateFDiv(left, right, "div");
                return unsignedOperation ? builder.CreateUDiv(left, right, "div")
                    : builder.CreateSDiv(left, right, "div");
            }
            if (floating) return builder.CreateFRem(left, right, "rem");
            return unsignedOperation ? builder.CreateURem(left, right, "rem")
                : builder.CreateSRem(left, right, "rem");
        }

        if (op == "==") return floating ? builder.CreateFCmpOEQ(left, right, "equal") : builder.CreateICmpEQ(left, right, "equal");
        // Unordered, so that `a != b` stays the negation of `a == b` when
        // either side is NaN. The ordered form is false whenever an operand is
        // NaN, which made `nan != nan` false while `nan == nan` was also false
        // -- both answers wrong at once, and silently.
        if (op == "!=") return floating ? builder.CreateFCmpUNE(left, right, "not.equal") : builder.CreateICmpNE(left, right, "not.equal");
        if (op == "<") {
            if (floating) return builder.CreateFCmpOLT(left, right, "less");
            return unsignedOperation ? builder.CreateICmpULT(left, right, "less")
                : builder.CreateICmpSLT(left, right, "less");
        }
        if (op == "<=") {
            if (floating) return builder.CreateFCmpOLE(left, right, "less.equal");
            return unsignedOperation ? builder.CreateICmpULE(left, right, "less.equal")
                : builder.CreateICmpSLE(left, right, "less.equal");
        }
        if (op == ">") {
            if (floating) return builder.CreateFCmpOGT(left, right, "greater");
            return unsignedOperation ? builder.CreateICmpUGT(left, right, "greater")
                : builder.CreateICmpSGT(left, right, "greater");
        }
        if (op == ">=") {
            if (floating) return builder.CreateFCmpOGE(left, right, "greater.equal");
            return unsignedOperation ? builder.CreateICmpUGE(left, right, "greater.equal")
                : builder.CreateICmpSGE(left, right, "greater.equal");
        }

        if (!type->isIntegerTy()) Fail("bitwise operator requires integer operands");
        if (op == "&") return builder.CreateAnd(left, right, "bit.and");
        if (op == "|") return builder.CreateOr(left, right, "bit.or");
        if (op == "^") return builder.CreateXor(left, right, "bit.xor");
        if (op == "<<" || op == ">>") {
            // A shift by the width of the shifted value, or more, is undefined
            // in LLVM the same way division by zero is: not a wrap, not a zero,
            // but a value the optimizer may choose freely. `1 << 32` printed
            // -1780665664 and `1 << 40` produced something the formatter could
            // not print at all. A written-out amount is refused by the analyzer,
            // where the message has a line; a computed one is checked here.
            const unsigned width = type->getIntegerBitWidth();
            llvm::Constant* limit = llvm::ConstantInt::get(type, width);
            if (const auto* amount = llvm::dyn_cast<llvm::ConstantInt>(right);
                amount && amount->getValue().uge(width))
                Fail("shift amount is not less than the width of the shifted value");
            // Unsigned, so a negative amount is caught by the same comparison.
            EmitOrExit(builder.CreateICmpULT(right, limit, "shift.amount.valid"),
                "shift.amount");
            if (op == "<<") return builder.CreateShl(left, right, "shift.left");
            return unsignedOperation ? builder.CreateLShr(left, right, "shift.right")
                : builder.CreateAShr(left, right, "shift.right");
        }
        Fail("unsupported binary operator '" + op + "'");
    }

    llvm::Value* CodeGenerator::Impl::One(llvm::Type* type) {
        if (type->isFloatingPointTy()) return llvm::ConstantFP::get(type, 1.0);
        if (type->isIntegerTy()) return llvm::ConstantInt::get(type, 1);
        Fail("increment requires a numeric operand");
    }

    llvm::Value* CodeGenerator::Impl::PointerOffset(llvm::Value* pointer,
        const std::string& pointerTypeName, llvm::Value* index, bool subtract) {
        llvm::Value* scaled = Coerce(index, builder.getInt64Ty());
        if (subtract) scaled = builder.CreateNeg(scaled, "pointer.negative.offset");
        return builder.CreateGEP(TypeFromName(PointerPointeeName(pointerTypeName)),
            pointer, scaled, "pointer.offset");
    }

    void CodeGenerator::Impl::BranchIfNeeded(llvm::BasicBlock* target) {
        llvm::BasicBlock* block = builder.GetInsertBlock();
        if (block && !block->getTerminator()) builder.CreateBr(target);
    }

    llvm::Function* CodeGenerator::Impl::CurrentFunction() {
        llvm::BasicBlock* block = builder.GetInsertBlock();
        return block ? block->getParent() : nullptr;
    }

    std::string CodeGenerator::Impl::Qualify(const std::string& name) const {
        if (name.empty() || currentNamespace.empty() || name.find('.') != std::string::npos) return name;
        return currentNamespace + "." + name;
    }

    std::string CodeGenerator::Impl::FunctionLinkName(const Symbol& symbol) const {
        if (symbol.kind == SymbolKind::Method)
            return CallableKey(symbol.name, symbol.parameterTypes);
        if (symbol.genericOrigin != InvalidSymbolId)
            return CallableKey(symbol.name, symbol.parameterTypes);
        if (symbol.externalFunction || symbol.exportedFunction || symbol.name == "main" || !analyzer ||
            analyzer->FunctionOverloadCount(symbol.name) <= 1) {
            if (symbol.externalFunction || symbol.exportedFunction)
                return symbol.name.substr(symbol.name.rfind('.') + 1);
            // `main` owns its C symbol; everything else that would shadow a
            // runtime entry point is moved out of the C namespace. The leading
            // dot cannot appear in a C identifier, so the result is private to
            // this module and can never satisfy a call from libc or the
            // Absolute runtime.
            if (symbol.name != "main" && IsReservedRuntimeSymbol(symbol.name))
                return ".absolute." + CallableKey(symbol.name, symbol.parameterTypes);
            return symbol.name;
        }
        return CallableKey(symbol.name, symbol.parameterTypes);
    }

    std::string CodeGenerator::Impl::ResolvedName(Expression* expression) const {
        if (analyzer && expression) {
            if (const ExpressionInfo* info = analyzer->GetExpressionInfo(*expression)) {
                if (const Symbol* symbol = analyzer->GetSymbol(info->symbol)) {
                    if (symbol->kind == SymbolKind::Function && symbol->genericOrigin != InvalidSymbolId &&
                        !currentGenericSubstitutions.empty()) {
                        std::vector<std::string> substitutedArgs = symbol->genericArguments;
                        for (std::string& arg : substitutedArgs)
                            arg = SubstituteCodegenType(arg, currentGenericSubstitutions);
                        const SymbolId specId = const_cast<Analyzer*>(analyzer)->InstantiateGenericFunction(
                            symbol->genericOrigin, substitutedArgs);
                        if (const Symbol* specSymbol = analyzer->GetSymbol(specId))
                            return FunctionLinkName(*specSymbol);
                    }
                    return FunctionLinkName(*symbol);
                }
            }
        }
        std::string name = IdentifierName(expression);
        const auto linked = functionLinkNames.find(name);
        return linked == functionLinkNames.end() ? name : linked->second;
    }

    bool CodeGenerator::Impl::IsBuiltinFunction(const std::string& name) const {
        return name == "print" || name == "println" || name == "format" ||
            name == "toString" || name == "assert" || name == "copy" ||
            name == "move" || name == "isOwner" || name == "debugBreak" ||
            name == "adoptRaw" || name == "retainRaw" || name == "borrowRaw" || name == "share" ||
            name == "unsafeArrayGet" || name == "unsafeArraySet" ||
            name == "unsafeArrayData" || name == "unsafeArrayCopy" ||
            name == "unsafeArrayMove" || name == "unsafeArrayTake" ||
            name == "unsafeArrayDrop" ||
            name == "seal" || name == "unseal" ||
            name == "load" || name == "isLoaded" || name == "loadError" ||
            name == "taskGroupAdd" || name == "tuple";
    }


    llvm::FunctionCallee CodeGenerator::Impl::DoubleText() {
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getInt32Ty(),
            {builder.getDoubleTy(), builder.getPtrTy(), builder.getInt32Ty()}, false);
        return module->getOrInsertFunction("absolute_double_text", type);
    }

    llvm::FunctionCallee CodeGenerator::Impl::FloatText() {
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getInt32Ty(),
            {builder.getFloatTy(), builder.getPtrTy(), builder.getInt32Ty()}, false);
        return module->getOrInsertFunction("absolute_float_text", type);
    }

    llvm::FunctionCallee CodeGenerator::Impl::Printf() {
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getInt32Ty(), {builder.getPtrTy()}, true);
        return module->getOrInsertFunction("printf", type);
    }

    llvm::FunctionCallee CodeGenerator::Impl::Snprintf() {
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getInt32Ty(), {builder.getPtrTy(), builder.getInt64Ty(), builder.getPtrTy()}, true);
        return module->getOrInsertFunction("snprintf", type);
    }

    llvm::FunctionCallee CodeGenerator::Impl::StringAlloc() {
        // The size is a `size_t`, which is the target's pointer width and not
        // always 64 bits: on wasm32 it is 32, and a declaration that disagrees
        // with the runtime's definition links to a trapping stub rather than to
        // the function.
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getPtrTy(), {SizeType()}, false);
        return module->getOrInsertFunction("absolute_string_alloc", type);
    }

    llvm::FunctionCallee CodeGenerator::Impl::StringRetain() {
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getPtrTy(), {builder.getPtrTy()}, false);
        return module->getOrInsertFunction("absolute_string_retain", type);
    }

    llvm::FunctionCallee CodeGenerator::Impl::StringRelease() {
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getVoidTy(), {builder.getPtrTy()}, false);
        return module->getOrInsertFunction("absolute_string_release", type);
    }

    llvm::IntegerType* CodeGenerator::Impl::SizeType() {
        return builder.getIntNTy(module->getDataLayout().getPointerSizeInBits());
    }

    llvm::FunctionCallee CodeGenerator::Impl::Malloc() {
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getPtrTy(), {builder.getInt64Ty()}, false);
        return module->getOrInsertFunction("malloc", type);
    }

    llvm::FunctionCallee CodeGenerator::Impl::Calloc() {
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getPtrTy(), {builder.getInt64Ty(), builder.getInt64Ty()}, false);
        return module->getOrInsertFunction("calloc", type);
    }

    llvm::FunctionCallee CodeGenerator::Impl::Free() {
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getVoidTy(), {builder.getPtrTy()}, false);
        return module->getOrInsertFunction("free", type);
    }

    llvm::FunctionCallee CodeGenerator::Impl::LoadDynamicLibrary() {
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getInt32Ty(), {builder.getPtrTy()}, false);
        return module->getOrInsertFunction("absolute_load_library", type);
    }

    llvm::FunctionCallee CodeGenerator::Impl::IsDynamicLibraryLoaded() {
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getInt32Ty(), {builder.getPtrTy()}, false);
        return module->getOrInsertFunction("absolute_library_is_loaded", type);
    }

    llvm::FunctionCallee CodeGenerator::Impl::DynamicLibraryError() {
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getPtrTy(), {}, false);
        return module->getOrInsertFunction("absolute_load_error", type);
    }

    llvm::Value* CodeGenerator::Impl::EncodeTaskSlot(llvm::IRBuilder<>& targetBuilder, llvm::Value* source) {
        llvm::Type* type = source->getType();
        if (type->isPointerTy())
            return targetBuilder.CreatePtrToInt(source, targetBuilder.getInt64Ty(), "task.slot.ptr");
        if (type->isIntegerTy())
            return targetBuilder.CreateIntCast(source, targetBuilder.getInt64Ty(), false, "task.slot.int");
        if (type->isFloatTy()) {
            llvm::Value* bits = targetBuilder.CreateBitCast(source, targetBuilder.getInt32Ty(), "task.slot.float");
            return targetBuilder.CreateZExt(bits, targetBuilder.getInt64Ty(), "task.slot.float64");
        }
        if (type->isDoubleTy())
            return targetBuilder.CreateBitCast(source, targetBuilder.getInt64Ty(), "task.slot.double");
        Fail("async tasks only support primitive and pointer values");
    }

    llvm::Value* CodeGenerator::Impl::DecodeTaskSlot(
        llvm::IRBuilder<>& targetBuilder, llvm::Value* slot, llvm::Type* target) {
        if (target->isPointerTy())
            return targetBuilder.CreateIntToPtr(slot, target, "task.value.ptr");
        if (target->isIntegerTy())
            return targetBuilder.CreateIntCast(slot, target, false, "task.value.int");
        if (target->isFloatTy()) {
            llvm::Value* bits = targetBuilder.CreateTrunc(slot, targetBuilder.getInt32Ty(), "task.value.float32");
            return targetBuilder.CreateBitCast(bits, target, "task.value.float");
        }
        if (target->isDoubleTy())
            return targetBuilder.CreateBitCast(slot, target, "task.value.double");
        Fail("async tasks only support primitive and pointer values");
    }

    llvm::Value* CodeGenerator::Impl::EmitSpawn(FunctionCallExpr& call) {
        const std::string name = ResolvedName(&call);
        const ExpressionInfo* callInfo = analyzer ? analyzer->GetExpressionInfo(call) : nullptr;
        const Symbol* selected = callInfo ? analyzer->GetSymbol(callInfo->symbol) : nullptr;
        const bool instanceMethod = selected && selected->kind == SymbolKind::Method &&
            !selected->isStatic;
        llvm::Function* target = nullptr;
        llvm::FunctionType* targetType = nullptr;
        llvm::Value* receiver = nullptr;
        llvm::StructType* virtualOwnerType = nullptr;
        std::optional<unsigned> virtualSlot;

        if (instanceMethod) {
            auto* member = dynamic_cast<MemberAccessExpr*>(call.base.get());
            const std::string receiverType = member
                ? SemanticType(member->base.get()) : currentClassName;
            const std::string ownerName = ClassNameFromType(receiverType);
            receiver = member ? ObjectPointer(member->base.get(), receiverType) : currentThis;
            if (!receiver) Fail("async instance method requires a receiver");
            const std::string methodName = member
                ? member->member : IdentifierName(call.base.get());
            const std::string methodKey = CallableKey(methodName, selected->parameterTypes);
            if (auto found = classes.find(ownerName); found != classes.end()) {
                const auto method = found->second.methods.find(methodKey);
                if (method == found->second.methods.end())
                    Fail("class '" + ownerName + "' has no async method '" + methodName + "'");
                targetType = MethodFunctionType(method->second);
                target = module->getFunction(method->second.linkName);
                virtualSlot = method->second.virtualSlot;
                virtualOwnerType = found->second.llvmType;
            }
            else if (auto found = structs.find(ownerName); found != structs.end()) {
                const auto method = found->second.methods.find(methodKey);
                if (method == found->second.methods.end())
                    Fail("struct '" + ownerName + "' has no async method '" + methodName + "'");
                targetType = MethodFunctionType(method->second);
                target = module->getFunction(method->second.linkName);
            }
            else Fail("async receiver type '" + ownerName + "' is not a class or struct");
        }
        else {
            target = module->getFunction(name);
            if (target) targetType = target->getFunctionType();
        }
        if (!target || !targetType) Fail("unknown async callable '" + name + "'");

        const size_t capturedCount = call.arguments.size() + (instanceMethod ? 1U : 0U);
        if (targetType->getNumParams() != capturedCount)
            Fail("invalid async argument count for '" + name + "'");

        const std::uint64_t slotCount = static_cast<std::uint64_t>(capturedCount) + 1;
        llvm::Value* contextPointer = builder.CreateCall(
            Malloc(), {builder.getInt64(slotCount * 8)}, "task.context");

        size_t argumentIndex = 0;
        if (instanceMethod) {
            llvm::Value* slot = builder.CreateGEP(
                builder.getInt64Ty(), contextPointer, builder.getInt64(1), "task.receiver.slot");
            builder.CreateStore(EncodeTaskSlot(builder, receiver), slot);
            argumentIndex = 1;
        }
        for (const auto& argument : call.arguments) {
            llvm::Value* argumentValue = Evaluate(argument.get());
            argumentValue = Coerce(argumentValue,
                targetType->getParamType(static_cast<unsigned>(argumentIndex)));
            llvm::Value* slot = builder.CreateGEP(
                builder.getInt64Ty(), contextPointer,
                builder.getInt64(static_cast<std::uint64_t>(argumentIndex + 1)), "task.argument.slot");
            builder.CreateStore(EncodeTaskSlot(builder, argumentValue), slot);
            ++argumentIndex;
        }

        llvm::FunctionType* thunkType = llvm::FunctionType::get(
            builder.getVoidTy(), {builder.getPtrTy()}, false);
        llvm::Function* thunk = llvm::Function::Create(
            thunkType, llvm::Function::InternalLinkage,
            "absolute.task.thunk." + std::to_string(taskThunkCounter++), *module);
        llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", thunk);
        llvm::IRBuilder<> thunkBuilder(entry);
        llvm::Value* thunkContext = thunk->getArg(0);

        std::vector<llvm::Value*> thunkArguments;
        thunkArguments.reserve(capturedCount);
        for (size_t index = 0; index < capturedCount; ++index) {
            llvm::Value* slot = thunkBuilder.CreateGEP(
                thunkBuilder.getInt64Ty(), thunkContext,
                thunkBuilder.getInt64(static_cast<std::uint64_t>(index + 1)), "task.argument.slot");
            llvm::Value* encoded = thunkBuilder.CreateLoad(
                thunkBuilder.getInt64Ty(), slot, "task.argument");
            thunkArguments.push_back(DecodeTaskSlot(
                thunkBuilder, encoded, targetType->getParamType(static_cast<unsigned>(index))));
        }
        llvm::Value* callee = target;
        if (virtualSlot) {
            llvm::Value* vtableAddress = thunkBuilder.CreateStructGEP(
                virtualOwnerType, thunkArguments.front(), 0, "task.vtable.address");
            llvm::Value* vtable = thunkBuilder.CreateLoad(
                thunkBuilder.getPtrTy(), vtableAddress, "task.vtable");
            llvm::Value* slot = thunkBuilder.CreateGEP(
                thunkBuilder.getPtrTy(), vtable, thunkBuilder.getInt64(*virtualSlot),
                "task.virtual.slot");
            callee = thunkBuilder.CreateLoad(
                thunkBuilder.getPtrTy(), slot, "task.virtual.method");
        }
        llvm::CallInst* result = thunkBuilder.CreateCall(targetType, callee, thunkArguments,
            targetType->getReturnType()->isVoidTy() ? "" : "task.result");
        if (!targetType->getReturnType()->isVoidTy()) {
            llvm::Value* resultSlot = thunkBuilder.CreateGEP(
                thunkBuilder.getInt64Ty(), thunkContext, thunkBuilder.getInt64(0), "task.result.slot");
            thunkBuilder.CreateStore(EncodeTaskSlot(thunkBuilder, result), resultSlot);
        }
        thunkBuilder.CreateRetVoid();

        std::int32_t core = -1;
        std::int32_t priority = 0;
        std::string role;
        const auto applyOptions = [&](const Attribute* attribute) {
            if (!attribute) return;
            for (const AttributeArgument& argument : attribute->arguments) {
                if (argument.name == "core") core = static_cast<std::int32_t>(
                    std::stoll(argument.value.text));
                else if (argument.name == "priority") priority = static_cast<std::int32_t>(
                    std::stoll(argument.value.text));
                else if (argument.name == "role") {
                    role = argument.value.text;
                    if (role.size() >= 2 && role.front() == '"' && role.back() == '"')
                        role = role.substr(1, role.size() - 2);
                }
            }
        };
        if (analyzer) {
            const ExpressionInfo* info = analyzer->GetExpressionInfo(call);
            FunctionDeclStmt* declaration = info
                ? analyzer->FunctionDeclaration(info->symbol) : nullptr;
            if (declaration) applyOptions(declaration->FindAttribute("task"));
        }
        applyOptions(currentSpawnAttribute);
        llvm::Value* roleValue = role.empty()
            ? static_cast<llvm::Value*>(llvm::ConstantPointerNull::get(builder.getPtrTy()))
            : builder.CreateGlobalStringPtr(role, "task.role");
        return builder.CreateCall(TaskSpawn(), {thunk, contextPointer,
            builder.getInt32(core), builder.getInt32(priority), roleValue}, "task.handle");
    }

    llvm::Value* CodeGenerator::Impl::EmitAwait(PrefixUnaryExpr& expression) {
        llvm::Value* handle = Evaluate(expression.operand.get());
        llvm::Value* contextPointer = builder.CreateCall(TaskAwait(), {handle}, "task.completed.context");
        // Awaiting a task held in a variable consumes it, so the variable is
        // cleared and its scope does not destroy the handle a second time.
        // Only a variable: `IdentifierName` walks an expression down to the
        // identifier it is built on, so `await spawn inner(2)` answered
        // "inner" -- the callee -- and the lookup failed on a name that was
        // never a variable, reporting an internal condition for a line the
        // author had every reason to write. A task nobody named has nobody to
        // clear.
        if (auto* named = dynamic_cast<IdentifierExpr*>(expression.operand.get())) {
            if (Variable* task = FindVariable(named->name))
                builder.CreateStore(
                    llvm::ConstantPointerNull::get(builder.getPtrTy()), task->address);
        }

        llvm::Function* function = CurrentFunction();
        llvm::Value* pending = builder.CreateCall(ErrorPending(), {}, "await.error.pending");
        llvm::BasicBlock* errorBlock = llvm::BasicBlock::Create(context, "await.error", function);
        llvm::BasicBlock* continueBlock = llvm::BasicBlock::Create(context, "await.continue", function);
        builder.CreateCondBr(pending, errorBlock, continueBlock);
        builder.SetInsertPoint(errorBlock);
        builder.CreateCall(Free(), {contextPointer});
        EmitExceptionPropagation();
        builder.SetInsertPoint(continueBlock);

        const std::string resultTypeName = SemanticType(&expression);
        if (resultTypeName == "void") {
            builder.CreateCall(Free(), {contextPointer});
            return nullptr;
        }
        llvm::Value* encoded = builder.CreateLoad(
            builder.getInt64Ty(), contextPointer, "task.result.encoded");
        llvm::Value* result = DecodeTaskSlot(builder, encoded, TypeFromName(resultTypeName));
        builder.CreateCall(Free(), {contextPointer});
        return result;
    }

    llvm::FunctionCallee CodeGenerator::Impl::ManagedCreate() {
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getInt64Ty(), {builder.getInt64Ty()}, false);
        return module->getOrInsertFunction("absolute_managed_create", type);
    }

    llvm::FunctionCallee CodeGenerator::Impl::ManagedSetType() {
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getVoidTy(), {builder.getInt64Ty(), builder.getInt64Ty()}, false);
        return module->getOrInsertFunction("absolute_managed_set_type", type);
    }

    llvm::FunctionCallee CodeGenerator::Impl::ManagedType() {
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getInt64Ty(), {builder.getInt64Ty()}, false);
        return module->getOrInsertFunction("absolute_managed_type", type);
    }

    llvm::FunctionCallee CodeGenerator::Impl::ManagedGet(bool requireValid) {
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getPtrTy(), {builder.getInt64Ty()}, false);
        return module->getOrInsertFunction(
            requireValid ? "absolute_managed_require" : "absolute_managed_get", type);
    }

    llvm::Value* CodeGenerator::Impl::EmitManagedGet(llvm::Value* handle, bool requireValid) {
        // Managed slot storage is owned by the runtime and may grow on another
        // thread. Keep generated code out of the runtime's container internals;
        // local unchanged owners still use their cached pointee and never reach
        // this synchronized lookup.
        return builder.CreateCall(
            ManagedGet(requireValid), {handle}, "managed.pointee");
    }


    llvm::FunctionCallee CodeGenerator::Impl::ManagedValid() {
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getInt1Ty(), {builder.getInt64Ty()}, false);
        return module->getOrInsertFunction("absolute_managed_valid", type);
    }

    llvm::FunctionCallee CodeGenerator::Impl::ManagedDestroy() {
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getVoidTy(), {builder.getInt64Ty()}, false);
        return module->getOrInsertFunction("absolute_managed_destroy", type);
    }

    std::uint64_t CodeGenerator::Impl::SizeOfTypeName(const std::string& name) {
        if (name == "int8" || name == "uint8" || name == "char" || name == "bool") return 1;
        if (name == "int16" || name == "uint16") return 2;
        if (name == "int32" || name == "uint32" || name == "float") return 4;
        if (name == "int64" || name == "uint64" || name == "double" || name == "string" ||
            IsPointerTypeName(name)) return 8;
        // A closure value is one pointer to the closure object, the same as
        // every other handle here. It is asked for by name rather than left to
        // the pointer test above, which reads type *names* and does not know
        // that `func<...>` is one -- so an array of callbacks could not be
        // allocated at all, while a field of the same type always could.
        std::string closureReturn;
        std::vector<std::string> closureParameters;
        if (ParseCodegenFunctionType(name, closureReturn, closureParameters)) return 8;
        std::string genericBase;
        std::vector<std::string> genericArguments;
        if (ParseCodegenGenericType(name, genericBase, genericArguments) &&
            genericBase == "tuple")
            return module->getDataLayout().getTypeAllocSize(
                TypeFromName(name)).getFixedValue();
        if (classes.contains(name)) {
            FinalizeClass(name);
            return module->getDataLayout().getTypeAllocSize(classes.at(name).llvmType).getFixedValue();
        }
        if (structs.contains(name)) {
            FinalizeStruct(name);
            return module->getDataLayout().getTypeAllocSize(structs.at(name).llvmType).getFixedValue();
        }
        Fail("cannot determine allocation size of '" + name + "'");
    }

    llvm::FunctionCallee CodeGenerator::Impl::Abort() {
        llvm::FunctionType* type = llvm::FunctionType::get(builder.getVoidTy(), {}, false);
        return module->getOrInsertFunction("abort", type);
    }

    llvm::FunctionCallee CodeGenerator::Impl::Puts() {
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getInt32Ty(), {builder.getPtrTy()}, false);
        return module->getOrInsertFunction("puts", type);
    }

    llvm::FunctionCallee CodeGenerator::Impl::ExitFailure() {
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getVoidTy(), {builder.getInt32Ty()}, false);
        llvm::FunctionCallee callee = module->getOrInsertFunction("exit", type);
        if (auto* exitFunc = llvm::dyn_cast<llvm::Function>(callee.getCallee())) {
            exitFunc->addFnAttr(llvm::Attribute::NoReturn);
            exitFunc->addFnAttr(llvm::Attribute::Cold);
        }
        return callee;
    }

    std::string CodeGenerator::Impl::SemanticType(Expression* expression) const {
        if (!analyzer || !expression) return {};
        const ExpressionInfo* info = analyzer->GetExpressionInfo(*expression);
        return info ? SubstituteCodegenType(info->type, currentGenericSubstitutions) : std::string{};
    }

    CodeGenerator::Impl::PrintableValue CodeGenerator::Impl::PreparePrintable(llvm::Value* source, Expression* expression) {
        // The same rule as a call argument: printing borrows the bytes, it does
        // not take them, so a string made only to be printed is released with
        // the statement.
        RegisterIfFreshString(expression, source);
        if (!source) Fail("cannot print an empty value");
        llvm::Type* type = source->getType();
        const std::string semanticType = SemanticType(expression);
        if (semanticType == "bool" || type->isIntegerTy(1)) {
            llvm::Value* trueText = EmitStringConstant("true", "bool.true");
            llvm::Value* falseText = EmitStringConstant("false", "bool.false");
            return {"%s", builder.CreateSelect(source, trueText, falseText, "bool.text")};
        }
        if (semanticType == "char") {
            return {"%c", builder.CreateZExt(source, builder.getInt32Ty(), "char.promoted")};
        }
        if (type->isIntegerTy()) {
            const bool unsignedInteger = semanticType.starts_with("uint");
            if (type->getIntegerBitWidth() < 32) {
                source = unsignedInteger
                    ? builder.CreateZExt(source, builder.getInt32Ty(), "integer.promoted")
                    : builder.CreateSExt(source, builder.getInt32Ty(), "integer.promoted");
            }
            if (type->getIntegerBitWidth() > 32)
                return {unsignedInteger ? "%llu" : "%lld", source};
            return {unsignedInteger ? "%u" : "%d", source};
        }
        // A real number becomes text before it reaches printf, through the
        // runtime's shared routine, and is printed as a string. "%g" is six
        // significant digits of the value rather than the value, and the wasm
        // shim's freestanding formatter has no float directive at all -- it
        // printed the two characters "%g". One routine, one answer on both
        // targets. docs/known-defects.md section 22.
        if (type->isFloatTy() || type->isDoubleTy()) {
            llvm::Value* buffer = CreateEntryAlloca(*CurrentFunction(),
                llvm::ArrayType::get(builder.getInt8Ty(), kRealTextCapacity),
                "real.text");
            builder.CreateCall(type->isFloatTy() ? FloatText() : DoubleText(),
                {source, buffer, builder.getInt32(kRealTextCapacity)});
            return {"%s", buffer};
        }
        if (type->isPointerTy()) {
            llvm::Value* nullText = EmitStringConstant("<null>", "null.text");
            llvm::Value* safeText = builder.CreateSelect(
                builder.CreateIsNull(source, "string.is.null"), nullText, source, "safe.string");
            return {"%s", safeText};
        }
        Fail("unsupported printable LLVM type");
    }

    llvm::CallInst* CodeGenerator::Impl::EmitPrintf(const std::string& format, const std::vector<PrintableValue>& values) {
        std::vector<llvm::Value*> arguments;
        arguments.reserve(values.size() + 1);
        arguments.push_back(builder.CreateGlobalStringPtr(format, "print.format"));
        for (const PrintableValue& printable : values) arguments.push_back(printable.value);
        return builder.CreateCall(Printf(), arguments, "print.result");
    }

    std::string CodeGenerator::Impl::BuildFormat(const std::string& source, const std::vector<PrintableValue>& values) {
        std::string result;
        size_t valueIndex = 0;
        for (size_t index = 0; index < source.size(); ++index) {
            const char current = source[index];
            if (current == '%') {
                result += "%%";
            }
            else if (current == '{') {
                if (index + 1 < source.size() && source[index + 1] == '{') {
                    result += '{';
                    ++index;
                }
                else if (index + 1 < source.size() && source[index + 1] == '}') {
                    if (valueIndex >= values.size()) Fail("format has too few values");
                    result += values[valueIndex++].specifier;
                    ++index;
                }
                else Fail("format contains an unmatched '{'");
            }
            else if (current == '}') {
                if (index + 1 < source.size() && source[index + 1] == '}') {
                    result += '}';
                    ++index;
                }
                else Fail("format contains an unmatched '}'");
            }
            else result += current;
        }
        if (valueIndex != values.size()) Fail("format has too many values");
        return result;
    }

    llvm::Value* CodeGenerator::Impl::EmitFormat(const std::string& format, const std::vector<PrintableValue>& values) {
        llvm::Value* formatValue = builder.CreateGlobalStringPtr(format, "format.template");
        std::vector<llvm::Value*> sizeArguments = {
            llvm::ConstantPointerNull::get(builder.getPtrTy()),
            builder.getInt64(0),
            formatValue
        };
        for (const PrintableValue& printable : values) sizeArguments.push_back(printable.value);
        llvm::Value* length = builder.CreateCall(Snprintf(), sizeArguments, "format.length");
        llvm::Value* textLength =
            builder.CreateSExt(length, builder.getInt64Ty(), "format.length64");
        llvm::Value* allocationSize = builder.CreateAdd(
            textLength, builder.getInt64(1), "format.allocation.size");
        // Through the string allocator rather than malloc: the result is a
        // string like any other, and a string is the bytes plus the header in
        // front of them that says how many names are holding it. Formatting
        // was the loudest of the leaks precisely because this buffer had no
        // way of being released.
        llvm::Value* buffer = builder.CreateCall(StringAlloc(),
            {builder.CreateIntCast(textLength, SizeType(), false, "format.size")},
            "format.buffer");

        std::vector<llvm::Value*> writeArguments = {buffer, allocationSize, formatValue};
        for (const PrintableValue& printable : values) writeArguments.push_back(printable.value);
        builder.CreateCall(Snprintf(), writeArguments, "format.write");
        return buffer;
    }
}
