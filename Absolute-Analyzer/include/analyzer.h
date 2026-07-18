#pragma once

#include "statement_visitor.h"
#include "type.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
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
        Constructor
    };

    struct ANALYZER_API Symbol {
        SymbolId id = InvalidSymbolId;
        SymbolKind kind = SymbolKind::Variable;
        std::string name;
        std::string type;
        std::vector<std::string> parameterTypes;
        size_t scopeDepth = 0;
    };

    struct ANALYZER_API ExpressionInfo {
        SymbolId symbol = InvalidSymbolId;
        std::string type;
        bool isLValue = false;
    };

    struct ANALYZER_API Diagnostic {
        std::string message;
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
    };

    class ANALYZER_API Analyzer final : public ExpressionVisitor, public StatementVisitor {
        struct MemberSignature {
            SymbolKind kind = SymbolKind::Field;
            std::string type;
            std::vector<std::string> parameterTypes;
        };

        struct TypeDefinition {
            std::unordered_map<std::string, MemberSignature> members;
            std::optional<MemberSignature> constructor;
        };

        enum class Phase {
            CollectDeclarations,
            ResolveBodies
        };

        struct Result {
            SymbolId symbol = InvalidSymbolId;
            std::string type;
            bool isLValue = false;
        };

        std::vector<Program*> programs;
        SymbolTable table;
        std::unordered_map<std::string, TypeDefinition> types;
        std::unordered_map<const Expression*, ExpressionInfo> expressionInfo;
        std::vector<Diagnostic> diagnostics;
        Phase phase = Phase::CollectDeclarations;
        Result result;
        int typeContextDepth = 0;
        int loopDepth = 0;
        int functionDepth = 0;
        std::string currentType;
        std::string currentReturnType;
        bool callable = false;
        std::vector<std::string> callableParameters;
        int constructorContextDepth = 0;

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
        const ExpressionInfo* GetExpressionInfo(const Expression& expression) const;
        const std::vector<Diagnostic>& Diagnostics() const;

        void Visit(PrimitiveTypeExpr* expr) override;
        void Visit(UserTypeExpr* expr) override;
        void Visit(IdentifierExpr* expr) override;
        void Visit(FunctionCallExpr* expr) override;
        void Visit(ArrayAccessExpr* expr) override;
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
        void Visit(ConstructorDeclStmt* stmt) override;
        void Visit(EnumDeclStmt* stmt) override;
        void Visit(GroupDeclStmt* stmt) override;
        void Visit(IfStmt* stmt) override;
        void Visit(ForStmt* stmt) override;
        void Visit(WhileStmt* stmt) override;
        void Visit(DoWhileStmt* stmt) override;
        void Visit(ForEachStmt* stmt) override;
        void Visit(ContinueStmt* stmt) override;
        void Visit(BreakStmt* stmt) override;

    private:
        void AnalyzeProgram(Program& program);
        void Report(std::string message);
        Result Evaluate(Expression* expression);
        std::string ResolveType(Expression* expression);
        void Save(Expression* expression, Result value);
        bool IsKnownType(const std::string& name) const;
        bool IsNumeric(const std::string& name) const;
        bool IsInteger(const std::string& name) const;
        bool IsAssignable(const std::string& target, const std::string& source) const;
        std::string CommonType(const std::string& left, const std::string& right) const;
        std::vector<std::string> ResolveParameterTypes(const std::vector<std::unique_ptr<VarDeclExpr>>& parameters);
        void DeclareGlobalFunction(FunctionDeclStmt& statement);
        void ResolveFunction(FunctionDeclStmt& statement, SymbolKind kind);
        void DeclareType(const std::string& name);
        void DeclareMember(const std::string& owner, std::string name, MemberSignature signature);
        const MemberSignature* FindMember(const std::string& owner, const std::string& name) const;
        std::string ExtractIdentifier(Expression* expression) const;
    };
}
