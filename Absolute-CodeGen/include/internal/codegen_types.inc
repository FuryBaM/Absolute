        llvm::Type* TypeFromName(const std::string& name);

        llvm::StructType* ArrayDescriptorType(const std::string& name);

        llvm::Type* ResolveType(Expression* expression);

        std::string ResolveTypeName(Expression* expression);

        std::string DeclaredTypeName(VarDeclExpr& expression);

        std::string CallableParameterTypeName(VarDeclExpr& expression);

        std::string QualifiedClassName(const std::string& name, const std::string& nameSpace) const;

        void CollectClass(ClassDeclStmt& statement, const std::string& nameSpace,
            const std::string& specializedName = {},
            std::unordered_map<std::string, std::string> substitutions = {});

        void CollectStruct(StructDeclStmt& statement, const std::string& nameSpace,
            const std::string& specializedName = {},
            std::unordered_map<std::string, std::string> substitutions = {});

        void CollectInterface(InterfaceDeclStmt& statement, const std::string& nameSpace,
            const std::string& specializedName = {},
            std::unordered_map<std::string, std::string> substitutions = {});

        void CollectClassDeclarations(const std::vector<std::unique_ptr<Statement>>& statements,
            const std::string& nameSpace = {});

        void FinalizeInterface(const std::string& name);

        void FinalizeInterfaces();

        void FinalizeStruct(const std::string& name);


        void FinalizeClass(const std::string& name);

        llvm::FunctionType* MethodFunctionType(const ClassMethod& method);

        llvm::Function* DeclareMethodFunction(const ClassMethod& method);

        bool ClassNeedsConstructor(const ClassInfo& info) const;
        std::string ConstructorLinkName(const std::string& typeName,
            const std::vector<std::string>& parameterTypes) const;
        std::vector<std::string> ConstructorParameterTypeNames(
            ConstructorDeclStmt* constructor,
            const std::unordered_map<std::string, std::string>& substitutions);
        llvm::Function* LookupConstructorFunction(
            const std::string& typeName, Expression* call);
        llvm::Function* DeclareConstructorFunction(
            ClassInfo& info, ConstructorDeclStmt* constructor);

        bool RuntimeTypeDerivesFrom(const std::string& type, const std::string& base) const;

        llvm::Value* EmitRuntimeTypeTest(
            llvm::Value* reference, const std::string& sourceType, const std::string& targetType);

        void DeclareStaticFields(ClassInfo& info);

        void DeclareStaticFields(StructInfo& info);

        void DeclareStaticFields(InterfaceInfo& info);

        llvm::Function* DeclareConstructorFunction(ClassInfo& info);

        llvm::Function* DeclareConstructorFunction(StructInfo& info);

        void DeclareClasses();

        void DeclareStructs();

        std::string ClassNameFromType(std::string typeName) const;

        ClassInfo& RequireClassForType(const std::string& typeName);

        llvm::Value* ObjectPointer(Expression* expression, const std::string& typeName);

        // Whether an expression names storage the program can point at. A
        // field is only as addressable as whatever it lives in, so a field of
        // a property's or a function's result is not -- unless the chain
        // passes through a pointer, which supplies a real address.
        bool HasAddressableStorage(Expression* expression);

        llvm::Value* EmitPropertyAccessor(Expression* receiver,
            const std::string& receiverType, const std::string& methodKey,
            const std::vector<llvm::Value*>& explicitArguments);

        llvm::Value* FieldAddress(llvm::Value* object, ClassInfo& info, const ClassField& field);

        llvm::Value* FieldAddress(llvm::Value* object, StructInfo& info, const ClassField& field);

        llvm::Value* ImplicitFieldAddress(const std::string& fieldName);

        llvm::Value* ObjectSize(ClassInfo& info);

        llvm::Value* ObjectSize(StructInfo& info);

        void InitializeObject(llvm::Value* object, ClassInfo& info);

        void InitializeObject(llvm::Value* object, StructInfo& info);

        // What copying, moving and releasing a value of this type mean. The one
        // answer every release site obeys; see docs/ownership-kinds.md.
        TypeSemantics SemanticsOfTypeName(const std::string& typeName);
        TypeSemantics SemanticsOfTypeName(const std::string& typeName,
            std::unordered_set<std::string>& visiting);

        bool TypeNeedsCleanup(const std::string& typeName);

        // Whether copying a value of this type takes a count of its own --
        // whether EmitValueRetain has anything to emit for it. That is the
        // question `move` has to ask: a value whose copy counts is read by a
        // move, and one whose copy counts nothing is transferred by it.
        bool ValueCountsOnCopy(const std::string& typeName);
        bool ValueCountsOnCopy(const std::string& typeName,
            std::unordered_set<std::string>& visiting);

        llvm::Function* DeclareClassDestructor(ClassInfo& info);

        llvm::Function* DeclareStructDestructor(StructInfo& info);

        void EmitClassDestructor(ClassInfo& info);

        void EmitStructDestructor(StructInfo& info);

        void EmitValueCleanup(llvm::Value* address, const std::string& typeName);
        // The other half of a copy: what a value's parts hold, held once more
        // because there is now a second name for them. Only shared parts have
        // anything to count -- an owner and an array cannot be copied at all,
        // which is what `copyable` says and what the role machinery enforces.
        void EmitValueRetain(llvm::Value* address, const std::string& typeName);
        // A string constant, laid out the way every other string is: a
        // reference-counted header, then the bytes. The pointer handed back is
        // the first byte, and the count says static so nothing ever frees it.
        llvm::Constant* EmitStringConstant(
            const std::string& text, const std::string& name);
        // Releasing what an array's elements own, guarded by the same owner
        // bit that decides whether its storage is freed at all.
        void EmitArrayElementCleanup(llvm::Value* data, llvm::Type* elementType,
            const std::vector<llvm::Value*>& dimensions,
            const std::string& elementTypeName, llvm::Value* owner);

        // The other half of that, for the one place an array is duplicated
        // rather than named again: copied bytes are a second name for whatever
        // the elements hold, and every element has to be told, not just an
        // element that is itself a counted value.
        void EmitArrayElementRetain(llvm::Value* data, llvm::Type* elementType,
            llvm::Value* count, const std::string& elementTypeName);

        void EmitPointeeCleanup(llvm::Value* pointer, const std::string& pointerTypeName);
