#include "codegen_internal.h"

namespace Absolute {
    llvm::Value* CodeGenerator::Impl::EmitPropertyAccessor(Expression* receiver,
        const std::string& receiverType, const std::string& methodKey,
        const std::vector<llvm::Value*>& explicitArguments) {
        const std::string aggregateName = ClassNameFromType(receiverType);
        llvm::Value* object = receiver ? ObjectPointer(receiver, receiverType) : currentThis;
        if (!object) Fail("property accessor requires an object receiver");

        const auto emitCall = [&](const ClassMethod& method, llvm::Value* callee) -> llvm::Value* {
            llvm::FunctionType* methodType = MethodFunctionType(method);
            llvm::Value* result = EmitAbiCall(methodType, callee, method.returnType,
                {object}, method.parameterTypes, explicitArguments, "property.result");
            EmitExceptionCheck();
            return result;
        };

        if (auto found = classes.find(aggregateName); found != classes.end()) {
            const auto method = found->second.methods.find(methodKey);
            if (method == found->second.methods.end())
                Fail("class '" + aggregateName + "' has no requested property accessor");
            llvm::Value* callee = nullptr;
            if (method->second.virtualSlot) {
                llvm::Value* vtableAddress = builder.CreateStructGEP(
                    found->second.llvmType, object, 0, "property.vtable.address");
                llvm::Value* vtable = builder.CreateLoad(
                    builder.getPtrTy(), vtableAddress, "property.vtable");
                llvm::Value* slot = builder.CreateGEP(builder.getPtrTy(), vtable,
                    builder.getInt64(*method->second.virtualSlot), "property.virtual.slot");
                callee = builder.CreateLoad(builder.getPtrTy(), slot, "property.virtual.method");
            }
            else {
                callee = module->getFunction(method->second.linkName);
                if (!callee) Fail("missing property accessor '" + method->second.linkName + "'");
            }
            return emitCall(method->second, callee);
        }
        if (auto found = interfaces.find(aggregateName); found != interfaces.end()) {
            const auto method = found->second.methods.find(methodKey);
            if (method == found->second.methods.end() || !method->second.virtualSlot)
                Fail("interface '" + aggregateName + "' has no requested property accessor");
            llvm::Value* vtable = builder.CreateLoad(builder.getPtrTy(), object, "property.interface.vtable");
            llvm::Value* slot = builder.CreateGEP(builder.getPtrTy(), vtable,
                builder.getInt64(*method->second.virtualSlot), "property.interface.slot");
            llvm::Value* callee = builder.CreateLoad(
                builder.getPtrTy(), slot, "property.interface.method");
            return emitCall(method->second, callee);
        }
        if (auto found = structs.find(aggregateName); found != structs.end()) {
            const auto method = found->second.methods.find(methodKey);
            if (method == found->second.methods.end())
                Fail("struct '" + aggregateName + "' has no requested property accessor");
            llvm::Function* callee = module->getFunction(method->second.linkName);
            if (!callee) Fail("missing property accessor '" + method->second.linkName + "'");
            return emitCall(method->second, callee);
        }
        Fail("type '" + receiverType + "' does not support properties");
    }

    void CodeGenerator::Visit(PrimitiveTypeExpr* expr) {
        (void)expr;
        impl->Fail("a type cannot be emitted as a runtime expression");
    }

    void CodeGenerator::Visit(UserTypeExpr* expr) {
        (void)expr;
        impl->Fail("user-defined values are not implemented yet");
    }

    void CodeGenerator::Visit(PointerTypeExpr* expr) {
        (void)expr;
        impl->Fail("a pointer type cannot be emitted as a runtime expression");
    }

    void CodeGenerator::Visit(ArrayTypeExpr* expr) {
        (void)expr;
        impl->Fail("an array type cannot be emitted as a runtime expression");
    }

    void CodeGenerator::Visit(IdentifierExpr* expr) {
        if (impl->analyzer) {
            const ExpressionInfo* info = impl->analyzer->GetExpressionInfo(*expr);
            const Symbol* symbol = info ? impl->analyzer->GetSymbol(info->symbol) : nullptr;
            if (symbol && symbol->kind == SymbolKind::Function) {
                if (impl->addressMode) impl->Fail("a function value is not assignable");
                if (symbol->externalFunction || symbol->exportedFunction) {
                    // C ABI: raw function pointer (cfunc), not Absolute closure object.
                    llvm::Function* target = impl->module->getFunction(
                        impl->FunctionLinkName(*symbol));
                    if (!target) impl->Fail("missing C ABI function '" + symbol->name + "'");
                    impl->value = target;
                    impl->valueCreatesManagedOwner = false;
                    impl->valueCreatesClosureOwner = false;
                    return;
                }
                impl->value = impl->FunctionClosure(*symbol);
                impl->valueCreatesManagedOwner = false;
                impl->valueCreatesClosureOwner = false;
                return;
            }
            if (symbol && symbol->kind == SymbolKind::Property) {
                if (impl->addressMode) impl->Fail("a property is not addressable");
                impl->value = impl->EmitPropertyAccessor(nullptr, impl->currentClassName,
                    CallableKey(PropertyGetterName(expr->name), {}), {});
                impl->valueCreatesManagedOwner = false;
                return;
            }
            if (symbol && symbol->kind == SymbolKind::Field && symbol->isStatic) {
                const std::string globalName = symbol->memberOwner + "." + expr->name;
                auto field = impl->globals.find(globalName);
                if (field == impl->globals.end())
                    impl->Fail("missing static field '" + globalName + "'");
                if (impl->addressMode) {
                    impl->addressValue = field->second.address;
                    return;
                }
                impl->value = impl->builder.CreateLoad(
                    field->second.type, field->second.address, expr->name + ".static.value");
                impl->valueCreatesManagedOwner = false;
                return;
            }
        }
        Impl::Variable* variable = impl->FindVariable(expr->name);
        if (!variable) {
            llvm::Value* fieldAddress = impl->ImplicitFieldAddress(expr->name);
            if (!fieldAddress) impl->Fail("unknown variable or field '" + expr->name + "'");
            if (impl->addressMode) {
                impl->addressValue = fieldAddress;
                return;
            }
            llvm::Type* fieldType = impl->TypeFromName(impl->SemanticType(expr));
            impl->value = impl->builder.CreateLoad(fieldType, fieldAddress, expr->name + ".value");
            impl->valueCreatesManagedOwner = false;
            return;
        }
        if (impl->addressMode) {
            impl->addressValue = variable->address;
            return;
        }
        if (variable->isArray) {
            impl->value = impl->BuildArrayDescriptor({variable->address, variable->arrayElementType,
                variable->typeName, variable->arrayDimensions, variable->arrayOwner});
            impl->valueCreatesManagedOwner = false;
            impl->valueCreatesArrayOwner = false;
            impl->valueArrayOwner = nullptr;
            return;
        }
        impl->value = impl->builder.CreateLoad(variable->type, variable->address, expr->name + ".value");
        impl->valueCreatesManagedOwner = false;
    }

    void CodeGenerator::Visit(FunctionCallExpr* expr) {
        const ExpressionInfo* callInfo = impl->analyzer ? impl->analyzer->GetExpressionInfo(*expr) : nullptr;
        const Symbol* selected = callInfo ? impl->analyzer->GetSymbol(callInfo->symbol) : nullptr;
        std::string functionValueReturn;
        std::vector<std::string> functionValueParameters;
        const std::string baseType = impl->SemanticType(expr->base.get());
        if (ParseCodegenCFunctionType(baseType, functionValueReturn,
            functionValueParameters)) {
            std::vector<llvm::Type*> llvmParameters;
            llvmParameters.reserve(functionValueParameters.size());
            for (const std::string& parameter : functionValueParameters)
                llvmParameters.push_back(impl->AbiParameterType(parameter, true));
            llvm::FunctionType* functionType = llvm::FunctionType::get(
                impl->AbiReturnType(functionValueReturn, true), llvmParameters, false);
            llvm::Value* callee = impl->Evaluate(expr->base.get());
            std::vector<llvm::Value*> arguments;
            std::vector<llvm::Value*> temporaryArrayOwners;
            std::vector<llvm::Value*> temporaryClosureOwners;
            for (size_t index = 0; index < expr->arguments.size(); ++index)
                arguments.push_back(impl->EvaluateCallArgument(
                    expr->arguments[index].get(), temporaryArrayOwners,
                    temporaryClosureOwners, index < functionValueParameters.size()
                        ? functionValueParameters[index] : std::string{}));
            impl->value = impl->EmitAbiCall(functionType, callee,
                functionValueReturn, {}, functionValueParameters, arguments,
                "cfunc.result", true);
            impl->ReleaseArrayTemporaries(temporaryArrayOwners);
            impl->ReleaseClosureTemporaries(temporaryClosureOwners);
            // Pure C function pointer: no Absolute exception state.
            impl->valueCreatesManagedOwner = false;
            impl->valueCreatesClosureOwner = false;
            return;
        }
        if (ParseCodegenFunctionType(baseType, functionValueReturn,
            functionValueParameters)) {
            llvm::FunctionType* functionType = impl->ClosureInvokeType(
                functionValueReturn, functionValueParameters);
            llvm::Value* closure = impl->Evaluate(expr->base.get());
            const bool temporaryClosure = impl->valueCreatesClosureOwner;
            impl->EmitOrExit(impl->builder.CreateICmpNE(closure,
                llvm::ConstantPointerNull::get(impl->builder.getPtrTy()),
                "function.value.nonnull"), "function.null");
            llvm::Value* invokeAddress = impl->builder.CreateStructGEP(
                impl->ClosureObjectType(), closure, 1, "closure.invoke.address");
            llvm::Value* callee = impl->builder.CreateLoad(
                impl->builder.getPtrTy(), invokeAddress, "closure.invoke");
            llvm::Value* environmentAddress = impl->builder.CreateStructGEP(
                impl->ClosureObjectType(), closure, 2, "closure.environment.address");
            llvm::Value* environment = impl->builder.CreateLoad(
                impl->builder.getPtrTy(), environmentAddress, "closure.environment");
            std::vector<llvm::Value*> arguments;
            std::vector<llvm::Value*> temporaryArrayOwners;
            std::vector<llvm::Value*> temporaryClosureOwners;
            for (size_t index = 0; index < expr->arguments.size(); ++index)
                arguments.push_back(impl->EvaluateCallArgument(
                    expr->arguments[index].get(), temporaryArrayOwners,
                    temporaryClosureOwners, index < functionValueParameters.size()
                        ? functionValueParameters[index] : std::string{}));
            impl->value = impl->EmitAbiCall(functionType, callee,
                functionValueReturn, {environment}, functionValueParameters, arguments,
                "function.value.result");
            impl->ReleaseArrayTemporaries(temporaryArrayOwners);
            impl->ReleaseClosureTemporaries(temporaryClosureOwners);
            if (temporaryClosure) impl->builder.CreateCall(
                impl->ClosureRelease(), {closure});
            impl->EmitExceptionCheck();
            impl->valueCreatesManagedOwner = IsStrongManagedPointerTypeName(functionValueReturn);
            impl->valueCreatesClosureOwner = IsCodegenFunctionType(functionValueReturn);
            return;
        }
        if (selected && selected->kind == SymbolKind::Method && selected->isStatic) {
            const std::string name = impl->ResolvedName(expr);
            llvm::Function* function = impl->module->getFunction(name);
            if (!function) impl->Fail("unknown static method '" + name + "'");
            std::vector<llvm::Value*> arguments;
            std::vector<llvm::Value*> temporaryArrayOwners;
            std::vector<llvm::Value*> temporaryClosureOwners;
            std::vector<Expression*> argumentExpressions;
            for (const auto& argument : expr->arguments)
                argumentExpressions.push_back(argument.get());
            arguments = impl->EvaluateCallArguments(argumentExpressions,
                temporaryArrayOwners, temporaryClosureOwners,
                selected->parameterTypes, selected->variadicParameter);
            llvm::Value* result = impl->EmitAbiCall(function->getFunctionType(), function,
                selected->type, {}, selected->parameterTypes, arguments, "static.method.result");
            impl->ReleaseArrayTemporaries(temporaryArrayOwners);
            impl->ReleaseClosureTemporaries(temporaryClosureOwners);
            impl->EmitExceptionCheck();
            impl->value = result;
            impl->valueCreatesManagedOwner = IsStrongManagedPointerTypeName(impl->SemanticType(expr));
            impl->valueCreatesClosureOwner = IsCodegenFunctionType(impl->SemanticType(expr));
            return;
        }
        if (selected && selected->kind == SymbolKind::Method && !selected->isStatic &&
            dynamic_cast<MemberAccessExpr*>(expr->base.get()) == nullptr && impl->currentThis) {
            std::vector<std::string> parameterTypes = selected->parameterTypes;
            for (std::string& parameter : parameterTypes)
                parameter = SubstituteCodegenType(
                    parameter, impl->currentGenericSubstitutions);
            const std::string methodName = IdentifierName(expr->base.get());
            const std::string methodKey = CallableKey(methodName, parameterTypes);
            const Impl::ClassMethod* method = nullptr;
            llvm::Value* callee = nullptr;
            if (auto owner = impl->classes.find(impl->currentClassName);
                owner != impl->classes.end()) {
                const auto found = owner->second.methods.find(methodKey);
                if (found == owner->second.methods.end())
                    impl->Fail("class '" + impl->currentClassName +
                        "' has no method '" + methodName + "'");
                method = &found->second;
                if (method->virtualSlot) {
                    llvm::Value* vtableAddress = impl->builder.CreateStructGEP(
                        owner->second.llvmType, impl->currentThis, 0, "vtable.address");
                    llvm::Value* vtable = impl->builder.CreateLoad(
                        impl->builder.getPtrTy(), vtableAddress, "vtable");
                    llvm::Value* slot = impl->builder.CreateGEP(
                        impl->builder.getPtrTy(), vtable,
                        impl->builder.getInt64(*method->virtualSlot), "virtual.slot");
                    callee = impl->builder.CreateLoad(
                        impl->builder.getPtrTy(), slot, "virtual.method");
                }
                else callee = impl->module->getFunction(method->linkName);
            }
            else if (auto owner = impl->structs.find(impl->currentClassName);
                owner != impl->structs.end()) {
                const auto found = owner->second.methods.find(methodKey);
                if (found == owner->second.methods.end())
                    impl->Fail("struct '" + impl->currentClassName +
                        "' has no method '" + methodName + "'");
                method = &found->second;
                callee = impl->module->getFunction(method->linkName);
            }
            if (!method || !callee)
                impl->Fail("missing implicit instance method '" + methodName + "'");
            std::vector<llvm::Value*> arguments;
            std::vector<llvm::Value*> temporaryArrayOwners;
            std::vector<llvm::Value*> temporaryClosureOwners;
            std::vector<Expression*> argumentExpressions;
            for (const auto& argument : expr->arguments)
                argumentExpressions.push_back(argument.get());
            arguments = impl->EvaluateCallArguments(argumentExpressions,
                temporaryArrayOwners, temporaryClosureOwners,
                method->parameterTypes, selected->variadicParameter);
            impl->value = impl->EmitAbiCall(impl->MethodFunctionType(*method), callee,
                method->returnType, {impl->currentThis}, method->parameterTypes,
                arguments, methodName + ".result");
            impl->ReleaseArrayTemporaries(temporaryArrayOwners);
            impl->ReleaseClosureTemporaries(temporaryClosureOwners);
            impl->EmitExceptionCheck();
            impl->valueCreatesManagedOwner =
                IsStrongManagedPointerTypeName(impl->SemanticType(expr));
            impl->valueCreatesClosureOwner =
                IsCodegenFunctionType(impl->SemanticType(expr));
            return;
        }
        if (auto* member = dynamic_cast<MemberAccessExpr*>(expr->base.get())) {
            if (selected && selected->extensionFunction &&
                !impl->SemanticType(member->base.get()).empty()) {
                const std::string name = impl->FunctionLinkName(*selected);
                llvm::Function* function = impl->module->getFunction(name);
                if (!function) impl->Fail("unknown extension method '" + name + "'");
                std::vector<llvm::Value*> arguments;
                std::vector<llvm::Value*> temporaryArrayOwners;
                std::vector<llvm::Value*> temporaryClosureOwners;
                std::vector<Expression*> argumentExpressions{member->base.get()};
                for (const auto& argument : expr->arguments)
                    argumentExpressions.push_back(argument.get());
                arguments = impl->EvaluateCallArguments(argumentExpressions,
                    temporaryArrayOwners, temporaryClosureOwners,
                    selected->parameterTypes, selected->variadicParameter);
                llvm::Value* result = impl->EmitAbiCall(function->getFunctionType(), function,
                    selected->type, {}, selected->parameterTypes, arguments,
                    member->member + ".extension.result");
                impl->ReleaseArrayTemporaries(temporaryArrayOwners);
                impl->ReleaseClosureTemporaries(temporaryClosureOwners);
                impl->EmitExceptionCheck();
                impl->value = result;
                impl->valueCreatesManagedOwner = IsStrongManagedPointerTypeName(impl->SemanticType(expr));
                impl->valueCreatesClosureOwner = IsCodegenFunctionType(impl->SemanticType(expr));
                return;
            }
            const std::string baseType = impl->SemanticType(member->base.get());
            const std::string className = impl->ClassNameFromType(baseType);
            std::vector<std::string> selectedParameterTypes = selected
                ? selected->parameterTypes : std::vector<std::string>{};
            for (std::string& parameter : selectedParameterTypes)
                parameter = SubstituteCodegenType(
                    parameter, impl->currentGenericSubstitutions);
            auto classIterator = impl->classes.find(className);
            if (classIterator != impl->classes.end() && (!selected || selected->kind == SymbolKind::Method)) {
                Impl::ClassInfo& info = classIterator->second;
                const std::string methodKey = CallableKey(member->member,
                    selectedParameterTypes);
                const auto method = info.methods.find(methodKey);
                if (method == info.methods.end())
                    impl->Fail("class '" + info.name + "' has no method '" + member->member + "'");
                llvm::Value* object = impl->ObjectPointer(member->base.get(), baseType);
                llvm::FunctionType* methodType = impl->MethodFunctionType(method->second);
                std::vector<llvm::Value*> arguments;
                std::vector<llvm::Value*> temporaryArrayOwners;
                std::vector<llvm::Value*> temporaryClosureOwners;
                std::vector<Expression*> argumentExpressions;
                for (const auto& argument : expr->arguments)
                    argumentExpressions.push_back(argument.get());
                arguments = impl->EvaluateCallArguments(argumentExpressions,
                    temporaryArrayOwners, temporaryClosureOwners,
                    method->second.parameterTypes,
                    selected && selected->variadicParameter);

                llvm::Value* callee = nullptr;
                if (method->second.virtualSlot) {
                    llvm::Value* vtableAddress = impl->builder.CreateStructGEP(
                        info.llvmType, object, 0, "vtable.address");
                    llvm::Value* vtable = impl->builder.CreateLoad(
                        impl->builder.getPtrTy(), vtableAddress, "vtable");
                    llvm::Value* slot = impl->builder.CreateGEP(impl->builder.getPtrTy(), vtable,
                        impl->builder.getInt64(*method->second.virtualSlot), "virtual.slot");
                    callee = impl->builder.CreateLoad(impl->builder.getPtrTy(), slot, "virtual.method");
                }
                else {
                    callee = impl->module->getFunction(method->second.linkName);
                    if (!callee) impl->Fail("missing method function '" + method->second.linkName + "'");
                }
                llvm::Value* result = impl->EmitAbiCall(methodType, callee,
                    method->second.returnType, {object}, method->second.parameterTypes,
                    arguments, member->member + ".result");
                impl->ReleaseArrayTemporaries(temporaryArrayOwners);
                impl->ReleaseClosureTemporaries(temporaryClosureOwners);
                impl->EmitExceptionCheck();
                impl->value = result;
                impl->valueCreatesManagedOwner = IsStrongManagedPointerTypeName(impl->SemanticType(expr));
                impl->valueCreatesClosureOwner = IsCodegenFunctionType(impl->SemanticType(expr));
                return;
            }
            auto interfaceIterator = impl->interfaces.find(className);
            if (interfaceIterator != impl->interfaces.end() &&
                (!selected || selected->kind == SymbolKind::Method)) {
                Impl::InterfaceInfo& info = interfaceIterator->second;
                const std::string methodKey = CallableKey(member->member,
                    selectedParameterTypes);
                const auto method = info.methods.find(methodKey);
                if (method == info.methods.end())
                    impl->Fail("interface '" + info.name + "' has no method '" + member->member + "'");
                if (!method->second.virtualSlot)
                    impl->Fail("interface method '" + info.name + "." + member->member +
                        "' has no dispatch slot");
                llvm::Value* object = impl->ObjectPointer(member->base.get(), baseType);
                llvm::FunctionType* methodType = impl->MethodFunctionType(method->second);
                std::vector<llvm::Value*> arguments;
                std::vector<llvm::Value*> temporaryArrayOwners;
                std::vector<llvm::Value*> temporaryClosureOwners;
                std::vector<Expression*> argumentExpressions;
                for (const auto& argument : expr->arguments)
                    argumentExpressions.push_back(argument.get());
                arguments = impl->EvaluateCallArguments(argumentExpressions,
                    temporaryArrayOwners, temporaryClosureOwners,
                    method->second.parameterTypes,
                    selected && selected->variadicParameter);
                llvm::Value* vtable = impl->builder.CreateLoad(
                    impl->builder.getPtrTy(), object, "interface.vtable");
                llvm::Value* slot = impl->builder.CreateGEP(
                    impl->builder.getPtrTy(), vtable,
                    impl->builder.getInt64(*method->second.virtualSlot), "interface.slot");
                llvm::Value* callee = impl->builder.CreateLoad(
                    impl->builder.getPtrTy(), slot, "interface.method");
                llvm::Value* result = impl->EmitAbiCall(methodType, callee,
                    method->second.returnType, {object}, method->second.parameterTypes,
                    arguments, member->member + ".interface.result");
                impl->ReleaseArrayTemporaries(temporaryArrayOwners);
                impl->ReleaseClosureTemporaries(temporaryClosureOwners);
                impl->EmitExceptionCheck();
                impl->value = result;
                impl->valueCreatesManagedOwner = IsStrongManagedPointerTypeName(impl->SemanticType(expr));
                impl->valueCreatesClosureOwner = IsCodegenFunctionType(impl->SemanticType(expr));
                return;
            }
            auto structIterator = impl->structs.find(className);
            if (structIterator != impl->structs.end() && (!selected || selected->kind == SymbolKind::Method)) {
                Impl::StructInfo& info = structIterator->second;
                const std::string methodKey = CallableKey(member->member,
                    selectedParameterTypes);
                const auto method = info.methods.find(methodKey);
                if (method == info.methods.end())
                    impl->Fail("struct '" + info.name + "' has no method '" + member->member + "'");
                llvm::Value* object = impl->ObjectPointer(member->base.get(), baseType);
                llvm::FunctionType* methodType = impl->MethodFunctionType(method->second);
                std::vector<llvm::Value*> arguments;
                std::vector<llvm::Value*> temporaryArrayOwners;
                std::vector<llvm::Value*> temporaryClosureOwners;
                std::vector<Expression*> argumentExpressions;
                for (const auto& argument : expr->arguments)
                    argumentExpressions.push_back(argument.get());
                arguments = impl->EvaluateCallArguments(argumentExpressions,
                    temporaryArrayOwners, temporaryClosureOwners,
                    method->second.parameterTypes,
                    selected && selected->variadicParameter);
                llvm::Function* callee = impl->module->getFunction(method->second.linkName);
                if (!callee) impl->Fail("missing method function '" + method->second.linkName + "'");
                llvm::Value* result = impl->EmitAbiCall(methodType, callee,
                    method->second.returnType, {object}, method->second.parameterTypes,
                    arguments, member->member + ".result");
                impl->ReleaseArrayTemporaries(temporaryArrayOwners);
                impl->ReleaseClosureTemporaries(temporaryClosureOwners);
                impl->EmitExceptionCheck();
                impl->value = result;
                impl->valueCreatesManagedOwner = IsStrongManagedPointerTypeName(impl->SemanticType(expr));
                impl->valueCreatesClosureOwner = IsCodegenFunctionType(impl->SemanticType(expr));
                return;
            }
        }
        const std::string name = impl->ResolvedName(expr);
        if (impl->IsBuiltinFunction(name)) {
            impl->EmitBuiltin(*expr, name);
            return;
        }
        llvm::Function* function = impl->module->getFunction(name);
        if (!function) impl->Fail("unknown function '" + name + "'");
        std::vector<llvm::Value*> arguments;
        std::vector<llvm::Value*> temporaryArrayOwners;
        std::vector<llvm::Value*> temporaryClosureOwners;
        arguments.reserve(expr->arguments.size());
        std::vector<std::string> parameterTypes;
        std::vector<Expression*> argumentExpressions;
        for (const auto& expression : expr->arguments) {
            argumentExpressions.push_back(expression.get());
            parameterTypes.push_back(impl->SemanticType(expression.get()));
        }
        if (selected) {
            parameterTypes = selected->parameterTypes;
            arguments = impl->EvaluateCallArguments(argumentExpressions,
                temporaryArrayOwners, temporaryClosureOwners,
                parameterTypes, selected->variadicParameter);
        }
        else {
            arguments = impl->EvaluateCallArguments(argumentExpressions,
                temporaryArrayOwners, temporaryClosureOwners,
                parameterTypes, false);
        }
        const std::string returnType = selected ? selected->type : impl->SemanticType(expr);
        const bool external = selected &&
            (selected->externalFunction || selected->exportedFunction);
        llvm::Value* result = impl->EmitAbiCall(function->getFunctionType(), function,
            returnType, {}, parameterTypes, arguments, name + ".result", external);
        impl->ReleaseArrayTemporaries(temporaryArrayOwners);
        impl->ReleaseClosureTemporaries(temporaryClosureOwners);
        if (!selected || !selected->externalFunction) impl->EmitExceptionCheck();
        impl->value = result;
        impl->valueCreatesManagedOwner = IsStrongManagedPointerTypeName(impl->SemanticType(expr));
        impl->valueCreatesClosureOwner = IsCodegenFunctionType(returnType);
    }

    void CodeGenerator::Visit(ArrayAccessExpr* expr) {
        if (impl->analyzer) {
            const ExpressionInfo* info = impl->analyzer->GetExpressionInfo(*expr);
            const Symbol* symbol = info ? impl->analyzer->GetSymbol(info->symbol) : nullptr;
            if (symbol && symbol->kind == SymbolKind::Indexer) {
                if (impl->addressMode) impl->Fail("an indexer is not addressable");
                std::vector<llvm::Value*> arguments;
                arguments.reserve(expr->indexes.size());
                for (const auto& index : expr->indexes)
                    arguments.push_back(impl->Evaluate(index.get()));
                impl->value = impl->EmitPropertyAccessor(
                    expr->base.get(), impl->SemanticType(expr->base.get()),
                    CallableKey(IndexerGetterName(), info->parameterTypes), arguments);
                impl->valueCreatesManagedOwner = false;
                return;
            }
        }
        if (expr->indexes.size() == 1 && !expr->indexes.front()) {
            if (impl->addressMode) impl->Fail("a slice is not assignable");
            Impl::ArrayView view = impl->ViewOfArray(expr->base.get());
            const bool createsOwner = impl->valueCreatesArrayOwner;
            llvm::Value* owner = impl->valueArrayOwner;
            impl->value = impl->BuildArrayDescriptor(view);
            impl->valueCreatesManagedOwner = false;
            impl->valueCreatesArrayOwner = createsOwner;
            impl->valueArrayOwner = owner;
            return;
        }
        Impl::ArrayView view = impl->ViewOfArray(expr->base.get());
        if (expr->indexes.size() < view.dimensions.size()) {
            if (impl->addressMode) impl->Fail("sub-array slice is not assignable");
            llvm::Value* address = impl->ArrayElementAddress(*expr);
            std::vector<llvm::Value*> subDims;
            for (size_t d = expr->indexes.size(); d < view.dimensions.size(); ++d) {
                subDims.push_back(view.dimensions[d]);
            }
            Impl::ArrayView subView;
            subView.address = address;
            subView.elementType = view.elementType;
            subView.typeName = impl->SemanticType(expr);
            subView.dimensions = std::move(subDims);
            subView.owner = view.owner;
            impl->value = impl->BuildArrayDescriptor(subView);
            impl->valueCreatesManagedOwner = false;
            return;
        }

        llvm::Value* address = impl->ArrayElementAddress(*expr);
        if (impl->addressMode) {
            impl->addressValue = address;
            return;
        }
        llvm::Type* elementType = impl->TypeFromName(impl->SemanticType(expr));
        impl->value = impl->builder.CreateLoad(elementType, address, "array.element");
        impl->valueCreatesManagedOwner = false;
    }

    void CodeGenerator::Visit(SliceExpr* expr) {
        if (impl->addressMode) impl->Fail("a slice is not assignable");
        Impl::ArrayView source = impl->ViewOfArray(expr->base.get());
        const bool createsOwner = impl->valueCreatesArrayOwner;
        llvm::Value* owner = impl->valueArrayOwner;

        const size_t sourceRank = source.dimensions.size();
        if (sourceRank == 0) impl->Fail("slice target is not an array");
        if (expr->ranges.size() > sourceRank)
            impl->Fail("slice range count exceeds array rank");

        std::vector<llvm::Value*> newDimensions = source.dimensions;
        std::vector<llvm::Value*> begins;
        std::vector<llvm::Value*> ends;
        begins.reserve(sourceRank);
        ends.reserve(sourceRank);

        bool isInnerSliced = false;
        for (size_t d = 0; d < sourceRank; ++d) {
            llvm::Value* begin = impl->builder.getInt64(0);
            llvm::Value* end = source.dimensions[d];

            if (d < expr->ranges.size()) {
                const auto& range = expr->ranges[d];
                if (range.hasColon) {
                    if (range.begin)
                        begin = impl->Coerce(impl->Evaluate(range.begin.get()), impl->builder.getInt64Ty());
                    if (range.end)
                        end = impl->Coerce(impl->Evaluate(range.end.get()), impl->builder.getInt64Ty());
                } else {
                    if (range.begin) {
                        begin = impl->Coerce(impl->Evaluate(range.begin.get()), impl->builder.getInt64Ty());
                        end = impl->builder.CreateAdd(begin, impl->builder.getInt64(1), "slice.single.end");
                    }
                }
            }

            llvm::Value* valid = impl->builder.CreateAnd(
                impl->builder.CreateICmpSGE(begin, impl->builder.getInt64(0), "slice.begin.nonnegative"),
                impl->builder.CreateICmpSGE(end, begin, "slice.order.valid"), "slice.lower.valid");
            valid = impl->builder.CreateAnd(valid,
                impl->builder.CreateICmpSLE(end, source.dimensions[d], "slice.end.below.size"),
                "slice.valid");
            impl->EmitOrExit(valid, "array.bounds");

            newDimensions[d] = impl->builder.CreateSub(end, begin, "slice.dim.length");
            begins.push_back(begin);
            ends.push_back(end);

            if (d > 0 && d < expr->ranges.size()) {
                isInnerSliced = true;
            }
        }

        if (!isInnerSliced) {
            llvm::Value* stride = impl->builder.getInt64(1);
            for (size_t k = 1; k < sourceRank; ++k) {
                stride = impl->builder.CreateMul(stride, source.dimensions[k], "slice.stride");
            }
            llvm::Value* offset = impl->builder.CreateMul(begins[0], stride, "slice.offset");

            Impl::ArrayView slice;
            slice.address = impl->builder.CreateInBoundsGEP(
                source.elementType, source.address, offset, "slice.data");
            slice.elementType = source.elementType;
            slice.typeName = source.typeName;
            slice.dimensions = std::move(newDimensions);
            slice.owner = source.owner;

            impl->value = impl->BuildArrayDescriptor(slice);
            impl->valueCreatesManagedOwner = false;
            impl->valueCreatesArrayOwner = createsOwner;
            impl->valueArrayOwner = owner;
            return;
        }

        llvm::Value* totalElements = impl->builder.getInt64(1);
        for (llvm::Value* dim : newDimensions) {
            totalElements = impl->builder.CreateMul(totalElements, dim, "slice.total.elements");
        }

        llvm::AllocaInst* storage = impl->builder.CreateAlloca(
            source.elementType, totalElements, "slice.storage");
        storage->setAlignment(llvm::Align(16));

        Impl::ArrayView slice;
        slice.address = storage;
        slice.elementType = source.elementType;
        slice.typeName = source.typeName;
        slice.dimensions = newDimensions;
        slice.owner = nullptr;

        if (sourceRank == 2) {
            llvm::Value* rBegin = begins[0];
            llvm::Value* rEnd = ends[0];
            llvm::Value* cBegin = begins[1];
            llvm::Value* cEnd = ends[1];
            llvm::Value* srcCols = source.dimensions[1];
            llvm::Value* dstCols = newDimensions[1];

            llvm::Function* function = impl->CurrentFunction();
            llvm::BasicBlock* preheader = impl->builder.GetInsertBlock();
            llvm::BasicBlock* rHeader = llvm::BasicBlock::Create(impl->context, "slice.r.header", function);
            llvm::BasicBlock* cHeader = llvm::BasicBlock::Create(impl->context, "slice.c.header", function);
            llvm::BasicBlock* body = llvm::BasicBlock::Create(impl->context, "slice.body", function);
            llvm::BasicBlock* cLatch = llvm::BasicBlock::Create(impl->context, "slice.c.latch", function);
            llvm::BasicBlock* rLatch = llvm::BasicBlock::Create(impl->context, "slice.r.latch", function);
            llvm::BasicBlock* exit = llvm::BasicBlock::Create(impl->context, "slice.exit", function);

            impl->builder.CreateBr(rHeader);

            impl->builder.SetInsertPoint(rHeader);
            llvm::PHINode* r = impl->builder.CreatePHI(impl->builder.getInt64Ty(), 2, "r");
            r->addIncoming(rBegin, preheader);
            llvm::Value* rCond = impl->builder.CreateICmpSLT(r, rEnd, "r.cond");
            impl->builder.CreateCondBr(rCond, cHeader, exit);

            impl->builder.SetInsertPoint(cHeader);
            llvm::PHINode* c = impl->builder.CreatePHI(impl->builder.getInt64Ty(), 2, "c");
            c->addIncoming(cBegin, rHeader);
            llvm::Value* cCond = impl->builder.CreateICmpSLT(c, cEnd, "c.cond");
            impl->builder.CreateCondBr(cCond, body, rLatch);

            impl->builder.SetInsertPoint(body);
            llvm::Value* srcIdx = impl->builder.CreateAdd(
                impl->builder.CreateMul(r, srcCols), c);
            llvm::Value* dstR = impl->builder.CreateSub(r, rBegin);
            llvm::Value* dstC = impl->builder.CreateSub(c, cBegin);
            llvm::Value* dstIdx = impl->builder.CreateAdd(
                impl->builder.CreateMul(dstR, dstCols), dstC);

            llvm::Value* srcPtr = impl->builder.CreateInBoundsGEP(source.elementType, source.address, srcIdx);
            llvm::Value* dstPtr = impl->builder.CreateInBoundsGEP(slice.elementType, slice.address, dstIdx);
            llvm::Value* elem = impl->builder.CreateLoad(source.elementType, srcPtr);
            impl->builder.CreateStore(elem, dstPtr);
            impl->builder.CreateBr(cLatch);

            impl->builder.SetInsertPoint(cLatch);
            llvm::Value* nextC = impl->builder.CreateAdd(c, impl->builder.getInt64(1));
            c->addIncoming(nextC, cLatch);
            impl->builder.CreateBr(cHeader);

            impl->builder.SetInsertPoint(rLatch);
            llvm::Value* nextR = impl->builder.CreateAdd(r, impl->builder.getInt64(1));
            r->addIncoming(nextR, rLatch);
            impl->builder.CreateBr(rHeader);

            impl->builder.SetInsertPoint(exit);
        }

        impl->value = impl->BuildArrayDescriptor(slice);
        impl->valueCreatesManagedOwner = false;
        impl->valueCreatesArrayOwner = false;
        impl->valueArrayOwner = nullptr;
    }

}
