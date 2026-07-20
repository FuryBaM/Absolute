#include "codegen_internal.h"

namespace Absolute {
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
                variable->typeName, variable->arrayDimensions});
            impl->valueCreatesManagedOwner = false;
            return;
        }
        impl->value = impl->builder.CreateLoad(variable->type, variable->address, expr->name + ".value");
        impl->valueCreatesManagedOwner = false;
    }

    void CodeGenerator::Visit(FunctionCallExpr* expr) {
        const ExpressionInfo* callInfo = impl->analyzer ? impl->analyzer->GetExpressionInfo(*expr) : nullptr;
        const Symbol* selected = callInfo ? impl->analyzer->GetSymbol(callInfo->symbol) : nullptr;
        if (selected && selected->kind == SymbolKind::Method && selected->isStatic) {
            const std::string name = impl->ResolvedName(expr);
            llvm::Function* function = impl->module->getFunction(name);
            if (!function) impl->Fail("unknown static method '" + name + "'");
            if (function->arg_size() != expr->arguments.size())
                impl->Fail("invalid argument count for static method '" + name + "'");
            std::vector<llvm::Value*> arguments;
            arguments.reserve(expr->arguments.size());
            for (size_t index = 0; index < expr->arguments.size(); ++index)
                arguments.push_back(impl->Coerce(impl->Evaluate(expr->arguments[index].get()),
                    function->getFunctionType()->getParamType(static_cast<unsigned>(index))));
            llvm::CallInst* call = impl->builder.CreateCall(function, arguments,
                function->getReturnType()->isVoidTy() ? "" : "static.method.result");
            impl->EmitExceptionCheck();
            impl->value = function->getReturnType()->isVoidTy() ? nullptr : call;
            impl->valueCreatesManagedOwner = IsManagedPointerTypeName(impl->SemanticType(expr));
            return;
        }
        if (auto* member = dynamic_cast<MemberAccessExpr*>(expr->base.get())) {
            if (selected && selected->extensionFunction) {
                const std::string name = impl->ResolvedName(expr);
                llvm::Function* function = impl->module->getFunction(name);
                if (!function) impl->Fail("unknown extension method '" + name + "'");
                if (function->arg_size() != expr->arguments.size() + 1)
                    impl->Fail("invalid argument count for extension method '" + member->member + "'");
                std::vector<llvm::Value*> arguments;
                arguments.reserve(expr->arguments.size() + 1);
                arguments.push_back(impl->Coerce(impl->Evaluate(member->base.get()),
                    function->getFunctionType()->getParamType(0)));
                for (size_t index = 0; index < expr->arguments.size(); ++index)
                    arguments.push_back(impl->Coerce(impl->Evaluate(expr->arguments[index].get()),
                        function->getFunctionType()->getParamType(static_cast<unsigned>(index + 1))));
                llvm::CallInst* call = impl->builder.CreateCall(function, arguments,
                    function->getReturnType()->isVoidTy() ? "" : member->member + ".extension.result");
                impl->EmitExceptionCheck();
                impl->value = function->getReturnType()->isVoidTy() ? nullptr : call;
                impl->valueCreatesManagedOwner = IsManagedPointerTypeName(impl->SemanticType(expr));
                return;
            }
            const std::string baseType = impl->SemanticType(member->base.get());
            const std::string className = impl->ClassNameFromType(baseType);
            auto classIterator = impl->classes.find(className);
            if (classIterator != impl->classes.end() && (!selected || selected->kind == SymbolKind::Method)) {
                Impl::ClassInfo& info = classIterator->second;
                const std::string methodKey = CallableKey(member->member,
                    selected ? selected->parameterTypes : std::vector<std::string>{});
                const auto method = info.methods.find(methodKey);
                if (method == info.methods.end())
                    impl->Fail("class '" + info.name + "' has no method '" + member->member + "'");
                llvm::Value* object = impl->ObjectPointer(member->base.get(), baseType);
                llvm::FunctionType* methodType = impl->MethodFunctionType(method->second);
                std::vector<llvm::Value*> arguments{object};
                for (size_t index = 0; index < expr->arguments.size(); ++index) {
                    if (index + 1 >= methodType->getNumParams())
                        impl->Fail("too many arguments for method '" + member->member + "'");
                    arguments.push_back(impl->Coerce(impl->Evaluate(expr->arguments[index].get()),
                        methodType->getParamType(static_cast<unsigned>(index + 1))));
                }
                if (arguments.size() != methodType->getNumParams())
                    impl->Fail("invalid argument count for method '" + member->member + "'");

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
                llvm::CallInst* call = impl->builder.CreateCall(methodType, callee, arguments,
                    methodType->getReturnType()->isVoidTy() ? "" : member->member + ".result");
                impl->EmitExceptionCheck();
                impl->value = methodType->getReturnType()->isVoidTy() ? nullptr : call;
                impl->valueCreatesManagedOwner = IsManagedPointerTypeName(impl->SemanticType(expr));
                return;
            }
            auto interfaceIterator = impl->interfaces.find(className);
            if (interfaceIterator != impl->interfaces.end() &&
                (!selected || selected->kind == SymbolKind::Method)) {
                Impl::InterfaceInfo& info = interfaceIterator->second;
                const std::string methodKey = CallableKey(member->member,
                    selected ? selected->parameterTypes : std::vector<std::string>{});
                const auto method = info.methods.find(methodKey);
                if (method == info.methods.end())
                    impl->Fail("interface '" + info.name + "' has no method '" + member->member + "'");
                if (!method->second.virtualSlot)
                    impl->Fail("interface method '" + info.name + "." + member->member +
                        "' has no dispatch slot");
                llvm::Value* object = impl->ObjectPointer(member->base.get(), baseType);
                llvm::FunctionType* methodType = impl->MethodFunctionType(method->second);
                std::vector<llvm::Value*> arguments{object};
                for (size_t index = 0; index < expr->arguments.size(); ++index) {
                    if (index + 1 >= methodType->getNumParams())
                        impl->Fail("too many arguments for interface method '" + member->member + "'");
                    arguments.push_back(impl->Coerce(impl->Evaluate(expr->arguments[index].get()),
                        methodType->getParamType(static_cast<unsigned>(index + 1))));
                }
                if (arguments.size() != methodType->getNumParams())
                    impl->Fail("invalid argument count for interface method '" + member->member + "'");
                llvm::Value* vtable = impl->builder.CreateLoad(
                    impl->builder.getPtrTy(), object, "interface.vtable");
                llvm::Value* slot = impl->builder.CreateGEP(
                    impl->builder.getPtrTy(), vtable,
                    impl->builder.getInt64(*method->second.virtualSlot), "interface.slot");
                llvm::Value* callee = impl->builder.CreateLoad(
                    impl->builder.getPtrTy(), slot, "interface.method");
                llvm::CallInst* call = impl->builder.CreateCall(methodType, callee, arguments,
                    methodType->getReturnType()->isVoidTy() ? "" : member->member + ".interface.result");
                impl->EmitExceptionCheck();
                impl->value = methodType->getReturnType()->isVoidTy() ? nullptr : call;
                impl->valueCreatesManagedOwner = IsManagedPointerTypeName(impl->SemanticType(expr));
                return;
            }
            auto structIterator = impl->structs.find(className);
            if (structIterator != impl->structs.end() && (!selected || selected->kind == SymbolKind::Method)) {
                Impl::StructInfo& info = structIterator->second;
                const std::string methodKey = CallableKey(member->member,
                    selected ? selected->parameterTypes : std::vector<std::string>{});
                const auto method = info.methods.find(methodKey);
                if (method == info.methods.end())
                    impl->Fail("struct '" + info.name + "' has no method '" + member->member + "'");
                llvm::Value* object = impl->ObjectPointer(member->base.get(), baseType);
                llvm::FunctionType* methodType = impl->MethodFunctionType(method->second);
                std::vector<llvm::Value*> arguments{object};
                for (size_t index = 0; index < expr->arguments.size(); ++index) {
                    if (index + 1 >= methodType->getNumParams())
                        impl->Fail("too many arguments for method '" + member->member + "'");
                    arguments.push_back(impl->Coerce(impl->Evaluate(expr->arguments[index].get()),
                        methodType->getParamType(static_cast<unsigned>(index + 1))));
                }
                if (arguments.size() != methodType->getNumParams())
                    impl->Fail("invalid argument count for method '" + member->member + "'");
                llvm::Function* callee = impl->module->getFunction(method->second.linkName);
                if (!callee) impl->Fail("missing method function '" + method->second.linkName + "'");
                llvm::CallInst* call = impl->builder.CreateCall(methodType, callee, arguments,
                    methodType->getReturnType()->isVoidTy() ? "" : member->member + ".result");
                impl->EmitExceptionCheck();
                impl->value = methodType->getReturnType()->isVoidTy() ? nullptr : call;
                impl->valueCreatesManagedOwner = IsManagedPointerTypeName(impl->SemanticType(expr));
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
        if (function->arg_size() != expr->arguments.size()) {
            impl->Fail("invalid argument count for '" + name + "'");
        }

        std::vector<llvm::Value*> arguments;
        arguments.reserve(expr->arguments.size());
        size_t index = 0;
        for (const auto& expression : expr->arguments) {
            llvm::Value* argument = impl->Evaluate(expression.get());
            arguments.push_back(impl->Coerce(argument,
                function->getFunctionType()->getParamType(static_cast<unsigned>(index++))));
        }

        llvm::CallInst* call = impl->builder.CreateCall(function, arguments,
            function->getReturnType()->isVoidTy() ? "" : name + ".result");
        if (!selected || !selected->externalFunction) impl->EmitExceptionCheck();
        impl->value = function->getReturnType()->isVoidTy() ? nullptr : call;
        impl->valueCreatesManagedOwner = IsManagedPointerTypeName(impl->SemanticType(expr));
    }

    void CodeGenerator::Visit(ArrayAccessExpr* expr) {
        if (expr->indexes.size() == 1 && !expr->indexes.front()) {
            if (impl->addressMode) impl->Fail("a slice is not assignable");
            impl->value = impl->BuildArrayDescriptor(impl->ViewOfArray(expr->base.get()));
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
        if (source.dimensions.size() != 1) impl->Fail("slices require a one-dimensional array");
        llvm::Value* begin = expr->begin
            ? impl->Coerce(impl->Evaluate(expr->begin.get()), impl->builder.getInt64Ty())
            : impl->builder.getInt64(0);
        llvm::Value* end = expr->end
            ? impl->Coerce(impl->Evaluate(expr->end.get()), impl->builder.getInt64Ty())
            : source.dimensions.front();
        llvm::Value* valid = impl->builder.CreateAnd(
            impl->builder.CreateICmpSGE(begin, impl->builder.getInt64(0), "slice.begin.nonnegative"),
            impl->builder.CreateICmpSGE(end, begin, "slice.order.valid"), "slice.lower.valid");
        valid = impl->builder.CreateAnd(valid,
            impl->builder.CreateICmpSLE(end, source.dimensions.front(), "slice.end.below.size"),
            "slice.valid");
        impl->EmitOrExit(valid, "array.bounds");
        Impl::ArrayView slice;
        slice.address = impl->builder.CreateInBoundsGEP(
            source.elementType, source.address, begin, "slice.data");
        slice.elementType = source.elementType;
        slice.typeName = source.typeName;
        slice.dimensions = {impl->builder.CreateSub(end, begin, "slice.length")};
        impl->value = impl->BuildArrayDescriptor(slice);
        impl->valueCreatesManagedOwner = false;
    }

}
