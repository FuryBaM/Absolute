#pragma once

#ifndef ANALYZER_API
#define ANALYZER_API
#endif

#include "expression_visitor.h"
#include "statement_visitor.h"
#include "type_names.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Absolute {
    using SymbolId = std::uint32_t;
    inline constexpr SymbolId InvalidSymbolId = static_cast<SymbolId>(-1);

    enum class AccessLevel {
        Public,
        Protected,
        Private
    };

    enum class SymbolKind {
        Variable,
        Parameter,
        Function,
        Type,
        Field,
        Property,
        Indexer,
        Method,
        Constructor,
        Namespace
    };

    struct ANALYZER_API Symbol {
        SymbolId id = InvalidSymbolId;
        SymbolKind kind = SymbolKind::Variable;
        std::string name;
        std::string type;
        std::vector<std::string> parameterTypes;
        size_t scopeDepth = 0;
        bool managedOwner = false;
        bool managedBorrower = false;
        bool asyncFunction = false;
        bool extensionFunction = false;
        bool externalFunction = false;
        bool exportedFunction = false;
        bool isConst = false;
        bool isStatic = false;
        bool valueReference = false;
        bool constValueReference = false;
        bool variadicParameter = false;
        bool canRead = true;
        bool canWrite = true;
        bool arrayStorageEscapes = false;
        bool ownsArrayStorage = false;
        bool rolePolymorphic = false;
        bool requiresOwner = false;
        size_t parameterIndex = static_cast<size_t>(-1);
        SymbolId callableOwner = InvalidSymbolId;
        std::vector<bool> parameterRequiresOwner;
        // The expression written after `=` for each parameter, or null. Only a
        // constant is allowed there, so evaluating one at a call site is the
        // same as evaluating it at the declaration; the backend fills the
        // missing arguments in from here.
        std::vector<Expression*> parameterDefaults;
        // How many of the trailing parameters have one. A call may leave out
        // that many arguments from the end.
        size_t defaultedParameters = 0;
        AccessLevel access = AccessLevel::Public;
        AccessLevel readAccess = AccessLevel::Public;
        AccessLevel writeAccess = AccessLevel::Public;
        std::string memberOwner;
        std::vector<std::string> genericParameters;
        std::vector<std::string> genericArguments;
        SymbolId genericOrigin = InvalidSymbolId;
    };

    enum class InitializationState {
        Initialized,
        Uninitialized,
        MaybeUninitialized
    };

    enum class PointerValidity {
        NotPointer,
        Null,
        Live,
        Deleted,
        Expired,
        MaybeNull,
        MaybeInvalid,
        Unknown
    };

    enum class TaskState {
        NotTask,
        Pending,
        Awaited,
        MaybePending,
        Unknown
    };

    enum class ValueCategory {
        Value,
        Place
    };

    struct PlaceInfo {
        bool readable = true;
        bool writable = true;
        bool addressable = true;
    };

    enum class PointerRole {
        None,
        ManagedOwner,
        ManagedSub,
        Weak,
        RawOwner,
        RawView,
        Ref
    };

    struct ANALYZER_API ExpressionInfo {
        SymbolId symbol = InvalidSymbolId;
        std::string type;
        bool isLValue = false;
        bool createsManagedOwner = false;
        bool referencesManagedOwner = false;
        InitializationState initialization = InitializationState::Initialized;
        PointerValidity pointerValidity = PointerValidity::NotPointer;
        SymbolId pointerOwner = InvalidSymbolId;
        TaskState taskState = TaskState::NotTask;
        bool createsTask = false;
        bool asyncCall = false;
        bool createsRawOwner = false;
        bool isMoveResult = false;
        ValueCategory category = ValueCategory::Value;
        PlaceInfo placeInfo;
        PointerRole pointerRole = PointerRole::None;
        std::vector<std::string> parameterTypes;
        // Which callable a constructor call resolved to. A function or a
        // method call reaches its callee through the name it was written
        // with; a constructor call has no such name, and the backend needs it
        // to fill in the arguments the call left out.
        SymbolId calleeSymbol = InvalidSymbolId;
        bool createsArrayOwner = false;
        // A string expression that produced storage of its own, rather than
        // naming storage something else already holds. Only a call can: a
        // literal is static, and reading a variable, a field or an element
        // hands back the pointer that is already there.
        bool producesFreshValue = false;
    };

    struct ANALYZER_API Diagnostic {
        std::string message;
        std::string code;
        SymbolId symbol = InvalidSymbolId;
        std::string sourceFile;
        int line = 0;
        int column = 0;
    };

    struct ANALYZER_API LambdaCapture {
        SymbolId symbol = InvalidSymbolId;
        std::string name;
        std::string type;
    };

    class ANALYZER_API SymbolTable {
        std::vector<Symbol> symbols;
        std::vector<std::unordered_map<std::string, SymbolId>> scopes;

    public:
        SymbolTable();

        void Reset();
        void EnterScope();
        void ExitScope();
        size_t ScopeDepth() const;

        std::optional<SymbolId> Declare(
            SymbolKind kind,
            std::string name,
            std::string type,
            std::vector<std::string> parameterTypes = {});

        SymbolId Lookup(const std::string& name) const;
        SymbolId LookupCurrent(const std::string& name) const;
        Symbol* Get(SymbolId id);
        const Symbol* Get(SymbolId id) const;
        const std::vector<Symbol>& All() const;
        SymbolId AppendSpecialization(Symbol symbol);
    };

    class ANALYZER_API Analyzer final : public ExpressionVisitor, public StatementVisitor {
        struct MemberSignature {
            SymbolKind kind = SymbolKind::Field;
            std::string type;
            std::vector<std::string> parameterTypes;
            SymbolId symbol = InvalidSymbolId;
            bool isConst = false;
            bool isStatic = false;
            bool canRead = true;
            bool canWrite = true;
            AccessLevel access = AccessLevel::Public;
            AccessLevel readAccess = AccessLevel::Public;
            AccessLevel writeAccess = AccessLevel::Public;
            std::string owner;

            MemberSignature() = default;
            MemberSignature(SymbolKind memberKind, std::string memberType,
                std::vector<std::string> parameters = {},
                SymbolId memberSymbol = InvalidSymbolId, bool memberConst = false,
                bool memberStatic = false,
                AccessLevel memberAccess = AccessLevel::Public,
                bool memberCanRead = true, bool memberCanWrite = true)
                : kind(memberKind), type(std::move(memberType)),
                  parameterTypes(std::move(parameters)), symbol(memberSymbol),
                  isConst(memberConst), isStatic(memberStatic),
                  canRead(memberCanRead), canWrite(memberCanWrite), access(memberAccess) {
            }
        };

        enum class TypeKind {
            Other,
            Enum,
            Struct,
            Class,
            Interface
        };

        struct TypeDefinition {
            std::unordered_map<std::string, std::vector<MemberSignature>> members;
            // Overloaded constructors (unique parameter-type signatures).
            std::vector<MemberSignature> constructors;
            std::vector<std::string> parents;
            std::vector<std::string> enumMembers;
            // Parallel to enumMembers: the number each one stands for, which a
            // member may be given rather than take from its position.
            std::vector<std::int32_t> enumValues;
            TypeKind kind = TypeKind::Other;
            std::vector<std::string> genericParameters;
        };

        struct TypeAliasDefinition {
            TypeExpr* target = nullptr;
            std::string namespaceName;
            std::string resolvedType;
        };

        enum class Phase {
            CollectTypeNames,
            CollectDeclarations,
            ResolveBodies
        };

        struct Result {
            SymbolId symbol = InvalidSymbolId;
            std::string type;
            bool isLValue = false;
            bool createsManagedOwner = false;
            bool referencesManagedOwner = false;
            InitializationState initialization = InitializationState::Initialized;
            PointerValidity pointerValidity = PointerValidity::NotPointer;
            SymbolId pointerOwner = InvalidSymbolId;
            TaskState taskState = TaskState::NotTask;
            bool createsTask = false;
            bool asyncCall = false;
            bool createsRawOwner = false;
            bool isMoveResult = false;
            ValueCategory category = ValueCategory::Value;
            PlaceInfo placeInfo;
            PointerRole pointerRole = PointerRole::None;
            bool createsArrayOwner = false;
            bool producesFreshValue = false;
        };

        enum class KeepState {
            Live,
            Deleted,
            MaybeDeleted
        };

        struct KeepLifetime {
            std::string name;
            KeepState state = KeepState::Live;
        };

        using KeepLifetimeMap = std::unordered_map<SymbolId, KeepLifetime>;

        struct ValueFlowState {
            InitializationState initialization = InitializationState::Initialized;
            PointerValidity pointerValidity = PointerValidity::NotPointer;
            SymbolId pointerOwner = InvalidSymbolId;
            TaskState taskState = TaskState::NotTask;
        };

        using ValueFlowMap = std::unordered_map<SymbolId, ValueFlowState>;

        enum class AccessMode {
            Read,
            Write,
            Address,
            Delete
        };

        std::vector<Program*> programs;
        SymbolTable table;
        std::unordered_map<std::string, TypeDefinition> types;
        std::unordered_map<std::string, TypeAliasDefinition> typeAliases;
        std::unordered_set<std::string> resolvingTypeAliases;
        std::unordered_map<std::string, std::vector<SymbolId>> functionOverloads;
        std::unordered_map<std::string, std::string> exportedFunctionNames;
        std::unordered_map<std::string, std::vector<SymbolId>> extensionMethods;
        std::unordered_map<SymbolId, FunctionDeclStmt*> functionDeclarations;
        std::unordered_map<std::string, SymbolId> genericFunctionSpecializations;
        std::vector<SymbolId> genericFunctionSpecializationOrder;
        std::unordered_set<std::string> instantiatedGenericTypes;

        // What the single pass over a generic body noticed but could not judge.
        //
        // A body is analyzed once with its parameters unsubstituted, so every
        // ownership rule -- all of which ask whether a type is a pointer --
        // silently does not run inside one: the type is `T`. Rather than
        // analyze every body again for every instantiation, the pass records
        // the shape it saw and the type it saw it on, and each instantiation
        // substitutes that type and asks the rule then.
        struct GenericBodyFact {
            enum class Shape {
                FieldFromNonOwner,   // a field assigned from something not fresh
                ReturnsField,        // a field handed back to the caller
                DeletesField,        // a field released from inside the body
                DeletesValue,        // anything released through a parameter type
                CopiesElements,      // array elements duplicated as bytes
                ElementFromNonOwner, // a slot filled from something not fresh
                OrdersValues         // `<`, `<=`, `>` or `>=` on the parameter
            };
            Shape shape = Shape::FieldFromNonOwner;
            std::string parameterType;   // as written in the body, e.g. "T"
            std::string detail;          // the name to put in the message
            std::string file;
            int line = 0;
            int column = 0;
        };
        std::unordered_map<std::string, std::vector<GenericBodyFact>> genericBodyFacts;

        // Whether a written type is one of the generic parameters the current
        // type is being analyzed with -- a name that stands for itself here and
        // will be something concrete at every instantiation.
        bool IsCurrentGenericParameter(const std::string& type) const;
        // A name that still stands for itself: a generic parameter of the type
        // or the callable being analyzed, not yet substituted for anything.
        bool IsOpenGenericParameter(const std::string& name) const;
        void RecordGenericBodyFact(GenericBodyFact::Shape shape,
            const std::string& parameterType, const std::string& detail,
            const ASTNode* node);
        void CheckGenericBodyFacts(const std::string& base,
            const std::unordered_map<std::string, std::string>& substitutions);
        std::vector<std::unordered_map<std::string, std::string>> genericTypeScopes;
        std::unordered_set<std::string> namespaces;
        std::unordered_set<std::string> importedNamespaces;
        std::unordered_map<const Expression*, ExpressionInfo> expressionInfo;
        std::vector<Diagnostic> diagnostics;
        std::vector<const ASTNode*> diagnosticNodeStack;
        Phase phase = Phase::CollectDeclarations;
        Result result;
        int typeContextDepth = 0;
        int loopDepth = 0;
        // Only so a stray break inside a switch can be told why it is
        // refused, rather than being called loop-related.
        int switchDepth = 0;
        int functionDepth = 0;
        int catchDepth = 0;
        int finallyDepth = 0;
        int deferDepth = 0;
        bool usesExceptions = false;
        std::string currentType;
        std::string currentReturnType;
        std::string expectedType;
        // -1 while a numeric literal is being visited as the operand of a
        // unary minus, so the range check sees the constant the source
        // actually writes: -2147483648 fits int32, 2147483648 does not.
        int literalSign = 1;
        std::string currentNamespace;
        bool callable = false;
        std::vector<std::string> callableParameters;
        int constructorContextDepth = 0;
        KeepLifetimeMap keepLifetimes;
        std::vector<std::vector<SymbolId>> keepScopes;
        std::vector<std::unordered_set<SymbolId>> deferredKeepScopes;
        std::vector<size_t> loopKeepDepths;
        std::vector<std::vector<KeepLifetimeMap>> loopBreakStates;
        ValueFlowMap valueFlow;
        std::vector<std::vector<SymbolId>> valueFlowScopes;
        std::vector<std::unordered_set<SymbolId>> deferredTaskScopes;
        std::vector<std::vector<ValueFlowMap>> loopBreakValueStates;
        std::vector<std::unordered_set<SymbolId>> exceptionTransferredOwners;
        AccessMode accessMode = AccessMode::Read;
        bool flowTerminated = false;
        int spawnContextDepth = 0;
        bool currentFunctionAsync = false;
        bool currentFunctionNoThrow = false;
        bool currentMethodConst = false;
        bool currentMethodStatic = false;
        bool currentConstructor = false;
        struct LambdaContext {
            LambdaExpr* expression = nullptr;
            size_t scopeDepth = 0;
            std::vector<LambdaCapture> captures;
            std::unordered_set<SymbolId> capturedSymbols;
        };
        struct LambdaFunctionBoundary {
            size_t keepScopeDepth = 0;
            size_t valueFlowScopeDepth = 0;
        };
        std::vector<LambdaContext> lambdaContexts;
        std::vector<LambdaFunctionBoundary> lambdaFunctionBoundaries;
        std::unordered_map<const LambdaExpr*, std::vector<LambdaCapture>> lambdaCaptures;
        AccessLevel pendingMemberAccess = AccessLevel::Public;
        SymbolId currentCallable = InvalidSymbolId;
        std::unordered_set<SymbolId> ownerGuardedParameters;

        struct DeferredOwnershipCall {
            SymbolId callable = InvalidSymbolId;
            std::vector<bool> ownerArguments;
            std::vector<SymbolId> argumentSymbols;
            std::string context;
            // Kept because the check runs after the walk, when nothing is left
            // on the diagnostic stack to say where the call was written.
            std::string sourceFile;
            int line = 0;
            int column = 0;
        };
        std::vector<DeferredOwnershipCall> deferredOwnershipCalls;

    public:
        explicit Analyzer(std::vector<Program*> programs)
            : programs(std::move(programs)) {
        }

        bool Analyze();
        bool HasErrors() const;
        void PrintVariables(std::ostream& output = std::cout) const;
        void PrintDiagnostics(std::ostream& output = std::cerr) const;

        const SymbolTable& Symbols() const;
        const Symbol* GetSymbol(SymbolId id) const;
        const Symbol* FindFunctionSymbol(
            const std::string& name, const std::vector<std::string>& parameterTypes) const;
        size_t FunctionOverloadCount(const std::string& name) const;
        const ExpressionInfo* GetExpressionInfo(const Expression& expression) const;
        bool UsesExceptions() const { return usesExceptions; }
        const std::vector<Diagnostic>& Diagnostics() const;
        FunctionDeclStmt* FunctionDeclaration(SymbolId symbol) const;
        const std::vector<SymbolId>& GenericFunctionSpecializations() const {
            return genericFunctionSpecializationOrder;
        }
        const std::unordered_set<std::string>& InstantiatedGenericTypes() const {
            return instantiatedGenericTypes;
        }
        SymbolId InstantiateGenericFunction(
            SymbolId origin, const std::vector<std::string>& arguments);
        const std::vector<LambdaCapture>& LambdaCaptures(const LambdaExpr& expression) const;
        void PushDiagnosticNode(const ASTNode* node);
        void PopDiagnosticNode();

        void Visit(PrimitiveTypeExpr* expr) override;
        void Visit(UserTypeExpr* expr) override;
        void Visit(PointerTypeExpr* expr) override;
        void Visit(ArrayTypeExpr* expr) override;
        void Visit(IdentifierExpr* expr) override;
        void Visit(FunctionCallExpr* expr) override;
        void Visit(ArrayAccessExpr* expr) override;
        void Visit(SliceExpr* expr) override;
        void Visit(BinaryExpr* expr) override;
        void Visit(TernaryExpr* expr) override;
        void Visit(NullExpr* expr) override;
        void Visit(BooleanLiteralExpr* expr) override;
        void Visit(NumberLiteralExpr* expr) override;
        void Visit(StringLiteralExpr* expr) override;
        void Visit(CharLiteralExpr* expr) override;
        void Visit(ArrayExpr* expr) override;
        void Visit(AssignmentExpr* expr) override;
        void Visit(VarDeclExpr* expr) override;
        void Visit(MemberAccessExpr* expr) override;
        void Visit(CastExpr* expr) override;
        void Visit(ConstructorCallExpr* expr) override;
        void Visit(DestructorCallExpr* expr) override;
        void Visit(InstanceDeclExpr* expr) override;
        void Visit(PrefixUnaryExpr* expr) override;
        void Visit(PostfixUnaryExpr* expr) override;
        void Visit(TemplateExpr* expr) override;
        void Visit(LambdaExpr* expr) override;

        void Visit(SingleStatement* stmt) override;
        void Visit(CompoundStmt* stmt) override;
        void Visit(FunctionCallStmt* stmt) override;
        void Visit(FunctionDeclStmt* stmt) override;
        void Visit(PropertyDeclStmt* stmt) override;
        void Visit(IndexerDeclStmt* stmt) override;
        void Visit(ReturnStmt* stmt) override;
        void Visit(AssignmentStmt* stmt) override;
        void Visit(VarDeclStmt* stmt) override;
        void Visit(StructDeclStmt* stmt) override;
        void Visit(ClassDeclStmt* stmt) override;
        void Visit(InterfaceDeclStmt* stmt) override;
        void Visit(ConstructorDeclStmt* stmt) override;
        void Visit(EnumDeclStmt* stmt) override;
        void Visit(GroupDeclStmt* stmt) override;
        void Visit(IfStmt* stmt) override;
        void Visit(SwitchStmt* stmt) override;
        void Visit(ThrowStmt* stmt) override;
        void Visit(TryStmt* stmt) override;
        void Visit(DeferStmt* stmt) override;
        void Visit(ForStmt* stmt) override;
        void Visit(WhileStmt* stmt) override;
        void Visit(DoWhileStmt* stmt) override;
        void Visit(ForEachStmt* stmt) override;
        void Visit(ContinueStmt* stmt) override;
        void Visit(BreakStmt* stmt) override;
        void Visit(TypeAliasStmt* stmt) override;
        void Visit(ImportStmt* stmt) override;
        void Visit(NamespaceDeclStmt* stmt) override;
        void Visit(OpaquePluginStmt* stmt) override;

    private:
        void CollectProgramTypeNames(Program& program);
        void CollectTypeName(Statement& statement);
        void AnalyzeProgram(Program& program);
        std::string EnclosingSourceFile() const;
        void ReportAtLocation(std::string sourceFile, int line, int column,
            std::string message, std::string code = {},
            SymbolId symbol = InvalidSymbolId);
        void Report(std::string message, std::string code = {}, SymbolId symbol = InvalidSymbolId);
        void ReportAt(const ASTNode* node, std::string message,
            std::string code = {}, SymbolId symbol = InvalidSymbolId);
        void ReportAt(const Token* token, std::string message,
            std::string code = {}, SymbolId symbol = InvalidSymbolId);
        void ValidateAttributes(const Statement& statement, const std::string& target, bool callableTarget);
        Result Evaluate(Expression* expression);
        // What a property or an indexer hands back. A getter is a call
        // however it is written, and a `T*` a callable returns is an owner --
        // E_MANAGED_RETURN_REQUIRES_OWNER refuses any other kind -- so the
        // name that receives it owns it, exactly as it would from a method.
        // Written once because the accessor is read from four places.
        Result AccessorValue(SymbolId symbol, const std::string& type, bool isLValue) const;
        Result EvaluateExpected(Expression* expression, const std::string& type);
        std::string ResolveType(Expression* expression);
        std::string ResolveDeclaredType(VarDeclExpr& expression);
        void Save(Expression* expression, Result value);
        bool IsKnownType(const std::string& name) const;
        // Whether a value of this type owns a resource that there is exactly
        // one of: a strong managed pointer, an array's storage, a plugin
        // resource, a hand-written destroy() hook, or an aggregate holding any
        // of them. This is the question the ownership rules ask -- may it be
        // copied, must it be moved, may a reference borrow it -- and it is
        // *not* `needsDrop`, which asks whether there is anything to release
        // at all. A string answers no here and yes there, and both are right:
        // a string is shared, so a second name for one is an ordinary thing to
        // have, and it still has a count to give back.
        bool TypeOwnsUniqueResource(const std::string& name) const;

        // What copying, moving and releasing a value of this type mean, in the
        // one meaning the backend uses (see type_names.h).
        TypeSemantics SemanticsOfType(const std::string& name) const;

    private:
        // A default parameter value is a constant, the same restriction a
        // static field's initializer carries. That is what lets the backend
        // evaluate one at the call site instead of the declaration.
        static bool IsConstantDefaultArgument(const Expression* expression);

        // How many trailing parameters may be left out of a call, and what to
        // put there. Recorded where the callable is declared, because a call
        // site may be analyzed before the declaration's body is.
        static void RecordParameterDefaults(Symbol& symbol,
            const std::vector<std::unique_ptr<VarDeclExpr>>& parameters);

        // Every name a value of this type can be seen as: the type itself and
        // each of its parents, transitively.
        void CollectAncestors(const std::string& type,
            std::vector<std::string>& into) const;

        // The least upper bound of two pointer types: the most derived type
        // both can be seen as. Empty if they share no supertype, and empty
        // with `ambiguous` set if they share several that are unrelated to
        // each other -- two interfaces neither of which extends the other.
        std::string CommonPointerType(const std::string& left,
            const std::string& right, bool& ambiguous) const;

        bool OwnsUniqueResourceByParts(const std::string& name) const;

        // Whether a value of this type has anything to release at all -- the
        // other question, and the one `needsDrop` answers. A field holding a
        // string counts here and not above.
        bool ReleasesByParts(const std::string& name) const;

        // Whether a value of this type can be a second name for what the first
        // one holds. A unique owner cannot -- there is one of it -- and
        // neither can an array, whose storage has one owner; a subscriber, a
        // weak reference and a string can, because sharing is what they are
        // for. An aggregate answers by asking its parts, the same walk
        // CodeGenerator::Impl::SemanticsOfTypeName does from the backend's own
        // tables.
        bool CopiesByParts(const std::string& name) const;

    public:
        bool IsAsyncTaskValueType(const std::string& name) const;
        bool IsNumeric(const std::string& name) const;
        // Whether `<`, `<=`, `>` and `>=` have something to order: the
        // backend orders numbers, characters, enum members and raw addresses,
        // and nothing else.
        bool IsOrderableType(const std::string& name) const;
        bool IsInteger(const std::string& name) const;
        void CheckShiftAmount(
            Expression* amount, const std::string& resultType, const std::string& op);
        bool IsAssignable(const std::string& target, const std::string& source) const;
        bool IsEnumType(const std::string& name) const;
        bool IsDerivedFrom(const std::string& type, const std::string& base) const;
        AccessLevel DeclaredAccess(const Statement& statement) const;
        void ValidateAccessModifiers(const Statement& statement,
            bool memberDeclaration, const std::string& target);
        bool CanAccess(AccessLevel access, const std::string& owner) const;
        void RequireAccess(AccessLevel access, const std::string& owner,
            const std::string& member, SymbolId symbol = InvalidSymbolId);
        void RequireAccess(const MemberSignature& member, const std::string& name);
        std::string CommonType(const std::string& left, const std::string& right) const;
        void ValidateValueReferenceParameter(VarDeclExpr& parameter,
            const std::string& type, const std::string& callable,
            bool asyncCallable = false, bool cAbi = false);
        void ValidateCAbiType(const std::string& type, const std::string& where,
            bool isReturn);
        std::vector<std::string> ResolveParameterTypes(const std::vector<std::unique_ptr<VarDeclExpr>>& parameters);
        void DeclareGlobalFunction(FunctionDeclStmt& statement);
        void ResolveFunction(FunctionDeclStmt& statement, SymbolKind kind);
        void DeclareType(const std::string& name, TypeKind kind = TypeKind::Other);
        void DeclareMember(const std::string& owner, std::string name, MemberSignature signature);
        std::vector<MemberSignature> FindMembers(const std::string& owner, const std::string& name);
        std::vector<MemberSignature> FindInheritedMembers(
            const std::string& owner, const std::string& name);
        std::unordered_map<std::string, std::vector<MemberSignature>> VisibleMembers(
            const std::string& owner) const;
        std::optional<MemberSignature> FindConcreteMethod(
            const std::string& owner, const std::string& name,
            const std::vector<std::string>& parameterTypes) const;
        void ValidateInterfaceImplementation(const std::string& className);
        std::string DirectBaseClass(const std::string& className) const;
        // Zero-argument constructor parameters when one exists (nullopt = no zero-arg ctor).
        std::optional<std::vector<std::string>> ConstructorParameterTypes(
            const std::string& typeName) const;
        std::vector<MemberSignature> ConstructorsOf(const std::string& typeName) const;
        const MemberSignature* SelectConstructor(
            const std::vector<MemberSignature>& constructors,
            const std::vector<Result>& arguments,
            const std::string& displayName,
            const std::unordered_map<std::string, std::string>& substitutions = {});
        std::vector<SymbolId> FindFunctionCandidates(const std::string& name) const;
        std::vector<SymbolId> FindExtensionCandidates(const std::string& name) const;
        SymbolId SelectOverload(const std::vector<SymbolId>& candidates,
            const std::vector<Result>& arguments, const std::string& displayName,
            const std::vector<std::string>& explicitTypeArguments = {});
        int ConversionCost(const std::string& target, const std::string& source) const;
        std::string ExtractIdentifier(Expression* expression) const;
        std::string ExtractQualifiedName(Expression* expression) const;
        std::string Qualify(const std::string& name) const;
        SymbolId LookupSymbol(const std::string& name) const;
        std::string ResolveTypeReference(const std::string& name);
        std::string LookupTypeAliasName(const std::string& name) const;
        std::string ResolveTypeAlias(const std::string& name);
        bool IsConstMutationTarget(Expression* expression, const Result& target) const;
        void CheckMutableTarget(Expression* expression, const Result& target,
            const std::string& operation);
        void PushKeepScope();
        void PopKeepScope();
        void CheckKeepScopesFrom(size_t firstScope, const std::string& exitKind);
        void MergeKeepPaths(const KeepLifetimeMap& base, const std::vector<KeepLifetimeMap>& paths);
        void PushValueFlowScope();
        void PopValueFlowScope();
        void MergeValueFlowPaths(const ValueFlowMap& base, const std::vector<ValueFlowMap>& paths);
        void RegisterFlowSymbol(SymbolId id, ValueFlowState state);
        void TransferManagedAliases(SymbolId previousOwner, SymbolId nextOwner);
        void CheckManagedArgumentOwnership(const Result& argument,
            const std::string& parameterType, size_t index,
            const std::string& context);
        bool ParameterSupportsOwnership(const std::string& type) const;
        bool TransfersOwnership(const Result& argument,
            const std::string& parameterType) const;
        void RecordOwnershipCall(SymbolId callable,
            const std::vector<Result>& arguments,
            const std::vector<std::string>& parameterTypes,
            const std::string& context);
        void ValidateDeferredOwnershipCalls();
        SymbolId OwnerGuardParameter(Expression* condition) const;
        void CheckTaskScopesFrom(size_t firstScope, const std::string& exitKind);
    };
}
