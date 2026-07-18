#include "analyzer_pch.h"
#include "analyzer.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace Absolute {
    namespace {
        template <typename T, typename Visitor>
        void AcceptIfPresent(const std::unique_ptr<T>& node, Visitor& visitor) {
            if (node) node->Accept(visitor);
        }

        template <typename T, typename Visitor>
        void AcceptAll(const std::vector<std::unique_ptr<T>>& nodes, Visitor& visitor) {
            for (const auto& node : nodes) AcceptIfPresent(node, visitor);
        }

        bool IsConditionType(const std::string& type) {
            return type == "bool" || type == "dynamic" || type == "error" ||
                type.starts_with("int") || type.starts_with("uint");
        }

        class CallTargetProbe final : public BaseIdentifierVisitor {
        public:
            bool isMember = false;

            void Visit(MemberAccessExpr* expr) override {
                isMember = true;
                BaseIdentifierVisitor::Visit(expr);
            }
        };

        class StringLiteralProbe final : public BaseIdentifierVisitor {
        public:
            const StringLiteralExpr* literal = nullptr;

            void Visit(StringLiteralExpr* expr) override { literal = expr; }
        };

        class QualifiedNameVisitor final : public BaseIdentifierVisitor {
        public:
            std::string name;

            void Visit(IdentifierExpr* expr) override { name = expr->name; }

            void Visit(MemberAccessExpr* expr) override {
                if (expr->base) expr->base->Accept(*this);
                if (!name.empty()) name += ".";
                name += expr->member;
            }

            void Visit(UserTypeExpr* expr) override {
                if (expr->typeExpr) expr->typeExpr->Accept(*this);
            }

            void Visit(TemplateExpr* expr) override {
                if (expr->base) expr->base->Accept(*this);
            }
        };

        bool IsBuiltinFunction(const std::string& name) {
            return name == "print" || name == "println" || name == "format" ||
                name == "toString" || name == "assert";
        }

        bool IsPrintableType(const std::string& type) {
            return type == "bool" || type == "string" || type == "char" || type == "null" ||
                type == "dynamic" || type == "error" || type == "float" || type == "double" ||
                type.starts_with("int") || type.starts_with("uint");
        }

        std::optional<size_t> CountFormatPlaceholders(const std::string& format) {
            size_t count = 0;
            for (size_t index = 0; index < format.size(); ++index) {
                if (format[index] == '{') {
                    if (index + 1 < format.size() && format[index + 1] == '{') {
                        ++index;
                    }
                    else if (index + 1 < format.size() && format[index + 1] == '}') {
                        ++count;
                        ++index;
                    }
                    else return std::nullopt;
                }
                else if (format[index] == '}') {
                    if (index + 1 < format.size() && format[index + 1] == '}') ++index;
                    else return std::nullopt;
                }
            }
            return count;
        }
    }

    SymbolTable::SymbolTable() { Reset(); }

    void SymbolTable::Reset() {
        symbols.clear();
        scopes.clear();
        scopes.emplace_back();
    }

    void SymbolTable::EnterScope() { scopes.emplace_back(); }

    void SymbolTable::ExitScope() {
        if (scopes.size() > 1) scopes.pop_back();
    }

    size_t SymbolTable::ScopeDepth() const { return scopes.empty() ? 0 : scopes.size() - 1; }

    std::optional<SymbolId> SymbolTable::Declare(
        SymbolKind kind, std::string name, std::string type,
        std::vector<std::string> parameterTypes) {
        if (scopes.empty()) scopes.emplace_back();
        if (scopes.back().contains(name)) return std::nullopt;
        const SymbolId id = static_cast<SymbolId>(symbols.size());
        symbols.push_back({id, kind, std::move(name), std::move(type),
            std::move(parameterTypes), ScopeDepth()});
        scopes.back().emplace(symbols.back().name, id);
        return id;
    }

    SymbolId SymbolTable::Lookup(const std::string& name) const {
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
            const auto found = scope->find(name);
            if (found != scope->end()) return found->second;
        }
        return InvalidSymbolId;
    }

    SymbolId SymbolTable::LookupCurrent(const std::string& name) const {
        if (scopes.empty()) return InvalidSymbolId;
        const auto found = scopes.back().find(name);
        return found == scopes.back().end() ? InvalidSymbolId : found->second;
    }

    Symbol* SymbolTable::Get(SymbolId id) {
        return id < symbols.size() ? &symbols[id] : nullptr;
    }

    const Symbol* SymbolTable::Get(SymbolId id) const {
        return id < symbols.size() ? &symbols[id] : nullptr;
    }

    const std::vector<Symbol>& SymbolTable::All() const { return symbols; }

    bool Analyzer::Analyze() {
        table.Reset();
        types.clear();
        namespaces.clear();
        importedNamespaces.clear();
        expressionInfo.clear();
        diagnostics.clear();
        currentType.clear();
        currentReturnType.clear();
        currentNamespace.clear();
        loopDepth = functionDepth = typeContextDepth = constructorContextDepth = 0;

        phase = Phase::CollectDeclarations;
        table.Declare(SymbolKind::Function, "print", "void", {"..."});
        table.Declare(SymbolKind::Function, "println", "void", {"..."});
        table.Declare(SymbolKind::Function, "format", "string", {"string", "..."});
        table.Declare(SymbolKind::Function, "toString", "string", {"dynamic"});
        table.Declare(SymbolKind::Function, "assert", "void", {"bool", "string?"});
        for (Program* program : programs) if (program) AnalyzeProgram(*program);
        phase = Phase::ResolveBodies;
        for (Program* program : programs) if (program) AnalyzeProgram(*program);
        return !HasErrors();
    }

    bool Analyzer::HasErrors() const { return !diagnostics.empty(); }

    void Analyzer::PrintVariables(std::ostream& output) const {
        for (const Symbol& symbol : table.All()) {
            if (symbol.kind == SymbolKind::Variable || symbol.kind == SymbolKind::Parameter ||
                symbol.kind == SymbolKind::Field) {
                output << symbol.name << ": " << symbol.type << '\n';
            }
        }
    }

    void Analyzer::PrintDiagnostics(std::ostream& output) const {
        for (const Diagnostic& diagnostic : diagnostics)
            output << "Semantic error: " << diagnostic.message << '\n';
    }

    const SymbolTable& Analyzer::Symbols() const { return table; }
    const Symbol* Analyzer::GetSymbol(SymbolId id) const { return table.Get(id); }

    const ExpressionInfo* Analyzer::GetExpressionInfo(const Expression& expression) const {
        const auto found = expressionInfo.find(&expression);
        return found == expressionInfo.end() ? nullptr : &found->second;
    }

    const std::vector<Diagnostic>& Analyzer::Diagnostics() const { return diagnostics; }

    void Analyzer::AnalyzeProgram(Program& program) { AcceptAll(program.statements, *this); }

    void Analyzer::Report(std::string message) { diagnostics.push_back({std::move(message)}); }

    Analyzer::Result Analyzer::Evaluate(Expression* expression) {
        callable = false;
        callableParameters.clear();
        result = {};
        if (expression) expression->Accept(*this);
        else result.type = "void";
        return result;
    }

    std::string Analyzer::ResolveType(Expression* expression) {
        ++typeContextDepth;
        const Result resolved = Evaluate(expression);
        --typeContextDepth;
        return resolved.type.empty() ? "error" : resolved.type;
    }

    void Analyzer::Save(Expression* expression, Result value) {
        result = std::move(value);
        if (expression) expressionInfo[expression] = {result.symbol, result.type, result.isLValue};
    }

    bool Analyzer::IsKnownType(const std::string& name) const {
        return PrimitiveStringToEnum(name).has_value() || types.contains(name) || name == "null" ||
            (name.size() > 2 && name.ends_with("[]") && IsKnownType(name.substr(0, name.size() - 2)));
    }

    bool Analyzer::IsNumeric(const std::string& name) const {
        return name == "float" || name == "double" || name.starts_with("int") || name.starts_with("uint") ||
            name == "char";
    }

    bool Analyzer::IsInteger(const std::string& name) const {
        return name.starts_with("int") || name.starts_with("uint") || name == "char";
    }

    bool Analyzer::IsAssignable(const std::string& target, const std::string& source) const {
        if (target == "error" || source == "error" || target == "dynamic" || source == "dynamic") return true;
        if (target == source) return true;
        if (IsNumeric(target) && IsNumeric(source)) return true;
        return source == "null" && !PrimitiveStringToEnum(target).has_value();
    }

    std::string Analyzer::CommonType(const std::string& left, const std::string& right) const {
        if (left == right) return left;
        if (left == "error" || right == "error") return "error";
        if (!IsNumeric(left) || !IsNumeric(right)) return "error";
        if (left == "double" || right == "double") return "double";
        if (left == "float" || right == "float") return "float";
        if (left == "int64" || right == "int64" || left == "uint64" || right == "uint64") return "int64";
        return "int32";
    }

    std::vector<std::string> Analyzer::ResolveParameterTypes(
        const std::vector<std::unique_ptr<VarDeclExpr>>& parameters) {
        std::vector<std::string> resolved;
        resolved.reserve(parameters.size());
        for (const auto& parameter : parameters)
            resolved.push_back(parameter ? ResolveType(parameter->type.get()) : "error");
        return resolved;
    }

    void Analyzer::DeclareGlobalFunction(FunctionDeclStmt& statement) {
        if (!statement.name || !statement.returnType) return;
        const std::string name = Qualify(statement.name->value);
        const auto declared = table.Declare(SymbolKind::Function, name,
            statement.returnType->value, ResolveParameterTypes(statement.parameters));
        if (!declared) Report("object '" + name + "' is already declared in this scope");
    }

    void Analyzer::ResolveFunction(FunctionDeclStmt& statement, SymbolKind kind) {
        if (!statement.name || !statement.returnType) return;
        const std::string returnType = ResolveTypeReference(statement.returnType->value);
        if (!IsKnownType(returnType))
            Report("unknown return type '" + statement.returnType->value + "' of function '" + statement.name->value + "'");

        if (statement.IsExternal() && kind == SymbolKind::Method)
            Report("extern functions cannot be class or struct members");

        const std::string oldReturn = currentReturnType;
        currentReturnType = returnType;
        ++functionDepth;
        table.EnterScope();
        for (const auto& parameter : statement.parameters) {
            if (!parameter) continue;
            const std::string name = ExtractIdentifier(parameter->name.get());
            std::string type = ResolveType(parameter->type.get());
            if (statement.IsExternal() && (type == "auto" || type == "dynamic" || type == "void"))
                Report("extern parameter '" + name + "' requires a concrete C-compatible type");
            if (name.empty()) Report("function parameter requires an identifier");
            else if (!table.Declare(SymbolKind::Parameter, name, type))
                Report("parameter '" + name + "' is already declared");
            if (parameter->value) {
                const Result value = Evaluate(parameter->value.get());
                if (!IsAssignable(type, value.type))
                    Report("default value of parameter '" + name + "' has type '" + value.type + "', expected '" + type + "'");
            }
        }
        if (!statement.IsExternal()) AcceptIfPresent(statement.body, *this);
        table.ExitScope();
        --functionDepth;
        currentReturnType = oldReturn;
        (void)kind;
    }

    void Analyzer::DeclareType(const std::string& name) {
        const std::string qualifiedName = Qualify(name);
        if (types.contains(qualifiedName)) {
            Report("type '" + qualifiedName + "' is already declared");
            return;
        }
        types.emplace(qualifiedName, TypeDefinition{});
        if (!table.Declare(SymbolKind::Type, qualifiedName, qualifiedName))
            Report("object '" + qualifiedName + "' is already declared in this scope");
    }

    void Analyzer::DeclareMember(const std::string& owner, std::string name, MemberSignature signature) {
        auto& members = types[owner].members;
        if (members.contains(name)) Report("member '" + owner + "." + name + "' is already declared");
        else members.emplace(std::move(name), std::move(signature));
    }

    const Analyzer::MemberSignature* Analyzer::FindMember(
        const std::string& owner, const std::string& name) const {
        const auto type = types.find(owner);
        if (type == types.end()) return nullptr;
        const auto member = type->second.members.find(name);
        return member == type->second.members.end() ? nullptr : &member->second;
    }

    std::string Analyzer::ExtractIdentifier(Expression* expression) const {
        if (!expression) return {};
        BaseIdentifierVisitor visitor;
        expression->Accept(visitor);
        return visitor.identifierExpr ? visitor.identifierExpr->name : std::string{};
    }

    std::string Analyzer::ExtractQualifiedName(Expression* expression) const {
        if (!expression) return {};
        QualifiedNameVisitor visitor;
        expression->Accept(visitor);
        return visitor.name;
    }

    std::string Analyzer::Qualify(const std::string& name) const {
        if (name.empty() || currentNamespace.empty() || name.find('.') != std::string::npos) return name;
        return currentNamespace + "." + name;
    }

    SymbolId Analyzer::LookupSymbol(const std::string& name) const {
        if (name.empty()) return InvalidSymbolId;
        if (const SymbolId direct = table.Lookup(name); direct != InvalidSymbolId) return direct;

        if (name.find('.') != std::string::npos) {
            if (!currentNamespace.empty()) {
                if (const SymbolId nested = table.Lookup(currentNamespace + "." + name);
                    nested != InvalidSymbolId) return nested;
            }
            return InvalidSymbolId;
        }

        std::string scope = currentNamespace;
        while (!scope.empty()) {
            if (const SymbolId scoped = table.Lookup(scope + "." + name); scoped != InvalidSymbolId)
                return scoped;
            const size_t separator = scope.rfind('.');
            if (separator == std::string::npos) break;
            scope.resize(separator);
        }
        for (const std::string& imported : importedNamespaces) {
            if (const SymbolId importedSymbol = table.Lookup(imported + "." + name);
                importedSymbol != InvalidSymbolId) return importedSymbol;
        }
        return InvalidSymbolId;
    }

    std::string Analyzer::ResolveTypeReference(const std::string& name) const {
        if (PrimitiveStringToEnum(name).has_value() || name == "null" || name == "error") return name;
        const Symbol* symbol = table.Get(LookupSymbol(name));
        return symbol && symbol->kind == SymbolKind::Type ? symbol->name : name;
    }

    void Analyzer::Visit(PrimitiveTypeExpr* expr) {
        if (typeContextDepth == 0) Report("type '" + expr->type + "' cannot be used as a value");
        Save(expr, {InvalidSymbolId, expr->type, false});
    }

    void Analyzer::Visit(UserTypeExpr* expr) {
        const std::string type = ResolveType(expr->typeExpr.get());
        Save(expr, {InvalidSymbolId, type, false});
    }

    void Analyzer::Visit(IdentifierExpr* expr) {
        if (typeContextDepth > 0) {
            const std::string type = ResolveTypeReference(expr->name);
            if (phase == Phase::ResolveBodies && !IsKnownType(type)) Report("unknown type '" + expr->name + "'");
            Save(expr, {InvalidSymbolId, type, false});
            return;
        }
        const SymbolId id = LookupSymbol(expr->name);
        const Symbol* symbol = table.Get(id);
        if (!symbol) {
            Report("unknown object '" + expr->name + "'");
            Save(expr, {InvalidSymbolId, "error", false});
            return;
        }
        const bool value = symbol->kind == SymbolKind::Variable || symbol->kind == SymbolKind::Parameter ||
            symbol->kind == SymbolKind::Field;
        if (!value) Report("object '" + expr->name + "' is not a value");
        Save(expr, {id, symbol->type, value});
    }

    void Analyzer::Visit(FunctionCallExpr* expr) {
        if (constructorContextDepth > 0) {
            const std::string typeName = ExtractIdentifier(expr->base.get());
            if (!IsKnownType(typeName) || !types.contains(typeName))
                Report("unknown constructible type '" + typeName + "'");
            const auto found = types.find(typeName);
            const std::vector<std::string> expected = found != types.end() && found->second.constructor
                ? found->second.constructor->parameterTypes : std::vector<std::string>{};
            if (expr->arguments.size() != expected.size())
                Report("constructor of '" + typeName + "' expects " + std::to_string(expected.size()) +
                    " argument(s), got " + std::to_string(expr->arguments.size()));
            for (size_t i = 0; i < expr->arguments.size(); ++i) {
                const Result argument = Evaluate(expr->arguments[i].get());
                if (i < expected.size() && !IsAssignable(expected[i], argument.type))
                    Report("constructor argument " + std::to_string(i + 1) + " has type '" + argument.type +
                        "', expected '" + expected[i] + "'");
            }
            Save(expr, {InvalidSymbolId, typeName.empty() ? "error" : typeName, false});
            return;
        }

        CallTargetProbe probe;
        if (expr->base) expr->base->Accept(probe);
        const std::string callName = probe.identifierExpr ? probe.identifierExpr->name : std::string{};
        const std::string qualifiedCallName = ExtractQualifiedName(expr->base.get());

        if (!probe.isMember && IsBuiltinFunction(callName)) {
            std::vector<Result> arguments;
            arguments.reserve(expr->arguments.size());
            for (const auto& argument : expr->arguments) arguments.push_back(Evaluate(argument.get()));

            if (callName == "print" || callName == "println") {
                for (size_t index = 0; index < arguments.size(); ++index) {
                    if (!IsPrintableType(arguments[index].type))
                        Report(callName + " argument " + std::to_string(index + 1) +
                            " has unsupported type '" + arguments[index].type + "'");
                }
                Save(expr, {table.Lookup(callName), "void", false});
                return;
            }

            if (callName == "toString") {
                if (arguments.size() != 1) Report("toString expects exactly one argument");
                else if (!IsPrintableType(arguments.front().type))
                    Report("toString cannot convert type '" + arguments.front().type + "'");
                Save(expr, {table.Lookup(callName), "string", false});
                return;
            }

            if (callName == "assert") {
                if (arguments.empty() || arguments.size() > 2)
                    Report("assert expects a condition and an optional message");
                else if (!IsConditionType(arguments.front().type))
                    Report("assert condition must be boolean-compatible");
                if (arguments.size() == 2 && arguments[1].type != "string" && arguments[1].type != "error")
                    Report("assert message must be a string");
                Save(expr, {table.Lookup(callName), "void", false});
                return;
            }

            if (arguments.empty()) {
                Report("format expects a string literal template");
            }
            else {
                if (arguments.front().type != "string" && arguments.front().type != "error")
                    Report("format template must be a string");
                StringLiteralProbe literalProbe;
                expr->arguments.front()->Accept(literalProbe);
                if (!literalProbe.literal) Report("format template must be a string literal");
                else {
                    const std::optional<size_t> placeholders = CountFormatPlaceholders(literalProbe.literal->value);
                    if (!placeholders) Report("format template contains an unmatched brace");
                    else if (*placeholders != arguments.size() - 1)
                        Report("format template expects " + std::to_string(*placeholders) +
                            " value(s), got " + std::to_string(arguments.size() - 1));
                }
                for (size_t index = 1; index < arguments.size(); ++index) {
                    if (!IsPrintableType(arguments[index].type))
                        Report("format value " + std::to_string(index) + " has unsupported type '" +
                            arguments[index].type + "'");
                }
            }
            Save(expr, {table.Lookup(callName), "string", false});
            return;
        }

        bool targetCallable = false;
        std::vector<std::string> parameters;
        SymbolId symbolId = InvalidSymbolId;
        std::string returnType = "error";

        const SymbolId qualifiedId = LookupSymbol(qualifiedCallName);
        const Symbol* qualifiedSymbol = table.Get(qualifiedId);
        if (qualifiedSymbol && (qualifiedSymbol->kind == SymbolKind::Function ||
            qualifiedSymbol->kind == SymbolKind::Method)) {
            targetCallable = true;
            parameters = qualifiedSymbol->parameterTypes;
            symbolId = qualifiedId;
            returnType = qualifiedSymbol->type;
        }
        else if (probe.isMember) {
            const Result target = Evaluate(expr->base.get());
            targetCallable = callable;
            parameters = callableParameters;
            symbolId = target.symbol;
            returnType = target.type;
        }
        else {
            const std::string name = callName;
            symbolId = LookupSymbol(name);
            const Symbol* symbol = table.Get(symbolId);
            if (symbol && (symbol->kind == SymbolKind::Function || symbol->kind == SymbolKind::Method)) {
                targetCallable = true;
                parameters = symbol->parameterTypes;
                returnType = symbol->type;
            }
            else {
                Report("unknown function '" + name + "'");
                returnType = "error";
            }
        }
        if (targetCallable && expr->arguments.size() != parameters.size())
            Report("function expects " + std::to_string(parameters.size()) + " argument(s), got " +
                std::to_string(expr->arguments.size()));
        for (size_t i = 0; i < expr->arguments.size(); ++i) {
            const Result argument = Evaluate(expr->arguments[i].get());
            if (i < parameters.size() && !IsAssignable(parameters[i], argument.type))
                Report("argument " + std::to_string(i + 1) + " has type '" + argument.type +
                    "', expected '" + parameters[i] + "'");
        }
        Save(expr, {symbolId, returnType, false});
    }

    void Analyzer::Visit(ArrayAccessExpr* expr) {
        Result base = Evaluate(expr->base.get());
        for (const auto& index : expr->indexes) {
            if (!index) continue;
            const Result indexResult = Evaluate(index.get());
            if (!IsInteger(indexResult.type) && indexResult.type != "error")
                Report("array index must be an integer, got '" + indexResult.type + "'");
        }
        if (!base.type.ends_with("[]")) {
            Report("object of type '" + base.type + "' is not an array");
            Save(expr, {base.symbol, "error", false});
        }
        else Save(expr, {base.symbol, base.type.substr(0, base.type.size() - 2), base.isLValue});
    }

    void Analyzer::Visit(BinaryExpr* expr) {
        const Result left = Evaluate(expr->left.get());
        const Result right = Evaluate(expr->right.get());
        const std::string& op = expr->op;
        if (op == "&&" || op == "||") {
            if (!IsConditionType(left.type) || !IsConditionType(right.type)) Report("logical operands must be boolean-compatible");
            Save(expr, {InvalidSymbolId, "bool", false});
        }
        else if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
            if (!IsAssignable(left.type, right.type) && !IsAssignable(right.type, left.type))
                Report("cannot compare '" + left.type + "' with '" + right.type + "'");
            Save(expr, {InvalidSymbolId, "bool", false});
        }
        else if (op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>") {
            if (!IsInteger(left.type) || !IsInteger(right.type)) Report("bitwise operands must be integers");
            Save(expr, {InvalidSymbolId, CommonType(left.type, right.type), false});
        }
        else {
            if (!IsNumeric(left.type) || !IsNumeric(right.type))
                Report("operator '" + op + "' requires numeric operands");
            Save(expr, {InvalidSymbolId, CommonType(left.type, right.type), false});
        }
    }

    void Analyzer::Visit(TernaryExpr* expr) {
        const Result condition = Evaluate(expr->condition.get());
        if (!IsConditionType(condition.type)) Report("ternary condition must be boolean-compatible");
        const Result trueResult = Evaluate(expr->trueExpr.get());
        const Result falseResult = Evaluate(expr->falseExpr.get());
        const std::string type = CommonType(trueResult.type, falseResult.type);
        if (type == "error" && trueResult.type != "error" && falseResult.type != "error")
            Report("ternary branches have incompatible types '" + trueResult.type + "' and '" + falseResult.type + "'");
        Save(expr, {InvalidSymbolId, type, false});
    }

    void Analyzer::Visit(NullExpr* expr) { Save(expr, {InvalidSymbolId, "null", false}); }
    void Analyzer::Visit(BooleanLiteralExpr* expr) { Save(expr, {InvalidSymbolId, "bool", false}); }
    void Analyzer::Visit(NumberLiteralExpr* expr) {
        Save(expr, {InvalidSymbolId, expr->value.find('.') == std::string::npos ? "int32" : "double", false});
    }
    void Analyzer::Visit(StringLiteralExpr* expr) { Save(expr, {InvalidSymbolId, "string", false}); }
    void Analyzer::Visit(CharLiteralExpr* expr) { Save(expr, {InvalidSymbolId, "char", false}); }

    void Analyzer::Visit(ArrayExpr* expr) {
        for (const auto& size : expr->sizes) {
            if (!size) continue;
            const Result resolved = Evaluate(size.get());
            if (!IsInteger(resolved.type)) Report("array size must be an integer");
        }
        std::string elementType;
        for (const auto& value : expr->values) {
            const Result resolved = Evaluate(value.get());
            elementType = elementType.empty() ? resolved.type : CommonType(elementType, resolved.type);
        }
        if (elementType.empty()) elementType = "dynamic";
        Save(expr, {InvalidSymbolId, elementType + "[]", false});
    }

    void Analyzer::Visit(AssignmentExpr* expr) {
        const Result target = Evaluate(expr->target.get());
        const Result value = Evaluate(expr->value.get());
        if (!target.isLValue) Report("assignment target is not assignable");
        if (!IsAssignable(target.type, value.type))
            Report("cannot assign '" + value.type + "' to '" + target.type + "'");
        Save(expr, {target.symbol, target.type, false});
    }

    void Analyzer::Visit(VarDeclExpr* expr) {
        const std::string name = ExtractIdentifier(expr->name.get());
        const std::string declarationName = currentType.empty() && functionDepth == 0 ? Qualify(name) : name;
        std::string type = ResolveType(expr->type.get());
        if (phase == Phase::CollectDeclarations) {
            if (currentType.empty()) {
                if (!table.Declare(SymbolKind::Variable, declarationName, type))
                    Report("object '" + declarationName + "' is already declared in this scope");
            }
            else DeclareMember(currentType, name, {SymbolKind::Field, type, {}});
            Save(expr, {InvalidSymbolId, type, false});
            return;
        }
        if (!IsKnownType(type)) Report("unknown type '" + type + "' of variable '" + name + "'");
        Result value;
        if (expr->value) value = Evaluate(expr->value.get());
        if (type == "auto") {
            if (!expr->value) Report("auto variable '" + name + "' requires an initializer");
            else type = value.type;
        }
        else if (expr->value && !IsAssignable(type, value.type))
            Report("initializer of '" + name + "' has type '" + value.type + "', expected '" + type + "'");

        const bool fieldDeclaration = !currentType.empty() && functionDepth == 0;
        const bool globalDeclaration = currentType.empty() && table.ScopeDepth() == 0;
        SymbolId id = fieldDeclaration ? table.Lookup(name) :
            (globalDeclaration ? table.Lookup(declarationName) : table.LookupCurrent(name));
        if (!fieldDeclaration && !globalDeclaration) {
            const auto declared = table.Declare(SymbolKind::Variable, name, type);
            if (!declared) Report("object '" + name + "' is already declared in this scope");
            else id = *declared;
        }
        else if (Symbol* symbol = table.Get(id)) symbol->type = type;
        Save(expr, {id, type, true});
    }

    void Analyzer::Visit(MemberAccessExpr* expr) {
        const std::string qualifiedName = ExtractQualifiedName(expr);
        if (typeContextDepth > 0) {
            const std::string type = ResolveTypeReference(qualifiedName);
            if (phase == Phase::ResolveBodies && !IsKnownType(type)) Report("unknown type '" + qualifiedName + "'");
            Save(expr, {InvalidSymbolId, type, false});
            return;
        }

        const SymbolId qualifiedId = LookupSymbol(qualifiedName);
        if (const Symbol* symbol = table.Get(qualifiedId)) {
            const bool isValue = symbol->kind == SymbolKind::Variable || symbol->kind == SymbolKind::Parameter ||
                symbol->kind == SymbolKind::Field;
            if (!isValue && symbol->kind != SymbolKind::Function && symbol->kind != SymbolKind::Method)
                Report("object '" + qualifiedName + "' is not a value");
            callable = symbol->kind == SymbolKind::Function || symbol->kind == SymbolKind::Method;
            callableParameters = symbol->parameterTypes;
            Save(expr, {qualifiedId, symbol->type, isValue});
            return;
        }

        const Result base = Evaluate(expr->base.get());
        const MemberSignature* member = FindMember(base.type, expr->member);
        if (!member) {
            Report("type '" + base.type + "' has no member '" + expr->member + "'");
            Save(expr, {InvalidSymbolId, "error", false});
            return;
        }
        callable = member->kind == SymbolKind::Method;
        callableParameters = member->parameterTypes;
        Save(expr, {InvalidSymbolId, member->type, member->kind == SymbolKind::Field});
    }

    void Analyzer::Visit(CastExpr* expr) {
        const std::string target = ResolveType(expr->typeName.get());
        const Result base = Evaluate(expr->base.get());
        if (!IsAssignable(target, base.type) && !(IsNumeric(target) && IsNumeric(base.type)))
            Report("cannot cast '" + base.type + "' to '" + target + "'");
        Save(expr, {InvalidSymbolId, target, false});
    }

    void Analyzer::Visit(ConstructorCallExpr* expr) {
        ++constructorContextDepth;
        const Result constructed = Evaluate(expr->constructName.get());
        --constructorContextDepth;
        Save(expr, {InvalidSymbolId, constructed.type, false});
    }

    void Analyzer::Visit(DestructorCallExpr* expr) {
        const Result target = Evaluate(expr->target.get());
        if (!types.contains(target.type) && target.type != "error")
            Report("destructor requires an object instance, got '" + target.type + "'");
        Save(expr, {InvalidSymbolId, "void", false});
    }

    void Analyzer::Visit(InstanceDeclExpr* expr) {
        const std::string name = ExtractIdentifier(expr->identifierName.get());
        const std::string declarationName = currentType.empty() && functionDepth == 0 ? Qualify(name) : name;
        const std::string type = ResolveType(expr->constructType.get());
        if (phase == Phase::CollectDeclarations) {
            if (currentType.empty()) {
                if (!table.Declare(SymbolKind::Variable, declarationName, type))
                    Report("object '" + declarationName + "' is already declared in this scope");
            }
            else DeclareMember(currentType, name, {SymbolKind::Field, type, {}});
            Save(expr, {InvalidSymbolId, type, false});
            return;
        }
        if (!types.contains(type)) Report("unknown object type '" + type + "'");
        Result value;
        if (expr->value) value = Evaluate(expr->value.get());
        if (expr->value && !IsAssignable(type, value.type))
            Report("initializer of '" + name + "' has type '" + value.type + "', expected '" + type + "'");
        const bool existingDeclaration = (!currentType.empty() && functionDepth == 0) ||
            (currentType.empty() && table.ScopeDepth() == 0);
        SymbolId id = (!currentType.empty() && functionDepth == 0) ? table.Lookup(name) :
            (existingDeclaration ? table.Lookup(declarationName) : table.LookupCurrent(name));
        if (!existingDeclaration) {
            const auto declared = table.Declare(SymbolKind::Variable, name, type);
            if (!declared) Report("object '" + name + "' is already declared in this scope");
            else id = *declared;
        }
        Save(expr, {id, type, true});
    }

    void Analyzer::Visit(PrefixUnaryExpr* expr) {
        const Result operand = Evaluate(expr->operand.get());
        if ((expr->op == "++" || expr->op == "--") && (!operand.isLValue || !IsNumeric(operand.type)))
            Report("operator '" + expr->op + "' requires an assignable numeric operand");
        else if (expr->op == "!" && !IsConditionType(operand.type)) Report("operator '!' requires a boolean-compatible operand");
        else if ((expr->op == "+" || expr->op == "-") && !IsNumeric(operand.type)) Report("unary numeric operator requires a number");
        else if (expr->op == "~" && !IsInteger(operand.type)) Report("operator '~' requires an integer");
        Save(expr, {operand.symbol, expr->op == "!" ? "bool" : operand.type, false});
    }

    void Analyzer::Visit(PostfixUnaryExpr* expr) {
        const Result operand = Evaluate(expr->operand.get());
        if (!operand.isLValue || !IsNumeric(operand.type))
            Report("operator '" + expr->op + "' requires an assignable numeric operand");
        Save(expr, {operand.symbol, operand.type, false});
    }

    void Analyzer::Visit(TemplateExpr* expr) {
        const bool typeContext = typeContextDepth > 0;
        std::string base;
        if (typeContext) base = ResolveType(expr->base.get());
        else {
            const Result baseResult = Evaluate(expr->base.get());
            base = baseResult.type;
        }
        for (const auto& type : expr->types) ResolveType(type.get());
        Save(expr, {InvalidSymbolId, base, false});
    }

    void Analyzer::Visit(SingleStatement* stmt) { AcceptIfPresent(stmt->expr, *this); }

    void Analyzer::Visit(CompoundStmt* stmt) {
        if (phase == Phase::CollectDeclarations && currentType.empty()) return;
        table.EnterScope();
        AcceptAll(stmt->statements, *this);
        table.ExitScope();
    }

    void Analyzer::Visit(FunctionCallStmt* stmt) {
        if (phase == Phase::ResolveBodies) AcceptIfPresent(stmt->value, *this);
    }

    void Analyzer::Visit(FunctionDeclStmt* stmt) {
        if (phase == Phase::CollectDeclarations) {
            if (currentType.empty()) DeclareGlobalFunction(*stmt);
            else if (stmt->name && stmt->returnType)
                DeclareMember(currentType, stmt->name->value,
                    {SymbolKind::Method, stmt->returnType->value, ResolveParameterTypes(stmt->parameters)});
        }
        else ResolveFunction(*stmt, currentType.empty() ? SymbolKind::Function : SymbolKind::Method);
    }

    void Analyzer::Visit(ReturnStmt* stmt) {
        if (phase == Phase::CollectDeclarations) return;
        if (functionDepth == 0) Report("return statement is outside a function");
        const Result value = Evaluate(stmt->expr.get());
        if (!IsAssignable(currentReturnType, value.type))
            Report("return type '" + value.type + "' does not match '" + currentReturnType + "'");
    }

    void Analyzer::Visit(AssignmentStmt* stmt) { if (phase == Phase::ResolveBodies) AcceptIfPresent(stmt->expr, *this); }
    void Analyzer::Visit(VarDeclStmt* stmt) { AcceptIfPresent(stmt->expr, *this); }

    void Analyzer::Visit(StructDeclStmt* stmt) {
        const std::string typeName = Qualify(stmt->name);
        if (phase == Phase::CollectDeclarations) {
            DeclareType(stmt->name);
            const std::string old = currentType;
            currentType = typeName;
            AcceptAll(stmt->members, *this);
            currentType = old;
            return;
        }
        const std::string old = currentType;
        currentType = typeName;
        table.EnterScope();
        for (const auto& [name, member] : types[typeName].members)
            table.Declare(member.kind, name, member.type, member.parameterTypes);
        AcceptAll(stmt->members, *this);
        table.ExitScope();
        currentType = old;
    }

    void Analyzer::Visit(ClassDeclStmt* stmt) {
        const std::string typeName = Qualify(stmt->name);
        if (phase == Phase::CollectDeclarations) {
            DeclareType(stmt->name);
            const std::string old = currentType;
            currentType = typeName;
            AcceptIfPresent(stmt->body, *this);
            currentType = old;
            return;
        }
        for (const std::string& parent : stmt->parents)
            if (!types.contains(ResolveTypeReference(parent)))
                Report("unknown parent type '" + parent + "' of class '" + typeName + "'");
        const std::string old = currentType;
        currentType = typeName;
        table.EnterScope();
        for (const auto& [name, member] : types[typeName].members)
            table.Declare(member.kind, name, member.type, member.parameterTypes);
        AcceptIfPresent(stmt->body, *this);
        table.ExitScope();
        currentType = old;
    }

    void Analyzer::Visit(ConstructorDeclStmt* stmt) {
        if (currentType.empty()) {
            Report("constructor declaration is outside a type");
            return;
        }
        if (phase == Phase::CollectDeclarations) {
            if (types[currentType].constructor) Report("constructor of '" + currentType + "' is already declared");
            else types[currentType].constructor = MemberSignature{SymbolKind::Constructor, currentType,
                ResolveParameterTypes(stmt->parameters)};
            return;
        }
        if (stmt->name && Qualify(stmt->name->value) != currentType)
            Report("constructor '" + stmt->name->value + "' must match type '" + currentType + "'");
        ++functionDepth;
        table.EnterScope();
        for (const auto& parameter : stmt->parameters) {
            const std::string name = parameter ? ExtractIdentifier(parameter->name.get()) : std::string{};
            const std::string type = parameter ? ResolveType(parameter->type.get()) : "error";
            if (!table.Declare(SymbolKind::Parameter, name, type)) Report("parameter '" + name + "' is already declared");
        }
        AcceptIfPresent(stmt->body, *this);
        table.ExitScope();
        --functionDepth;
    }

    void Analyzer::Visit(EnumDeclStmt* stmt) {
        if (phase == Phase::CollectDeclarations) DeclareType(stmt->name);
    }

    void Analyzer::Visit(GroupDeclStmt* stmt) {
        if (phase == Phase::CollectDeclarations) DeclareType(stmt->name);
        AcceptAll(stmt->enums, *this);
        AcceptAll(stmt->subgroups, *this);
    }

    void Analyzer::Visit(IfStmt* stmt) {
        if (phase == Phase::CollectDeclarations) return;
        for (auto& branch : stmt->branches) {
            const Result condition = Evaluate(branch.condition.get());
            if (!IsConditionType(condition.type)) Report("if condition must be boolean-compatible");
            AcceptIfPresent(branch.body, *this);
        }
        AcceptIfPresent(stmt->elseBranch, *this);
    }

    void Analyzer::Visit(ForStmt* stmt) {
        if (phase == Phase::CollectDeclarations) return;
        table.EnterScope();
        AcceptAll(stmt->init, *this);
        if (stmt->condition) {
            const Result condition = Evaluate(stmt->condition.get());
            if (!IsConditionType(condition.type)) Report("for condition must be boolean-compatible");
        }
        ++loopDepth;
        AcceptIfPresent(stmt->body, *this);
        AcceptAll(stmt->update, *this);
        --loopDepth;
        table.ExitScope();
    }

    void Analyzer::Visit(WhileStmt* stmt) {
        if (phase == Phase::CollectDeclarations) return;
        const Result condition = Evaluate(stmt->condition.get());
        if (!IsConditionType(condition.type)) Report("while condition must be boolean-compatible");
        ++loopDepth;
        AcceptIfPresent(stmt->body, *this);
        --loopDepth;
    }

    void Analyzer::Visit(DoWhileStmt* stmt) {
        if (phase == Phase::CollectDeclarations) return;
        ++loopDepth;
        AcceptIfPresent(stmt->body, *this);
        --loopDepth;
        const Result condition = Evaluate(stmt->condition.get());
        if (!IsConditionType(condition.type)) Report("do-while condition must be boolean-compatible");
    }

    void Analyzer::Visit(ForEachStmt* stmt) {
        if (phase == Phase::CollectDeclarations) return;
        table.EnterScope();
        const Result iterable = Evaluate(stmt->iterable.get());
        if (!iterable.type.ends_with("[]") && iterable.type != "error") Report("for-each source must be an array");
        AcceptIfPresent(stmt->var, *this);
        ++loopDepth;
        AcceptIfPresent(stmt->body, *this);
        --loopDepth;
        table.ExitScope();
    }

    void Analyzer::Visit(ContinueStmt* stmt) {
        (void)stmt;
        if (phase == Phase::ResolveBodies && loopDepth == 0) Report("continue statement is outside a loop");
    }

    void Analyzer::Visit(BreakStmt* stmt) {
        (void)stmt;
        if (phase == Phase::ResolveBodies && loopDepth == 0) Report("break statement is outside a loop");
    }

    void Analyzer::Visit(ImportStmt* stmt) {
        if (stmt->isFile) return;
        importedNamespaces.insert(stmt->target);
        if (phase == Phase::ResolveBodies && !namespaces.contains(stmt->target))
            Report("unknown imported namespace '" + stmt->target + "'");
    }

    void Analyzer::Visit(NamespaceDeclStmt* stmt) {
        const std::string oldNamespace = currentNamespace;
        const std::string namespaceName = Qualify(stmt->name);
        if (phase == Phase::CollectDeclarations && namespaces.insert(namespaceName).second) {
            if (!table.Declare(SymbolKind::Namespace, namespaceName, namespaceName))
                Report("object '" + namespaceName + "' is already declared in this scope");
        }
        currentNamespace = namespaceName;
        if (stmt->body) AcceptAll(stmt->body->statements, *this);
        currentNamespace = oldNamespace;
    }
}
