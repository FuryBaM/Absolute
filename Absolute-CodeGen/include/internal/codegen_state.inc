        enum class Phase {
            DeclareFunctions,
            EmitBodies
        };

        struct Variable {
            llvm::Value* address = nullptr;
            llvm::Type* type = nullptr;
            std::string typeName;
            bool managedOwner = false;
            bool isArray = false;
            llvm::Type* arrayElementType = nullptr;
            std::vector<llvm::Value*> arrayDimensions;
            llvm::AllocaInst* managedPointee = nullptr;
            SymbolId symbol = InvalidSymbolId;
        };

        struct ArrayView {
            llvm::Value* address = nullptr;
            llvm::Type* elementType = nullptr;
            std::string typeName;
            std::vector<llvm::Value*> dimensions;
        };

        struct ClassField {
            std::string name;
            std::string typeName;
            unsigned index = 0;
        };

        struct ClassMethod {
            FunctionDeclStmt* statement = nullptr;
            std::string owner;
            std::string linkName;
            std::optional<unsigned> virtualSlot;
        };

        struct ClassInfo {
            std::string name;
            std::string nameSpace;
            ClassDeclStmt* statement = nullptr;
            std::vector<std::string> parents;
            std::vector<VarDeclExpr*> ownPrimitiveFields;
            std::vector<InstanceDeclExpr*> ownObjectFields;
            std::vector<FunctionDeclStmt*> ownMethods;
            ConstructorDeclStmt* constructor = nullptr;
            std::vector<ClassField> fields;
            std::unordered_map<std::string, ClassField> fieldByName;
            std::unordered_map<std::string, ClassMethod> methods;
            std::unordered_map<std::string, ClassMethod> declaredMethods;
            std::vector<std::string> virtualNames;
            llvm::StructType* llvmType = nullptr;
            llvm::GlobalVariable* vtable = nullptr;
            bool finalizing = false;
            bool finalized = false;
            bool emitted = false;
        };

        struct StructInfo {
            std::string name;
            std::string nameSpace;
            StructDeclStmt* statement = nullptr;
            std::vector<VarDeclExpr*> ownPrimitiveFields;
            std::vector<InstanceDeclExpr*> ownObjectFields;
            std::vector<FunctionDeclStmt*> ownMethods;
            ConstructorDeclStmt* constructor = nullptr;
            std::vector<ClassField> fields;
            std::unordered_map<std::string, ClassField> fieldByName;
            std::unordered_map<std::string, ClassMethod> methods;
            llvm::StructType* llvmType = nullptr;
            bool finalizing = false;
            bool finalized = false;
            bool emitted = false;
        };

        struct InterfaceInfo {
            std::string name;
            std::string nameSpace;
            InterfaceDeclStmt* statement = nullptr;
            std::vector<std::string> parents;
            std::unordered_map<std::string, ClassMethod> methods;
            bool finalizing = false;
            bool finalized = false;
        };

        struct LoopTarget {
            llvm::BasicBlock* continueBlock = nullptr;
            llvm::BasicBlock* breakBlock = nullptr;
            size_t scopeCount = 0;
        };

        struct ExceptionTarget {
            llvm::BasicBlock* handlerBlock = nullptr;
            size_t scopeCount = 0;
        };

        struct FinallyTarget {
            TryStmt* statement = nullptr;
            size_t scopeCount = 0;
        };

        struct CaughtException {
            llvm::Value* address = nullptr;
            llvm::Value* type = nullptr;
            SymbolId symbol = InvalidSymbolId;
        };

        struct PrintableValue {
            std::string specifier;
            llvm::Value* value = nullptr;
        };

        CodeGenerator& visitor;
        const Analyzer* analyzer = nullptr;
        llvm::LLVMContext context;
        std::unique_ptr<llvm::Module> module;
        llvm::IRBuilder<> builder;
        llvm::Value* value = nullptr;
        Phase phase = Phase::DeclareFunctions;
        std::vector<std::unordered_map<std::string, Variable>> scopes;
        std::unordered_map<std::string, Variable> globals;
        std::unordered_map<std::string, llvm::StructType*> arrayDescriptorTypes;
        std::unordered_map<std::string, ClassInfo> classes;
        std::vector<std::string> classOrder;
        std::unordered_map<std::string, StructInfo> structs;
        std::vector<std::string> structOrder;
        std::unordered_map<std::string, InterfaceInfo> interfaces;
        std::vector<std::string> interfaceOrder;
        std::unordered_set<std::string> enumTypes;
        std::unordered_map<std::string, std::int32_t> enumConstants;
        unsigned interfaceSlotCount = 0;
        std::vector<LoopTarget> loops;
        std::vector<ExceptionTarget> exceptionTargets;
        std::vector<FinallyTarget> finallyTargets;
        std::vector<CaughtException> caughtExceptions;
        bool emittingFinally = false;
        bool exceptionsEnabled = false;
        std::string currentNamespace;
        std::unordered_map<std::string, std::string> functionLinkNames;
        bool valueCreatesManagedOwner = false;
        llvm::Value* valueManagedPointee = nullptr;
        std::string currentValueType;
        bool addressMode = false;
        llvm::Value* addressValue = nullptr;
        std::string currentReturnTypeName;
        std::string currentClassName;
        llvm::Value* currentThis = nullptr;
        std::uint64_t taskThunkCounter = 0;

        explicit Impl(CodeGenerator& visitor, const Analyzer* analyzer);

        [[noreturn]] void Fail(const std::string& message) const;
