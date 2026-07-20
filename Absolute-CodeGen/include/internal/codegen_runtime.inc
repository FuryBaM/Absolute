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

        Variable& RequireVariable(const std::string& name);

        Variable& AddressOf(Expression* expression);

        SymbolId SemanticSymbol(Expression* expression) const;

        bool StaticManagedOwner(SymbolId symbol) const;

        llvm::Value* ManagedPointee(Expression* expression, llvm::Value* handle);

        llvm::Value* BuildArrayDescriptor(const ArrayView& view);

        ArrayView ArrayViewFromValue(llvm::Value* descriptor, const std::string& typeName);

        ArrayView ViewOfArray(Expression* expression);

        void EmitOrExit(llvm::Value* condition, const std::string& name);

        llvm::Value* ArrayElementAddress(ArrayAccessExpr& expression);

        llvm::AllocaInst* CreateEntryAlloca(llvm::Function& function, llvm::Type* type, const std::string& name);

        llvm::Value* Coerce(llvm::Value* source, llvm::Type* target);

        llvm::Type* CommonNumericType(llvm::Type* left, llvm::Type* right);

        llvm::Value* AsCondition(llvm::Value* condition);

        llvm::Value* ApplyBinary(const std::string& op, llvm::Value* left, llvm::Value* right);

        llvm::Value* One(llvm::Type* type);

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

        llvm::Value* EncodeTaskSlot(llvm::IRBuilder<>& targetBuilder, llvm::Value* source);

        llvm::Value* DecodeTaskSlot(
            llvm::IRBuilder<>& targetBuilder, llvm::Value* slot, llvm::Type* target);

        llvm::Value* EmitSpawn(FunctionCallExpr& call);

        llvm::Value* EmitAwait(PrefixUnaryExpr& expression);

        llvm::FunctionCallee ManagedCreate();

        llvm::FunctionCallee ManagedGet(bool requireValid = false);

        llvm::FunctionCallee ManagedValid();

        llvm::FunctionCallee ManagedDestroy();

        std::uint64_t SizeOfTypeName(const std::string& name);

        llvm::FunctionCallee Abort();

        llvm::FunctionCallee Puts();

        llvm::FunctionCallee ExitFailure();

        std::string SemanticType(Expression* expression) const;

        PrintableValue PreparePrintable(llvm::Value* source, Expression* expression);

        llvm::CallInst* EmitPrintf(const std::string& format, const std::vector<PrintableValue>& values);

        std::string BuildFormat(const std::string& source, const std::vector<PrintableValue>& values);

        llvm::Value* EmitFormat(const std::string& format, const std::vector<PrintableValue>& values);
