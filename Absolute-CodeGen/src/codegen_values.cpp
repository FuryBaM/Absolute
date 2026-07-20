#include "codegen_internal.h"

namespace Absolute {
    // Value lowering is isolated; its implementation is intentionally absent from the private PCH.
    void CodeGenerator::Visit(AssignmentExpr* expr) {
        const std::string targetTypeName = impl->SemanticType(expr->target.get());
        const ExpressionInfo* targetInfo = impl->analyzer
            ? impl->analyzer->GetExpressionInfo(*expr->target) : nullptr;
        const Symbol* targetSymbol = targetInfo && impl->analyzer
            ? impl->analyzer->GetSymbol(targetInfo->symbol) : nullptr;
        if (targetSymbol && targetSymbol->kind == SymbolKind::Property) {
            auto* member = dynamic_cast<MemberAccessExpr*>(expr->target.get());
            Expression* receiver = member ? member->base.get() : nullptr;
            const std::string receiverType = member
                ? impl->SemanticType(member->base.get()) : impl->currentClassName;
            const std::string propertyName = member ? member->member : targetSymbol->name.substr(
                targetSymbol->name.rfind('.') == std::string::npos
                    ? 0 : targetSymbol->name.rfind('.') + 1);
            llvm::Value* assigned = impl->Evaluate(expr->value.get());
            if (expr->op != "=") {
                llvm::Value* current = impl->EmitPropertyAccessor(receiver, receiverType,
                    CallableKey(PropertyGetterName(propertyName), {}), {});
                assigned = impl->ApplyBinary(
                    expr->op.substr(0, expr->op.size() - 1), current, assigned);
            }
            assigned = impl->Coerce(assigned, impl->TypeFromName(targetTypeName));
            impl->EmitPropertyAccessor(receiver, receiverType,
                CallableKey(PropertySetterName(propertyName), {targetTypeName}), {assigned});
            impl->value = assigned;
            impl->valueCreatesManagedOwner = false;
            impl->valueManagedPointee = nullptr;
            return;
        }
        if (targetSymbol && targetSymbol->kind == SymbolKind::Indexer) {
            auto* indexer = dynamic_cast<ArrayAccessExpr*>(expr->target.get());
            if (!indexer) impl->Fail("indexer assignment requires an indexed target");
            const std::string receiverType = impl->SemanticType(indexer->base.get());
            std::vector<llvm::Value*> arguments;
            arguments.reserve(indexer->indexes.size() + 1);
            for (const auto& index : indexer->indexes)
                arguments.push_back(impl->Evaluate(index.get()));
            llvm::Value* assigned = impl->Evaluate(expr->value.get());
            if (expr->op != "=") {
                llvm::Value* current = impl->EmitPropertyAccessor(
                    indexer->base.get(), receiverType,
                    CallableKey(IndexerGetterName(), targetInfo->parameterTypes), arguments);
                assigned = impl->ApplyBinary(
                    expr->op.substr(0, expr->op.size() - 1), current, assigned);
            }
            assigned = impl->Coerce(assigned, impl->TypeFromName(targetTypeName));
            std::vector<std::string> setterTypes = targetInfo->parameterTypes;
            setterTypes.push_back(targetTypeName);
            arguments.push_back(assigned);
            impl->EmitPropertyAccessor(indexer->base.get(), receiverType,
                CallableKey(IndexerSetterName(), setterTypes), arguments);
            impl->value = assigned;
            impl->valueCreatesManagedOwner = false;
            impl->valueManagedPointee = nullptr;
            return;
        }
        if (ArrayRankName(targetTypeName) > 0 &&
            dynamic_cast<MemberAccessExpr*>(expr->target.get()) == nullptr)
            impl->Fail("array variables cannot be reassigned; only array descriptor fields are assignable");
        if (ArrayRankName(targetTypeName) > 0 && expr->op != "=")
            impl->Fail("array fields only support direct '=' assignment");
        llvm::Value* targetAddress = impl->EvaluateAddress(expr->target.get());
        llvm::Type* targetType = impl->TypeFromName(targetTypeName);
        llvm::Value* assigned = impl->Evaluate(expr->value.get());
        const bool createsOwner = impl->valueCreatesManagedOwner;
        llvm::Value* assignedPointee = impl->valueManagedPointee;

        if (expr->op != "=") {
            llvm::Value* current = impl->builder.CreateLoad(targetType, targetAddress, "assignment.current");
            assigned = impl->ApplyBinary(expr->op.substr(0, expr->op.size() - 1), current, assigned);
        }

        if (IsManagedPointerTypeName(targetTypeName)) {
            const std::string name = IdentifierName(expr->target.get());
            if (!name.empty()) {
                Impl::Variable& variable = impl->RequireVariable(name);
                if (variable.managedOwner) {
                    llvm::Value* oldHandle = impl->builder.CreateLoad(variable.type, variable.address, "assignment.old.handle");
                    impl->builder.CreateCall(impl->ManagedDestroy(), {oldHandle});
                    if (createsOwner && assignedPointee) {
                        if (!variable.managedPointee)
                            variable.managedPointee = impl->CreateEntryAlloca(
                                *impl->CurrentFunction(), impl->builder.getPtrTy(), name + ".cached.pointee");
                        impl->builder.CreateStore(assignedPointee, variable.managedPointee);
                    }
                    else if (variable.managedPointee)
                        impl->builder.CreateStore(
                            llvm::ConstantPointerNull::get(impl->builder.getPtrTy()), variable.managedPointee);
                }
            }
        }
        assigned = impl->Coerce(assigned, targetType);
        impl->builder.CreateStore(assigned, targetAddress);
        impl->value = assigned;
        impl->valueCreatesManagedOwner = false;
        impl->valueManagedPointee = nullptr;
    }

    void CodeGenerator::Visit(VarDeclExpr* expr) {
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("global variables are not implemented yet");
        if (impl->scopes.empty()) impl->Fail("variable declaration outside a scope");

        const std::string name = IdentifierName(expr->name.get());
        if (name.empty()) impl->Fail("variable declaration requires an identifier");
        const std::string baseTypeName = impl->ResolveTypeName(expr->type.get());
        const std::string declaredTypeName = impl->DeclaredTypeName(*expr);
        if (auto* arrayDeclarator = dynamic_cast<ArrayAccessExpr*>(expr->name.get())) {
            if (arrayDeclarator->indexes.empty()) impl->Fail("array declaration requires a dimension");
            llvm::Type* elementType = impl->TypeFromName(baseTypeName);
            std::vector<llvm::Value*> dimensions;
            dimensions.reserve(arrayDeclarator->indexes.size());

            auto* literal = dynamic_cast<ArrayExpr*>(expr->value.get());
            const auto inferredShape = literal ? InferArrayShape(*literal)
                : std::optional<std::vector<size_t>>{};
            for (size_t dimension = 0; dimension < arrayDeclarator->indexes.size(); ++dimension) {
                llvm::Value* size = nullptr;
                if (arrayDeclarator->indexes[dimension]) {
                    size = impl->Coerce(
                        impl->Evaluate(arrayDeclarator->indexes[dimension].get()),
                        impl->builder.getInt64Ty());
                }
                else if (inferredShape && dimension < inferredShape->size()) {
                    size = impl->builder.getInt64((*inferredShape)[dimension]);
                }
                else impl->Fail("array dimension requires a size or an initializer");
                impl->EmitOrExit(
                    impl->builder.CreateICmpSGT(size, impl->builder.getInt64(0), "array.size.positive"),
                    "array.size");
                dimensions.push_back(size);
            }

            llvm::Value* elementCount = impl->builder.getInt64(1);
            for (llvm::Value* dimension : dimensions)
                elementCount = impl->builder.CreateMul(elementCount, dimension, "array.element.count");
            llvm::AllocaInst* address = impl->builder.CreateAlloca(elementType, elementCount, name);
            address->setAlignment(llvm::Align(16));
            llvm::Value* byteCount = impl->builder.CreateMul(
                elementCount,
                impl->builder.getInt64(impl->SizeOfTypeName(baseTypeName)),
                "array.byte.count");
            impl->builder.CreateMemSet(
                address, impl->builder.getInt8(0), byteCount, llvm::MaybeAlign(16));

            Impl::Variable variable;
            variable.address = address;
            variable.type = elementType;
            variable.typeName = declaredTypeName;
            variable.isArray = true;
            variable.arrayElementType = elementType;
            variable.arrayDimensions = dimensions;
            variable.symbol = impl->SemanticSymbol(expr);
            if (!impl->scopes.back().emplace(name, std::move(variable)).second)
                impl->Fail("duplicate variable '" + name + "'");

            if (literal) {
                std::vector<Expression*> values;
                FlattenArrayValues(*literal, values);
                for (size_t index = 0; index < values.size(); ++index) {
                    llvm::Value* initial = impl->Coerce(impl->Evaluate(values[index]), elementType);
                    llvm::Value* destination = impl->builder.CreateInBoundsGEP(
                        elementType, address, impl->builder.getInt64(index), "array.initializer.element");
                    impl->builder.CreateStore(initial, destination);
                }
            }
            else if (expr->value) impl->Fail("array variable requires an array literal initializer");

            impl->value = address;
            impl->valueCreatesManagedOwner = false;
            return;
        }
        if (ArrayRankName(declaredTypeName) > 0) {
            if (!expr->value) impl->Fail("array view declaration requires an initializer");
            llvm::Value* descriptor = impl->Evaluate(expr->value.get());
            const bool ownsStorage = impl->valueCreatesArrayOwner;
            Impl::ArrayView view = impl->ArrayViewFromValue(descriptor, declaredTypeName);
            Impl::Variable variable;
            variable.address = view.address;
            variable.type = view.elementType;
            variable.typeName = declaredTypeName;
            variable.isArray = true;
            variable.arrayElementType = view.elementType;
            variable.arrayDimensions = view.dimensions;
            variable.symbol = impl->SemanticSymbol(expr);
            variable.arrayOwner = view.owner;
            variable.ownsArrayStorage = ownsStorage;
            if (ownsStorage) variable.arrayOwnerSymbol = variable.symbol;
            else if (Impl::Variable* source = impl->FindVariable(
                impl->SemanticSymbol(expr->value.get()))) {
                variable.arrayOwnerSymbol = source->ownsArrayStorage
                    ? source->symbol : source->arrayOwnerSymbol;
            }
            if (!impl->scopes.back().emplace(name, std::move(variable)).second) {
                impl->Fail("duplicate variable '" + name + "'");
            }
            impl->value = impl->BuildArrayDescriptor(view);
            impl->valueCreatesManagedOwner = false;
            impl->valueCreatesArrayOwner = false;
            impl->valueArrayOwner = nullptr;
            return;
        }
        const std::string& typeName = declaredTypeName;
        llvm::Type* type = impl->TypeFromName(typeName);
        if (type->isVoidTy()) impl->Fail("variable '" + name + "' cannot have type void");

        llvm::AllocaInst* address = impl->CreateEntryAlloca(*function, type, name);
        const SymbolId symbol = impl->SemanticSymbol(expr);
        const bool staticOwner = IsManagedPointerTypeName(typeName) && impl->StaticManagedOwner(symbol);
        if (!impl->scopes.back().emplace(name,
            Impl::Variable{address, type, typeName, staticOwner, false, nullptr, {},
                nullptr, symbol}).second) {
            impl->Fail("duplicate variable '" + name + "'");
        }

        llvm::Value* initial = expr->value ? impl->Evaluate(expr->value.get()) : llvm::Constant::getNullValue(type);
        const bool createsOwner = impl->valueCreatesManagedOwner;
        llvm::Value* managedPointee = impl->valueManagedPointee;
        initial = impl->Coerce(initial, type);
        impl->builder.CreateStore(initial, address);
        if (staticOwner && createsOwner && managedPointee) {
            Impl::Variable& variable = impl->RequireVariable(name);
            variable.managedPointee = impl->CreateEntryAlloca(
                *function, impl->builder.getPtrTy(), name + ".cached.pointee");
            impl->builder.CreateStore(managedPointee, variable.managedPointee);
        }
        impl->value = initial;
        impl->valueCreatesManagedOwner = false;
        impl->valueManagedPointee = nullptr;
    }

    void CodeGenerator::Visit(MemberAccessExpr* expr) {
        if (impl->analyzer) {
            const ExpressionInfo* info = impl->analyzer->GetExpressionInfo(*expr);
            const Symbol* symbol = info ? impl->analyzer->GetSymbol(info->symbol) : nullptr;
            if (symbol && symbol->kind == SymbolKind::Property) {
                if (impl->addressMode) impl->Fail("a property is not addressable");
                const std::string baseType = impl->SemanticType(expr->base.get());
                impl->value = impl->EmitPropertyAccessor(expr->base.get(), baseType,
                    CallableKey(PropertyGetterName(expr->member), {}), {});
                impl->valueCreatesManagedOwner = false;
                return;
            }
            if (symbol && symbol->kind == SymbolKind::Field && symbol->isStatic) {
                const std::string globalName = symbol->memberOwner + "." + expr->member;
                auto field = impl->globals.find(globalName);
                if (field == impl->globals.end())
                    impl->Fail("missing static field '" + globalName + "'");
                if (impl->addressMode) {
                    impl->addressValue = field->second.address;
                    return;
                }
                impl->value = impl->builder.CreateLoad(
                    field->second.type, field->second.address, expr->member + ".static.value");
                impl->valueCreatesManagedOwner = false;
                return;
            }
            const auto constant = symbol ? impl->enumConstants.find(symbol->name) : impl->enumConstants.end();
            if (constant != impl->enumConstants.end()) {
                if (impl->addressMode) impl->Fail("an enum constant is not assignable");
                impl->value = impl->builder.getInt32(constant->second);
                impl->valueCreatesManagedOwner = false;
                return;
            }
        }
        const std::string baseType = impl->SemanticType(expr->base.get());
        const std::string aggregateName = impl->ClassNameFromType(baseType);
        llvm::Value* fieldAddress = nullptr;
        std::string fieldTypeName;
        if (auto found = impl->classes.find(aggregateName); found != impl->classes.end()) {
            const auto field = found->second.fieldByName.find(expr->member);
            if (field == found->second.fieldByName.end())
                impl->Fail("class '" + found->second.name + "' has no field '" + expr->member + "'");
            llvm::Value* object = impl->ObjectPointer(expr->base.get(), baseType);
            fieldAddress = impl->FieldAddress(object, found->second, field->second);
            fieldTypeName = field->second.typeName;
        }
        else if (auto found = impl->structs.find(aggregateName); found != impl->structs.end()) {
            const auto field = found->second.fieldByName.find(expr->member);
            if (field == found->second.fieldByName.end())
                impl->Fail("struct '" + found->second.name + "' has no field '" + expr->member + "'");
            llvm::Value* object = impl->ObjectPointer(expr->base.get(), baseType);
            fieldAddress = impl->FieldAddress(object, found->second, field->second);
            fieldTypeName = field->second.typeName;
        }
        else impl->Fail("type '" + baseType + "' does not support member access");
        if (impl->addressMode) {
            impl->addressValue = fieldAddress;
            return;
        }
        impl->value = impl->builder.CreateLoad(
            impl->TypeFromName(fieldTypeName), fieldAddress, expr->member + ".value");
        impl->valueCreatesManagedOwner = false;
    }

    void CodeGenerator::Visit(CastExpr* expr) {
        const std::string sourceType = impl->SemanticType(expr->base.get());
        const std::string declaredTarget = impl->ResolveTypeName(expr->typeName.get());
        const std::string runtimeTarget = IsPointerTypeName(declaredTarget)
            ? PointerPointeeName(declaredTarget) : declaredTarget;
        const bool runtimeOperation = expr->operation == "is" ||
            (IsPointerTypeName(sourceType) &&
                (impl->classes.contains(runtimeTarget) || impl->interfaces.contains(runtimeTarget)));
        if (runtimeOperation) {
            llvm::Value* reference = impl->Evaluate(expr->base.get());
            llvm::Value* sourcePointee = impl->valueManagedPointee;
            const ExpressionInfo* sourceInfo = impl->analyzer
                ? impl->analyzer->GetExpressionInfo(*expr->base) : nullptr;
            llvm::Value* matches = impl->EmitRuntimeTypeTest(
                reference, sourceType, runtimeTarget);
            if (expr->operation == "is") {
                impl->value = matches;
                impl->valueCreatesManagedOwner = false;
                impl->valueManagedPointee = nullptr;
                return;
            }

            llvm::Constant* nullReference = llvm::Constant::getNullValue(reference->getType());
            const bool temporaryManagedOwner = sourceInfo && sourceInfo->createsManagedOwner;
            const bool temporaryRawOwner = sourceInfo && sourceInfo->createsRawOwner;
            if (temporaryManagedOwner || temporaryRawOwner) {
                llvm::Function* function = impl->CurrentFunction();
                llvm::BasicBlock* success = llvm::BasicBlock::Create(
                    impl->context, "safe.cast.success", function);
                llvm::BasicBlock* failure = llvm::BasicBlock::Create(
                    impl->context, "safe.cast.failure", function);
                llvm::BasicBlock* complete = llvm::BasicBlock::Create(
                    impl->context, "safe.cast.end", function);
                impl->builder.CreateCondBr(matches, success, failure);
                impl->builder.SetInsertPoint(success);
                impl->builder.CreateBr(complete);
                success = impl->builder.GetInsertBlock();
                impl->builder.SetInsertPoint(failure);
                if (temporaryManagedOwner)
                    impl->builder.CreateCall(impl->ManagedDestroy(), {reference});
                if (temporaryRawOwner)
                    impl->builder.CreateCall(impl->Free(), {reference});
                impl->builder.CreateBr(complete);
                failure = impl->builder.GetInsertBlock();
                impl->builder.SetInsertPoint(complete);
                llvm::PHINode* cast = impl->builder.CreatePHI(
                    reference->getType(), 2, "safe.cast.result");
                cast->addIncoming(reference, success);
                cast->addIncoming(nullReference, failure);
                impl->value = cast;
            }
            else impl->value = impl->builder.CreateSelect(
                matches, reference, nullReference, "safe.cast.result");

            const ExpressionInfo* resultInfo = impl->analyzer
                ? impl->analyzer->GetExpressionInfo(*expr) : nullptr;
            impl->valueCreatesManagedOwner = resultInfo && resultInfo->createsManagedOwner;
            impl->valueManagedPointee = impl->valueCreatesManagedOwner && sourcePointee
                ? impl->builder.CreateSelect(matches, sourcePointee,
                    llvm::ConstantPointerNull::get(impl->builder.getPtrTy()),
                    "safe.cast.pointee")
                : nullptr;
            return;
        }
        llvm::Type* target = impl->ResolveType(expr->typeName.get());
        impl->value = impl->Coerce(impl->Evaluate(expr->base.get()), target);
    }

    void CodeGenerator::Visit(ConstructorCallExpr* expr) {
        const std::string allocationType = impl->SemanticType(expr);
        const std::string pointeeType = allocationType.empty()
            ? impl->ResolveTypeName(expr->constructName.get())
            : PointerPointeeName(allocationType);
        const bool rawAllocation = IsRawPointerTypeName(allocationType) ||
            (allocationType.empty() && expr->raw);
        llvm::Type* pointee = impl->TypeFromName(pointeeType);
        llvm::Value* pointer = nullptr;
        llvm::Value* result = nullptr;
        const bool classAllocation = impl->classes.contains(pointeeType);
        const bool structAllocation = impl->structs.contains(pointeeType);
        llvm::Value* allocationSize = classAllocation
            ? impl->ObjectSize(impl->classes.at(pointeeType))
            : (structAllocation ? impl->ObjectSize(impl->structs.at(pointeeType))
                : impl->builder.getInt64(impl->SizeOfTypeName(pointeeType)));
        if (rawAllocation) {
            pointer = impl->builder.CreateCall(
                impl->Malloc(), {allocationSize}, "raw.allocation");
            result = pointer;
        }
        else {
            llvm::Value* handle = impl->builder.CreateCall(
                impl->ManagedCreate(), {allocationSize}, "managed.handle");
            if (classAllocation && impl->exceptionsEnabled) {
                impl->builder.CreateCall(impl->ManagedSetType(), {handle,
                    impl->builder.getInt64(impl->ExceptionTypeId(pointeeType))});
            }
            pointer = impl->builder.CreateCall(impl->ManagedGet(true), {handle}, "managed.allocation");
            result = handle;
        }

        if (classAllocation) {
            Impl::ClassInfo& info = impl->classes.at(pointeeType);
            impl->InitializeObject(pointer, info);
            if (llvm::Function* constructor = impl->module->getFunction(info.name + ".__ctor")) {
                std::vector<llvm::Value*> arguments;
                std::vector<std::string> parameterTypes;
                for (const auto& argument : expr->arguments)
                    arguments.push_back(impl->Evaluate(argument.get()));
                if (info.constructor) {
                    for (const auto& parameter : info.constructor->parameters)
                        parameterTypes.push_back(SubstituteCodegenType(
                            impl->DeclaredTypeName(*parameter), info.substitutions));
                }
                impl->EmitAbiCall(constructor->getFunctionType(), constructor, "void",
                    {pointer}, parameterTypes, arguments, "constructor.result");
                impl->EmitExceptionCheck(
                    rawAllocation ? nullptr : result,
                    rawAllocation ? pointer : nullptr);
            }
            else if (!expr->arguments.empty())
                impl->Fail("class '" + info.name + "' has no constructor");
            impl->value = result;
            impl->valueCreatesManagedOwner = !rawAllocation;
            impl->valueManagedPointee = rawAllocation ? nullptr : pointer;
            return;
        }

        if (structAllocation) {
            Impl::StructInfo& info = impl->structs.at(pointeeType);
            impl->InitializeObject(pointer, info);
            if (info.constructor) {
                llvm::Function* constructor = impl->module->getFunction(info.name + ".__ctor");
                if (!constructor) impl->Fail("missing constructor for '" + info.name + "'");
                std::vector<llvm::Value*> arguments;
                std::vector<std::string> parameterTypes;
                for (const auto& argument : expr->arguments)
                    arguments.push_back(impl->Evaluate(argument.get()));
                for (const auto& parameter : info.constructor->parameters)
                    parameterTypes.push_back(SubstituteCodegenType(
                        impl->DeclaredTypeName(*parameter), info.substitutions));
                impl->EmitAbiCall(constructor->getFunctionType(), constructor, "void",
                    {pointer}, parameterTypes, arguments, "constructor.result");
                impl->EmitExceptionCheck(
                    rawAllocation ? nullptr : result,
                    rawAllocation ? pointer : nullptr);
            }
            else if (!expr->arguments.empty())
                impl->Fail("struct '" + info.name + "' has no constructor");
            impl->value = result;
            impl->valueCreatesManagedOwner = !rawAllocation;
            impl->valueManagedPointee = rawAllocation ? nullptr : pointer;
            return;
        }

        llvm::Value* initial = expr->arguments.empty()
            ? llvm::Constant::getNullValue(pointee)
            : impl->Evaluate(expr->arguments.front().get());
        initial = impl->Coerce(initial, pointee);
        impl->builder.CreateStore(initial, pointer);
        impl->value = result;
        impl->valueCreatesManagedOwner = !rawAllocation;
        impl->valueManagedPointee = rawAllocation ? nullptr : pointer;
    }

    void CodeGenerator::Visit(DestructorCallExpr* expr) {
        const std::string typeName = impl->SemanticType(expr->target.get());
        llvm::Value* targetAddress = impl->EvaluateAddress(expr->target.get());
        llvm::Type* type = impl->TypeFromName(typeName);
        llvm::Value* pointer = impl->builder.CreateLoad(type, targetAddress, "delete.target");
        if (IsManagedPointerTypeName(typeName)) {
            const std::string name = IdentifierName(expr->target.get());
            if (!name.empty()) {
                Impl::Variable& variable = impl->RequireVariable(name);
                if (variable.managedPointee)
                    impl->builder.CreateStore(
                        llvm::ConstantPointerNull::get(impl->builder.getPtrTy()), variable.managedPointee);
            }
            impl->builder.CreateCall(impl->ManagedDestroy(), {pointer});
            impl->builder.CreateStore(impl->builder.getInt64(0), targetAddress);
        }
        else {
            impl->builder.CreateCall(impl->Free(), {pointer});
            impl->builder.CreateStore(llvm::ConstantPointerNull::get(impl->builder.getPtrTy()), targetAddress);
        }
        impl->value = nullptr;
        impl->valueCreatesManagedOwner = false;
    }

    void CodeGenerator::Visit(InstanceDeclExpr* expr) {
        llvm::Function* function = impl->CurrentFunction();
        if (!function || impl->scopes.empty()) impl->Fail("object declaration outside a function");
        const std::string name = IdentifierName(expr->identifierName.get());
        const std::string typeName = impl->SemanticType(expr);
        if (!impl->classes.contains(typeName) && !impl->structs.contains(typeName)) {
            if (ArrayRankName(typeName) > 0) {
                if (!expr->value) impl->Fail("array alias variable requires an initializer");
                llvm::Value* descriptor = impl->Evaluate(expr->value.get());
                const bool ownsStorage = impl->valueCreatesArrayOwner;
                Impl::ArrayView view = impl->ArrayViewFromValue(descriptor, typeName);
                Impl::Variable variable;
                variable.address = view.address;
                variable.type = view.elementType;
                variable.typeName = typeName;
                variable.isArray = true;
                variable.arrayElementType = view.elementType;
                variable.arrayDimensions = view.dimensions;
                variable.symbol = impl->SemanticSymbol(expr);
                variable.arrayOwner = view.owner;
                variable.ownsArrayStorage = ownsStorage;
                if (ownsStorage) variable.arrayOwnerSymbol = variable.symbol;
                else if (Impl::Variable* source = impl->FindVariable(
                    impl->SemanticSymbol(expr->value.get()))) {
                    variable.arrayOwnerSymbol = source->ownsArrayStorage
                        ? source->symbol : source->arrayOwnerSymbol;
                }
                if (!impl->scopes.back().emplace(name, std::move(variable)).second) {
                    impl->Fail("duplicate array variable '" + name + "'");
                }
                impl->value = impl->BuildArrayDescriptor(view);
                impl->valueCreatesManagedOwner = false;
                impl->valueCreatesArrayOwner = false;
                impl->valueArrayOwner = nullptr;
                return;
            }
            llvm::Type* type = impl->TypeFromName(typeName);
            if (type->isVoidTy()) impl->Fail("variable '" + name + "' cannot have type void");
            llvm::AllocaInst* address = impl->CreateEntryAlloca(*function, type, name);
            const SymbolId symbol = impl->SemanticSymbol(expr);
            const bool staticOwner = IsManagedPointerTypeName(typeName) &&
                impl->StaticManagedOwner(symbol);
            llvm::Value* initial = expr->value
                ? impl->Coerce(impl->Evaluate(expr->value.get()), type)
                : llvm::Constant::getNullValue(type);
            const bool createsOwner = impl->valueCreatesManagedOwner;
            llvm::Value* managedPointee = impl->valueManagedPointee;
            impl->builder.CreateStore(initial, address);
            if (!impl->scopes.back().emplace(name,
                Impl::Variable{address, type, typeName, staticOwner, false, nullptr, {},
                    nullptr, symbol}).second)
                impl->Fail("duplicate variable '" + name + "'");
            if (staticOwner && createsOwner && managedPointee) {
                Impl::Variable& variable = impl->RequireVariable(name);
                variable.managedPointee = impl->CreateEntryAlloca(
                    *function, impl->builder.getPtrTy(), name + ".cached.pointee");
                impl->builder.CreateStore(managedPointee, variable.managedPointee);
            }
            impl->value = initial;
            impl->valueCreatesManagedOwner = false;
            impl->valueManagedPointee = nullptr;
            return;
        }
        llvm::StructType* llvmType = nullptr;
        std::string constructorName;
        if (auto found = impl->classes.find(typeName); found != impl->classes.end()) {
            llvmType = found->second.llvmType;
            constructorName = found->second.name + ".__ctor";
        }
        else if (auto found = impl->structs.find(typeName); found != impl->structs.end()) {
            llvmType = found->second.llvmType;
            constructorName = found->second.name + ".__ctor";
        }
        else impl->Fail("type '" + typeName + "' is not a class or struct");

        llvm::AllocaInst* address = impl->CreateEntryAlloca(*function, llvmType, name);
        if (auto found = impl->classes.find(typeName); found != impl->classes.end())
            impl->InitializeObject(address, found->second);
        else impl->InitializeObject(address, impl->structs.at(typeName));
        if (expr->value) {
            llvm::Value* initial = impl->Coerce(impl->Evaluate(expr->value.get()), llvmType);
            impl->builder.CreateStore(initial, address);
        }
        else if (llvm::Function* constructor = impl->module->getFunction(constructorName);
            constructor && constructor->arg_size() == 1) {
            impl->builder.CreateCall(constructor, {address});
            impl->EmitExceptionCheck();
        }
        if (!impl->scopes.back().emplace(name,
            Impl::Variable{address, llvmType, typeName, false, false, nullptr, {},
                nullptr, impl->SemanticSymbol(expr)}).second)
            impl->Fail("duplicate object '" + name + "'");
        impl->value = impl->builder.CreateLoad(llvmType, address, name + ".value");
        impl->valueCreatesManagedOwner = false;
    }

    void CodeGenerator::Visit(PrefixUnaryExpr* expr) {
        if (expr->op == "spawn") {
            FunctionCallProbe probe;
            if (expr->operand) expr->operand->Accept(probe);
            if (!probe.call) impl->Fail("spawn requires a direct async function call");
            impl->value = impl->EmitSpawn(*probe.call);
            impl->valueCreatesManagedOwner = false;
            return;
        }
        if (expr->op == "await") {
            impl->value = impl->EmitAwait(*expr);
            impl->valueCreatesManagedOwner = false;
            return;
        }
        if (expr->op == "&") {
            impl->value = impl->EvaluateAddress(expr->operand.get());
            impl->valueCreatesManagedOwner = false;
            return;
        }
        if (expr->op == "*") {
            const std::string pointerType = impl->SemanticType(expr->operand.get());
            const std::string pointeeName = PointerPointeeName(pointerType);
            const bool outerAddressMode = impl->addressMode;
            impl->addressMode = false;
            llvm::Value* pointer = impl->Evaluate(expr->operand.get());
            impl->addressMode = outerAddressMode;
            llvm::Value* pointeeAddress = IsManagedPointerTypeName(pointerType)
                ? impl->ManagedPointee(expr->operand.get(), pointer)
                : pointer;
            if (outerAddressMode) {
                impl->addressValue = pointeeAddress;
                return;
            }
            impl->value = impl->builder.CreateLoad(
                impl->TypeFromName(pointeeName), pointeeAddress, "pointer.value");
            impl->valueCreatesManagedOwner = false;
            return;
        }
        if (expr->op == "++" || expr->op == "--") {
            const ExpressionInfo* operandInfo = impl->analyzer
                ? impl->analyzer->GetExpressionInfo(*expr->operand) : nullptr;
            const Symbol* operandSymbol = operandInfo && impl->analyzer
                ? impl->analyzer->GetSymbol(operandInfo->symbol) : nullptr;
            if (operandSymbol && operandSymbol->kind == SymbolKind::Property) {
                auto* member = dynamic_cast<MemberAccessExpr*>(expr->operand.get());
                Expression* receiver = member ? member->base.get() : nullptr;
                const std::string receiverType = member
                    ? impl->SemanticType(member->base.get()) : impl->currentClassName;
                const std::string propertyName = member ? member->member : operandSymbol->name.substr(
                    operandSymbol->name.rfind('.') == std::string::npos
                        ? 0 : operandSymbol->name.rfind('.') + 1);
                llvm::Value* current = impl->EmitPropertyAccessor(receiver, receiverType,
                    CallableKey(PropertyGetterName(propertyName), {}), {});
                llvm::Value* updated = impl->ApplyBinary(expr->op == "++" ? "+" : "-",
                    current, impl->One(current->getType()));
                impl->EmitPropertyAccessor(receiver, receiverType,
                    CallableKey(PropertySetterName(propertyName), {operandInfo->type}), {updated});
                impl->value = updated;
                return;
            }
            if (operandSymbol && operandSymbol->kind == SymbolKind::Indexer) {
                auto* indexer = dynamic_cast<ArrayAccessExpr*>(expr->operand.get());
                if (!indexer) impl->Fail("indexer increment requires an indexed operand");
                std::vector<llvm::Value*> arguments;
                for (const auto& index : indexer->indexes)
                    arguments.push_back(impl->Evaluate(index.get()));
                const std::string receiverType = impl->SemanticType(indexer->base.get());
                llvm::Value* current = impl->EmitPropertyAccessor(
                    indexer->base.get(), receiverType,
                    CallableKey(IndexerGetterName(), operandInfo->parameterTypes), arguments);
                llvm::Value* updated = impl->ApplyBinary(expr->op == "++" ? "+" : "-",
                    current, impl->One(current->getType()));
                std::vector<std::string> setterTypes = operandInfo->parameterTypes;
                setterTypes.push_back(operandInfo->type);
                arguments.push_back(updated);
                impl->EmitPropertyAccessor(indexer->base.get(), receiverType,
                    CallableKey(IndexerSetterName(), setterTypes), arguments);
                impl->value = updated;
                return;
            }
            Impl::Variable& variable = impl->AddressOf(expr->operand.get());
            llvm::Value* current = impl->builder.CreateLoad(variable.type, variable.address, "prefix.current");
            llvm::Value* updated = impl->ApplyBinary(expr->op == "++" ? "+" : "-", current, impl->One(variable.type));
            impl->builder.CreateStore(updated, variable.address);
            impl->value = updated;
            return;
        }

        llvm::Value* operand = impl->Evaluate(expr->operand.get());
        if (expr->op == "+") impl->value = operand;
        else if (expr->op == "-") {
            impl->value = operand->getType()->isFloatingPointTy()
                ? impl->builder.CreateFNeg(operand, "negate")
                : impl->builder.CreateNeg(operand, "negate");
        }
        else if (expr->op == "!") impl->value = impl->builder.CreateNot(impl->AsCondition(operand), "logical.not");
        else if (expr->op == "~") impl->value = impl->builder.CreateNot(operand, "bit.not");
        else impl->Fail("unsupported prefix operator '" + expr->op + "'");
    }

    void CodeGenerator::Visit(PostfixUnaryExpr* expr) {
        if (expr->op != "++" && expr->op != "--") {
            impl->Fail("unsupported postfix operator '" + expr->op + "'");
        }
        const ExpressionInfo* operandInfo = impl->analyzer
            ? impl->analyzer->GetExpressionInfo(*expr->operand) : nullptr;
        const Symbol* operandSymbol = operandInfo && impl->analyzer
            ? impl->analyzer->GetSymbol(operandInfo->symbol) : nullptr;
        if (operandSymbol && operandSymbol->kind == SymbolKind::Property) {
            auto* member = dynamic_cast<MemberAccessExpr*>(expr->operand.get());
            Expression* receiver = member ? member->base.get() : nullptr;
            const std::string receiverType = member
                ? impl->SemanticType(member->base.get()) : impl->currentClassName;
            const std::string propertyName = member ? member->member : operandSymbol->name.substr(
                operandSymbol->name.rfind('.') == std::string::npos
                    ? 0 : operandSymbol->name.rfind('.') + 1);
            llvm::Value* current = impl->EmitPropertyAccessor(receiver, receiverType,
                CallableKey(PropertyGetterName(propertyName), {}), {});
            llvm::Value* updated = impl->ApplyBinary(expr->op == "++" ? "+" : "-",
                current, impl->One(current->getType()));
            impl->EmitPropertyAccessor(receiver, receiverType,
                CallableKey(PropertySetterName(propertyName), {operandInfo->type}), {updated});
            impl->value = current;
            return;
        }
        if (operandSymbol && operandSymbol->kind == SymbolKind::Indexer) {
            auto* indexer = dynamic_cast<ArrayAccessExpr*>(expr->operand.get());
            if (!indexer) impl->Fail("indexer increment requires an indexed operand");
            std::vector<llvm::Value*> arguments;
            for (const auto& index : indexer->indexes)
                arguments.push_back(impl->Evaluate(index.get()));
            const std::string receiverType = impl->SemanticType(indexer->base.get());
            llvm::Value* current = impl->EmitPropertyAccessor(
                indexer->base.get(), receiverType,
                CallableKey(IndexerGetterName(), operandInfo->parameterTypes), arguments);
            llvm::Value* updated = impl->ApplyBinary(expr->op == "++" ? "+" : "-",
                current, impl->One(current->getType()));
            std::vector<std::string> setterTypes = operandInfo->parameterTypes;
            setterTypes.push_back(operandInfo->type);
            arguments.push_back(updated);
            impl->EmitPropertyAccessor(indexer->base.get(), receiverType,
                CallableKey(IndexerSetterName(), setterTypes), arguments);
            impl->value = current;
            return;
        }
        Impl::Variable& variable = impl->AddressOf(expr->operand.get());
        llvm::Value* current = impl->builder.CreateLoad(variable.type, variable.address, "postfix.current");
        llvm::Value* updated = impl->ApplyBinary(expr->op == "++" ? "+" : "-", current, impl->One(variable.type));
        impl->builder.CreateStore(updated, variable.address);
        impl->value = current;
    }

    void CodeGenerator::Visit(TemplateExpr* expr) {
        (void)expr;
        impl->Fail("generic specialization is not implemented yet");
    }

}
