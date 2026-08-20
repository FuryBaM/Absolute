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
            // abort() does not flush stdio, and stdout is fully buffered
            // whenever it is not a terminal -- a pipe, a file, a CI log. The
            // message was written into that buffer and died there, so a failing
            // assertion looked like a bare exit code 134 everywhere it mattered
            // most. Flushing every stream first costs one call on a path that
            // is about to end the process.
            builder.CreateCall(module->getOrInsertFunction("fflush",
                llvm::FunctionType::get(builder.getInt32Ty(), {builder.getPtrTy()}, false)),
                {llvm::ConstantPointerNull::get(builder.getPtrTy())});
            builder.CreateCall(Abort());
            builder.CreateUnreachable();
            builder.SetInsertPoint(success);
            value = nullptr;
            return;
        }

        if (name == "debugBreak") {
            if (!expression.arguments.empty())
                Fail("debugBreak expects no arguments");
            llvm::Function* trap = llvm::Intrinsic::getDeclaration(
                module.get(), llvm::Intrinsic::debugtrap);
            builder.CreateCall(trap);
            value = nullptr;
            return;
        }

        if (name == "unsafeArrayGet" || name == "unsafeArraySet" ||
            name == "unsafeArrayTake") {
            const bool isSet = name == "unsafeArraySet";
            const bool isTake = name == "unsafeArrayTake";
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
            const std::string accessedElementType =
                ArrayElementTypeName(SemanticType(expression.arguments[0].get()));
            if (!isSet) {
                value = builder.CreateLoad(view.elementType, address, "unsafe.array.element");
                // A take clears the slot it read. The array is left holding a
                // zero, which is what "owns nothing" is spelled as everywhere
                // else -- so the drop that walks it later skips this element
                // instead of releasing what the caller now holds.
                if (isTake && SemanticsOfTypeName(accessedElementType).needsDrop)
                    builder.CreateStore(
                        llvm::Constant::getNullValue(view.elementType), address);
                // Described as an element like any other: the check in front of
                // it is the caller's, not a reason for the access to alias more
                // than it does. Without this the indexer of a collection
                // reloaded the object's count and data pointer on every
                // element, and its bounds check stayed inside the loop.
                TagAccess(value, TbaaElementAccess(accessedElementType));
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
            // A slot is a place the array releases, so what goes into it is
            // one more name for the bytes unless the expression made them.
            // Without this a container that stores its parameter -- which is
            // what every `add` does -- left the slot holding what the
            // parameter was about to give back.
            if (elementTypeName == "string")
                assigned = RetainStoredString(source, assigned);
            TagAccess(builder.CreateStore(assigned, address),
                TbaaElementAccess(elementTypeName));
            // The same rule one level up: an aggregate is stored by copying
            // its bytes, which duplicates the pointers its parts hold without
            // duplicating their counts. The slot is a second name for them, so
            // it says so -- unless the expression made the value, in which
            // case the count it arrived with is the one the slot keeps.
            if (elementTypeName != "string" && !CreatesFreshString(source))
                EmitValueRetain(address, elementTypeName);
            value = nullptr;
            valueCreatesManagedOwner = false;
            valueCreatesArrayOwner = false;
            valueCreatesClosureOwner = false;
            return;
        }

        // Lowered to a memcpy rather than a loop. The destination is freshly
        // allocated storage the caller has not published yet and the source is
        // a different allocation, so the two cannot overlap -- which is what
        // makes the non-overlapping form correct here.
        if (name == "unsafeArrayCopy" || name == "unsafeArrayMove") {
            const bool transfers = name == "unsafeArrayMove";
            if (expression.arguments.size() != 3)
                Fail(name + " expects a destination, a source and a count");
            ArrayView destination = ViewOfArray(expression.arguments[0].get());
            ArrayView source = ViewOfArray(expression.arguments[1].get());
            if (destination.dimensions.size() != 1 || source.dimensions.size() != 1)
                Fail(name + " requires one-dimensional arrays");
            llvm::Value* count = Evaluate(expression.arguments[2].get());
            if (!count->getType()->isIntegerTy())
                Fail(name + " count must be an integer");
            const std::string countTypeName = SemanticType(expression.arguments[2].get());
            const bool unsignedCount = countTypeName.starts_with("uint") ||
                countTypeName == "char";
            if (!count->getType()->isIntegerTy(64))
                count = builder.CreateIntCast(count, builder.getInt64Ty(),
                    !unsignedCount, "unsafe.array.copy.count");
            const std::string elementTypeName = ArrayElementTypeName(
                SemanticType(expression.arguments[0].get()));
            llvm::Value* bytes = builder.CreateMul(count,
                builder.getInt64(SizeOfTypeName(elementTypeName)),
                "unsafe.array.copy.bytes");
            builder.CreateMemCpy(destination.address, llvm::MaybeAlign(),
                source.address, llvm::MaybeAlign(), bytes);
            // What makes it a move: the source range is left owning nothing.
            // Without this the block that was copied out of is still holding
            // every handle that was copied, and dropping it releases what the
            // destination now owns. For elements that own nothing there is
            // nothing to transfer and the clear is not emitted.
            if (transfers && SemanticsOfTypeName(elementTypeName).needsDrop)
                builder.CreateMemSet(source.address, builder.getInt8(0), bytes,
                    llvm::MaybeAlign());
            value = nullptr;
            valueCreatesManagedOwner = false;
            valueCreatesArrayOwner = false;
            valueCreatesClosureOwner = false;
            return;
        }

        // Releasing a run of elements. What releasing one means is the
        // element type's answer -- so this is a loop for a run of owners and
        // nothing at all for a run of numbers.
        if (name == "unsafeArrayDrop") {
            if (expression.arguments.size() != 3)
                Fail("unsafeArrayDrop expects an array, a first index and a count");
            ArrayView view = ViewOfArray(expression.arguments[0].get());
            if (view.dimensions.size() != 1)
                Fail("unsafeArrayDrop requires a one-dimensional array");
            const std::string elementTypeName = ArrayElementTypeName(
                SemanticType(expression.arguments[0].get()));
            const auto toIndex = [&](size_t argument, const char* label) {
                llvm::Value* raw = Evaluate(expression.arguments[argument].get());
                if (!raw->getType()->isIntegerTy())
                    Fail("unsafeArrayDrop range must be integers");
                const std::string typeName = SemanticType(expression.arguments[argument].get());
                const bool isUnsigned = typeName.starts_with("uint") || typeName == "char";
                if (!raw->getType()->isIntegerTy(64))
                    raw = builder.CreateIntCast(raw, builder.getInt64Ty(), !isUnsigned, label);
                return raw;
            };
            llvm::Value* first = toIndex(1, "unsafe.array.drop.first");
            llvm::Value* count = toIndex(2, "unsafe.array.drop.count");
            if (SemanticsOfTypeName(elementTypeName).needsDrop) {
                llvm::Function* function = builder.GetInsertBlock()->getParent();
                llvm::BasicBlock* test = llvm::BasicBlock::Create(
                    context, "unsafe.array.drop.test", function);
                llvm::BasicBlock* body = llvm::BasicBlock::Create(
                    context, "unsafe.array.drop.body", function);
                llvm::BasicBlock* done = llvm::BasicBlock::Create(
                    context, "unsafe.array.drop.done", function);
                llvm::Value* limit = builder.CreateAdd(first, count, "unsafe.array.drop.limit");
                llvm::BasicBlock* entry = builder.GetInsertBlock();
                builder.CreateBr(test);
                builder.SetInsertPoint(test);
                llvm::PHINode* index = builder.CreatePHI(
                    builder.getInt64Ty(), 2, "unsafe.array.drop.index");
                index->addIncoming(first, entry);
                builder.CreateCondBr(
                    builder.CreateICmpSLT(index, limit, "unsafe.array.drop.more"), body, done);
                builder.SetInsertPoint(body);
                llvm::Value* address = builder.CreateInBoundsGEP(
                    view.elementType, view.address, index, "unsafe.array.drop.address");
                EmitValueCleanup(address, elementTypeName);
                llvm::Value* next = builder.CreateAdd(
                    index, builder.getInt64(1), "unsafe.array.drop.next");
                index->addIncoming(next, builder.GetInsertBlock());
                builder.CreateBr(test);
                builder.SetInsertPoint(done);
            }
            value = nullptr;
            valueCreatesManagedOwner = false;
            valueCreatesArrayOwner = false;
            valueCreatesClosureOwner = false;
            return;
        }

        if (name == "unsafeArrayData") {
            if (expression.arguments.size() != 1)
                Fail("unsafeArrayData expects exactly one argument");
            ArrayView view = ViewOfArray(expression.arguments.front().get());
            if (view.dimensions.size() != 1)
                Fail("unsafeArrayData requires a one-dimensional array");
            value = view.address;
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

        if (name == "isOwner") {
            if (expression.arguments.size() != 1)
                Fail("isOwner expects exactly one argument");
            Expression* argument = expression.arguments.front().get();
            if (auto* identifier = dynamic_cast<IdentifierExpr*>(argument)) {
                if (Variable* variable = FindVariable(identifier->name);
                    variable && variable->ownershipFlagStorage) {
                    value = builder.CreateLoad(
                        builder.getInt1Ty(),
                        variable->ownershipFlagStorage,
                        "is.owner");
                    return;
                }
            }
            const ExpressionInfo* info = analyzer
                ? analyzer->GetExpressionInfo(*argument) : nullptr;
            const Symbol* symbol = info
                ? analyzer->GetSymbol(info->symbol) : nullptr;
            const bool owns =
                (info && (info->createsManagedOwner ||
                    info->createsArrayOwner ||
                    info->pointerRole == PointerRole::ManagedOwner ||
                    info->pointerRole == PointerRole::RawOwner)) ||
                (symbol && (symbol->managedOwner ||
                    symbol->ownsArrayStorage)) ||
                (info && !info->isLValue &&
                    TypeNeedsCleanup(info->type));
            value = builder.getInt1(owns);
            return;
        }

        if (name == "move") {
            if (expression.arguments.size() != 1) Fail(name + " expects exactly one argument");
            Expression* argument = expression.arguments.front().get();
            const ExpressionInfo* info = analyzer && argument
                ? analyzer->GetExpressionInfo(*argument) : nullptr;
            const bool isLValue = info && info->isLValue;
            if (isLValue) {
                const std::string argumentType = SemanticType(argument);
                if (auto* identifier = dynamic_cast<IdentifierExpr*>(argument)) {
                    if (Variable* variable = FindVariable(identifier->name);
                        variable && variable->ownershipFlagStorage) {
                        llvm::Value* owns = builder.CreateLoad(
                            builder.getInt1Ty(),
                            variable->ownershipFlagStorage,
                            "move.is_owner");
                        EmitOrExit(owns, "move.requires.owner");
                    }
                }
                if (ArrayRankName(argumentType) > 0) {
                    ArrayView source = ViewOfArray(argument);
                    value = BuildArrayDescriptor(source);
                    valueCreatesManagedOwner = false;
                    valueCreatesArrayOwner = true;
                    valueArrayOwner = source.owner;
                    if (auto* identifier = dynamic_cast<IdentifierExpr*>(argument)) {
                        Variable* variable = FindVariable(identifier->name);
                        if (!variable || !variable->arrayOwnerStorage)
                            Fail("moved array variable has no owner storage");
                        builder.CreateStore(
                            llvm::ConstantPointerNull::get(builder.getPtrTy()),
                            variable->arrayOwnerStorage);
                        if (variable->debugStorage)
                            builder.CreateStore(
                                llvm::Constant::getNullValue(
                                    variable->debugStorage->getAllocatedType()),
                                variable->debugStorage);
                        if (variable->ownershipFlagStorage)
                            builder.CreateStore(
                                builder.getFalse(),
                                variable->ownershipFlagStorage);
                    }
                    else {
                        llvm::Value* address = EvaluateAddress(argument);
                        llvm::Type* descriptorType = TypeFromName(argumentType);
                        const llvm::DataLayout& layout = module->getDataLayout();
                        builder.CreateMemSet(
                            address, builder.getInt8(0),
                            layout.getTypeAllocSize(descriptorType).getFixedValue(),
                            llvm::MaybeAlign(layout.getABITypeAlign(descriptorType)));
                    }
                    return;
                }
                llvm::Value* address = EvaluateAddress(argument);
                llvm::Type* type = TypeFromName(argumentType);
                value = builder.CreateLoad(type, address, name + ".value");
                valueCreatesManagedOwner = IsStrongManagedPointerTypeName(argumentType);
                if (auto* identifier = dynamic_cast<IdentifierExpr*>(argument)) {
                    if (Variable* variable = FindVariable(identifier->name);
                        variable && variable->ownershipFlagStorage)
                        builder.CreateStore(
                            builder.getFalse(),
                            variable->ownershipFlagStorage);
                }
                // Clearing the source is what makes a move a transfer, and it
                // is a transfer only where the destination cannot take a count
                // of its own. For a value whose copy counts -- a string, or an
                // aggregate holding one -- a move is a read: it hands back what
                // the source still names, and the store that takes it counts
                // it. Clearing as well meant the source's own count was dropped
                // on the floor, one leak per `unsafeArraySet(items, n,
                // move(element))`, which is how every container stores what it
                // is given.
                const uint64_t size = SizeOfTypeName(argumentType);
                if (!ValueCountsOnCopy(argumentType) && size > 0) {
                    builder.CreateMemSet(
                        address, builder.getInt8(0), size,
                        llvm::MaybeAlign(8));
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
                // The bytes are copied; what they refer to is not. For an
                // element that owns something shared, the copy is one more
                // name holding it and has to say so -- otherwise the two
                // arrays release the same storage. An element that owns
                // something *uniquely* cannot be copied at all, which is what
                // E_COPY_OWNING_ELEMENTS refuses before reaching here.
                const std::string copiedElement = ArrayElementTypeName(typeName, rank);
                if (SemanticsOfTypeName(copiedElement).dropKind ==
                    DropKind::StringStorage) {
                    llvm::Function* function = builder.GetInsertBlock()->getParent();
                    llvm::BasicBlock* entry = builder.GetInsertBlock();
                    llvm::BasicBlock* test = llvm::BasicBlock::Create(
                        context, "copy.retain.test", function);
                    llvm::BasicBlock* body = llvm::BasicBlock::Create(
                        context, "copy.retain.body", function);
                    llvm::BasicBlock* done = llvm::BasicBlock::Create(
                        context, "copy.retain.done", function);
                    builder.CreateBr(test);
                    builder.SetInsertPoint(test);
                    llvm::PHINode* index = builder.CreatePHI(
                        builder.getInt64Ty(), 2, "copy.retain.index");
                    index->addIncoming(builder.getInt64(0), entry);
                    builder.CreateCondBr(
                        builder.CreateICmpSLT(index, elementCount, "copy.retain.more"),
                        body, done);
                    builder.SetInsertPoint(body);
                    llvm::Value* slot = builder.CreateInBoundsGEP(
                        builder.getPtrTy(), copiedData, index, "copy.retain.slot");
                    builder.CreateCall(StringRetain(),
                        {builder.CreateLoad(builder.getPtrTy(), slot, "copy.retain.text")});
                    llvm::Value* next = builder.CreateAdd(
                        index, builder.getInt64(1), "copy.retain.next");
                    index->addIncoming(next, builder.GetInsertBlock());
                    builder.CreateBr(test);
                    builder.SetInsertPoint(done);
                }
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
