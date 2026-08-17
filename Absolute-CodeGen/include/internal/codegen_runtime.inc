        llvm::Value* Evaluate(Expression* expression);

        llvm::Value* EvaluateAddress(Expression* expression);

        void PushScope();

        llvm::FunctionCallee TaskSpawn();

        llvm::FunctionCallee TaskAwait();

        llvm::FunctionCallee TaskDestroy();

        void EmitScopeCleanup(size_t index, SymbolId transferredOwner = InvalidSymbolId);

        void EmitCleanupsFrom(size_t firstScope, SymbolId transferredOwner = InvalidSymbolId);

        void PopScope(bool cleanup = false);

        Variable* FindVariable(const std::string& name);

        Variable* FindVariable(SymbolId symbol);

        Variable& RequireVariable(const std::string& name);

        Variable& AddressOf(Expression* expression);

        SymbolId SemanticSymbol(Expression* expression) const;

        bool StaticManagedOwner(SymbolId symbol) const;

        llvm::Value* ManagedPointee(Expression* expression, llvm::Value* handle);

        llvm::Value* EvaluateCallArgument(Expression* expression,
            std::vector<llvm::Value*>& temporaryArrayOwners,
            std::vector<llvm::Value*>& temporaryClosureOwners,
            const std::string& parameterType = {});

        std::vector<llvm::Value*> EvaluateCallArguments(
            const std::vector<Expression*>& expressions,
            std::vector<llvm::Value*>& temporaryArrayOwners,
            std::vector<llvm::Value*>& temporaryClosureOwners,
            const std::vector<std::string>& parameterTypes,
            bool variadicParameter,
            std::vector<llvm::Value*>* ownershipFlags = nullptr);

        llvm::Value* ArgumentOwnershipFlag(
            Expression* expression, const std::string& parameterType);

        void ConsumeTaskArgument(Expression* expression,
            const std::string& parameterType);

        void ReleaseArrayTemporaries(const std::vector<llvm::Value*>& owners);

        void ReleaseClosureTemporaries(const std::vector<llvm::Value*>& owners);

        llvm::StructType* ClosureObjectType();

        llvm::FunctionType* ClosureInvokeType(const std::string& returnType,
            const std::vector<std::string>& parameterTypes);

        llvm::Function* ClosureRetain();

        llvm::Function* ClosureRelease();

        llvm::Value* FunctionClosure(const Symbol& symbol);

        llvm::Value* BuildArrayDescriptor(const ArrayView& view);

        ArrayView ArrayViewFromValue(llvm::Value* descriptor, const std::string& typeName);

        ArrayView ViewOfArray(Expression* expression);

        void EmitOrExit(llvm::Value* condition, const std::string& name);

        llvm::Value* ArrayElementAddress(ArrayAccessExpr& expression);

        llvm::AllocaInst* CreateEntryAlloca(llvm::Function& function, llvm::Type* type, const std::string& name);

        llvm::Value* Coerce(llvm::Value* source, llvm::Type* target);

        llvm::Value* Coerce(llvm::Value* source, llvm::Type* target,
            const std::string& sourceTypeName,
            const std::string& targetTypeName);

        llvm::Type* CommonNumericType(llvm::Type* left, llvm::Type* right);

        llvm::Value* AsCondition(llvm::Value* condition);

        llvm::Value* ApplyBinary(const std::string& op, llvm::Value* left, llvm::Value* right);

        llvm::Value* ApplyBinary(const std::string& op,
            llvm::Value* left, llvm::Value* right,
            const std::string& leftTypeName,
            const std::string& rightTypeName);

        llvm::Value* One(llvm::Type* type);

        // Offsets a raw pointer by `index` elements of its pointee type, the
        // one lowering `p + n`, `p += n`, `p++` and `p[n]` all share so they
        // cannot drift apart. `subtract` negates the index rather than taking
        // a separate path, because that is the only difference between them.
        llvm::Value* PointerOffset(llvm::Value* pointer,
            const std::string& pointerTypeName,
            llvm::Value* index, bool subtract);

        void BranchIfNeeded(llvm::BasicBlock* target);

        llvm::Function* CurrentFunction();

        std::string Qualify(const std::string& name) const;

        std::string FunctionLinkName(const Symbol& symbol) const;

        std::string ResolvedName(Expression* expression) const;

        bool IsBuiltinFunction(const std::string& name) const;

        llvm::FunctionCallee Printf();

        llvm::FunctionCallee Snprintf();

        llvm::FunctionCallee Malloc();

        llvm::FunctionCallee Free();

        llvm::FunctionCallee LoadDynamicLibrary();

        llvm::FunctionCallee IsDynamicLibraryLoaded();

        llvm::FunctionCallee DynamicLibraryError();

        llvm::Value* EncodeTaskSlot(llvm::IRBuilder<>& targetBuilder, llvm::Value* source);

        llvm::Value* DecodeTaskSlot(
            llvm::IRBuilder<>& targetBuilder, llvm::Value* slot, llvm::Type* target);

        llvm::Value* EmitSpawn(FunctionCallExpr& call);

        llvm::Value* EmitAwait(PrefixUnaryExpr& expression);

        llvm::FunctionCallee ManagedCreate();

        llvm::FunctionCallee ManagedGet(bool requireValid = false);
        
        llvm::Value* EmitManagedGet(llvm::Value* handle, bool requireValid = false);

        llvm::FunctionCallee ManagedValid();

        llvm::FunctionCallee ManagedDestroy();

        llvm::FunctionCallee ManagedSetType();

        llvm::FunctionCallee ManagedType();

        llvm::FunctionCallee ErrorSet();

        llvm::FunctionCallee ErrorPending();

        llvm::FunctionCallee ErrorType();

        llvm::FunctionCallee ErrorTake();

        llvm::FunctionCallee ErrorReport();

        std::uint64_t ExceptionTypeId(const std::string& name) const;

        bool IsClassDerivedFrom(const std::string& type, const std::string& base) const;

        void EmitTransferCleanups(size_t destinationScope, bool includeEqualFinalizer,
            SymbolId transferredOwner = InvalidSymbolId);

        void EmitExceptionPropagation(SymbolId transferredOwner = InvalidSymbolId);

        void EmitExceptionCheck(llvm::Value* temporaryManagedOwner = nullptr,
            llvm::Value* temporaryRawOwner = nullptr);

        std::uint64_t SizeOfTypeName(const std::string& name);

        llvm::FunctionCallee Abort();

        llvm::FunctionCallee Puts();

        llvm::FunctionCallee ExitFailure();

        std::string SemanticType(Expression* expression) const;

        PrintableValue PreparePrintable(llvm::Value* source, Expression* expression);

        llvm::CallInst* EmitPrintf(const std::string& format, const std::vector<PrintableValue>& values);

        std::string BuildFormat(const std::string& source, const std::vector<PrintableValue>& values);

        llvm::Value* EmitFormat(const std::string& format, const std::vector<PrintableValue>& values);
