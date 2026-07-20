        void EmitBuiltin(FunctionCallExpr& expression, const std::string& name);

        llvm::Constant* GlobalConstant(Expression* expression, llvm::Type* type);

        void DeclareGlobalArray(VarDeclExpr& expression);

        llvm::Function* DeclareFunction(FunctionDeclStmt& statement);

        llvm::Function* DeclareFunction(FunctionDeclStmt& statement, const Symbol* specialization);

        void EmitFunction(FunctionDeclStmt& statement);

        void EmitFunction(FunctionDeclStmt& statement, const Symbol* specialization);

        void BindCallableParameter(llvm::Argument& argument, VarDeclExpr& parameter);

        void FinishClassCallable(llvm::Function& function);

        void EmitMethod(ClassInfo& info, const ClassMethod& method);

        void EmitConstructor(ClassInfo& info);

        void EmitClassBodies(ClassInfo& info);

        void EmitInterfaceMethod(InterfaceInfo& info, const ClassMethod& method);

        void EmitInterfaceBodies(InterfaceInfo& info);

        void EmitStructMethod(StructInfo& info, const ClassMethod& method);

        void EmitStructConstructor(StructInfo& info);

        void EmitStructBodies(StructInfo& info);

        llvm::Module& BuildModule(Program& program, const std::string& moduleName,
            const std::string& targetTriple = {}, const std::string& dataLayout = {});

        std::string Generate(Program& program, const std::string& moduleName);

        void GenerateObject(Program& program, const std::string& moduleName, const std::string& outputPath);
