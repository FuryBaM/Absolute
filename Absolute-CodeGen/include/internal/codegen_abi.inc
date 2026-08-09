        static constexpr std::uint64_t DirectValueAbiLimit = 16;

        bool IsIndirectValueType(const std::string& name);

        llvm::Type* AbiReturnType(const std::string& name, bool external = false);

        llvm::Type* AbiParameterType(const std::string& name, bool external = false);

        bool ParameterSupportsOwnershipName(
            const std::string& name, bool external = false);

        unsigned AbiReturnOffset(const std::string& name, bool external = false);

        llvm::Value* EmitAbiCall(llvm::FunctionType* functionType, llvm::Value* callee,
            const std::string& returnTypeName,
            const std::vector<llvm::Value*>& fixedArguments,
            const std::vector<std::string>& parameterTypeNames,
            const std::vector<llvm::Value*>& argumentValues,
            const std::string& resultName,
            bool external = false,
            const std::vector<llvm::Value*>& ownershipFlags = {});
