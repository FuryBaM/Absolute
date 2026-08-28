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

        // A call may leave out the trailing arguments whose parameters have
        // defaults. The default is a constant, so emitting it here means the
        // same thing as emitting it at the declaration.
        void AppendDefaultArguments(
            std::vector<Expression*>& expressions, const Symbol* callee);

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

        // Evaluate an operand that only reads what it is given. If the operand
        // produced an owner -- `make()` rather than a variable holding one --
        // the handle is kept for the end of the statement instead of being
        // dropped on the floor, which is what leaked it.
        llvm::Value* EvaluateBorrowed(Expression* expression);

        void RegisterTemporaryOwner(llvm::Value* handle, const std::string& typeName);

        // An array an expression made and nobody kept. The whole descriptor
        // rather than its buffer alone: releasing one means releasing what its
        // elements hold and then the storage, which is what the type already
        // says and what a scope-owned array already does.
        void RegisterTemporaryArrayOwner(
            llvm::Value* descriptor, const std::string& typeName,
            llvm::Value* owner = nullptr, llvm::Value* ownedCount = nullptr);

        void RegisterTemporaryStringOwner(llvm::Value* text);

        void RegisterTemporaryAggregate(
            llvm::Value* address, const std::string& typeName);

        void RegisterAggregateTemporaryBase(
            Expression* base, llvm::Value* object, const std::string& typeName);

        void RegisterIfFreshString(Expression* expression, llvm::Value* value);
        void RegisterFreshValueArgument(Expression* expression, llvm::Value* value);

        // A value on its way out of a callable. What is returned is usually a
        // local or a field -- storage something here still names -- and the
        // scope cleanup is about to let that name go, so the value is held
        // once more on the way out. One that the return expression made itself
        // arrives held once already and needs no second count. Both bodies a
        // callable can have ask it here: a `return` statement and a lambda's
        // one expression.
        llvm::Value* RetainReturnedValue(Expression* source, llvm::Value* result);

        // Everything between a value and the caller holding it: the count it
        // takes on the way out, the temporaries the expression made, the
        // scopes this callable is leaving -- an owner it hands over is left
        // alone -- and the return itself. Both bodies a callable can have end
        // here, so a lambda written as one expression closes its scopes the
        // way a `return` statement does. Before this it closed none, and every
        // count its parameters took on the way in was kept forever.
        void EmitCallableReturn(Expression* source, llvm::Value* result,
            size_t temporaryMark);

        // An indexer is a call written with brackets, and its indexes are its
        // arguments: a string made only in order to look something up is
        // released with the statement, the same as one passed to a method.
        // Every bracket form goes through here -- read, assign, compound
        // assign, increment -- so there is one place that answers it.
        std::vector<llvm::Value*> EvaluateIndexArguments(
            const std::vector<std::unique_ptr<Expression>>& indexes);

        // One arm of a conditional, made to hold a count of its own so that
        // both arms hand the same thing to the merge.
        llvm::Value* CountedArmValue(
            Expression* arm, llvm::Value* value, const std::string& typeName);

        bool CreatesFreshString(Expression* expression) const;

        llvm::Value* RetainStoredString(Expression* source, llvm::Value* value);

        void ReleaseTemporaryOwners(size_t mark);
        // The same releases, emitted without taking the temporaries off the
        // list: a `return` inside a loop leaves the statement that made them
        // on one path only, and the other paths still reach the end of that
        // statement and release them there. Popping here would make the
        // ordinary path forget.
        void EmitTemporaryOwnerCleanups(size_t mark);

        // Destroys, at the end of the statement it was opened for, every owner
        // registered inside it. Statements nest, so each scope only releases
        // what appeared after it opened.
        struct TemporaryOwnerScope {
            explicit TemporaryOwnerScope(Impl& owner)
                : impl(owner), mark(owner.temporaryManagedOwners.size()) {
            }
            TemporaryOwnerScope(const TemporaryOwnerScope&) = delete;
            TemporaryOwnerScope& operator=(const TemporaryOwnerScope&) = delete;
            ~TemporaryOwnerScope() { impl.ReleaseTemporaryOwners(mark); }

            Impl& impl;
            size_t mark;
        };

        // Keeps one body's pending temporaries out of another's. Bodies nest --
        // a lambda is emitted in the middle of the function that writes it, a
        // generic specialization in the middle of its first caller -- and an
        // SSA value belongs to exactly one function, so a release emitted in
        // the wrong one is invalid IR rather than a leak.
        struct FunctionTemporaryScope {
            explicit FunctionTemporaryScope(Impl& owner) : impl(owner) {
                saved.swap(owner.temporaryManagedOwners);
            }
            FunctionTemporaryScope(const FunctionTemporaryScope&) = delete;
            FunctionTemporaryScope& operator=(const FunctionTemporaryScope&) = delete;
            ~FunctionTemporaryScope() { impl.temporaryManagedOwners.swap(saved); }

            Impl& impl;
            std::vector<TemporaryOwner> saved;
        };

        // Every statement releases what it produced, so anything left when a
        // body ends is a statement kind that never opened a scope.
        void RequireNoPendingTemporaries(const std::string& where);

        // Type-based alias information, so the optimizer knows that storing an
        // element of an array cannot change a field of an object. Without it
        // every access through `this` reloads the array's data pointer and its
        // length, and a bounds check that is the same on every iteration
        // cannot be lifted out of the loop.
        //
        // Only two kinds of access are described, and only where the type is
        // known to be its own storage: an object's field, and an element of an
        // array of primitives. Everything else -- a raw pointer, a value
        // reference, an element of an array of structs -- is left untagged,
        // which means "may alias anything" and is always safe.
        llvm::MDNode* TbaaNode(const std::string& name);
        llvm::MDNode* TbaaFieldAccess(const std::string& typeName);
        llvm::MDNode* TbaaElementAccess(const std::string& typeName);
        void TagAccess(llvm::Value* instruction, llvm::MDNode* tag);

        llvm::StructType* ClosureObjectType();

        llvm::FunctionType* ClosureInvokeType(const std::string& returnType,
            const std::vector<std::string>& parameterTypes);

        llvm::Function* ClosureRetain();

        llvm::Function* ClosureRelease();

        llvm::Value* FunctionClosure(const Symbol& symbol);

        llvm::Value* BuildArrayDescriptor(const ArrayView& view);

        // The number of elements a set of dimensions covers.
        llvm::Value* ArrayElementCount(const std::vector<llvm::Value*>& dimensions);

        ArrayView ArrayViewFromValue(llvm::Value* descriptor, const std::string& typeName);

        ArrayView ViewOfArray(Expression* expression);

        void EmitOrExit(llvm::Value* condition, const std::string& name);

        // Takes the view of the base rather than computing one, because the
        // caller already has it and computing a second one evaluates the base a
        // second time. For a variable that only cost two loads; for anything
        // else -- `batch()[0]`, `snapshot()[i]` -- it ran the expression twice,
        // so a call with a side effect happened twice, the index read one array
        // while the release freed the other, and the value came from a
        // different array than the one the program thought it indexed.
        llvm::Value* ArrayElementAddress(
            ArrayAccessExpr& expression, const ArrayView& view);

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
        // What the analyzer resolved an operator expression to, or an empty
        // one. The declared parameter types come with it: an operand may need
        // the same conversion an ordinary argument would.
        struct OperatorOverload {
            llvm::Function* function = nullptr;
            std::string returnType;
            std::vector<std::string> parameterTypes;
        };
        OperatorOverload ResolvedOperator(Expression* expression) const;
        OperatorOverload ResolvedCompoundOperator(Expression* expression) const;
        llvm::Value* EmitOperatorCall(const OperatorOverload& overload,
            const std::vector<llvm::Value*>& operands,
            const std::vector<std::string>& operandTypes);

        bool IsBuiltinFunction(const std::string& name) const;

        // The runtime's shared real-to-text routine, so that printing a
        // number does not depend on the C library the target happens to have.
        static constexpr int kRealTextCapacity = 32;
        Expression* InstanceFieldInitializer(
            Statement* member, std::string& fieldName) const;
        bool ClassHasFieldInitializers(const ClassInfo& info) const;
        void EmitFieldInitializers(ClassInfo& info);
        llvm::FunctionCallee DoubleText();
        llvm::FunctionCallee FloatText();
        llvm::FunctionCallee Printf();

        llvm::FunctionCallee Snprintf();

        llvm::FunctionCallee Malloc();
        // A string is its bytes plus a header in front of them saying how many
        // names hold it. These three are the only way that count changes.
        // `size_t` for the target being emitted: 64 bits natively, 32 on
        // wasm32. A declaration that disagrees with the runtime's definition
        // links to a trapping stub rather than to the function.
        llvm::IntegerType* SizeType();
        llvm::FunctionCallee StringAlloc();
        llvm::FunctionCallee StringRetain();
        llvm::FunctionCallee StringRelease();

        // Array storage is zero-initialized, which is what the language
        // promises and what `malloc` does not do. calloc rather than a memset
        // because a large allocation then costs zero pages from the operating
        // system instead of a write over every byte.
        llvm::FunctionCallee Calloc();

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

        // Records what an unhandled report can actually say: the thrown
        // type's name and the Error's own message, both known here and
        // neither recoverable from the hashed type identifier.
        llvm::FunctionCallee ErrorSetDetails();
        void EmitErrorDetails(llvm::Value* handle, const std::string& typeName);

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
