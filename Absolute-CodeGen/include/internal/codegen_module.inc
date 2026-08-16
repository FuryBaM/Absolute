        void EmitBuiltin(FunctionCallExpr& expression, const std::string& name);

        llvm::Constant* GlobalConstant(Expression* expression, llvm::Type* type);

        void DeclareGlobalArray(VarDeclExpr& expression);
        void DeclareGlobalScalar(VarDeclExpr& expression);

        llvm::Function* DeclareFunction(FunctionDeclStmt& statement);

        llvm::Function* DeclareFunction(FunctionDeclStmt& statement, const Symbol* specialization);

        void EmitFunction(FunctionDeclStmt& statement);

        void EmitFunction(FunctionDeclStmt& statement, const Symbol* specialization);

        void BindCallableParameter(llvm::Argument& argument, VarDeclExpr& parameter,
            const std::string& typeName = {}, bool external = false,
            llvm::Argument* ownershipArgument = nullptr);

        void FinishClassCallable(llvm::Function& function);

        void EmitMethod(ClassInfo& info, const ClassMethod& method);

        void EmitConstructor(ClassInfo& info);
        void EmitConstructorOverload(ClassInfo& info, ConstructorDeclStmt* constructor);

        void EmitClassBodies(ClassInfo& info);

        void EmitInterfaceMethod(InterfaceInfo& info, const ClassMethod& method);

        void EmitInterfaceBodies(InterfaceInfo& info);

        void EmitStructMethod(StructInfo& info, const ClassMethod& method);

        void EmitStructConstructor(StructInfo& info);

        void EmitStructBodies(StructInfo& info);

        llvm::Module& BuildModule(Program& program, const std::string& moduleName,
            const std::string& targetTriple = {}, const std::string& dataLayout = {});

        std::string Generate(Program& program, const std::string& moduleName,
            const std::string& targetTriple = {},
            std::optional<OptimizationLevel> optimizationLevel = std::nullopt,
            bool debugInfo = false);

        void GenerateObject(Program& program, const std::string& moduleName,
            const std::string& outputPath, bool sanitizeAddress = false,
            const std::string& targetTriple = {},
            OptimizationLevel optimizationLevel = OptimizationLevel::O3,
            bool debugInfo = false);

        llvm::DIFile* DebugFile(const ASTNode* node = nullptr);
        llvm::DIType* DebugType(const std::string& typeName);
        void BeginDebugFunction(
            llvm::Function& function, const ASTNode* node,
            const std::string& sourceName);
        void EndDebugFunction();
        void PushDebugScope(const ASTNode* node);
        void PopDebugScope();
        void SetDebugLocation(const ASTNode* node);
        void DeclareDebugVariable(
            const std::string& name, const std::string& typeName,
            llvm::Value* storage, const ASTNode* node,
            bool parameter = false, unsigned argumentIndex = 0);
