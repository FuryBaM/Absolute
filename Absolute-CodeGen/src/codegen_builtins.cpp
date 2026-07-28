#include "codegen_internal.h"

namespace Absolute {
    void CodeGenerator::Impl::EmitBuiltin(FunctionCallExpr& expression, const std::string& name) {
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
            if (expression.arguments.size() != 1) Fail("move expects exactly one argument");
            Expression* argument = expression.arguments.front().get();
            llvm::Value* address = EvaluateAddress(argument);
            llvm::Type* type = TypeFromName(SemanticType(argument));
            value = builder.CreateLoad(type, address, "move.value");
            valueCreatesManagedOwner = IsStrongManagedPointerTypeName(SemanticType(argument));
            if (ArrayRankName(SemanticType(argument)) > 0) {
                valueCreatesArrayOwner = true;
                valueArrayOwner = builder.CreateExtractValue(value, {1}, "move.array.owner");
            }
            uint64_t size = SizeOfTypeName(SemanticType(argument));
            if (size > 0) {
                builder.CreateMemSet(address, builder.getInt8(0), size, llvm::MaybeAlign(8));
            }
            return;
        }

        if (name == "seal") {
            if (expression.arguments.size() != 1)
                Fail("seal expects exactly one moved managed owner");
            llvm::Value* handle = Coerce(
                Evaluate(expression.arguments.front().get()), builder.getInt64Ty());
            llvm::FunctionType* capsuleCreateType = llvm::FunctionType::get(
                builder.getPtrTy(), {builder.getInt64Ty()}, false);
            value = builder.CreateCall(
                module->getOrInsertFunction("absolute_capsule_create", capsuleCreateType),
                {handle}, "capsule.sealed");
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
