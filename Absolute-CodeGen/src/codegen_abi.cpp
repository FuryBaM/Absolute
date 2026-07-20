#include "codegen_internal.h"

namespace Absolute {
    bool CodeGenerator::Impl::IsIndirectValueType(const std::string& name) {
        if (!structs.contains(name)) return false;
        FinalizeStruct(name);
        return module->getDataLayout().getTypeAllocSize(structs.at(name).llvmType).getFixedValue() >
            DirectValueAbiLimit;
    }

    llvm::Type* CodeGenerator::Impl::AbiReturnType(
        const std::string& name, bool external) {
        return !external && IsIndirectValueType(name) ? builder.getVoidTy() : TypeFromName(name);
    }

    llvm::Type* CodeGenerator::Impl::AbiParameterType(
        const std::string& name, bool external) {
        return !external && IsIndirectValueType(name) ? builder.getPtrTy() : TypeFromName(name);
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
        bool external) {
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
            if (!external && IsIndirectValueType(typeName)) {
                llvm::Type* valueType = TypeFromName(typeName);
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
        }
        if (loweredArguments.size() != functionType->getNumParams())
            Fail("invalid ABI argument count");

        llvm::CallInst* call = builder.CreateCall(functionType, callee, loweredArguments,
            functionType->getReturnType()->isVoidTy() ? "" : resultName);
        if (indirectReturn)
            return builder.CreateLoad(TypeFromName(returnTypeName), resultStorage,
                resultName.empty() ? "value.result.load" : resultName + ".load");
        return functionType->getReturnType()->isVoidTy() ? nullptr : call;
    }
}
