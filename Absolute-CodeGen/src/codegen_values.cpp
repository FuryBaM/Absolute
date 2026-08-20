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
            assigned = impl->Coerce(assigned, impl->TypeFromName(targetTypeName),
                impl->SemanticType(expr->value.get()), targetTypeName);
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
            assigned = impl->Coerce(assigned, impl->TypeFromName(targetTypeName),
                impl->SemanticType(expr->value.get()), targetTypeName);
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
            dynamic_cast<MemberAccessExpr*>(expr->target.get()) == nullptr &&
            (!targetSymbol || (targetSymbol->kind != SymbolKind::Field &&
                targetSymbol->kind != SymbolKind::Property)))
            impl->Fail("array variables cannot be reassigned; only array descriptor fields are assignable");
        if (ArrayRankName(targetTypeName) > 0 && expr->op != "=")
            impl->Fail("array fields only support direct '=' assignment");
        llvm::Value* targetAddress = impl->EvaluateAddress(expr->target.get());
        llvm::Type* targetType = impl->TypeFromName(targetTypeName);
        llvm::Value* assigned = impl->Evaluate(expr->value.get());
        const bool createsOwner = impl->valueCreatesManagedOwner;
        llvm::Value* assignedPointee = impl->valueManagedPointee;
        const bool createsClosureOwner = impl->valueCreatesClosureOwner;
        std::string closureReturn;
        std::vector<std::string> closureParameters;
        const bool functionValue = ParseCodegenFunctionType(
            targetTypeName, closureReturn, closureParameters);

        if (expr->op != "=" && IsRawPointerTypeName(targetTypeName)) {
            // `p += n` steps by elements, so it goes through the same offset
            // the binary form uses. ApplyBinary works on numbers and would
            // have to treat the address as one.
            llvm::Value* current = impl->builder.CreateLoad(targetType, targetAddress, "assignment.current");
            assigned = impl->PointerOffset(current, targetTypeName, assigned, expr->op == "-=");
        }
        else if (expr->op != "=") {
            llvm::Value* current = impl->builder.CreateLoad(targetType, targetAddress, "assignment.current");
            // With the type names, so a compound assignment picks the same
            // instruction its spelled-out form would. Without them this call
            // went through the untyped overload and every unsigned `/=`, `%=`
            // and `>>=` was emitted signed while `a = a / b` was not:
            // 4294967295u /= 2 gave zero where the same division gave
            // 2147483647.
            assigned = impl->ApplyBinary(
                expr->op.substr(0, expr->op.size() - 1), current, assigned,
                targetTypeName, impl->SemanticType(expr->value.get()));
        }

        if (IsManagedPointerTypeName(targetTypeName)) {
            const std::string name = IdentifierName(expr->target.get());
            if (!name.empty() && (!targetSymbol ||
                targetSymbol->kind == SymbolKind::Variable ||
                targetSymbol->kind == SymbolKind::Parameter)) {
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
        // A string being stored is one more name for the bytes -- unless the
        // value made them, in which case it is already counted once. Retained
        // before the old value is released, because the two can be the same
        // storage: `text = text` must not free what it is about to store.
        if (targetTypeName == "string")
            assigned = impl->RetainStoredString(expr->value.get(), assigned);
        // The same one level up: an aggregate is assigned by copying its
        // bytes, which duplicates the pointers its parts hold without
        // duplicating their counts. The target is one more name for them, and
        // it gives back what it held. Counted through a spill because the
        // walk needs an address, and counted before the old value is released
        // for the reason the string comment above gives.
        const TypeSemantics targetSemantics =
            impl->SemanticsOfTypeName(targetTypeName);
        const bool countsParts = targetSemantics.copyable &&
            (targetSemantics.dropKind == DropKind::TupleValue ||
             targetSemantics.dropKind == DropKind::ClassObject ||
             targetSemantics.dropKind == DropKind::StructObject);
        if (countsParts && assigned && !functionValue &&
            !impl->CreatesFreshString(expr->value.get())) {
            llvm::AllocaInst* counted = impl->CreateEntryAlloca(
                *impl->CurrentFunction(), assigned->getType(), "assignment.counted");
            impl->builder.CreateStore(assigned, counted);
            impl->EmitValueRetain(counted, targetTypeName);
            assigned = impl->builder.CreateLoad(
                assigned->getType(), counted, "assignment.counted.value");
        }
        if (targetSymbol && targetSymbol->kind == SymbolKind::Field &&
            impl->TypeNeedsCleanup(targetTypeName) && !functionValue) {
            impl->EmitValueCleanup(targetAddress, targetTypeName);
        }
        // A local or a parameter holding a string releases what it held before
        // taking the new one, the same way a field does -- and so does one
        // holding an aggregate whose parts are counted, which is the same
        // rule read one level up.
        else if ((targetTypeName == "string" || countsParts) && targetSymbol &&
            (targetSymbol->kind == SymbolKind::Variable ||
             targetSymbol->kind == SymbolKind::Parameter)) {
            if (const std::string name = IdentifierName(expr->target.get());
                !name.empty()) {
                if (Impl::Variable* variable = impl->FindVariable(name);
                    variable && variable->ownsAggregateResources)
                    impl->EmitValueCleanup(targetAddress, targetTypeName);
            }
        }
        if (functionValue) {
            if (!createsClosureOwner)
                impl->builder.CreateCall(impl->ClosureRetain(), {assigned});
            impl->EmitValueCleanup(targetAddress, targetTypeName);
        }
        assigned = impl->Coerce(assigned, targetType,
            impl->SemanticType(expr->value.get()), targetTypeName);
        llvm::Value* store = impl->builder.CreateStore(assigned, targetAddress);
        // An element of an array and a field of an object are different
        // storage, and saying so is what lets the optimizer keep an array's
        // data pointer and its length in registers across a loop.
        if (dynamic_cast<ArrayAccessExpr*>(expr->target.get()))
            impl->TagAccess(store, impl->TbaaElementAccess(targetTypeName));
        else if (dynamic_cast<MemberAccessExpr*>(expr->target.get()) ||
            (!impl->FindVariable(IdentifierName(expr->target.get())) &&
                !IdentifierName(expr->target.get()).empty()))
            impl->TagAccess(store, impl->TbaaFieldAccess(targetTypeName));
        impl->value = assigned;
        impl->valueCreatesManagedOwner = false;
        impl->valueManagedPointee = nullptr;
        impl->valueCreatesArrayOwner = false;
        impl->valueArrayOwner = nullptr;
        impl->valueCreatesClosureOwner = false;
    }

    void CodeGenerator::Visit(VarDeclExpr* expr) {
        impl->SetDebugLocation(expr);
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("global variables are not implemented yet");
        if (impl->scopes.empty()) impl->Fail("variable declaration outside a scope");

        const std::string name = IdentifierName(expr->name.get());
        if (name.empty()) impl->Fail("variable declaration requires an identifier");
        const std::string baseTypeName = impl->ResolveTypeName(expr->type.get());
        const std::string declaredTypeName = impl->DeclaredTypeName(*expr);
        std::vector<Expression*> arrayDeclaratorIndexes;
        CollectArrayDeclaratorIndexes(expr->name.get(), arrayDeclaratorIndexes);
        if (!arrayDeclaratorIndexes.empty()) {
            llvm::Type* elementType = impl->TypeFromName(baseTypeName);
            std::vector<llvm::Value*> dimensions;
            dimensions.reserve(arrayDeclaratorIndexes.size());

            auto* literal = dynamic_cast<ArrayExpr*>(expr->value.get());
            const auto inferredShape = literal ? InferArrayStorageShape(
                *literal, arrayDeclaratorIndexes.size())
                : std::optional<std::vector<size_t>>{};
            for (size_t dimension = 0; dimension < arrayDeclaratorIndexes.size(); ++dimension) {
                llvm::Value* size = nullptr;
                if (arrayDeclaratorIndexes[dimension]) {
                    size = impl->Coerce(
                        impl->Evaluate(arrayDeclaratorIndexes[dimension]),
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
            variable.arrayOwnerStorage = impl->CreateEntryAlloca(
                *function, impl->builder.getPtrTy(), name + ".array.owner");
            impl->builder.CreateStore(
                llvm::ConstantPointerNull::get(impl->builder.getPtrTy()),
                variable.arrayOwnerStorage);
            // The frame provides the storage and keeps it, but what the
            // elements hold is still this name's to give back. Without this an
            // `Entry fixed[2] = { ... }` in a loop lost one string an
            // iteration: the owner pointer is null, and the release walk was
            // guarded by it, because for a *view* a null owner means someone
            // else holds the elements. A frame array is not a view.
            variable.ownsArrayElements =
                impl->SemanticsOfTypeName(baseTypeName).needsDrop;
            if (!impl->scopes.back().emplace(name, std::move(variable)).second)
                impl->Fail("duplicate variable '" + name + "'");

            if (impl->debugBuilder) {
                Impl::ArrayView debugView;
                debugView.address = address;
                debugView.elementType = elementType;
                debugView.typeName = declaredTypeName;
                debugView.dimensions = dimensions;
                debugView.owner = llvm::ConstantPointerNull::get(
                    impl->builder.getPtrTy());
                llvm::Value* debugDescriptor =
                    impl->BuildArrayDescriptor(debugView);
                llvm::AllocaInst* debugStorage = impl->CreateEntryAlloca(
                    *function, debugDescriptor->getType(),
                    name + ".debug.descriptor");
                impl->builder.CreateStore(debugDescriptor, debugStorage);
                impl->DeclareDebugVariable(
                    name, declaredTypeName, debugStorage, expr);
                impl->RequireVariable(name).debugStorage = debugStorage;
            }

            if (literal) {
                std::vector<Expression*> values;
                FlattenArrayValues(*literal, values);
                for (size_t index = 0; index < values.size(); ++index) {
                    llvm::Value* initial = impl->Coerce(
                        impl->Evaluate(values[index]), elementType,
                        impl->SemanticType(values[index]), baseTypeName);
                    llvm::Value* destination = impl->builder.CreateInBoundsGEP(
                        elementType, address, impl->builder.getInt64(index), "array.initializer.element");
                    // A slot this array releases is a name that counts, the
                    // same as a literal's or one `unsafeArraySet` writes.
                    if (baseTypeName == "string")
                        initial = impl->RetainStoredString(values[index], initial);
                    impl->builder.CreateStore(initial, destination);
                    if (baseTypeName != "string" &&
                        !impl->CreatesFreshString(values[index]))
                        impl->EmitValueRetain(destination, baseTypeName);
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
            variable.arrayOwnerStorage = impl->CreateEntryAlloca(
                *function, impl->builder.getPtrTy(), name + ".array.owner");
            impl->builder.CreateStore(
                view.owner ? view.owner
                    : llvm::ConstantPointerNull::get(impl->builder.getPtrTy()),
                variable.arrayOwnerStorage);
            if (ownsStorage) variable.arrayOwnerSymbol = variable.symbol;
            else if (Impl::Variable* source = impl->FindVariable(
                impl->SemanticSymbol(expr->value.get()))) {
                variable.arrayOwnerSymbol = source->ownsArrayStorage
                    ? source->symbol : source->arrayOwnerSymbol;
            }
            if (!impl->scopes.back().emplace(name, std::move(variable)).second) {
                impl->Fail("duplicate variable '" + name + "'");
            }
            if (impl->debugBuilder) {
                llvm::AllocaInst* debugStorage = impl->CreateEntryAlloca(
                    *function, descriptor->getType(),
                    name + ".debug.descriptor");
                impl->builder.CreateStore(descriptor, debugStorage);
                impl->DeclareDebugVariable(
                    name, declaredTypeName, debugStorage, expr);
                impl->RequireVariable(name).debugStorage = debugStorage;
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
        const bool createsClosureOwner = impl->valueCreatesClosureOwner;
        std::string closureReturn;
        std::vector<std::string> closureParameters;
        const bool functionValue = ParseCodegenFunctionType(
            typeName, closureReturn, closureParameters);
        if (functionValue && !createsClosureOwner)
            impl->builder.CreateCall(impl->ClosureRetain(), {initial});
        // The same for a string, for the same reason: the variable is one more
        // name holding the bytes, and it says so again when it goes out of
        // scope. A fresh one already counts as held once.
        const bool stringValue = typeName == "string";
        if (stringValue && expr->value)
            initial = impl->RetainStoredString(expr->value.get(), initial);
        initial = impl->Coerce(initial, type,
            expr->value ? impl->SemanticType(expr->value.get()) : typeName, typeName);
        impl->builder.CreateStore(initial, address);
        impl->DeclareDebugVariable(
            name, typeName, address, expr);
        // A value that is not a handle releases what its parts hold when its
        // name goes out of scope, and says so on the way in when the parts
        // arrived already held by someone else. A managed pointer is excluded:
        // whether a local owns the object it names is a question about the
        // symbol, not the type, and `staticOwner` answers it.
        // A tuple reaches here and nowhere else -- it is in neither `classes`
        // nor `structs` -- so before this a `tuple<int64, string>` local
        // dropped the string it carried.
        const TypeSemantics semantics = impl->SemanticsOfTypeName(typeName);
        const bool ownsParts = semantics.needsDrop &&
            semantics.dropKind != DropKind::ManagedOwner;
        if (semantics.dropKind == DropKind::TupleValue && expr->value &&
            !impl->CreatesFreshString(expr->value.get()))
            impl->EmitValueRetain(address, typeName);
        if (ownsParts)
            impl->RequireVariable(name).ownsAggregateResources = true;
        if (staticOwner && createsOwner && managedPointee) {
            Impl::Variable& variable = impl->RequireVariable(name);
            variable.managedPointee = impl->CreateEntryAlloca(
                *function, impl->builder.getPtrTy(), name + ".cached.pointee");
            impl->builder.CreateStore(managedPointee, variable.managedPointee);
        }
        impl->value = initial;
        impl->valueCreatesManagedOwner = false;
        impl->valueManagedPointee = nullptr;
        impl->valueCreatesClosureOwner = false;
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
        if (ArrayRankName(baseType) > 0 && (expr->member == "length" || expr->member == "count")) {
            if (impl->addressMode) impl->Fail("array length property is not assignable");
            llvm::Value* descriptor = impl->EvaluateBorrowed(expr->base.get());
            if (descriptor->getType()->isPointerTy()) {
                llvm::LoadInst* load = impl->builder.CreateLoad(
                    impl->ArrayDescriptorType(baseType), descriptor, "array.descriptor");
                load->setMetadata(llvm::LLVMContext::MD_invariant_load, llvm::MDNode::get(impl->context, {}));
                descriptor = load;
            }
            llvm::Value* length64 = impl->builder.CreateExtractValue(descriptor, {2}, "array.length.i64");
            impl->value = impl->builder.CreateTrunc(length64, impl->builder.getInt32Ty(), "array.length");
            impl->valueCreatesManagedOwner = false;
            return;
        }

        std::string tupleBase;
        std::vector<std::string> tupleElements;
        if (ParseCodegenGenericType(baseType, tupleBase, tupleElements) &&
            tupleBase == "tuple") {
            if (expr->member == "length" || expr->member == "count") {
                if (impl->addressMode)
                    impl->Fail("tuple length property is not assignable");
                impl->value = impl->builder.getInt32(
                    static_cast<std::uint32_t>(tupleElements.size()));
                impl->valueCreatesManagedOwner = false;
                return;
            }
            if (!expr->member.starts_with("item"))
                impl->Fail("tuple has no member '" + expr->member + "'");
            size_t index = tupleElements.size();
            try {
                index = static_cast<size_t>(
                    std::stoull(expr->member.substr(4)));
            }
            catch (...) {
                impl->Fail("invalid tuple member '" + expr->member + "'");
            }
            if (index >= tupleElements.size())
                impl->Fail("tuple member index is out of range");
            auto* tupleType = llvm::cast<llvm::StructType>(
                impl->TypeFromName(baseType));
            if (impl->addressMode) {
                llvm::Value* baseAddress =
                    impl->EvaluateAddress(expr->base.get());
                impl->addressValue = impl->builder.CreateStructGEP(
                    tupleType, baseAddress, static_cast<unsigned>(index),
                    expr->member + ".address");
                return;
            }
            llvm::Value* tuple = impl->Evaluate(expr->base.get());
            impl->value = impl->builder.CreateExtractValue(
                tuple, {static_cast<unsigned>(index)}, expr->member + ".value");
            impl->valueCreatesManagedOwner = false;
            return;
        }

        const std::string aggregateName = impl->ClassNameFromType(baseType);

        llvm::Value* fieldAddress = nullptr;
        std::string fieldTypeName;
        if (auto found = impl->classes.find(aggregateName); found != impl->classes.end()) {
            const auto field = found->second.fieldByName.find(expr->member);
            if (field == found->second.fieldByName.end())
                impl->Fail("class '" + found->second.name + "' has no field '" + expr->member + "'");
            llvm::Value* object = impl->ObjectPointer(expr->base.get(), baseType);
            impl->RegisterAggregateTemporaryBase(expr->base.get(), object, baseType);
            fieldAddress = impl->FieldAddress(object, found->second, field->second);
            fieldTypeName = field->second.typeName;
        }
        else if (auto found = impl->structs.find(aggregateName); found != impl->structs.end()) {
            const auto field = found->second.fieldByName.find(expr->member);
            if (field == found->second.fieldByName.end())
                impl->Fail("struct '" + found->second.name + "' has no field '" + expr->member + "'");
            llvm::Value* object = impl->ObjectPointer(expr->base.get(), baseType);
            impl->RegisterAggregateTemporaryBase(expr->base.get(), object, baseType);
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
        impl->TagAccess(impl->value, impl->TbaaFieldAccess(fieldTypeName));
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
            const bool baseCreatesManagedOwner = impl->valueCreatesManagedOwner;
            llvm::Value* sourcePointee = impl->valueManagedPointee;
            const ExpressionInfo* sourceInfo = impl->analyzer
                ? impl->analyzer->GetExpressionInfo(*expr->base) : nullptr;
            llvm::Value* matches = impl->EmitRuntimeTypeTest(
                reference, sourceType, runtimeTarget);
            if (expr->operation == "is") {
                // `is` answers a question about the object and gives back a
                // bool, so an owner asked about -- `make() is Base` -- is
                // nobody's afterwards. It waits for the end of the statement.
                if (baseCreatesManagedOwner) impl->RegisterTemporaryOwner(reference, sourceType);
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
        impl->value = impl->Coerce(impl->Evaluate(expr->base.get()), target,
            sourceType, declaredTarget);
    }

    void CodeGenerator::Visit(ConstructorCallExpr* expr) {
        const std::string allocationType = impl->SemanticType(expr);
        const std::string arrayTypeName = allocationType.empty()
            ? impl->ResolveTypeName(expr->constructName.get())
            : allocationType;
        if (ArrayRankName(arrayTypeName) > 0) {
            const size_t rank = ArrayRankName(arrayTypeName);
            const std::string elemTypeName = ArrayElementTypeName(arrayTypeName, rank);
            llvm::Type* elemType = impl->TypeFromName(elemTypeName);
            llvm::Value* count = expr->arguments.empty()
                ? impl->builder.getInt64(0)
                : impl->Coerce(impl->Evaluate(expr->arguments[0].get()),
                    impl->builder.getInt64Ty());
            llvm::Value* elemSize = impl->builder.getInt64(impl->SizeOfTypeName(elemTypeName));
            // calloc, not malloc: "Array storage is zero-initialized" is a
            // documented promise, and it was not kept. A fresh `new int64[16]`
            // handed back whatever the allocator had there, so a program that
            // read an element it had not written yet got a different answer at
            // -O0 than at -O3, and a different one again under a sanitizer --
            // three answers to a question the language says has one.
            llvm::Value* dataPtr = impl->builder.CreateCall(
                impl->Calloc(), {count, elemSize}, "array.data.alloc");
            Impl::ArrayView view;
            view.address = dataPtr;
            view.elementType = elemType;
            view.typeName = arrayTypeName;
            view.dimensions = {count};
            view.owner = dataPtr;
            impl->value = impl->BuildArrayDescriptor(view);
            impl->valueCreatesManagedOwner = false;
            impl->valueManagedPointee = nullptr;
            // `new T[n]` produces an owner, and saying so is what connects it to
            // the release that already exists: a scope frees the array storage
            // it owns, a call frees an array temporary the callee did not take.
            // Without these two lines the buffer was allocated and never freed
            // -- every local array in every program -- and only -O0 showed it,
            // because at -O1 and above the optimizer deletes an allocation
            // nothing reads back.
            impl->valueCreatesArrayOwner = true;
            impl->valueArrayOwner = dataPtr;
            return;
        }
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
            pointer = impl->EmitManagedGet(handle, true);
            result = handle;
        }

        if (classAllocation) {
            Impl::ClassInfo& info = impl->classes.at(pointeeType);
            impl->InitializeObject(pointer, info);
            if (llvm::Function* constructor = impl->LookupConstructorFunction(info.name, expr)) {
                std::vector<llvm::Value*> arguments;
                std::vector<llvm::Value*> ownershipFlags;
                std::vector<std::string> parameterTypes;
                if (const ExpressionInfo* callInfo = impl->analyzer
                    ? impl->analyzer->GetExpressionInfo(*expr) : nullptr)
                    parameterTypes = callInfo->parameterTypes;
                else if (info.constructors.size() == 1)
                    parameterTypes = impl->ConstructorParameterTypeNames(
                        info.constructors.front(), info.substitutions);
                std::vector<llvm::Value*> temporaryArrays;
                std::vector<llvm::Value*> temporaryClosures;
                for (size_t index = 0; index < expr->arguments.size(); ++index) {
                    const std::string parameterType =
                        index < parameterTypes.size()
                        ? parameterTypes[index] : std::string{};
                    ownershipFlags.push_back(impl->ArgumentOwnershipFlag(
                        expr->arguments[index].get(), parameterType));
                    arguments.push_back(impl->EvaluateCallArgument(
                        expr->arguments[index].get(), temporaryArrays, temporaryClosures,
                        parameterType));
                }
                impl->EmitAbiCall(constructor->getFunctionType(), constructor, "void",
                    {pointer}, parameterTypes, arguments, "constructor.result",
                    false, ownershipFlags);
                impl->EmitExceptionCheck(
                    rawAllocation ? nullptr : result,
                    rawAllocation ? pointer : nullptr);
            }
            else if (!expr->arguments.empty() || !info.constructors.empty())
                impl->Fail("class '" + info.name + "' has no matching constructor");
            impl->value = result;
            impl->valueCreatesManagedOwner = !rawAllocation;
            impl->valueManagedPointee = rawAllocation ? nullptr : pointer;
            return;
        }

        if (structAllocation) {
            Impl::StructInfo& info = impl->structs.at(pointeeType);
            impl->InitializeObject(pointer, info);
            if (!info.constructors.empty()) {
                llvm::Function* constructor = impl->LookupConstructorFunction(info.name, expr);
                if (!constructor) impl->Fail("missing constructor for '" + info.name + "'");
                std::vector<llvm::Value*> arguments;
                std::vector<llvm::Value*> ownershipFlags;
                std::vector<std::string> parameterTypes;
                if (const ExpressionInfo* callInfo = impl->analyzer
                    ? impl->analyzer->GetExpressionInfo(*expr) : nullptr)
                    parameterTypes = callInfo->parameterTypes;
                else if (info.constructors.size() == 1)
                    parameterTypes = impl->ConstructorParameterTypeNames(
                        info.constructors.front(), info.substitutions);
                std::vector<llvm::Value*> temporaryArrays;
                std::vector<llvm::Value*> temporaryClosures;
                for (size_t index = 0; index < expr->arguments.size(); ++index) {
                    const std::string parameterType =
                        index < parameterTypes.size()
                        ? parameterTypes[index] : std::string{};
                    ownershipFlags.push_back(impl->ArgumentOwnershipFlag(
                        expr->arguments[index].get(), parameterType));
                    arguments.push_back(impl->EvaluateCallArgument(
                        expr->arguments[index].get(), temporaryArrays, temporaryClosures,
                        parameterType));
                }
                impl->EmitAbiCall(constructor->getFunctionType(), constructor, "void",
                    {pointer}, parameterTypes, arguments, "constructor.result",
                    false, ownershipFlags);
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
                if (variable.ownershipFlagStorage) {
                    llvm::Value* owns = impl->builder.CreateLoad(
                        impl->builder.getInt1Ty(),
                        variable.ownershipFlagStorage,
                        "delete.is_owner");
                    impl->EmitOrExit(owns, "delete.requires.owner");
                    impl->builder.CreateStore(
                        impl->builder.getFalse(),
                        variable.ownershipFlagStorage);
                }
                if (variable.managedPointee)
                    impl->builder.CreateStore(
                        llvm::ConstantPointerNull::get(impl->builder.getPtrTy()), variable.managedPointee);
            }
            llvm::Value* pointee = impl->EmitManagedGet(pointer, false);
            impl->EmitPointeeCleanup(pointee, typeName);
            impl->builder.CreateCall(impl->ManagedDestroy(), {pointer});
            impl->builder.CreateStore(impl->builder.getInt64(0), targetAddress);
        }
        else {
            impl->EmitPointeeCleanup(pointer, typeName);
            impl->builder.CreateCall(impl->Free(), {pointer});
            impl->builder.CreateStore(llvm::ConstantPointerNull::get(impl->builder.getPtrTy()), targetAddress);
        }
        impl->value = nullptr;
        impl->valueCreatesManagedOwner = false;
    }

    void CodeGenerator::Visit(InstanceDeclExpr* expr) {
        impl->SetDebugLocation(expr);
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
                variable.arrayOwnerStorage = impl->CreateEntryAlloca(
                    *function, impl->builder.getPtrTy(), name + ".array.owner");
                impl->builder.CreateStore(
                    view.owner ? view.owner
                        : llvm::ConstantPointerNull::get(impl->builder.getPtrTy()),
                    variable.arrayOwnerStorage);
                if (ownsStorage) variable.arrayOwnerSymbol = variable.symbol;
                else if (Impl::Variable* source = impl->FindVariable(
                    impl->SemanticSymbol(expr->value.get()))) {
                    variable.arrayOwnerSymbol = source->ownsArrayStorage
                        ? source->symbol : source->arrayOwnerSymbol;
                }
                if (!impl->scopes.back().emplace(name, std::move(variable)).second) {
                    impl->Fail("duplicate array variable '" + name + "'");
                }
                if (impl->debugBuilder) {
                    llvm::AllocaInst* debugStorage = impl->CreateEntryAlloca(
                        *function, descriptor->getType(),
                        name + ".debug.descriptor");
                    impl->builder.CreateStore(descriptor, debugStorage);
                    impl->DeclareDebugVariable(
                        name, typeName, debugStorage, expr);
                    impl->RequireVariable(name).debugStorage = debugStorage;
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
                ? impl->Evaluate(expr->value.get())
                : llvm::Constant::getNullValue(type);
            const bool createsClosureOwner = impl->valueCreatesClosureOwner;
            std::string closureReturn;
            std::vector<std::string> closureParameters;
            const bool functionValue = ParseCodegenFunctionType(
                typeName, closureReturn, closureParameters);
            if (functionValue && !createsClosureOwner)
                impl->builder.CreateCall(impl->ClosureRetain(), {initial});
            // The same for a string, for the same reason: the variable is one
            // more name holding the bytes, and it will say so again when it
            // goes out of scope.
            const bool stringValue = typeName == "string";
            if (stringValue && expr->value)
                initial = impl->RetainStoredString(expr->value.get(), initial);
            initial = impl->Coerce(initial, type);
            const bool createsOwner = impl->valueCreatesManagedOwner;
            llvm::Value* managedPointee = impl->valueManagedPointee;
            impl->builder.CreateStore(initial, address);
            // A value that is not a handle releases what its parts hold when
            // its name goes out of scope, and says so on the way in if the
            // parts arrived already held by someone else. A managed pointer is
            // excluded because whether a local owns the object it names is a
            // question about the symbol, not the type, and `staticOwner`
            // answers it. A tuple reaches here and nowhere else: it is not in
            // `classes` or `structs`, so before this a `tuple<int64, string>`
            // local dropped its string on the floor.
            const TypeSemantics semantics = impl->SemanticsOfTypeName(typeName);
            const bool ownsParts = semantics.needsDrop &&
                semantics.dropKind != DropKind::ManagedOwner;
            if (semantics.dropKind == DropKind::TupleValue && expr->value &&
                !impl->CreatesFreshString(expr->value.get()))
                impl->EmitValueRetain(address, typeName);
            if (!impl->scopes.back().emplace(name,
                Impl::Variable{address, type, typeName, staticOwner, false, nullptr, {},
                    nullptr, symbol}).second)
                impl->Fail("duplicate variable '" + name + "'");
            impl->DeclareDebugVariable(
                name, typeName, address, expr);
            if (ownsParts)
                impl->RequireVariable(name).ownsAggregateResources = true;
            if (staticOwner && createsOwner && managedPointee) {
                Impl::Variable& variable = impl->RequireVariable(name);
                variable.managedPointee = impl->CreateEntryAlloca(
                    *function, impl->builder.getPtrTy(), name + ".cached.pointee");
                impl->builder.CreateStore(managedPointee, variable.managedPointee);
            }
            impl->value = initial;
            impl->valueCreatesManagedOwner = false;
            impl->valueManagedPointee = nullptr;
            impl->valueCreatesClosureOwner = false;
            return;
        }
        llvm::StructType* llvmType = nullptr;
        if (auto found = impl->classes.find(typeName); found != impl->classes.end())
            llvmType = found->second.llvmType;
        else if (auto found = impl->structs.find(typeName); found != impl->structs.end())
            llvmType = found->second.llvmType;
        else impl->Fail("type '" + typeName + "' is not a class or struct");

        llvm::AllocaInst* address = impl->CreateEntryAlloca(*function, llvmType, name);
        if (auto found = impl->classes.find(typeName); found != impl->classes.end())
            impl->InitializeObject(address, found->second);
        else impl->InitializeObject(address, impl->structs.at(typeName));
        if (expr->value) {
            llvm::Value* initial = impl->Coerce(impl->Evaluate(expr->value.get()), llvmType);
            impl->builder.CreateStore(initial, address);
            // The bytes of the aggregate are copied; what its parts hold is
            // not, so the parts are told there is a second name. Without this
            // a `Header entry = entries[index];` released the vector's strings
            // when the local went out of scope. A value the expression made
            // itself arrives already counted.
            if (!impl->CreatesFreshString(expr->value.get()))
                impl->EmitValueRetain(address, typeName);
        }
        else if (llvm::Function* constructor =
            impl->module->getFunction(impl->ConstructorLinkName(typeName, {}));
            constructor && constructor->arg_size() == 1) {
            // Zero-argument constructor (declared or synthetic).
            impl->builder.CreateCall(constructor, {address});
            impl->EmitExceptionCheck();
        }
        Impl::Variable variable{address, llvmType, typeName, false, false, nullptr, {},
            nullptr, impl->SemanticSymbol(expr)};
        variable.ownsAggregateResources = impl->TypeNeedsCleanup(typeName);
        if (!impl->scopes.back().emplace(name, std::move(variable)).second)
            impl->Fail("duplicate object '" + name + "'");
        impl->DeclareDebugVariable(
            name, typeName, address, expr);
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
            // A raw pointer steps by one element, not by one byte, so it takes
            // the same offset the binary and compound forms take.
            const std::string operandType = impl->SemanticType(expr->operand.get());
            llvm::Value* updated = IsRawPointerTypeName(operandType)
                ? impl->PointerOffset(current, operandType,
                    impl->builder.getInt64(1), expr->op == "--")
                : impl->ApplyBinary(expr->op == "++" ? "+" : "-", current, impl->One(variable.type));
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
        const std::string operandType = impl->SemanticType(expr->operand.get());
        llvm::Value* updated = IsRawPointerTypeName(operandType)
            ? impl->PointerOffset(current, operandType,
                impl->builder.getInt64(1), expr->op == "--")
            : impl->ApplyBinary(expr->op == "++" ? "+" : "-", current, impl->One(variable.type));
        impl->builder.CreateStore(updated, variable.address);
        impl->value = current;
    }

    void CodeGenerator::Visit(TemplateExpr* expr) {
        (void)expr;
        impl->Fail("generic specialization is not implemented yet");
    }

    void CodeGenerator::Visit(LambdaExpr* expr) {
        const std::string lambdaType = impl->SemanticType(expr);
        std::string returnType;
        std::vector<std::string> parameterTypes;
        if (!ParseCodegenFunctionType(lambdaType, returnType, parameterTypes))
            impl->Fail("invalid lambda type '" + lambdaType + "'");

        const std::uint64_t lambdaId = impl->lambdaCounter++;
        const std::vector<LambdaCapture>& captures = impl->analyzer
            ? impl->analyzer->LambdaCaptures(*expr)
            : std::vector<LambdaCapture>{};
        std::vector<llvm::Type*> captureTypes;
        captureTypes.reserve(captures.size());
        for (const LambdaCapture& capture : captures)
            captureTypes.push_back(impl->TypeFromName(capture.type));
        llvm::StructType* environmentType = captures.empty() ? nullptr
            : llvm::StructType::create(impl->context, captureTypes,
                "absolute.lambda.environment." + std::to_string(lambdaId));

        llvm::FunctionType* functionType = impl->ClosureInvokeType(
            returnType, parameterTypes);
        llvm::Function* lambda = llvm::Function::Create(functionType,
            llvm::Function::InternalLinkage,
            "__absolute.lambda." + std::to_string(lambdaId), *impl->module);
        lambda->setCallingConv(llvm::CallingConv::C);

        const auto savedInsertPoint = impl->builder.saveIP();
        auto savedScopes = std::move(impl->scopes);
        auto savedLoops = std::move(impl->loops);
        auto savedExceptionTargets = std::move(impl->exceptionTargets);
        auto savedFinallyTargets = std::move(impl->finallyTargets);
        auto savedCaughtExceptions = std::move(impl->caughtExceptions);
        auto savedDeferredScopes = std::move(impl->deferredScopes);
        const std::string savedReturnType = impl->currentReturnTypeName;
        llvm::Value* savedReturnStorage = impl->currentReturnStorage;
        const std::string savedClass = impl->currentClassName;
        llvm::Value* savedThis = impl->currentThis;
        // A lambda body emitted inside a constructor is a separate function that
        // may run long after construction finished, so it must not inherit the
        // constructor's partial-object cleanup.
        const std::string savedConstructorClass = impl->currentConstructorClass;
        impl->currentConstructorClass.clear();
        const auto savedSubstitutions = impl->currentGenericSubstitutions;
        llvm::DIScope* savedDebugScope = impl->currentDebugScope;
        const auto savedDebugScopeStack = impl->debugScopeStack;
        const llvm::DebugLoc savedDebugLocation =
            impl->builder.getCurrentDebugLocation();

        llvm::BasicBlock* entry = llvm::BasicBlock::Create(impl->context, "entry", lambda);
        impl->builder.SetInsertPoint(entry);
        impl->scopes.clear();
        impl->loops.clear();
        impl->exceptionTargets.clear();
        impl->finallyTargets.clear();
        impl->caughtExceptions.clear();
        impl->deferredScopes.clear();
        // A lambda body is emitted in the middle of the body that writes it, so
        // it needs the same isolation a function body gets: an owner produced
        // here belongs to this function's blocks and must be released in them.
        std::vector<Impl::TemporaryOwner> enclosingTemporaries;
        enclosingTemporaries.swap(impl->temporaryManagedOwners);
        impl->PushScope();
        impl->BeginDebugFunction(
            *lambda, expr, "lambda." + std::to_string(lambdaId));
        impl->currentReturnTypeName = returnType;
        impl->currentClassName.clear();
        impl->currentThis = nullptr;
        unsigned argumentIndex = 0;
        impl->currentReturnStorage = impl->AbiReturnOffset(returnType) != 0
            ? lambda->getArg(argumentIndex++) : nullptr;
        llvm::Argument* environment = lambda->getArg(argumentIndex++);
        environment->setName("environment");
        for (size_t index = 0; index < captures.size(); ++index) {
            llvm::Value* address = impl->builder.CreateStructGEP(
                environmentType, environment, static_cast<unsigned>(index),
                captures[index].name + ".capture.address");
            Impl::Variable variable;
            variable.address = address;
            variable.type = captureTypes[index];
            variable.typeName = captures[index].type;
            variable.symbol = captures[index].symbol;
            if (!impl->scopes.back().emplace(captures[index].name,
                std::move(variable)).second)
                impl->Fail("duplicate lambda capture '" + captures[index].name + "'");
            impl->DeclareDebugVariable(
                captures[index].name, captures[index].type,
                address, expr);
            if (captures[index].name == "this") {
                impl->currentThis = impl->builder.CreateLoad(
                    captureTypes[index], address, "lambda.this");
                impl->currentClassName = PointerPointeeName(captures[index].type);
            }
        }
        for (size_t index = 0; index < expr->parameters.size(); ++index) {
            llvm::Argument* argument = lambda->getArg(argumentIndex++);
            argument->setName(IdentifierName(expr->parameters[index]->name.get()));
            llvm::Argument* ownershipArgument =
                impl->ParameterSupportsOwnershipName(parameterTypes[index])
                ? lambda->getArg(argumentIndex++) : nullptr;
            impl->BindCallableParameter(*argument, *expr->parameters[index],
                parameterTypes[index], false, ownershipArgument);
        }

        if (expr->expressionBody) {
            // The body is one expression and the return terminates the block,
            // so what it borrowed is released by hand, before the terminator.
            const size_t temporaryMark = impl->temporaryManagedOwners.size();
            llvm::Value* result = impl->Evaluate(expr->expressionBody.get());
            const bool createsClosureOwner = impl->valueCreatesClosureOwner;
            impl->ReleaseTemporaryOwners(temporaryMark);
            std::string nestedReturn;
            std::vector<std::string> nestedParameters;
            if (ParseCodegenFunctionType(returnType, nestedReturn, nestedParameters) &&
                !createsClosureOwner)
                impl->builder.CreateCall(impl->ClosureRetain(), {result});
            result = impl->Coerce(result, impl->TypeFromName(returnType));
            if (impl->currentReturnStorage) {
                impl->builder.CreateStore(result, impl->currentReturnStorage);
                impl->builder.CreateRetVoid();
            }
            else impl->builder.CreateRet(result);
        }
        else if (expr->statementBody) {
            expr->statementBody->Accept(*this);
            if (impl->builder.GetInsertBlock() &&
                !impl->builder.GetInsertBlock()->getTerminator()) {
                if (returnType == "void") impl->builder.CreateRetVoid();
                else impl->Fail("lambda body reached code generation without a return");
            }
        }

        if (!impl->temporaryManagedOwners.empty())
            impl->Fail("temporary owner left unreleased in a lambda body");
        impl->temporaryManagedOwners.swap(enclosingTemporaries);

        std::string verifierMessage;
        llvm::raw_string_ostream verifierStream(verifierMessage);
        if (llvm::verifyFunction(*lambda, &verifierStream)) {
            verifierStream.flush();
            impl->Fail("invalid lambda: " + verifierMessage);
        }

        impl->PopScope();
        impl->EndDebugFunction();
        impl->scopes = std::move(savedScopes);
        impl->loops = std::move(savedLoops);
        impl->exceptionTargets = std::move(savedExceptionTargets);
        impl->finallyTargets = std::move(savedFinallyTargets);
        impl->caughtExceptions = std::move(savedCaughtExceptions);
        impl->deferredScopes = std::move(savedDeferredScopes);
        impl->currentReturnTypeName = savedReturnType;
        impl->currentReturnStorage = savedReturnStorage;
        impl->currentClassName = savedClass;
        impl->currentThis = savedThis;
        impl->currentConstructorClass = savedConstructorClass;
        impl->currentGenericSubstitutions = savedSubstitutions;
        impl->builder.restoreIP(savedInsertPoint);
        impl->currentDebugScope = savedDebugScope;
        impl->debugScopeStack = savedDebugScopeStack;
        impl->builder.SetCurrentDebugLocation(savedDebugLocation);

        llvm::Value* environmentValue = llvm::ConstantPointerNull::get(
            impl->builder.getPtrTy());
        llvm::Function* destroyEnvironment = nullptr;
        if (!captures.empty()) {
            llvm::Value* environmentSize = impl->Coerce(
                llvm::ConstantExpr::getSizeOf(environmentType),
                impl->builder.getInt64Ty());
            environmentValue = impl->builder.CreateCall(
                impl->Malloc(), {environmentSize}, "lambda.environment");
            for (size_t index = 0; index < captures.size(); ++index) {
                Impl::Variable* source = impl->FindVariable(captures[index].symbol);
                if (!source)
                    impl->Fail("missing captured value '" + captures[index].name + "'");
                llvm::Value* captured = impl->builder.CreateLoad(
                    source->type, source->address, captures[index].name + ".captured");
                std::string capturedReturn;
                std::vector<std::string> capturedParameters;
                if (ParseCodegenFunctionType(captures[index].type,
                    capturedReturn, capturedParameters))
                    impl->builder.CreateCall(impl->ClosureRetain(), {captured});
                llvm::Value* destination = impl->builder.CreateStructGEP(
                    environmentType, environmentValue, static_cast<unsigned>(index),
                    captures[index].name + ".environment.address");
                impl->builder.CreateStore(captured, destination);
            }

            llvm::FunctionType* destroyType = llvm::FunctionType::get(
                impl->builder.getVoidTy(), {impl->builder.getPtrTy()}, false);
            destroyEnvironment = llvm::Function::Create(destroyType,
                llvm::Function::InternalLinkage,
                "__absolute.lambda.destroy." + std::to_string(lambdaId), *impl->module);
            const auto destroySaved = impl->builder.saveIP();
            const llvm::DebugLoc destroySavedDebugLocation =
                impl->builder.getCurrentDebugLocation();
            llvm::BasicBlock* destroyEntry = llvm::BasicBlock::Create(
                impl->context, "entry", destroyEnvironment);
            impl->builder.SetInsertPoint(destroyEntry);
            impl->builder.SetCurrentDebugLocation(llvm::DebugLoc());
            llvm::Value* destroyedEnvironment = destroyEnvironment->getArg(0);
            for (size_t index = 0; index < captures.size(); ++index) {
                std::string capturedReturn;
                std::vector<std::string> capturedParameters;
                if (!ParseCodegenFunctionType(captures[index].type,
                    capturedReturn, capturedParameters)) continue;
                llvm::Value* address = impl->builder.CreateStructGEP(
                    environmentType, destroyedEnvironment, static_cast<unsigned>(index),
                    "captured.closure.address");
                llvm::Value* captured = impl->builder.CreateLoad(
                    impl->builder.getPtrTy(), address, "captured.closure");
                impl->builder.CreateCall(impl->ClosureRelease(), {captured});
            }
            impl->builder.CreateCall(impl->Free(), {destroyedEnvironment});
            impl->builder.CreateRetVoid();
            impl->builder.restoreIP(destroySaved);
            impl->builder.SetCurrentDebugLocation(destroySavedDebugLocation);
        }

        if (captures.empty()) {
            llvm::Constant* initializer = llvm::ConstantStruct::get(
                impl->ClosureObjectType(), {
                    llvm::ConstantInt::getSigned(impl->builder.getInt64Ty(), -1), lambda,
                    llvm::ConstantPointerNull::get(impl->builder.getPtrTy()),
                    llvm::ConstantPointerNull::get(impl->builder.getPtrTy())});
            impl->value = new llvm::GlobalVariable(*impl->module,
                impl->ClosureObjectType(), true, llvm::GlobalValue::InternalLinkage,
                initializer, "__absolute.lambda.value." + std::to_string(lambdaId));
            impl->valueCreatesClosureOwner = false;
        }
        else {
            llvm::Value* closureSize = impl->Coerce(
                llvm::ConstantExpr::getSizeOf(impl->ClosureObjectType()),
                impl->builder.getInt64Ty());
            llvm::Value* closure = impl->builder.CreateCall(
                impl->Malloc(), {closureSize}, "lambda.closure");
            impl->builder.CreateStore(impl->builder.getInt64(1),
                impl->builder.CreateStructGEP(
                    impl->ClosureObjectType(), closure, 0, "closure.count.address"));
            impl->builder.CreateStore(lambda, impl->builder.CreateStructGEP(
                impl->ClosureObjectType(), closure, 1, "closure.invoke.address"));
            impl->builder.CreateStore(environmentValue, impl->builder.CreateStructGEP(
                impl->ClosureObjectType(), closure, 2, "closure.environment.address"));
            impl->builder.CreateStore(destroyEnvironment, impl->builder.CreateStructGEP(
                impl->ClosureObjectType(), closure, 3, "closure.destroy.address"));
            impl->value = closure;
            impl->valueCreatesClosureOwner = true;
        }
        impl->valueCreatesManagedOwner = false;
    }

}
