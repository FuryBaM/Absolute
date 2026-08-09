#include "codegen_internal.h"

namespace Absolute {
    bool CodeGenerator::Impl::IsIndirectValueType(const std::string& name) {
        const std::string valueType = ValueReferenceBaseTypeName(name);
        std::string genericBase;
        std::vector<std::string> genericArguments;
        if (ParseCodegenGenericType(valueType, genericBase, genericArguments) &&
            genericBase == "tuple")
            return module->getDataLayout().getTypeAllocSize(
                TypeFromName(valueType)).getFixedValue() > DirectValueAbiLimit;
        if (!structs.contains(valueType)) return false;
        FinalizeStruct(valueType);
        return module->getDataLayout().getTypeAllocSize(
            structs.at(valueType).llvmType).getFixedValue() > DirectValueAbiLimit;
    }

    llvm::Type* CodeGenerator::Impl::AbiReturnType(
        const std::string& name, bool external) {
        if (!external && IsIndirectValueType(name)) return builder.getVoidTy();
        // C _Bool is i8; Absolute internal bool remains i1.
        if (external && ValueReferenceBaseTypeName(name) == "bool") return builder.getInt8Ty();
        return TypeFromName(name);
    }

    llvm::Type* CodeGenerator::Impl::AbiParameterType(
        const std::string& name, bool external) {
        if (!external && IsValueReferenceTypeName(name)) return builder.getPtrTy();
        if (!external && IsIndirectValueType(name)) return builder.getPtrTy();
        if (external && ValueReferenceBaseTypeName(name) == "bool") return builder.getInt8Ty();
        return TypeFromName(ValueReferenceBaseTypeName(name));
    }

    bool CodeGenerator::Impl::ParameterSupportsOwnershipName(
        const std::string& name, bool external) {
        if (external || IsValueReferenceTypeName(name)) return false;
        const std::string valueType = ValueReferenceBaseTypeName(name);
        return IsStrongManagedPointerTypeName(valueType) ||
            ArrayRankName(valueType) > 0 ||
            (!IsPointerTypeName(valueType) && TypeNeedsCleanup(valueType));
    }

    unsigned CodeGenerator::Impl::AbiReturnOffset(
        const std::string& name, bool external) {
        return !external && IsIndirectValueType(name) ? 1U : 0U;
    }

    llvm::Value* CodeGenerator::Impl::EmitAbiCall(
        llvm::FunctionType* functionType, llvm::Value* callee,
        const std::string& returnTypeName,
        const std::vector<llvm::Value*>& fixedArguments,
        const std::vector<std::string>& parameterTypeNames,
        const std::vector<llvm::Value*>& argumentValues,
        const std::string& resultName,
        bool external,
        const std::vector<llvm::Value*>& ownershipFlags) {
        if (parameterTypeNames.size() != argumentValues.size())
            Fail("internal ABI argument metadata mismatch");

        llvm::Function* caller = CurrentFunction();
        if (!caller) Fail("call outside a function");
        const bool indirectReturn = !external && IsIndirectValueType(returnTypeName);
        llvm::AllocaInst* resultStorage = nullptr;
        std::vector<llvm::Value*> loweredArguments;
        loweredArguments.reserve(functionType->getNumParams());
        if (indirectReturn) {
            resultStorage = CreateEntryAlloca(*caller, TypeFromName(returnTypeName),
                resultName.empty() ? "value.result" : resultName);
            loweredArguments.push_back(resultStorage);
        }

        unsigned loweredIndex = static_cast<unsigned>(loweredArguments.size());
        for (llvm::Value* fixed : fixedArguments) {
            if (loweredIndex >= functionType->getNumParams())
                Fail("too many fixed ABI arguments");
            loweredArguments.push_back(Coerce(
                fixed, functionType->getParamType(loweredIndex++)));
        }

        for (size_t index = 0; index < argumentValues.size(); ++index) {
            if (loweredIndex >= functionType->getNumParams())
                Fail("too many ABI arguments");
            const std::string& typeName = parameterTypeNames[index];
            const std::string valueTypeName = ValueReferenceBaseTypeName(typeName);
            if (!external && IsValueReferenceTypeName(typeName)) {
                loweredArguments.push_back(Coerce(argumentValues[index],
                    functionType->getParamType(loweredIndex++)));
            }
            else if (!external && IsIndirectValueType(valueTypeName)) {
                llvm::Type* valueType = TypeFromName(valueTypeName);
                llvm::AllocaInst* copy = CreateEntryAlloca(
                    *caller, valueType, "value.argument.copy");
                builder.CreateStore(Coerce(argumentValues[index], valueType), copy);
                loweredArguments.push_back(copy);
                ++loweredIndex;
            }
            else {
                loweredArguments.push_back(Coerce(argumentValues[index],
                    functionType->getParamType(loweredIndex++)));
            }
            if (ParameterSupportsOwnershipName(typeName, external)) {
                if (loweredIndex >= functionType->getNumParams())
                    Fail("missing ownership-role ABI argument");
                llvm::Value* role = index < ownershipFlags.size() &&
                    ownershipFlags[index]
                    ? ownershipFlags[index] : builder.getFalse();
                loweredArguments.push_back(Coerce(
                    role, functionType->getParamType(loweredIndex++)));
            }
        }
        if (loweredArguments.size() != functionType->getNumParams())
            Fail("invalid ABI argument count");

        llvm::CallInst* call = builder.CreateCall(functionType, callee, loweredArguments,
            functionType->getReturnType()->isVoidTy() ? "" : resultName);
        if (indirectReturn)
            return builder.CreateLoad(TypeFromName(returnTypeName), resultStorage,
                resultName.empty() ? "value.result.load" : resultName + ".load");
        if (functionType->getReturnType()->isVoidTy()) return nullptr;
        // Coerce C ABI bool (i8) back to Absolute bool (i1) for the caller.
        return Coerce(call, TypeFromName(returnTypeName));
    }
}
