#include "codegen_internal.h"
#include <llvm/IR/MDBuilder.h>

namespace Absolute {
    llvm::Value* CodeGenerator::Impl::Evaluate(Expression* expression) {
        if (!expression) Fail("missing expression");
        value = nullptr;
        valueCreatesManagedOwner = false;
        valueManagedPointee = nullptr;
        expression->Accept(visitor);
        if (!value) Fail("expression does not produce a value");
        currentValueType = SemanticType(expression);
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
            builder.getPtrTy(), {entryType->getPointerTo(), builder.getPtrTy()}, false);
        return module->getOrInsertFunction("absolute_task_spawn", type);
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
            if (!variable.managedOwner || variable.symbol == transferredOwner) continue;
            llvm::Value* handle = builder.CreateLoad(variable.type, variable.address, "cleanup.handle");
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
        return builder.CreateCall(ManagedGet(true), {handle}, "managed.pointee");
    }

    llvm::Value* CodeGenerator::Impl::BuildArrayDescriptor(const ArrayView& view) {
        llvm::StructType* type = ArrayDescriptorType(view.typeName);
        llvm::Value* descriptor = llvm::UndefValue::get(type);
        descriptor = builder.CreateInsertValue(descriptor, view.address, {0}, "array.data");
        for (size_t index = 0; index < view.dimensions.size(); ++index)
            descriptor = builder.CreateInsertValue(
                descriptor, view.dimensions[index], {static_cast<unsigned>(index + 1)}, "array.dimension");
        return descriptor;
    }

    CodeGenerator::Impl::ArrayView CodeGenerator::Impl::ArrayViewFromValue(llvm::Value* descriptor, const std::string& typeName) {
        const size_t rank = ArrayRankName(typeName);
        if (rank == 0 || !descriptor->getType()->isStructTy())
            Fail("array expression does not produce a descriptor");
        ArrayView view;
        view.address = builder.CreateExtractValue(descriptor, {0}, "array.data");
        view.elementType = TypeFromName(ArrayElementTypeName(typeName, rank));
        view.typeName = typeName;
        for (size_t index = 0; index < rank; ++index)
            view.dimensions.push_back(builder.CreateExtractValue(
                descriptor, {static_cast<unsigned>(index + 1)}, "array.dimension"));
        return view;
    }

    CodeGenerator::Impl::ArrayView CodeGenerator::Impl::ViewOfArray(Expression* expression) {
        if (auto* identifier = dynamic_cast<IdentifierExpr*>(expression)) {
            Variable& variable = RequireVariable(identifier->name);
            if (!variable.isArray) Fail("object is not an array");
            return {variable.address, variable.arrayElementType,
                variable.typeName, variable.arrayDimensions};
        }
        const std::string typeName = SemanticType(expression);
        return ArrayViewFromValue(Evaluate(expression), typeName);
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
        if (expression.indexes.size() != view.dimensions.size())
            Fail("array access must provide all dimensions");

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
            llvm::Value* nonNegative = unsignedIndex
                ? builder.getTrue()
                : builder.CreateICmpSGE(
                    index, llvm::ConstantInt::get(index->getType(), 0), "array.index.nonnegative");
            llvm::Value* wideIndex = index->getType()->isIntegerTy(64)
                ? index
                : builder.CreateIntCast(index, builder.getInt64Ty(), !unsignedIndex, "array.index.wide");
            llvm::Value* belowSize = unsignedIndex
                ? builder.CreateICmpULT(wideIndex, view.dimensions[dimension], "array.index.below.size")
                : builder.CreateICmpSLT(wideIndex, view.dimensions[dimension], "array.index.below.size");
            EmitOrExit(builder.CreateAnd(nonNegative, belowSize, "array.index.valid"), "array.bounds");
            if (expression.indexes.size() == 1) oneDimensionalIndex = index;
            if (dimension == 0) offset = wideIndex;
            else {
                offset = builder.CreateMul(offset, view.dimensions[dimension], "array.row.offset");
                offset = builder.CreateAdd(offset, wideIndex, "array.linear.offset");
            }
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
        if (!source) Fail("cannot convert an empty value");
        llvm::Type* sourceType = source->getType();
        if (sourceType == target) return source;

        if (target->isIntegerTy(64) && llvm::isa<llvm::ConstantPointerNull>(source))
            return builder.getInt64(0);

        if (sourceType->isIntegerTy() && target->isIntegerTy()) {
            return builder.CreateIntCast(source, target, true, "int.cast");
        }
        if (sourceType->isIntegerTy() && target->isFloatingPointTy()) {
            return builder.CreateSIToFP(source, target, "int.to.fp");
        }
        if (sourceType->isFloatingPointTy() && target->isIntegerTy()) {
            return builder.CreateFPToSI(source, target, "fp.to.int");
        }
        if (sourceType->isFloatingPointTy() && target->isFloatingPointTy()) {
            return builder.CreateFPCast(source, target, "fp.cast");
        }
        if (sourceType->isPointerTy() && target->isPointerTy()) {
            return builder.CreatePointerCast(source, target, "ptr.cast");
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
        if (op == "&&" || op == "||") {
            left = AsCondition(left);
            right = AsCondition(right);
            return op == "&&" ? builder.CreateAnd(left, right, "logical.and")
                                : builder.CreateOr(left, right, "logical.or");
        }

        llvm::Type* type = CommonNumericType(left->getType(), right->getType());
        left = Coerce(left, type);
        right = Coerce(right, type);
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
        if (symbol.externalFunction || symbol.name == "main" || !analyzer ||
            analyzer->FunctionOverloadCount(symbol.name) <= 1)
            return symbol.externalFunction ? symbol.name.substr(symbol.name.rfind('.') + 1) : symbol.name;
        return CallableKey(symbol.name, symbol.parameterTypes);
    }

    std::string CodeGenerator::Impl::ResolvedName(Expression* expression) const {
        std::string name;
        if (analyzer && expression) {
            if (const ExpressionInfo* info = analyzer->GetExpressionInfo(*expression)) {
                if (const Symbol* symbol = analyzer->GetSymbol(info->symbol))
                    return FunctionLinkName(*symbol);
            }
        }
        if (name.empty()) name = IdentifierName(expression);
        const auto linked = functionLinkNames.find(name);
        return linked == functionLinkNames.end() ? name : linked->second;
    }

    bool CodeGenerator::Impl::IsBuiltinFunction(const std::string& name) const {
        return name == "print" || name == "println" || name == "format" ||
            name == "toString" || name == "assert" || name == "copy";
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
        llvm::Function* target = module->getFunction(name);
        if (!target) Fail("unknown async function '" + name + "'");
        if (target->arg_size() != call.arguments.size())
            Fail("invalid async argument count for '" + name + "'");

        const std::uint64_t slotCount = static_cast<std::uint64_t>(call.arguments.size()) + 1;
        llvm::Value* contextPointer = builder.CreateCall(
            Malloc(), {builder.getInt64(slotCount * 8)}, "task.context");

        size_t argumentIndex = 0;
        for (const auto& argument : call.arguments) {
            llvm::Value* argumentValue = Evaluate(argument.get());
            argumentValue = Coerce(argumentValue,
                target->getFunctionType()->getParamType(static_cast<unsigned>(argumentIndex)));
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
        thunkArguments.reserve(call.arguments.size());
        for (size_t index = 0; index < call.arguments.size(); ++index) {
            llvm::Value* slot = thunkBuilder.CreateGEP(
                thunkBuilder.getInt64Ty(), thunkContext,
                thunkBuilder.getInt64(static_cast<std::uint64_t>(index + 1)), "task.argument.slot");
            llvm::Value* encoded = thunkBuilder.CreateLoad(
                thunkBuilder.getInt64Ty(), slot, "task.argument");
            thunkArguments.push_back(DecodeTaskSlot(
                thunkBuilder, encoded, target->getFunctionType()->getParamType(static_cast<unsigned>(index))));
        }
        llvm::CallInst* result = thunkBuilder.CreateCall(target, thunkArguments,
            target->getReturnType()->isVoidTy() ? "" : "task.result");
        if (!target->getReturnType()->isVoidTy()) {
            llvm::Value* resultSlot = thunkBuilder.CreateGEP(
                thunkBuilder.getInt64Ty(), thunkContext, thunkBuilder.getInt64(0), "task.result.slot");
            thunkBuilder.CreateStore(EncodeTaskSlot(thunkBuilder, result), resultSlot);
        }
        thunkBuilder.CreateRetVoid();

        return builder.CreateCall(TaskSpawn(), {thunk, contextPointer}, "task.handle");
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
        return module->getOrInsertFunction("exit", type);
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
