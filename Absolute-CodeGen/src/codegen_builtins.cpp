#include "codegen_internal.h"

namespace Absolute {
    void CodeGenerator::Impl::EmitBuiltin(FunctionCallExpr& expression, const std::string& name) {
        if (name == "tuple") {
            if (expression.arguments.size() < 2)
                Fail("tuple literal requires at least two values");
            auto* tupleType = llvm::cast<llvm::StructType>(
                TypeFromName(SemanticType(&expression)));
            llvm::Value* tupleValue = llvm::UndefValue::get(tupleType);
            for (size_t index = 0; index < expression.arguments.size(); ++index) {
                llvm::Value* element = Evaluate(expression.arguments[index].get());
                llvm::Type* target = tupleType->getStructElementType(
                    static_cast<unsigned>(index));
                tupleValue = builder.CreateInsertValue(tupleValue, Coerce(element, target),
                    {static_cast<unsigned>(index)}, "tuple.element");
            }
            value = tupleValue;
            valueCreatesManagedOwner = false;
            valueCreatesArrayOwner = false;
            valueCreatesClosureOwner = false;
            return;
        }

        if (name == "print" || name == "println") {
            std::vector<PrintableValue> values;
            std::string format;
            for (const auto& argument : expression.arguments) {
                PrintableValue printable = PreparePrintable(Evaluate(argument.get()), argument.get());
                format += printable.specifier;
                values.push_back(std::move(printable));
            }
            if (name == "println") format += '\n';
            EmitPrintf(format, values);
            value = nullptr;
            return;
        }

        if (name == "toString") {
            if (expression.arguments.size() != 1) Fail("toString expects exactly one argument");
            PrintableValue printable = PreparePrintable(
                Evaluate(expression.arguments.front().get()), expression.arguments.front().get());
            value = EmitFormat(printable.specifier, {printable});
            return;
        }

        if (name == "format") {
            if (expression.arguments.empty()) Fail("format expects a string literal template");
            StringLiteralProbe probe;
            expression.arguments.front()->Accept(probe);
            if (!probe.literal) Fail("format template must be a string literal");
            std::vector<PrintableValue> values;
            for (size_t index = 1; index < expression.arguments.size(); ++index)
                values.push_back(PreparePrintable(
                    Evaluate(expression.arguments[index].get()), expression.arguments[index].get()));
            value = EmitFormat(BuildFormat(probe.literal->value, values), values);
            return;
        }

        if (name == "assert") {
            if (expression.arguments.empty() || expression.arguments.size() > 2)
                Fail("assert expects a condition and an optional message");
            llvm::Function* function = CurrentFunction();
            if (!function) Fail("assert outside a function");
            llvm::Value* condition = AsCondition(Evaluate(expression.arguments.front().get()));
            llvm::BasicBlock* success = llvm::BasicBlock::Create(context, "assert.success", function);
            llvm::BasicBlock* failure = llvm::BasicBlock::Create(context, "assert.failure", function);
            builder.CreateCondBr(condition, success, failure);
            builder.SetInsertPoint(failure);
            std::vector<PrintableValue> values;
            std::string format = "Assertion failed";
            if (expression.arguments.size() == 2) {
                format += ": %s";
                values.push_back(PreparePrintable(
                    Evaluate(expression.arguments[1].get()), expression.arguments[1].get()));
            }
            format += '\n';
            EmitPrintf(format, values);
            builder.CreateCall(Abort());
            builder.CreateUnreachable();
            builder.SetInsertPoint(success);
            value = nullptr;
            return;
        }

        if (name == "unsafeArrayGet" || name == "unsafeArraySet") {
            const bool isSet = name == "unsafeArraySet";
            const size_t expectedCount = isSet ? 3 : 2;
            if (expression.arguments.size() != expectedCount)
                Fail(name + " received an invalid argument count");
            ArrayView view = ViewOfArray(expression.arguments[0].get());
            if (view.dimensions.size() != 1)
                Fail(name + " requires a one-dimensional array");
            llvm::Value* index = Evaluate(expression.arguments[1].get());
            if (!index->getType()->isIntegerTy())
                Fail(name + " index must be an integer");
            const std::string indexTypeName = SemanticType(expression.arguments[1].get());
            const bool unsignedIndex = indexTypeName.starts_with("uint") ||
                indexTypeName == "char";
            if (!index->getType()->isIntegerTy(64))
                index = builder.CreateIntCast(
                    index, builder.getInt64Ty(), !unsignedIndex, "unsafe.array.index");
            llvm::Value* address = builder.CreateInBoundsGEP(
                view.elementType, view.address, index, "unsafe.array.element.address");
            if (!isSet) {
                value = builder.CreateLoad(view.elementType, address, "unsafe.array.element");
                valueCreatesManagedOwner = false;
                valueCreatesArrayOwner = false;
                valueCreatesClosureOwner = false;
                return;
            }
            Expression* source = expression.arguments[2].get();
            const std::string elementTypeName =
                ArrayElementTypeName(SemanticType(expression.arguments[0].get()));
            llvm::Value* assigned = Coerce(
                Evaluate(source), view.elementType,
                SemanticType(source), elementTypeName);
            builder.CreateStore(assigned, address);
            value = nullptr;
            valueCreatesManagedOwner = false;
            valueCreatesArrayOwner = false;
            valueCreatesClosureOwner = false;
            return;
        }

        if (name == "load") {
            if (expression.arguments.size() != 1)
                Fail("load expects exactly one library path");
            llvm::Value* path = Evaluate(expression.arguments.front().get());
            llvm::Value* status = builder.CreateCall(
                LoadDynamicLibrary(), {path}, "load.library.status");
            value = builder.CreateICmpNE(status, builder.getInt32(0), "load.library.success");
            return;
        }

        if (name == "isLoaded") {
            if (expression.arguments.size() != 1)
                Fail("isLoaded expects exactly one library path");
            llvm::Value* path = Evaluate(expression.arguments.front().get());
            llvm::Value* status = builder.CreateCall(
                IsDynamicLibraryLoaded(), {path}, "library.loaded.status");
            value = builder.CreateICmpNE(status, builder.getInt32(0), "library.loaded");
            return;
        }

        if (name == "loadError") {
            if (!expression.arguments.empty()) Fail("loadError expects no arguments");
            value = builder.CreateCall(DynamicLibraryError(), {}, "load.library.error");
            return;
        }

        if (name == "taskGroupAdd") {
            if (expression.arguments.size() != 2)
                Fail("taskGroupAdd expects a group handle and one task");
            llvm::Value* group = Coerce(
                Evaluate(expression.arguments[0].get()), builder.getPtrTy());
            Expression* childExpression = expression.arguments[1].get();
            llvm::Value* child = Coerce(Evaluate(childExpression), builder.getPtrTy());
            ConsumeTaskArgument(childExpression, SemanticType(childExpression));
            llvm::FunctionType* addType = llvm::FunctionType::get(
                builder.getInt1Ty(), {builder.getPtrTy(), builder.getPtrTy()}, false);
            value = builder.CreateCall(
                module->getOrInsertFunction("absolute_task_group_add", addType),
                {group, child}, "task.group.added");
            return;
        }

        if (name == "move") {
            if (expression.arguments.size() != 1) Fail(name + " expects exactly one argument");
            Expression* argument = expression.arguments.front().get();
            const ExpressionInfo* info = analyzer && argument
                ? analyzer->GetExpressionInfo(*argument) : nullptr;
            const bool isLValue = info && info->isLValue;
            if (isLValue) {
                llvm::Value* address = EvaluateAddress(argument);
                llvm::Type* type = TypeFromName(SemanticType(argument));
                value = builder.CreateLoad(type, address, name + ".value");
                valueCreatesManagedOwner = IsStrongManagedPointerTypeName(SemanticType(argument));
                if (ArrayRankName(SemanticType(argument)) > 0) {
                    valueCreatesArrayOwner = true;
                    valueArrayOwner = builder.CreateExtractValue(value, {1}, "move.array.owner");
                }
                uint64_t size = SizeOfTypeName(SemanticType(argument));
                if (size > 0 && name == "move") {
                    builder.CreateMemSet(address, builder.getInt8(0), size, llvm::MaybeAlign(8));
                }
            } else {
                value = Evaluate(argument);
            }
            return;
        }

        if (name == "adoptRaw" || name == "retainRaw" || name == "borrowRaw" || name == "share") {
            if (expression.arguments.empty()) Fail(name + " expects at least 1 argument");
            llvm::Value* argValue = Evaluate(expression.arguments.front().get());
            if (name == "borrowRaw") {
                value = argValue;
                return;
            }
            if (name == "retainRaw" || name == "share")
                Fail(name + " is unavailable in the deterministic unique-ownership model");
            llvm::Value* deleterVal = expression.arguments.size() > 1
                ? Evaluate(expression.arguments[1].get())
                : llvm::ConstantPointerNull::get(builder.getPtrTy());
            if (!argValue->getType()->isPointerTy()) {
                argValue = builder.CreateIntToPtr(argValue, builder.getPtrTy());
            }
            if (name == "adoptRaw") {
                llvm::FunctionType* funcType = llvm::FunctionType::get(
                    builder.getInt64Ty(), {builder.getPtrTy(), builder.getPtrTy()}, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("absolute_managed_adopt_raw", funcType);
                value = builder.CreateCall(fn, {argValue, deleterVal}, "adopt.handle");
                valueCreatesManagedOwner = true;
            }
            return;
        }

        if (name == "seal") {
            if (expression.arguments.size() != 1)
                Fail("seal expects exactly one moved managed owner");
            Expression* argument = expression.arguments.front().get();
            const std::string pointerType = SemanticType(argument);
            llvm::Value* handle = Coerce(
                Evaluate(argument), builder.getInt64Ty());
            llvm::Value* pointeeDestructor =
                llvm::ConstantPointerNull::get(builder.getPtrTy());
            const std::string pointee = PointerPointeeName(pointerType);
            if (auto found = classes.find(pointee); found != classes.end())
                pointeeDestructor = DeclareClassDestructor(found->second);
            else if (auto found = structs.find(pointee); found != structs.end() &&
                TypeNeedsCleanup(pointee))
                pointeeDestructor = DeclareStructDestructor(found->second);
            llvm::FunctionType* capsuleCreateType = llvm::FunctionType::get(
                builder.getPtrTy(), {builder.getInt64Ty(), builder.getPtrTy()}, false);
            value = builder.CreateCall(
                module->getOrInsertFunction(
                    "absolute_capsule_create_typed", capsuleCreateType),
                {handle, pointeeDestructor}, "capsule.sealed");
            valueCreatesManagedOwner = false;
            valueManagedPointee = nullptr;
            return;
        }

        if (name == "unseal") {
            if (expression.arguments.size() != 1)
                Fail("unseal expects exactly one raw capsule");
            llvm::Value* capsule = Coerce(
                Evaluate(expression.arguments.front().get()), builder.getPtrTy());
            llvm::FunctionType* capsuleUnwrapType = llvm::FunctionType::get(
                builder.getInt64Ty(), {builder.getPtrTy()}, false);
            value = builder.CreateCall(
                module->getOrInsertFunction("absolute_capsule_unwrap", capsuleUnwrapType),
                {capsule}, "capsule.unsealed");
            valueCreatesManagedOwner = true;
            valueManagedPointee = nullptr;
            return;
        }

        if (name == "copy") {
            if (expression.arguments.size() != 1)
                Fail("copy expects exactly one array, slice, or cloneable value argument");
            Expression* argument = expression.arguments.front().get();
            const std::string typeName = SemanticType(argument);
            const size_t rank = ArrayRankName(typeName);

            if (rank > 0) {
                ArrayView source = ViewOfArray(argument);
                const bool releaseTemporarySource = valueCreatesArrayOwner;
                llvm::Value* temporarySourceOwner = valueArrayOwner;
                llvm::Value* elementCount = builder.getInt64(1);
                for (llvm::Value* dimension : source.dimensions)
                    elementCount = builder.CreateMul(
                        elementCount, dimension, "copy.element.count");
                llvm::Value* byteCount = builder.CreateMul(elementCount,
                    builder.getInt64(SizeOfTypeName(ArrayElementTypeName(typeName, rank))),
                    "copy.byte.count");
                llvm::Value* copiedData = builder.CreateCall(Malloc(), {byteCount}, "copy.data");
                builder.CreateMemCpy(copiedData, llvm::MaybeAlign(16),
                    source.address, llvm::MaybeAlign(1), byteCount);
                if (releaseTemporarySource)
                    builder.CreateCall(Free(), {temporarySourceOwner});
                source.address = copiedData;
                source.owner = copiedData;
                value = BuildArrayDescriptor(source);
                valueCreatesManagedOwner = false;
                valueManagedPointee = nullptr;
                valueCreatesArrayOwner = true;
                valueArrayOwner = copiedData;
                return;
            }

            const std::string aggregateName = ClassNameFromType(typeName);
            const std::string methodKey = CallableKey("clone", {});
            llvm::Value* object = ObjectPointer(argument, typeName);
            const ClassMethod* cloneMethod = nullptr;
            llvm::Value* callee = nullptr;

            if (auto found = classes.find(aggregateName); found != classes.end()) {
                const auto method = found->second.methods.find(methodKey);
                if (method == found->second.methods.end())
                    Fail("class '" + aggregateName + "' has no clone() method");
                cloneMethod = &method->second;
                if (cloneMethod->virtualSlot) {
                    llvm::Value* vtableAddress = builder.CreateStructGEP(
                        found->second.llvmType, object, 0, "clone.vtable.address");
                    llvm::Value* vtable = builder.CreateLoad(
                        builder.getPtrTy(), vtableAddress, "clone.vtable");
                    llvm::Value* slot = builder.CreateGEP(builder.getPtrTy(), vtable,
                        builder.getInt64(*cloneMethod->virtualSlot), "clone.virtual.slot");
                    callee = builder.CreateLoad(
                        builder.getPtrTy(), slot, "clone.virtual.method");
                }
                else {
                    callee = module->getFunction(cloneMethod->linkName);
                }
            }
            else if (auto found = interfaces.find(aggregateName); found != interfaces.end()) {
                const auto method = found->second.methods.find(methodKey);
                if (method == found->second.methods.end() || !method->second.virtualSlot)
                    Fail("interface '" + aggregateName + "' has no clone() dispatch slot");
                cloneMethod = &method->second;
                llvm::Value* vtable = builder.CreateLoad(
                    builder.getPtrTy(), object, "clone.interface.vtable");
                llvm::Value* slot = builder.CreateGEP(builder.getPtrTy(), vtable,
                    builder.getInt64(*cloneMethod->virtualSlot), "clone.interface.slot");
                callee = builder.CreateLoad(
                    builder.getPtrTy(), slot, "clone.interface.method");
            }
            else if (auto found = structs.find(aggregateName); found != structs.end()) {
                const auto method = found->second.methods.find(methodKey);
                if (method == found->second.methods.end())
                    Fail("struct '" + aggregateName + "' has no clone() method");
                cloneMethod = &method->second;
                callee = module->getFunction(cloneMethod->linkName);
            }
            else {
                Fail("copy expects an array, slice, or cloneable aggregate");
            }

            if (!cloneMethod || !callee)
                Fail("missing clone() implementation for type '" + aggregateName + "'");
            value = EmitAbiCall(MethodFunctionType(*cloneMethod), callee,
                cloneMethod->returnType, {object}, cloneMethod->parameterTypes, {},
                "copy.clone.result");
            EmitExceptionCheck();
            valueCreatesManagedOwner =
                IsStrongManagedPointerTypeName(cloneMethod->returnType);
            valueManagedPointee = nullptr;
            valueCreatesArrayOwner = false;
            valueArrayOwner = nullptr;
            return;
        }

        Fail("unknown builtin function '" + name + "'");
    }

}
