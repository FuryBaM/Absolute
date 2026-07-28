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
            bool transfersOwner = dynamic_cast<FunctionCallExpr*>(expression) != nullptr;
            if (transfersOwner) {
                ArrayView returned = ArrayViewFromValue(value, currentValueType);
                valueCreatesArrayOwner = true;
                valueArrayOwner = returned.owner;
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
            if (IsTaskTypeName(variable.typeName)) {
                llvm::Value* handle = builder.CreateLoad(variable.type, variable.address, "cleanup.task");
                builder.CreateCall(TaskDestroy(), {handle});
                builder.CreateStore(llvm::ConstantPointerNull::get(builder.getPtrTy()), variable.address);
                continue;
            }
            const bool transfersThisOwner = transferredOwner != InvalidSymbolId &&
                variable.symbol == transferredOwner;
            if (variable.ownsArrayStorage && !transfersThisOwner) {
                builder.CreateCall(Free(), {variable.arrayOwner});
                continue;
            }
            if (variable.ownsAggregateResources) {
                EmitValueCleanup(variable.address, variable.typeName);
                continue;
            }
            if (!variable.managedOwner || transfersThisOwner) continue;
            llvm::Value* handle = builder.CreateLoad(variable.type, variable.address, "cleanup.handle");
            llvm::Value* pointee = EmitManagedGet(handle, false);
            EmitPointeeCleanup(pointee, variable.typeName);
            builder.CreateCall(ManagedDestroy(), {handle});
            builder.CreateStore(builder.getInt64(0), variable.address);
            if (variable.managedPointee)
                builder.CreateStore(llvm::ConstantPointerNull::get(builder.getPtrTy()), variable.managedPointee);
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
        if (valueCreatesArrayOwner) temporaryArrayOwners.push_back(valueArrayOwner);
        if (valueCreatesClosureOwner) temporaryClosureOwners.push_back(argument);
        if (!parameterType.empty()) {
            const std::string targetType = ValueReferenceBaseTypeName(parameterType);
            argument = Coerce(argument, TypeFromName(targetType),
                SemanticType(expression), targetType);
        }
        ConsumeTaskArgument(expression, parameterType);
        return argument;
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
        for (const std::string& parameter : parameterTypes)
            parameters.push_back(AbiParameterType(parameter));
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
                return {variable->address, variable->arrayElementType,
                    variable->typeName, variable->arrayDimensions, variable->arrayOwner};
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
        const std::string message = name == "array.size"
            ? "Array size must be greater than zero"
            : "Array index out of bounds";
        builder.CreateCall(Puts(), {builder.CreateGlobalStringPtr(message, name + ".message")});
        builder.CreateCall(ExitFailure(), {builder.getInt32(1)});
        builder.CreateUnreachable();
        builder.SetInsertPoint(success);
    }

    llvm::Value* CodeGenerator::Impl::ArrayElementAddress(ArrayAccessExpr& expression) {
        ArrayView view = ViewOfArray(expression.base.get());
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
            return targetUnsigned
                ? builder.CreateFPToUI(source, target, "fp.to.uint")
                : builder.CreateFPToSI(source, target, "fp.to.int");
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
        Fail("incompatible value conversion");
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
        left = Coerce(left, type, leftTypeName, commonTypeName);
        right = Coerce(right, type, rightTypeName, commonTypeName);
        const bool floating = type->isFloatingPointTy();

        if (op == "+") return floating ? builder.CreateFAdd(left, right, "add") : builder.CreateAdd(left, right, "add");
        if (op == "-") return floating ? builder.CreateFSub(left, right, "sub") : builder.CreateSub(left, right, "sub");
        if (op == "*") return floating ? builder.CreateFMul(left, right, "mul") : builder.CreateMul(left, right, "mul");
        if (op == "/") return floating ? builder.CreateFDiv(left, right, "div") : builder.CreateSDiv(left, right, "div");
        if (op == "%") return floating ? builder.CreateFRem(left, right, "rem") : builder.CreateSRem(left, right, "rem");

        if (op == "==") return floating ? builder.CreateFCmpOEQ(left, right, "equal") : builder.CreateICmpEQ(left, right, "equal");
        if (op == "!=") return floating ? builder.CreateFCmpONE(left, right, "not.equal") : builder.CreateICmpNE(left, right, "not.equal");
        if (op == "<") return floating ? builder.CreateFCmpOLT(left, right, "less") : builder.CreateICmpSLT(left, right, "less");
        if (op == "<=") return floating ? builder.CreateFCmpOLE(left, right, "less.equal") : builder.CreateICmpSLE(left, right, "less.equal");
        if (op == ">") return floating ? builder.CreateFCmpOGT(left, right, "greater") : builder.CreateICmpSGT(left, right, "greater");
        if (op == ">=") return floating ? builder.CreateFCmpOGE(left, right, "greater.equal") : builder.CreateICmpSGE(left, right, "greater.equal");

        if (!type->isIntegerTy()) Fail("bitwise operator requires integer operands");
        if (op == "&") return builder.CreateAnd(left, right, "bit.and");
        if (op == "|") return builder.CreateOr(left, right, "bit.or");
        if (op == "^") return builder.CreateXor(left, right, "bit.xor");
        if (op == "<<") return builder.CreateShl(left, right, "shift.left");
        if (op == ">>") return builder.CreateAShr(left, right, "shift.right");
        Fail("unsupported binary operator '" + op + "'");
    }

    llvm::Value* CodeGenerator::Impl::One(llvm::Type* type) {
        if (type->isFloatingPointTy()) return llvm::ConstantFP::get(type, 1.0);
        if (type->isIntegerTy()) return llvm::ConstantInt::get(type, 1);
        Fail("increment requires a numeric operand");
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
            analyzer->FunctionOverloadCount(symbol.name) <= 1)
            return (symbol.externalFunction || symbol.exportedFunction)
                ? symbol.name.substr(symbol.name.rfind('.') + 1) : symbol.name;
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
            name == "toString" || name == "assert" || name == "copy" || name == "move" ||
            name == "seal" || name == "unseal" ||
            name == "load" || name == "isLoaded" || name == "loadError" ||
            name == "taskGroupAdd";
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

    llvm::FunctionCallee CodeGenerator::Impl::Malloc() {
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getPtrTy(), {builder.getInt64Ty()}, false);
        return module->getOrInsertFunction("malloc", type);
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
        const std::string taskName = IdentifierName(expression.operand.get());
        if (!taskName.empty()) {
            Variable& task = RequireVariable(taskName);
            builder.CreateStore(llvm::ConstantPointerNull::get(builder.getPtrTy()), task.address);
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
        llvm::Function* function = CurrentFunction();
        if (!function) return builder.CreateCall(ManagedGet(requireValid), {handle}, "managed.pointee");

        llvm::BasicBlock* checkBlock = llvm::BasicBlock::Create(context, "managed.fast.check", function);
        llvm::BasicBlock* loadBlock = llvm::BasicBlock::Create(context, "managed.fast.load", function);
        llvm::BasicBlock* validBlock = llvm::BasicBlock::Create(context, "managed.fast.valid", function);
        llvm::BasicBlock* slowBlock = llvm::BasicBlock::Create(context, "managed.slow", function);
        llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(context, "managed.end", function);

        llvm::Value* id = builder.CreateTrunc(handle, builder.getInt32Ty(), "managed.id");
        llvm::Value* gen = builder.CreateTrunc(builder.CreateLShr(handle, 32), builder.getInt32Ty(), "managed.gen");

        llvm::GlobalVariable* slotsSize = module->getGlobalVariable("absolute_managed_slots_size");
        if (!slotsSize) {
            slotsSize = new llvm::GlobalVariable(*module, builder.getInt32Ty(), false, 
                llvm::GlobalValue::ExternalLinkage, nullptr, "absolute_managed_slots_size");
        }
        
        llvm::Value* size = builder.CreateLoad(builder.getInt32Ty(), slotsSize, "managed.slots.size");
        llvm::Value* inBounds = builder.CreateICmpULT(id, size, "managed.in.bounds");
        
        builder.CreateCondBr(inBounds, loadBlock, slowBlock);
        
        builder.SetInsertPoint(loadBlock);
        llvm::StructType* slotType = llvm::StructType::getTypeByName(context, "absolute.Slot");
        if (!slotType) {
            slotType = llvm::StructType::create(context, "absolute.Slot");
            slotType->setBody({builder.getPtrTy(), builder.getInt32Ty(), builder.getInt32Ty(), builder.getInt64Ty()});
        }
        
        llvm::GlobalVariable* slotsData = module->getGlobalVariable("absolute_managed_slots_data");
        if (!slotsData) {
            slotsData = new llvm::GlobalVariable(*module, builder.getPtrTy(), false, 
                llvm::GlobalValue::ExternalLinkage, nullptr, "absolute_managed_slots_data");
        }
        
        llvm::Value* dataPtr = builder.CreateLoad(builder.getPtrTy(), slotsData, "managed.slots.data");
        llvm::Value* slotPtr = builder.CreateGEP(slotType, dataPtr, {id}, "managed.slot.ptr");
        
        llvm::Value* slotGenPtr = builder.CreateStructGEP(slotType, slotPtr, 1, "managed.slot.gen.ptr");
        llvm::Value* slotGen = builder.CreateLoad(builder.getInt32Ty(), slotGenPtr, "managed.slot.gen");
        
        llvm::Value* genMatch = builder.CreateICmpEQ(slotGen, gen, "managed.gen.match");
        builder.CreateCondBr(genMatch, checkBlock, slowBlock);
        
        builder.SetInsertPoint(checkBlock);
        llvm::Value* slotPtrField = builder.CreateStructGEP(slotType, slotPtr, 0, "managed.slot.ptr.field");
        llvm::Value* ptr = builder.CreateLoad(builder.getPtrTy(), slotPtrField, "managed.ptr");
        llvm::Value* ptrNotNull = builder.CreateICmpNE(ptr, llvm::ConstantPointerNull::get(builder.getPtrTy()), "managed.ptr.not.null");
        
        builder.CreateCondBr(ptrNotNull, validBlock, slowBlock);
        
        builder.SetInsertPoint(validBlock);
        builder.CreateBr(endBlock);
        
        builder.SetInsertPoint(slowBlock);
        llvm::Value* slowPtr = builder.CreateCall(ManagedGet(requireValid), {handle}, "managed.slow.ptr");
        builder.CreateBr(endBlock);
        
        builder.SetInsertPoint(endBlock);
        llvm::PHINode* phi = builder.CreatePHI(builder.getPtrTy(), 2, "managed.final.ptr");
        phi->addIncoming(ptr, validBlock);
        phi->addIncoming(slowPtr, slowBlock);
        
        return phi;
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
        if (!source) Fail("cannot print an empty value");
        llvm::Type* type = source->getType();
        const std::string semanticType = SemanticType(expression);
        if (semanticType == "bool" || type->isIntegerTy(1)) {
            llvm::Value* trueText = builder.CreateGlobalStringPtr("true", "bool.true");
            llvm::Value* falseText = builder.CreateGlobalStringPtr("false", "bool.false");
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
        if (type->isFloatTy()) {
            return {"%g", builder.CreateFPExt(source, builder.getDoubleTy(), "float.promoted")};
        }
        if (type->isDoubleTy()) return {"%g", source};
        if (type->isPointerTy()) {
            llvm::Value* nullText = builder.CreateGlobalStringPtr("<null>", "null.text");
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
        llvm::Value* allocationSize = builder.CreateAdd(
            builder.CreateSExt(length, builder.getInt64Ty(), "format.length64"),
            builder.getInt64(1), "format.allocation.size");
        llvm::Value* buffer = builder.CreateCall(Malloc(), {allocationSize}, "format.buffer");

        std::vector<llvm::Value*> writeArguments = {buffer, allocationSize, formatValue};
        for (const PrintableValue& printable : values) writeArguments.push_back(printable.value);
        builder.CreateCall(Snprintf(), writeArguments, "format.write");
        return buffer;
    }
}
