        llvm::Type* TypeFromName(const std::string& name);

        llvm::StructType* ArrayDescriptorType(const std::string& name);

        llvm::Type* ResolveType(Expression* expression);

        std::string ResolveTypeName(Expression* expression);

        std::string DeclaredTypeName(VarDeclExpr& expression);

        std::string QualifiedClassName(const std::string& name, const std::string& nameSpace) const;

        void CollectClass(ClassDeclStmt& statement, const std::string& nameSpace,
            const std::string& specializedName = {},
            std::unordered_map<std::string, std::string> substitutions = {});

        void CollectStruct(StructDeclStmt& statement, const std::string& nameSpace,
            const std::string& specializedName = {},
            std::unordered_map<std::string, std::string> substitutions = {});

        void CollectInterface(InterfaceDeclStmt& statement, const std::string& nameSpace);

        void CollectClassDeclarations(const std::vector<std::unique_ptr<Statement>>& statements,
            const std::string& nameSpace = {});

        void FinalizeInterface(const std::string& name);

        void FinalizeInterfaces();

        void FinalizeStruct(const std::string& name);

        bool SameMethodSignature(FunctionDeclStmt& left, FunctionDeclStmt& right);

        void FinalizeClass(const std::string& name);

        llvm::FunctionType* MethodFunctionType(const ClassMethod& method);

        llvm::Function* DeclareMethodFunction(const ClassMethod& method);

        llvm::Function* DeclareConstructorFunction(ClassInfo& info);

        llvm::Function* DeclareConstructorFunction(StructInfo& info);

        void DeclareClasses();

        void DeclareStructs();

        std::string ClassNameFromType(std::string typeName) const;

        ClassInfo& RequireClassForType(const std::string& typeName);

        llvm::Value* ObjectPointer(Expression* expression, const std::string& typeName);

        llvm::Value* FieldAddress(llvm::Value* object, ClassInfo& info, const ClassField& field);

        llvm::Value* FieldAddress(llvm::Value* object, StructInfo& info, const ClassField& field);

        llvm::Value* ImplicitFieldAddress(const std::string& fieldName);

        llvm::Value* ObjectSize(ClassInfo& info);

        llvm::Value* ObjectSize(StructInfo& info);

        void InitializeObject(llvm::Value* object, ClassInfo& info);

        void InitializeObject(llvm::Value* object, StructInfo& info);
