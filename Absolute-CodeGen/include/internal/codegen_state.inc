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
            llvm::Value* arrayOwner = nullptr;
            bool ownsArrayStorage = false;
            SymbolId arrayOwnerSymbol = InvalidSymbolId;
            bool ownsAggregateResources = false;
            llvm::AllocaInst* arrayOwnerStorage = nullptr;
            llvm::AllocaInst* ownershipFlagStorage = nullptr;
            llvm::AllocaInst* debugStorage = nullptr;
        };

        struct ArrayView {
            llvm::Value* address = nullptr;
            llvm::Type* elementType = nullptr;
            std::string typeName;
            std::vector<llvm::Value*> dimensions;
            llvm::Value* owner = nullptr;
        };

        struct ClassField {
            std::string name;
            std::string typeName;
            unsigned index = 0;
        };

        struct StaticField {
            std::string name;
            std::string typeName;
            Expression* initializer = nullptr;
            bool isConst = false;
        };

        struct ClassMethod {
            FunctionDeclStmt* statement = nullptr;
            std::string owner;
            std::string linkName;
            std::optional<unsigned> virtualSlot;
            std::vector<std::string> parameterTypes;
            std::string returnType;
            std::unordered_map<std::string, std::string> substitutions;
            bool isStatic = false;
        };

        struct ClassInfo {
            std::string name;
            std::string nameSpace;
            ClassDeclStmt* statement = nullptr;
            std::vector<std::string> parents;
            std::string baseClass;
            std::vector<VarDeclExpr*> ownPrimitiveFields;
            std::vector<InstanceDeclExpr*> ownObjectFields;
            std::vector<VarDeclExpr*> ownStaticPrimitiveFields;
            std::vector<InstanceDeclExpr*> ownStaticObjectFields;
            std::vector<FunctionDeclStmt*> ownMethods;
            std::vector<PropertyDeclStmt*> ownProperties;
            // Overloaded constructors; empty means synthetic zero-arg if base needs one.
            std::vector<ConstructorDeclStmt*> constructors;
            std::vector<ClassField> fields;
            std::unordered_map<std::string, ClassField> fieldByName;
            std::vector<StaticField> staticFields;
            std::unordered_map<std::string, ClassMethod> methods;
            std::unordered_map<std::string, ClassMethod> declaredMethods;
            std::vector<std::string> virtualNames;
            llvm::StructType* llvmType = nullptr;
            llvm::GlobalVariable* vtable = nullptr;
            std::unordered_map<std::string, std::string> substitutions;
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
            std::vector<VarDeclExpr*> ownStaticPrimitiveFields;
            std::vector<InstanceDeclExpr*> ownStaticObjectFields;
            std::vector<FunctionDeclStmt*> ownMethods;
            std::vector<PropertyDeclStmt*> ownProperties;
            std::vector<ConstructorDeclStmt*> constructors;
            std::vector<ClassField> fields;
            std::unordered_map<std::string, ClassField> fieldByName;
            std::vector<StaticField> staticFields;
            std::unordered_map<std::string, ClassMethod> methods;
            llvm::StructType* llvmType = nullptr;
            std::unordered_map<std::string, std::string> substitutions;
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
            std::unordered_map<std::string, std::vector<ClassMethod>> methodContracts;
            std::unordered_map<std::string, ClassMethod> declaredMethods;
            std::unordered_map<std::string, ClassMethod> staticMethods;
            std::vector<StaticField> staticFields;
            std::unordered_map<std::string, std::string> substitutions;
            std::unordered_set<std::string> ambiguousDefaults;
            bool finalizing = false;
            bool finalized = false;
            bool emitted = false;
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
        bool debugInfoEnabled = false;
        OptimizationLevel currentOptimizationLevel = OptimizationLevel::O3;
        std::unique_ptr<llvm::DIBuilder> debugBuilder;
        llvm::DICompileUnit* debugCompileUnit = nullptr;
        llvm::DIFile* defaultDebugFile = nullptr;
        llvm::DIScope* currentDebugScope = nullptr;
        std::vector<llvm::DIScope*> debugScopeStack;
        std::unordered_map<std::string, llvm::DIFile*> debugFiles;
        std::unordered_map<std::string, llvm::DIType*> debugTypes;
        llvm::Value* value = nullptr;
        Phase phase = Phase::DeclareFunctions;
        std::vector<std::unordered_map<std::string, Variable>> scopes;
        std::unordered_map<std::string, Variable> globals;
        std::unordered_map<std::string, llvm::StructType*> arrayDescriptorTypes;
        // One constant per distinct literal: two occurrences of the same text
        // are the same storage, which is what they always were.
        std::unordered_map<std::string, llvm::Constant*> stringConstants;
        llvm::StructType* closureObjectType = nullptr;
        std::unordered_map<SymbolId, llvm::GlobalVariable*> functionClosures;
        std::unordered_map<std::string, ClassInfo> classes;
        std::vector<std::string> classOrder;
        std::unordered_map<std::string, StructInfo> structs;
        std::vector<std::string> structOrder;
        std::unordered_map<std::string, InterfaceInfo> interfaces;
        std::vector<std::string> interfaceOrder;
        std::unordered_set<std::string> enumTypes;
        std::unordered_map<std::string, std::int32_t> enumConstants;
        // Slot zero is reserved for the generated aggregate destructor.
        unsigned interfaceSlotCount = 1;
        std::vector<LoopTarget> loops;
        std::vector<ExceptionTarget> exceptionTargets;
        std::vector<FinallyTarget> finallyTargets;
        std::vector<CaughtException> caughtExceptions;
        std::vector<std::vector<DeferStmt*>> deferredScopes;
        std::unordered_map<size_t, std::vector<size_t>> deferredCleanupCursors;
        bool emittingFinally = false;
        bool exceptionsEnabled = false;
        std::string currentNamespace;
        std::unordered_map<std::string, std::string> functionLinkNames;
        bool valueCreatesManagedOwner = false;
        llvm::Value* valueManagedPointee = nullptr;
        // Owners produced inside a larger expression that nothing takes: a
        // comparison, a member read, a condition. They are destroyed at the end
        // of the statement that produced them, so the handles wait here until
        // the statement's TemporaryOwnerScope closes. The type name travels
        // with the handle because releasing an owner means releasing what it
        // owns first, which needs to know what it is.
        struct TemporaryOwner {
            llvm::Value* handle = nullptr;
            std::string typeName;
            // An array owner is a plain buffer: it is released with free(),
            // while a managed owner needs the two steps delete takes.
            bool isArrayBuffer = false;
        };
        std::vector<TemporaryOwner> temporaryManagedOwners;
        // Type-based alias information can be turned off, which is only ever
        // useful for one thing: building the same program twice and comparing.
        // A tag that claims two accesses cannot overlap when they can is
        // undefined behaviour the optimizer acts on, and the difference between
        // the two builds is the only way to see it -- both are correct code by
        // themselves, and the wrong one is still wrong at -O0.
        bool typeAliasInfoEnabled = true;
        llvm::MDNode* tbaaRoot = nullptr;
        std::unordered_map<std::string, llvm::MDNode*> tbaaTypeNodes;
        bool valueCreatesArrayOwner = false;
        llvm::Value* valueArrayOwner = nullptr;
        bool valueCreatesClosureOwner = false;
        std::string currentValueType;
        bool addressMode = false;
        llvm::Value* addressValue = nullptr;
        std::string currentReturnTypeName;
        llvm::Value* currentReturnStorage = nullptr;
        std::string currentClassName;
        // Set while a constructor body is being emitted. A constructor that
        // leaves through an exception has already initialized some fields, and
        // nothing outside holds a name for them, so it releases them itself.
        std::string currentConstructorClass;
        std::unordered_map<std::string, std::string> currentGenericSubstitutions;
        llvm::Value* currentThis = nullptr;
        std::uint64_t taskThunkCounter = 0;
        const Attribute* currentSpawnAttribute = nullptr;
        std::uint64_t lambdaCounter = 0;

        explicit Impl(CodeGenerator& visitor, const Analyzer* analyzer);

        [[noreturn]] void Fail(const std::string& message) const;
