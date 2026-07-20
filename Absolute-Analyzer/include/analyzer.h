#pragma once

#ifndef ANALYZER_API
#define ANALYZER_API
#endif

#include "expression_visitor.h"
#include "statement_visitor.h"

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

    enum class SymbolKind {
        Variable,
        Parameter,
        Function,
        Type,
        Field,
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
    };

    struct ANALYZER_API Diagnostic {
        std::string message;
        std::string code;
        SymbolId symbol = InvalidSymbolId;
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
            std::optional<MemberSignature> constructor;
            std::vector<std::string> parents;
            std::vector<std::string> enumMembers;
            TypeKind kind = TypeKind::Other;
            std::vector<std::string> genericParameters;
        };

        enum class Phase {
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
        std::unordered_map<std::string, std::vector<SymbolId>> functionOverloads;
        std::unordered_map<std::string, std::vector<SymbolId>> extensionMethods;
        std::unordered_map<SymbolId, FunctionDeclStmt*> functionDeclarations;
        std::unordered_map<std::string, SymbolId> genericFunctionSpecializations;
        std::vector<SymbolId> genericFunctionSpecializationOrder;
        std::unordered_set<std::string> instantiatedGenericTypes;
        std::vector<std::unordered_map<std::string, std::string>> genericTypeScopes;
        std::unordered_set<std::string> namespaces;
        std::unordered_set<std::string> importedNamespaces;
        std::unordered_map<const Expression*, ExpressionInfo> expressionInfo;
        std::vector<Diagnostic> diagnostics;
        Phase phase = Phase::CollectDeclarations;
        Result result;
        int typeContextDepth = 0;
        int loopDepth = 0;
        int functionDepth = 0;
        int catchDepth = 0;
        int finallyDepth = 0;
        int deferDepth = 0;
        bool usesExceptions = false;
        std::string currentType;
        std::string currentReturnType;
        std::string expectedType;
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

        void Visit(SingleStatement* stmt) override;
        void Visit(CompoundStmt* stmt) override;
        void Visit(FunctionCallStmt* stmt) override;
        void Visit(FunctionDeclStmt* stmt) override;
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
        void Visit(ImportStmt* stmt) override;
        void Visit(NamespaceDeclStmt* stmt) override;
        void Visit(OpaquePluginStmt* stmt) override;

    private:
        void AnalyzeProgram(Program& program);
        void Report(std::string message, std::string code = {}, SymbolId symbol = InvalidSymbolId);
        void ValidateAttributes(const Statement& statement, const std::string& target, bool callableTarget);
        Result Evaluate(Expression* expression);
        Result EvaluateExpected(Expression* expression, const std::string& type);
        std::string ResolveType(Expression* expression);
        std::string ResolveDeclaredType(VarDeclExpr& expression);
        void Save(Expression* expression, Result value);
        bool IsKnownType(const std::string& name) const;
        bool IsNumeric(const std::string& name) const;
        bool IsInteger(const std::string& name) const;
        bool IsAssignable(const std::string& target, const std::string& source) const;
        bool IsDerivedFrom(const std::string& type, const std::string& base) const;
        std::string CommonType(const std::string& left, const std::string& right) const;
        std::vector<std::string> ResolveParameterTypes(const std::vector<std::unique_ptr<VarDeclExpr>>& parameters);
        void DeclareGlobalFunction(FunctionDeclStmt& statement);
        void ResolveFunction(FunctionDeclStmt& statement, SymbolKind kind);
        void DeclareType(const std::string& name, TypeKind kind = TypeKind::Other);
        void DeclareMember(const std::string& owner, std::string name, MemberSignature signature);
        std::vector<MemberSignature> FindMembers(const std::string& owner, const std::string& name);
        std::unordered_map<std::string, std::vector<MemberSignature>> VisibleMembers(
            const std::string& owner) const;
        std::optional<MemberSignature> FindConcreteMethod(
            const std::string& owner, const std::string& name,
            const std::vector<std::string>& parameterTypes) const;
        void ValidateInterfaceImplementation(const std::string& className);
        std::vector<SymbolId> FindFunctionCandidates(const std::string& name) const;
        std::vector<SymbolId> FindExtensionCandidates(const std::string& name) const;
        SymbolId SelectOverload(const std::vector<SymbolId>& candidates,
            const std::vector<Result>& arguments, const std::string& displayName,
            const std::vector<std::string>& explicitTypeArguments = {});
        SymbolId InstantiateGenericFunction(
            SymbolId origin, const std::vector<std::string>& arguments);
        int ConversionCost(const std::string& target, const std::string& source) const;
        std::string ExtractIdentifier(Expression* expression) const;
        std::string ExtractQualifiedName(Expression* expression) const;
        std::string Qualify(const std::string& name) const;
        SymbolId LookupSymbol(const std::string& name) const;
        std::string ResolveTypeReference(const std::string& name);
        void PushKeepScope();
        void PopKeepScope();
        void CheckKeepScopesFrom(size_t firstScope, const std::string& exitKind);
        void MergeKeepPaths(const KeepLifetimeMap& base, const std::vector<KeepLifetimeMap>& paths);
        void PushValueFlowScope();
        void PopValueFlowScope();
        void MergeValueFlowPaths(const ValueFlowMap& base, const std::vector<ValueFlowMap>& paths);
        void RegisterFlowSymbol(SymbolId id, ValueFlowState state);
        void CheckTaskScopesFrom(size_t firstScope, const std::string& exitKind);
    };
}
