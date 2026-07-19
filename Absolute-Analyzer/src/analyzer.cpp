#include "analyzer_pch.h"
#include "analyzer.h"
#include "syntax_plugins.h"

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
                type.starts_with("int") || type.starts_with("uint") || type.ends_with("*");
        }

        bool IsRawPointerType(const std::string& type) {
            return type.starts_with("raw ") && type.ends_with("*");
        }

        bool IsManagedPointerType(const std::string& type) {
            return !IsRawPointerType(type) && type.ends_with("*");
        }

        bool IsPointerType(const std::string& type) {
            return IsRawPointerType(type) || IsManagedPointerType(type);
        }

        std::string PointerPointee(std::string type) {
            if (IsRawPointerType(type)) type.erase(0, 4);
            if (!type.empty() && type.back() == '*') type.pop_back();
            return type;
        }

        bool IsTaskType(const std::string& type) {
            return type.size() > 6 && type.starts_with("task<") && type.ends_with(">");
        }

        size_t ArrayRank(std::string type) {
            size_t rank = 0;
            while (type.ends_with("[]")) {
                type.resize(type.size() - 2);
                ++rank;
            }
            return rank;
        }

        std::string ArrayElementType(std::string type, size_t dimensions = 1) {
            while (dimensions-- > 0 && type.ends_with("[]")) type.resize(type.size() - 2);
            return type;
        }

        std::string ArrayType(std::string elementType, size_t rank) {
            while (rank-- > 0) elementType += "[]";
            return elementType;
        }

        std::optional<std::vector<size_t>> InferArrayShape(const ArrayExpr& array) {
            std::vector<size_t> childShape;
            bool hasChildShape = false;
            for (const auto& value : array.values) {
                std::vector<size_t> currentShape;
                if (const auto* nested = dynamic_cast<const ArrayExpr*>(value.get())) {
                    const auto nestedShape = InferArrayShape(*nested);
                    if (!nestedShape) return std::nullopt;
                    currentShape = *nestedShape;
                }
                if (!hasChildShape) {
                    childShape = std::move(currentShape);
                    hasChildShape = true;
                }
                else if (childShape != currentShape) return std::nullopt;
            }
            childShape.insert(childShape.begin(), array.values.size());
            return childShape;
        }

        std::string TaskValueType(const std::string& type) {
            return IsTaskType(type) ? type.substr(5, type.size() - 6) : "error";
        }

        bool HasModifier(const Statement& statement, const std::string& name) {
            return std::any_of(statement.modifiers.begin(), statement.modifiers.end(),
                [&](const Token& modifier) { return modifier.value == name; });
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
        expectedType.clear();
        currentNamespace.clear();
        keepLifetimes.clear();
        keepScopes.clear();
        loopKeepDepths.clear();
        loopBreakStates.clear();
        valueFlow.clear();
        valueFlowScopes.clear();
        loopBreakValueStates.clear();
        accessMode = AccessMode::Read;
        flowTerminated = false;
        spawnContextDepth = 0;
        currentFunctionAsync = false;
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

    void Analyzer::Report(std::string message, std::string code, SymbolId symbol) {
        diagnostics.push_back({std::move(message), std::move(code), symbol});
    }

    void Analyzer::PushKeepScope() { keepScopes.emplace_back(); }

    void Analyzer::CheckKeepScopesFrom(size_t firstScope, const std::string& exitKind) {
        for (size_t scope = firstScope; scope < keepScopes.size(); ++scope) {
            for (SymbolId id : keepScopes[scope]) {
                const auto found = keepLifetimes.find(id);
                if (found == keepLifetimes.end() || found->second.state == KeepState::Deleted) continue;
                const std::string qualifier = found->second.state == KeepState::MaybeDeleted
                    ? " is not deleted on every control-flow path"
                    : " must be explicitly deleted";
                Report("raw owner '" + found->second.name + "'" + qualifier + " before " + exitKind,
                    "E_RAW_DELETE_REQUIRED", id);
            }
        }
    }

    void Analyzer::PopKeepScope() {
        if (keepScopes.empty()) return;
        if (!flowTerminated) CheckKeepScopesFrom(keepScopes.size() - 1, "leaving its scope");
        for (SymbolId id : keepScopes.back()) keepLifetimes.erase(id);
        keepScopes.pop_back();
    }

    void Analyzer::MergeKeepPaths(const KeepLifetimeMap& base, const std::vector<KeepLifetimeMap>& paths) {
        keepLifetimes = base;
        if (paths.empty()) return;
        for (auto& [id, lifetime] : keepLifetimes) {
            KeepState merged = paths.front().contains(id) ? paths.front().at(id).state : lifetime.state;
            for (size_t index = 1; index < paths.size(); ++index) {
                const KeepState state = paths[index].contains(id) ? paths[index].at(id).state : lifetime.state;
                if (state != merged) merged = KeepState::MaybeDeleted;
            }
            lifetime.state = merged;
        }
    }

    void Analyzer::PushValueFlowScope() { valueFlowScopes.emplace_back(); }

    void Analyzer::PopValueFlowScope() {
        if (valueFlowScopes.empty()) return;
        if (!flowTerminated) CheckTaskScopesFrom(valueFlowScopes.size() - 1, "leaving its scope");
        for (SymbolId id : valueFlowScopes.back()) valueFlow.erase(id);
        valueFlowScopes.pop_back();
    }

    void Analyzer::CheckTaskScopesFrom(size_t firstScope, const std::string& exitKind) {
        for (size_t scope = firstScope; scope < valueFlowScopes.size(); ++scope) {
            for (SymbolId id : valueFlowScopes[scope]) {
                const auto found = valueFlow.find(id);
                if (found == valueFlow.end()) continue;
                const Symbol* symbol = table.Get(id);
                const std::string name = symbol ? symbol->name : std::string("<unknown>");
                if (found->second.taskState == TaskState::Pending)
                    Report("task '" + name + "' must be awaited before " + exitKind,
                        "E_TASK_NOT_AWAITED", id);
                else if (found->second.taskState == TaskState::MaybePending)
                    Report("task '" + name +
                        "' is not awaited on every control-flow path before " + exitKind,
                        "E_TASK_MAY_NOT_BE_AWAITED", id);
            }
        }
    }

    void Analyzer::RegisterFlowSymbol(SymbolId id, ValueFlowState state) {
        if (id == InvalidSymbolId) return;
        if (valueFlowScopes.empty()) PushValueFlowScope();
        valueFlow[id] = state;
        valueFlowScopes.back().push_back(id);
    }

    void Analyzer::MergeValueFlowPaths(const ValueFlowMap& base, const std::vector<ValueFlowMap>& paths) {
        valueFlow = base;
        if (paths.empty()) return;
        for (auto& [id, merged] : valueFlow) {
            const auto stateAt = [&](const ValueFlowMap& path) -> ValueFlowState {
                const auto found = path.find(id);
                return found == path.end() ? merged : found->second;
            };
            ValueFlowState state = stateAt(paths.front());
            for (size_t index = 1; index < paths.size(); ++index) {
                const ValueFlowState next = stateAt(paths[index]);
                if (state.initialization != next.initialization)
                    state.initialization = InitializationState::MaybeUninitialized;

                const PointerValidity previousPointer = state.pointerValidity;
                if (previousPointer != next.pointerValidity) {
                    const bool stateInvalid = state.pointerValidity == PointerValidity::Null ||
                        state.pointerValidity == PointerValidity::Deleted ||
                        state.pointerValidity == PointerValidity::Expired ||
                        state.pointerValidity == PointerValidity::MaybeInvalid;
                    const bool nextInvalid = next.pointerValidity == PointerValidity::Null ||
                        next.pointerValidity == PointerValidity::Deleted ||
                        next.pointerValidity == PointerValidity::Expired ||
                        next.pointerValidity == PointerValidity::MaybeInvalid;
                    const bool dangerousState = state.pointerValidity == PointerValidity::Deleted ||
                        state.pointerValidity == PointerValidity::Expired ||
                        state.pointerValidity == PointerValidity::MaybeInvalid;
                    const bool dangerousNext = next.pointerValidity == PointerValidity::Deleted ||
                        next.pointerValidity == PointerValidity::Expired ||
                        next.pointerValidity == PointerValidity::MaybeInvalid;
                    const bool nullablePair =
                        (state.pointerValidity == PointerValidity::Null ||
                            state.pointerValidity == PointerValidity::Live ||
                            state.pointerValidity == PointerValidity::MaybeNull) &&
                        (next.pointerValidity == PointerValidity::Null ||
                            next.pointerValidity == PointerValidity::Live ||
                            next.pointerValidity == PointerValidity::MaybeNull);
                    if (dangerousState || dangerousNext) state.pointerValidity = PointerValidity::MaybeInvalid;
                    else if (nullablePair) state.pointerValidity = PointerValidity::MaybeNull;
                    else state.pointerValidity = (stateInvalid || nextInvalid)
                        ? PointerValidity::MaybeInvalid : PointerValidity::Unknown;
                }
                if (state.pointerOwner != next.pointerOwner) {
                    const bool previousWasNull = previousPointer == PointerValidity::Null ||
                        previousPointer == PointerValidity::MaybeNull;
                    const bool nextIsNull = next.pointerValidity == PointerValidity::Null ||
                        next.pointerValidity == PointerValidity::MaybeNull;
                    if (previousWasNull && state.pointerOwner == InvalidSymbolId)
                        state.pointerOwner = next.pointerOwner;
                    else if (!nextIsNull) state.pointerOwner = InvalidSymbolId;
                }
                if (state.taskState != next.taskState) {
                    const bool pending = state.taskState == TaskState::Pending ||
                        state.taskState == TaskState::MaybePending ||
                        next.taskState == TaskState::Pending ||
                        next.taskState == TaskState::MaybePending;
                    state.taskState = pending ? TaskState::MaybePending : TaskState::Unknown;
                }
            }
            merged = state;
        }
    }

    Analyzer::Result Analyzer::Evaluate(Expression* expression) {
        callable = false;
        callableParameters.clear();
        result = {};
        if (expression) expression->Accept(*this);
        else result.type = "void";
        return result;
    }

    Analyzer::Result Analyzer::EvaluateExpected(Expression* expression, const std::string& type) {
        const std::string previous = expectedType;
        expectedType = type;
        const Result resolved = Evaluate(expression);
        expectedType = previous;
        return resolved;
    }

    std::string Analyzer::ResolveType(Expression* expression) {
        ++typeContextDepth;
        const Result resolved = Evaluate(expression);
        --typeContextDepth;
        return resolved.type.empty() ? "error" : resolved.type;
    }

    std::string Analyzer::ResolveDeclaredType(VarDeclExpr& expression) {
        std::string type = ResolveType(expression.type.get());
        if (const auto* declarator = dynamic_cast<const ArrayAccessExpr*>(expression.name.get());
            declarator && !declarator->indexes.empty()) {
            type = ArrayType(std::move(type), declarator->indexes.size());
        }
        return type;
    }

    void Analyzer::Save(Expression* expression, Result value) {
        result = std::move(value);
        if (expression) expressionInfo[expression] = {
            result.symbol, result.type, result.isLValue,
            result.createsManagedOwner, result.referencesManagedOwner,
            result.initialization, result.pointerValidity, result.pointerOwner,
            result.taskState, result.createsTask, result.asyncCall,
            result.createsRawOwner};
    }

    bool Analyzer::IsKnownType(const std::string& name) const {
        if (IsPointerType(name)) return IsKnownType(PointerPointee(name));
        if (IsTaskType(name)) return IsKnownType(TaskValueType(name));
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
        if (target.ends_with("[]") && source.ends_with("[]"))
            return IsAssignable(ArrayElementType(target), ArrayElementType(source));
        if (IsPointerType(target) && source == "null") return true;
        if (IsPointerType(target) && IsPointerType(source) &&
            IsRawPointerType(target) == IsRawPointerType(source))
            return IsDerivedFrom(PointerPointee(source), PointerPointee(target));
        if (IsNumeric(target) && IsNumeric(source)) return true;
        return source == "null" && !PrimitiveStringToEnum(target).has_value();
    }

    bool Analyzer::IsDerivedFrom(const std::string& type, const std::string& base) const {
        if (type == base) return true;
        const auto found = types.find(type);
        if (found == types.end()) return false;
        for (const std::string& parent : found->second.parents)
            if (parent == base || IsDerivedFrom(parent, base)) return true;
        return false;
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
            resolved.push_back(parameter ? ResolveDeclaredType(*parameter) : "error");
        return resolved;
    }

    void Analyzer::DeclareGlobalFunction(FunctionDeclStmt& statement) {
        if (!statement.name || !statement.returnType) return;
        const std::string name = Qualify(statement.name->value);
        const std::string returnType = ResolveType(statement.returnType.get());
        const auto declared = table.Declare(SymbolKind::Function, name,
            returnType, ResolveParameterTypes(statement.parameters));
        if (!declared) Report("object '" + name + "' is already declared in this scope");
        else if (Symbol* symbol = table.Get(*declared))
            symbol->asyncFunction = HasModifier(statement, "async");
    }

    void Analyzer::ResolveFunction(FunctionDeclStmt& statement, SymbolKind kind) {
        if (!statement.name || !statement.returnType) return;
        const std::string returnType = ResolveType(statement.returnType.get());
        if (!IsKnownType(returnType))
            Report("unknown return type '" + returnType + "' of function '" + statement.name->value + "'");

        if (statement.IsExternal() && kind == SymbolKind::Method)
            Report("extern functions cannot be class or struct members");
        if (statement.IsExternal() && HasModifier(statement, "async"))
            Report("extern functions cannot be async", "E_ASYNC_EXTERN");
        if (kind == SymbolKind::Method && HasModifier(statement, "async"))
            Report("async methods are not implemented yet; use a namespace function",
                "E_ASYNC_METHOD_UNSUPPORTED");
        if (statement.IsExternal() && (returnType == "auto" || returnType == "dynamic" ||
            IsManagedPointerType(returnType)))
            Report("extern function '" + statement.name->value +
                "' requires a concrete C-compatible return type; use raw T* for pointers");
        if (statement.IsExternal() && returnType.ends_with("[]"))
            Report("extern function '" + statement.name->value +
                "' cannot return an Absolute array descriptor");

        const std::string oldReturn = currentReturnType;
        const bool oldAsync = currentFunctionAsync;
        currentReturnType = returnType;
        currentFunctionAsync = HasModifier(statement, "async");
        ++functionDepth;
        table.EnterScope();
        keepLifetimes.clear();
        keepScopes.clear();
        loopKeepDepths.clear();
        loopBreakStates.clear();
        valueFlow.clear();
        valueFlowScopes.clear();
        loopBreakValueStates.clear();
        accessMode = AccessMode::Read;
        flowTerminated = false;
        PushKeepScope();
        PushValueFlowScope();
        for (const auto& parameter : statement.parameters) {
            if (!parameter) continue;
            const std::string name = ExtractIdentifier(parameter->name.get());
            std::string type = ResolveDeclaredType(*parameter);
            if (statement.IsExternal() && (type == "auto" || type == "dynamic" || type == "void"))
                Report("extern parameter '" + name + "' requires a concrete C-compatible type");
            if (statement.IsExternal() && IsManagedPointerType(type))
                Report("extern parameter '" + name + "' must use raw T* instead of a managed pointer");
            if (statement.IsExternal() && type.ends_with("[]"))
                Report("extern parameter '" + name + "' cannot use an Absolute array descriptor");
            SymbolId parameterId = InvalidSymbolId;
            if (name.empty()) Report("function parameter requires an identifier");
            else if (const auto declared = table.Declare(SymbolKind::Parameter, name, type)) {
                parameterId = *declared;
                if (Symbol* symbol = table.Get(parameterId); symbol && IsManagedPointerType(type))
                    symbol->managedBorrower = true;
            }
            else Report("parameter '" + name + "' is already declared");
        RegisterFlowSymbol(parameterId, {
                InitializationState::Initialized,
                IsPointerType(type) ? PointerValidity::Unknown : PointerValidity::NotPointer,
                InvalidSymbolId,
                IsTaskType(type) ? TaskState::Unknown : TaskState::NotTask});
            if (parameter->value) {
                const Result value = Evaluate(parameter->value.get());
                if (!IsAssignable(type, value.type))
                    Report("default value of parameter '" + name + "' has type '" + value.type + "', expected '" + type + "'");
            }
        }
        if (!statement.IsExternal()) AcceptIfPresent(statement.body, *this);
        if (!statement.IsExternal() && returnType != "void" && !flowTerminated)
            Report("function '" + statement.name->value + "' does not return a value on every control-flow path",
                "E_MISSING_RETURN", table.Lookup(Qualify(statement.name->value)));
        PopValueFlowScope();
        PopKeepScope();
        keepLifetimes.clear();
        valueFlow.clear();
        table.ExitScope();
        --functionDepth;
        currentReturnType = oldReturn;
        currentFunctionAsync = oldAsync;
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
        const std::string objectType = IsPointerType(owner) ? PointerPointee(owner) : owner;
        const auto type = types.find(objectType);
        if (type == types.end()) return nullptr;
        const auto member = type->second.members.find(name);
        if (member != type->second.members.end()) return &member->second;
        for (const std::string& parent : type->second.parents)
            if (const MemberSignature* inherited = FindMember(parent, name)) return inherited;
        return nullptr;
    }

    std::unordered_map<std::string, Analyzer::MemberSignature> Analyzer::VisibleMembers(
        const std::string& owner) const {
        std::unordered_map<std::string, MemberSignature> result;
        const auto found = types.find(owner);
        if (found == types.end()) return result;
        for (const std::string& parent : found->second.parents) {
            auto inherited = VisibleMembers(parent);
            result.insert(inherited.begin(), inherited.end());
        }
        for (const auto& [name, member] : found->second.members) result[name] = member;
        return result;
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
        if (name.ends_with("[]"))
            return ResolveTypeReference(name.substr(0, name.size() - 2)) + "[]";
        if (IsPointerType(name)) {
            const std::string prefix = IsRawPointerType(name) ? "raw " : "";
            return prefix + ResolveTypeReference(PointerPointee(name)) + "*";
        }
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

    void Analyzer::Visit(PointerTypeExpr* expr) {
        const std::string pointee = ResolveType(expr->pointee.get());
        if (pointee == "void" && !expr->raw) Report("managed pointers cannot point to void");
        Save(expr, {InvalidSymbolId, (expr->raw ? "raw " : "") + pointee + "*", false});
    }

    void Analyzer::Visit(ArrayTypeExpr* expr) {
        const std::string element = ResolveType(expr->element.get());
        if (element == "void") Report("array element type cannot be void");
        Save(expr, {InvalidSymbolId, element + "[]", false});
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
        ValueFlowState flow;
        if (const auto found = valueFlow.find(id); found != valueFlow.end()) flow = found->second;
        if (value && accessMode == AccessMode::Read) {
            if (flow.initialization == InitializationState::Uninitialized)
                Report("object '" + expr->name + "' is read before initialization",
                    "E_UNINITIALIZED_READ", id);
            else if (flow.initialization == InitializationState::MaybeUninitialized)
                Report("object '" + expr->name + "' is not initialized on every control-flow path",
                    "E_MAYBE_UNINITIALIZED_READ", id);
        }
        Save(expr, {id, symbol->type, value, false,
            value && IsManagedPointerType(symbol->type) && symbol->managedOwner,
            flow.initialization, flow.pointerValidity, flow.pointerOwner,
            flow.taskState});
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
                const Result argument = EvaluateExpected(expr->arguments[i].get(),
                    i < expected.size() ? expected[i] : std::string{});
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
        bool asyncCall = false;

        const SymbolId qualifiedId = LookupSymbol(qualifiedCallName);
        const Symbol* qualifiedSymbol = table.Get(qualifiedId);
        if (qualifiedSymbol && (qualifiedSymbol->kind == SymbolKind::Function ||
            qualifiedSymbol->kind == SymbolKind::Method)) {
            targetCallable = true;
            parameters = qualifiedSymbol->parameterTypes;
            symbolId = qualifiedId;
            returnType = qualifiedSymbol->type;
            asyncCall = qualifiedSymbol->asyncFunction;
        }
        else if (probe.isMember) {
            const Result target = Evaluate(expr->base.get());
            targetCallable = callable;
            parameters = callableParameters;
            symbolId = target.symbol;
            returnType = target.type;
            if (const Symbol* symbol = table.Get(symbolId)) asyncCall = symbol->asyncFunction;
        }
        else {
            const std::string name = callName;
            symbolId = LookupSymbol(name);
            const Symbol* symbol = table.Get(symbolId);
            if (symbol && (symbol->kind == SymbolKind::Function || symbol->kind == SymbolKind::Method)) {
                targetCallable = true;
                parameters = symbol->parameterTypes;
                returnType = symbol->type;
                asyncCall = symbol->asyncFunction;
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
            const Result argument = EvaluateExpected(expr->arguments[i].get(),
                i < parameters.size() ? parameters[i] : std::string{});
            if (i < parameters.size() && !IsAssignable(parameters[i], argument.type))
                Report("argument " + std::to_string(i + 1) + " has type '" + argument.type +
                    "', expected '" + parameters[i] + "'");
            if (argument.pointerValidity == PointerValidity::Deleted ||
                argument.pointerValidity == PointerValidity::Expired)
                Report("argument " + std::to_string(i + 1) + " passes an invalid pointer",
                    "E_INVALID_POINTER_ARGUMENT", argument.symbol);
            else if (argument.pointerValidity == PointerValidity::MaybeInvalid)
                Report("argument " + std::to_string(i + 1) + " may pass an invalid pointer",
                    "E_MAYBE_INVALID_POINTER_ARGUMENT", argument.symbol);
        }
        if (asyncCall && spawnContextDepth == 0)
            Report("async function must be started with spawn", "E_ASYNC_CALL_REQUIRES_SPAWN", symbolId);
        Save(expr, {symbolId, returnType, false, IsManagedPointerType(returnType), false,
            InitializationState::Initialized,
            IsPointerType(returnType) ? PointerValidity::Unknown : PointerValidity::NotPointer,
            InvalidSymbolId, TaskState::NotTask, false, asyncCall});
    }

    void Analyzer::Visit(ArrayAccessExpr* expr) {
        Result base = Evaluate(expr->base.get());
        const size_t rank = ArrayRank(base.type);
        const bool fullSlice = rank == 1 && expr->indexes.size() == 1 && !expr->indexes.front();
        if (fullSlice) {
            Save(expr, {base.symbol, base.type, false});
            return;
        }
        for (const auto& index : expr->indexes) {
            if (!index) {
                Report("array access requires an index");
                continue;
            }
            const Result indexResult = Evaluate(index.get());
            if (!IsInteger(indexResult.type) && indexResult.type != "error")
                Report("array index must be an integer, got '" + indexResult.type + "'");
        }
        if (rank == 0) {
            Report("object of type '" + base.type + "' is not an array");
            Save(expr, {base.symbol, "error", false});
        }
        else if (expr->indexes.size() != rank) {
            Report("array access provides " + std::to_string(expr->indexes.size()) +
                " index(es), but the array has " + std::to_string(rank) + " dimension(s)");
            Save(expr, {base.symbol, "error", false});
        }
        else Save(expr, {base.symbol, ArrayElementType(base.type, rank), base.isLValue});
    }

    void Analyzer::Visit(SliceExpr* expr) {
        const Result base = Evaluate(expr->base.get());
        if (ArrayRank(base.type) != 1 && base.type != "error")
            Report("slices are supported only for one-dimensional arrays");
        for (Expression* bound : {expr->begin.get(), expr->end.get()}) {
            if (!bound) continue;
            const Result resolved = Evaluate(bound);
            if (!IsInteger(resolved.type) && resolved.type != "error")
                Report("slice bounds must be integers");
        }
        Save(expr, {base.symbol, ArrayRank(base.type) == 1 ? base.type : "error", false});
    }

    void Analyzer::Visit(BinaryExpr* expr) {
        const Result left = Evaluate(expr->left.get());
        const Result right = Evaluate(expr->right.get());
        const std::string& op = expr->op;
        if (const PluginBinaryOperator* pluginOperator =
                FindPluginBinaryOperator(left.type, op, right.type)) {
            const SymbolId functionId = LookupSymbol(pluginOperator->functionName);
            const Symbol* function = table.Get(functionId);
            const std::vector<std::string> expectedParameters{left.type, right.type};
            if (!function || function->kind != SymbolKind::Function ||
                function->parameterTypes != expectedParameters || function->type != pluginOperator->resultType) {
                Report("plugin operator '" + left.type + " " + op + " " + right.type +
                    "' requires function '" + pluginOperator->functionName + "(" + left.type + ", " +
                    right.type + ") -> " + pluginOperator->resultType + "'", "E_PLUGIN_OPERATOR_SIGNATURE");
                Save(expr, {InvalidSymbolId, "error", false});
                return;
            }
            const bool managedResult = IsManagedPointerType(pluginOperator->resultType);
            Save(expr, {functionId, pluginOperator->resultType, false, managedResult, false,
                InitializationState::Initialized,
                managedResult ? PointerValidity::Live : PointerValidity::NotPointer});
            return;
        }
        const bool leftPointer = IsPointerType(left.type);
        const bool rightPointer = IsPointerType(right.type);
        if ((leftPointer || rightPointer) && (op == "+" || op == "-")) {
            if (IsManagedPointerType(left.type) || IsManagedPointerType(right.type)) {
                Report("managed pointers do not support arithmetic; use raw T* when address arithmetic is required");
                Save(expr, {InvalidSymbolId, "error", false});
            }
            else if (op == "+" && IsRawPointerType(left.type) && IsInteger(right.type))
                Save(expr, {InvalidSymbolId, left.type, false, false, false,
                    left.initialization, left.pointerValidity, left.pointerOwner});
            else if (op == "+" && IsInteger(left.type) && IsRawPointerType(right.type))
                Save(expr, {InvalidSymbolId, right.type, false, false, false,
                    right.initialization, right.pointerValidity, right.pointerOwner});
            else if (op == "-" && IsRawPointerType(left.type) && IsInteger(right.type))
                Save(expr, {InvalidSymbolId, left.type, false, false, false,
                    left.initialization, left.pointerValidity, left.pointerOwner});
            else if (op == "-" && IsRawPointerType(left.type) && left.type == right.type)
                Save(expr, {InvalidSymbolId, "int64", false});
            else {
                Report("invalid pointer arithmetic between '" + left.type + "' and '" + right.type + "'");
                Save(expr, {InvalidSymbolId, "error", false});
            }
        }
        else if (op == "&&" || op == "||") {
            if (!IsConditionType(left.type) || !IsConditionType(right.type)) Report("logical operands must be boolean-compatible");
            Save(expr, {InvalidSymbolId, "bool", false});
        }
        else if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
            if (op != "==" && op != "!=" &&
                (IsManagedPointerType(left.type) || IsManagedPointerType(right.type)))
                Report("managed pointers only support equality and null comparisons");
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
        const ValueFlowMap baseValues = valueFlow;
        const Result trueResult = Evaluate(expr->trueExpr.get());
        const ValueFlowMap trueValues = valueFlow;
        valueFlow = baseValues;
        const Result falseResult = Evaluate(expr->falseExpr.get());
        const ValueFlowMap falseValues = valueFlow;
        MergeValueFlowPaths(baseValues, {trueValues, falseValues});
        std::string type = CommonType(trueResult.type, falseResult.type);
        if (type == "error" && IsPointerType(trueResult.type) && falseResult.type == "null") type = trueResult.type;
        if (type == "error" && IsPointerType(falseResult.type) && trueResult.type == "null") type = falseResult.type;
        if (type == "error" && trueResult.type != "error" && falseResult.type != "error")
            Report("ternary branches have incompatible types '" + trueResult.type + "' and '" + falseResult.type + "'");
        PointerValidity pointerValidity = PointerValidity::NotPointer;
        if (IsPointerType(type)) {
            if (trueResult.pointerValidity == falseResult.pointerValidity)
                pointerValidity = trueResult.pointerValidity;
            else if ((trueResult.pointerValidity == PointerValidity::Null &&
                falseResult.pointerValidity == PointerValidity::Live) ||
                (falseResult.pointerValidity == PointerValidity::Null &&
                    trueResult.pointerValidity == PointerValidity::Live))
                pointerValidity = PointerValidity::MaybeNull;
            else pointerValidity = PointerValidity::MaybeInvalid;
        }
        SymbolId pointerOwner = trueResult.pointerOwner == falseResult.pointerOwner
            ? trueResult.pointerOwner : InvalidSymbolId;
        if (trueResult.pointerValidity == PointerValidity::Null) pointerOwner = falseResult.pointerOwner;
        if (falseResult.pointerValidity == PointerValidity::Null) pointerOwner = trueResult.pointerOwner;
        Result combined{InvalidSymbolId, type, false,
            trueResult.createsManagedOwner && falseResult.createsManagedOwner,
            trueResult.referencesManagedOwner && falseResult.referencesManagedOwner,
            InitializationState::Initialized, pointerValidity, pointerOwner};
        combined.createsRawOwner = trueResult.createsRawOwner && falseResult.createsRawOwner;
        Save(expr, std::move(combined));
    }

    void Analyzer::Visit(NullExpr* expr) {
        Save(expr, {InvalidSymbolId, "null", false, false, false,
            InitializationState::Initialized, PointerValidity::Null, InvalidSymbolId});
    }
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
        const bool hasExpectedArrayType = expectedType.ends_with("[]");
        const std::string expectedElementType = hasExpectedArrayType
            ? ArrayElementType(expectedType) : std::string{};
        std::string elementType;
        for (const auto& value : expr->values) {
            const Result resolved = hasExpectedArrayType
                ? EvaluateExpected(value.get(), expectedElementType)
                : Evaluate(value.get());
            if (hasExpectedArrayType && !IsAssignable(expectedElementType, resolved.type))
                Report("array element has type '" + resolved.type + "', expected '" + expectedElementType + "'");
            const std::string common = elementType.empty()
                ? resolved.type : CommonType(elementType, resolved.type);
            if (!elementType.empty() && common == "error" &&
                elementType != "error" && resolved.type != "error")
                Report("array literal mixes incompatible element types '" + elementType +
                    "' and '" + resolved.type + "'");
            elementType = common;
        }
        if (hasExpectedArrayType) Save(expr, {InvalidSymbolId, expectedType, false});
        else {
            if (elementType.empty()) elementType = "dynamic";
            Save(expr, {InvalidSymbolId, elementType + "[]", false});
        }
    }

    void Analyzer::Visit(AssignmentExpr* expr) {
        const AccessMode previousAccess = accessMode;
        accessMode = expr->op == "=" ? AccessMode::Write : AccessMode::Read;
        const Result target = Evaluate(expr->target.get());
        accessMode = previousAccess;
        const Result value = EvaluateExpected(expr->value.get(), target.type);
        if (!target.isLValue) Report("assignment target is not assignable");
        if (ArrayRank(target.type) > 0)
            Report("array variables cannot be reassigned; assign an element or declare a new array view");
        if (!IsAssignable(target.type, value.type))
            Report("cannot assign '" + value.type + "' to '" + target.type + "'");
        if (IsTaskType(target.type))
            Report("tasks cannot be reassigned or copied", "E_TASK_ASSIGNMENT", target.symbol);
        if (IsManagedPointerType(target.type)) {
            if (Symbol* symbol = table.Get(target.symbol)) {
                const bool assignsOwner = value.createsManagedOwner;
                const bool assignsBorrower = value.type != "null" && !assignsOwner &&
                    value.pointerOwner != InvalidSymbolId;
                if (assignsOwner && symbol->managedBorrower)
                    Report("managed borrower '" + symbol->name +
                        "' cannot become an owner; declare a separate owner variable",
                        "E_MANAGED_ROLE_CHANGE", target.symbol);
                if (assignsBorrower && symbol->managedOwner)
                    Report("managed owner '" + symbol->name +
                        "' cannot become a borrower; use a separate subscriber variable",
                        "E_MANAGED_ROLE_CHANGE", target.symbol);
                symbol->managedOwner = symbol->managedOwner || assignsOwner;
                symbol->managedBorrower = symbol->managedBorrower || assignsBorrower;
            }
        }
        if (const auto keep = keepLifetimes.find(target.symbol); keep != keepLifetimes.end()) {
            if (keep->second.state != KeepState::Deleted)
                Report("raw owner '" + keep->second.name + "' is overwritten before delete",
                    "E_RAW_OVERWRITE", target.symbol);
            keep->second.state = value.createsRawOwner ? KeepState::Live : KeepState::Deleted;
        }
        if (auto flow = valueFlow.find(target.symbol); flow != valueFlow.end()) {
            flow->second.initialization = InitializationState::Initialized;
            flow->second.pointerValidity = IsPointerType(target.type)
                ? value.pointerValidity : PointerValidity::NotPointer;
            flow->second.pointerOwner = IsManagedPointerType(target.type)
                ? (value.createsManagedOwner ? target.symbol : value.pointerOwner)
                : InvalidSymbolId;
            flow->second.taskState = IsTaskType(target.type)
                ? value.taskState : TaskState::NotTask;
        }
        Save(expr, {target.symbol, target.type, false, false, false,
            InitializationState::Initialized,
            IsPointerType(target.type) ? value.pointerValidity : PointerValidity::NotPointer,
            IsManagedPointerType(target.type)
                ? (value.createsManagedOwner ? target.symbol : value.pointerOwner)
                : InvalidSymbolId,
            IsTaskType(target.type) ? value.taskState : TaskState::NotTask});
    }

    void Analyzer::Visit(VarDeclExpr* expr) {
        const std::string name = ExtractIdentifier(expr->name.get());
        const std::string declarationName = currentType.empty() && functionDepth == 0 ? Qualify(name) : name;
        std::string type = ResolveType(expr->type.get());
        auto* arrayDeclarator = dynamic_cast<ArrayAccessExpr*>(expr->name.get());
        const size_t arrayRank = arrayDeclarator ? arrayDeclarator->indexes.size() : 0;
        if (arrayRank > 0) type = ArrayType(std::move(type), arrayRank);
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
        std::optional<std::vector<size_t>> initializerShape;
        if (arrayDeclarator) {
            if (arrayDeclarator->indexes.empty()) Report("array variable '" + name + "' requires a dimension");
            for (const auto& size : arrayDeclarator->indexes) {
                if (!size) continue;
                const Result resolved = Evaluate(size.get());
                if (!IsInteger(resolved.type) && resolved.type != "error")
                    Report("array size must be an integer, got '" + resolved.type + "'");
                if (currentType.empty() && functionDepth == 0 &&
                    !dynamic_cast<const NumberLiteralExpr*>(size.get()))
                    Report("global array dimensions must be constant integer literals");
                if (const auto* literal = dynamic_cast<const NumberLiteralExpr*>(size.get())) {
                    try {
                        if (std::stoll(literal->value) <= 0) Report("array size must be greater than zero");
                    }
                    catch (const std::exception&) {
                        Report("array size is outside the supported integer range");
                    }
                }
            }
            if (const auto* literal = dynamic_cast<const ArrayExpr*>(expr->value.get())) {
                initializerShape = InferArrayShape(*literal);
                if (!initializerShape) Report("array initializer must be rectangular");
                else if (initializerShape->size() != arrayRank)
                    Report("array initializer has " + std::to_string(initializerShape->size()) +
                        " dimension(s), expected " + std::to_string(arrayRank));
            }
            for (size_t dimension = 0; dimension < arrayDeclarator->indexes.size(); ++dimension) {
                const auto& size = arrayDeclarator->indexes[dimension];
                if (!size) {
                    if (!initializerShape || dimension >= initializerShape->size())
                        Report("array dimension " + std::to_string(dimension + 1) +
                            " requires a size or an initializer");
                    continue;
                }
                if (initializerShape && dimension < initializerShape->size()) {
                    if (const auto* literal = dynamic_cast<const NumberLiteralExpr*>(size.get())) {
                        try {
                            if (static_cast<size_t>(std::stoull(literal->value)) != (*initializerShape)[dimension])
                                Report("array initializer size does not match dimension " +
                                    std::to_string(dimension + 1));
                        }
                        catch (const std::exception&) {
                        }
                    }
                }
            }
            if (expr->value && !dynamic_cast<ArrayExpr*>(expr->value.get()))
                Report("array variable '" + name + "' requires an array literal initializer");
        }
        Result value;
        if (expr->value) value = EvaluateExpected(expr->value.get(), type == "auto" ? std::string{} : type);
        if (type == "auto") {
            if (!expr->value) Report("auto variable '" + name + "' requires an initializer");
            else type = value.type;
        }
        else if (expr->value && !IsAssignable(type, value.type))
            Report("initializer of '" + name + "' has type '" + value.type + "', expected '" + type + "'");
        if (IsTaskType(type) && expr->value && !value.createsTask)
            Report("task variable '" + name + "' requires a spawn initializer",
                "E_TASK_SPAWN_REQUIRED");
        if (IsTaskType(type) && !expr->value)
            Report("task variable '" + name + "' requires a spawn initializer",
                "E_TASK_SPAWN_REQUIRED");

        const bool fieldDeclaration = !currentType.empty() && functionDepth == 0;
        const bool globalDeclaration = currentType.empty() && table.ScopeDepth() == 0;
        SymbolId id = fieldDeclaration ? table.Lookup(name) :
            (globalDeclaration ? table.Lookup(declarationName) : table.LookupCurrent(name));
        if (!fieldDeclaration && !globalDeclaration) {
            const auto declared = table.Declare(SymbolKind::Variable, name, type);
            if (!declared) Report("object '" + name + "' is already declared in this scope");
            else id = *declared;
        }
        if (IsManagedPointerType(type)) {
            if (Symbol* symbol = table.Get(id)) {
                symbol->managedOwner = value.createsManagedOwner;
                symbol->managedBorrower = expr->value && value.type != "null" &&
                    !value.createsManagedOwner && value.pointerOwner != InvalidSymbolId;
            }
        }
        else if (Symbol* symbol = table.Get(id)) symbol->type = type;
        ValueFlowState flow;
        flow.initialization = (expr->value || arrayRank > 0)
            ? InitializationState::Initialized : InitializationState::Uninitialized;
        flow.pointerValidity = IsPointerType(type)
            ? (expr->value ? value.pointerValidity : PointerValidity::Unknown)
            : PointerValidity::NotPointer;
        flow.pointerOwner = IsManagedPointerType(type) && expr->value
            ? (value.createsManagedOwner ? id : value.pointerOwner)
            : InvalidSymbolId;
        flow.taskState = IsTaskType(type) && expr->value
            ? value.taskState : TaskState::NotTask;
        if (functionDepth > 0) RegisterFlowSymbol(id, flow);
        Save(expr, {id, type, true, false, false,
            flow.initialization, flow.pointerValidity, flow.pointerOwner,
            flow.taskState, value.createsTask, false});
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
        const std::string constructedType = ResolveType(expr->constructName.get());
        const bool rawAllocation = expr->raw ||
            (IsRawPointerType(expectedType) && PointerPointee(expectedType) == constructedType);
        if (!IsKnownType(constructedType) || constructedType == "void" || constructedType == "auto" ||
            constructedType == "dynamic")
            Report("cannot allocate type '" + constructedType + "'");
        const bool primitive = PrimitiveStringToEnum(constructedType).has_value();
        if (expr->arguments.size() > 1 && primitive)
            Report("primitive allocation accepts at most one initializer");
        std::vector<std::string> parameters;
        if (!primitive) {
            const auto found = types.find(constructedType);
            if (found != types.end() && found->second.constructor)
                parameters = found->second.constructor->parameterTypes;
            if (expr->arguments.size() != parameters.size())
                Report("constructor of '" + constructedType + "' expects " +
                    std::to_string(parameters.size()) + " argument(s), got " +
                    std::to_string(expr->arguments.size()));
        }
        for (size_t index = 0; index < expr->arguments.size(); ++index) {
            const std::string expected = primitive ? constructedType :
                (index < parameters.size() ? parameters[index] : std::string{});
            const Result value = EvaluateExpected(expr->arguments[index].get(), expected);
            if (!expected.empty() && !IsAssignable(expected, value.type))
                Report("constructor argument " + std::to_string(index + 1) + " has type '" +
                    value.type + "', expected '" + expected + "'");
        }
        Result allocation{InvalidSymbolId,
            (rawAllocation ? "raw " : "") + constructedType + "*", false,
            !rawAllocation, false, InitializationState::Initialized,
            PointerValidity::Live, InvalidSymbolId};
        allocation.createsRawOwner = rawAllocation;
        Save(expr, std::move(allocation));
    }

    void Analyzer::Visit(DestructorCallExpr* expr) {
        const AccessMode previousAccess = accessMode;
        accessMode = AccessMode::Delete;
        const Result target = Evaluate(expr->target.get());
        accessMode = previousAccess;
        if (!IsPointerType(target.type) && target.type != "error")
            Report("delete requires a pointer, got '" + target.type + "'");
        if (!target.isLValue) Report("delete target must be an assignable pointer variable");
        const auto keep = keepLifetimes.find(target.symbol);
        if (target.initialization == InitializationState::Uninitialized)
            Report("pointer is deleted before initialization", "E_DELETE_UNINITIALIZED", target.symbol);
        else if (target.initialization == InitializationState::MaybeUninitialized)
            Report("pointer may be uninitialized when deleted", "E_DELETE_MAYBE_UNINITIALIZED", target.symbol);
        if (target.pointerValidity == PointerValidity::Deleted && keep == keepLifetimes.end())
            Report("pointer is deleted more than once", "E_DOUBLE_DELETE", target.symbol);
        else if (target.pointerValidity == PointerValidity::Expired)
            Report("expired managed subscriber cannot be deleted", "E_DELETE_EXPIRED", target.symbol);
        else if (target.pointerValidity == PointerValidity::MaybeInvalid)
            Report("pointer may already be invalid when deleted", "E_DELETE_MAYBE_INVALID", target.symbol);

        if (IsManagedPointerType(target.type) && target.pointerValidity != PointerValidity::Null &&
            target.pointerOwner != target.symbol)
            Report("managed subscriber cannot be deleted; delete its owner instead",
                "E_DELETE_SUBSCRIBER", target.symbol);

        if (keep != keepLifetimes.end()) {
            if (keep->second.state == KeepState::Deleted)
                Report("raw owner '" + keep->second.name + "' is deleted more than once",
                    "E_RAW_DOUBLE_DELETE", target.symbol);
            keep->second.state = KeepState::Deleted;
        }
        if (auto flow = valueFlow.find(target.symbol); flow != valueFlow.end()) {
            if (IsManagedPointerType(target.type) && target.pointerOwner == target.symbol) {
                for (auto& [id, alias] : valueFlow) {
                    if (id != target.symbol && alias.pointerOwner == target.symbol &&
                        alias.pointerValidity != PointerValidity::Null)
                        alias.pointerValidity = PointerValidity::Expired;
                }
            }
            flow->second.initialization = InitializationState::Initialized;
            flow->second.pointerValidity = target.pointerValidity == PointerValidity::Null
                ? PointerValidity::Null : PointerValidity::Deleted;
        }
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
        if (expr->op == "spawn") {
            ++spawnContextDepth;
            const Result operand = Evaluate(expr->operand.get());
            --spawnContextDepth;
            if (!operand.asyncCall)
                Report("spawn requires a call to an async function", "E_SPAWN_REQUIRES_ASYNC_CALL",
                    operand.symbol);
            Save(expr, {InvalidSymbolId, "task<" + operand.type + ">", false,
                false, false, InitializationState::Initialized,
                PointerValidity::NotPointer, InvalidSymbolId,
                TaskState::Pending, true, false});
            return;
        }
        if (expr->op == "await") {
            const Result operand = Evaluate(expr->operand.get());
            if (!currentFunctionAsync)
                Report("await is only allowed inside an async function", "E_AWAIT_OUTSIDE_ASYNC");
            if (!IsTaskType(operand.type)) {
                Report("await requires task<T>, got '" + operand.type + "'", "E_AWAIT_REQUIRES_TASK",
                    operand.symbol);
                Save(expr, {InvalidSymbolId, "error", false});
                return;
            }
            if (operand.taskState == TaskState::Awaited)
                Report("task is awaited more than once", "E_TASK_DOUBLE_AWAIT", operand.symbol);
            else if (operand.taskState == TaskState::MaybePending)
                Report("task may already have been awaited on another control-flow path",
                    "E_TASK_MAYBE_DOUBLE_AWAIT", operand.symbol);
            if (auto flow = valueFlow.find(operand.symbol); flow != valueFlow.end())
                flow->second.taskState = TaskState::Awaited;
            Save(expr, {operand.symbol, TaskValueType(operand.type), false,
                false, false, InitializationState::Initialized,
                PointerValidity::NotPointer, InvalidSymbolId,
                TaskState::NotTask, false, false});
            return;
        }
        const AccessMode previousAccess = accessMode;
        if (expr->op == "&") accessMode = AccessMode::Address;
        const Result operand = Evaluate(expr->operand.get());
        accessMode = previousAccess;
        if (expr->op == "&") {
            if (!operand.isLValue) Report("operator '&' requires an assignable value");
            Save(expr, {InvalidSymbolId, "raw " + operand.type + "*", false, false, false,
                InitializationState::Initialized, PointerValidity::Live, InvalidSymbolId});
            return;
        }
        if (expr->op == "*") {
            if (!IsPointerType(operand.type)) {
                Report("operator '*' requires a pointer");
                Save(expr, {InvalidSymbolId, "error", false});
            }
            else {
                if (operand.pointerValidity == PointerValidity::Null)
                    Report("null pointer is dereferenced", "E_NULL_DEREFERENCE", operand.symbol);
                else if (operand.pointerValidity == PointerValidity::MaybeNull)
                    Report("pointer may be null when dereferenced", "E_MAYBE_NULL_DEREFERENCE", operand.symbol);
                else if (operand.pointerValidity == PointerValidity::Deleted)
                    Report("deleted pointer is dereferenced", "E_USE_AFTER_DELETE", operand.symbol);
                else if (operand.pointerValidity == PointerValidity::Expired)
                    Report("expired managed subscriber is dereferenced", "E_EXPIRED_DEREFERENCE", operand.symbol);
                else if (operand.pointerValidity == PointerValidity::MaybeInvalid)
                    Report("pointer may be invalid when dereferenced", "E_MAYBE_INVALID_DEREFERENCE", operand.symbol);
                Save(expr, {InvalidSymbolId, PointerPointee(operand.type), true});
            }
            return;
        }
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
        const std::string templateName = ExtractIdentifier(expr->base.get());
        if (typeContext && templateName == "task") {
            if (expr->types.size() != 1) {
                Report("task requires exactly one result type", "E_TASK_TYPE_ARITY");
                Save(expr, {InvalidSymbolId, "error", false});
                return;
            }
            const std::string valueType = ResolveType(expr->types.front().get());
            Save(expr, {InvalidSymbolId, "task<" + valueType + ">", false});
            return;
        }
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
        const bool typeBody = !currentType.empty() && functionDepth == 0;
        table.EnterScope();
        if (phase == Phase::ResolveBodies) {
            PushKeepScope();
            PushValueFlowScope();
        }
        for (const auto& statement : stmt->statements) {
            AcceptIfPresent(statement, *this);
            if (phase == Phase::ResolveBodies && flowTerminated) {
                if (!typeBody) break;
                flowTerminated = false;
            }
        }
        if (phase == Phase::ResolveBodies) {
            PopValueFlowScope();
            PopKeepScope();
        }
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
                    {SymbolKind::Method, ResolveType(stmt->returnType.get()), ResolveParameterTypes(stmt->parameters)});
        }
        else ResolveFunction(*stmt, currentType.empty() ? SymbolKind::Function : SymbolKind::Method);
    }

    void Analyzer::Visit(ReturnStmt* stmt) {
        if (phase == Phase::CollectDeclarations) return;
        if (functionDepth == 0) Report("return statement is outside a function");
        const Result value = EvaluateExpected(stmt->expr.get(), currentReturnType);
        if (!IsAssignable(currentReturnType, value.type))
            Report("return type '" + value.type + "' does not match '" + currentReturnType + "'");
        if (value.pointerValidity == PointerValidity::Deleted || value.pointerValidity == PointerValidity::Expired)
            Report("return expression contains an invalid pointer", "E_RETURN_INVALID_POINTER", value.symbol);
        else if (value.pointerValidity == PointerValidity::MaybeInvalid)
            Report("return expression may contain an invalid pointer", "E_RETURN_MAYBE_INVALID_POINTER", value.symbol);
        if (IsManagedPointerType(currentReturnType) && value.type != "error" &&
            value.type != "null" &&
            !value.createsManagedOwner && !value.referencesManagedOwner)
            Report("a managed pointer return must transfer an owner; subscribers cannot escape their owner");
        CheckKeepScopesFrom(0, "return");
        CheckTaskScopesFrom(0, "return");
        flowTerminated = true;
    }

    void Analyzer::Visit(AssignmentStmt* stmt) { if (phase == Phase::ResolveBodies) AcceptIfPresent(stmt->expr, *this); }
    void Analyzer::Visit(VarDeclStmt* stmt) {
        AcceptIfPresent(stmt->expr, *this);
        if (phase != Phase::ResolveBodies || functionDepth == 0 || !stmt->expr) return;
        const ExpressionInfo* declaration = GetExpressionInfo(*stmt->expr);
        const ExpressionInfo* initializer = stmt->expr->value
            ? GetExpressionInfo(*stmt->expr->value) : nullptr;
        const SymbolId id = declaration ? declaration->symbol : InvalidSymbolId;
        const Symbol* symbol = table.Get(id);
        const std::string name = symbol ? symbol->name : ExtractIdentifier(stmt->expr->name.get());
        if (!declaration || !IsRawPointerType(declaration->type) ||
            !initializer || !initializer->createsRawOwner) return;
        if (keepScopes.empty()) PushKeepScope();
        keepLifetimes[id] = {name, KeepState::Live};
        keepScopes.back().push_back(id);
    }

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
            auto& definition = types[typeName];
            definition.parents.clear();
            for (const std::string& parent : stmt->parents)
                definition.parents.push_back(ResolveTypeReference(parent));
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
        for (const auto& [name, member] : VisibleMembers(typeName))
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
        keepLifetimes.clear();
        keepScopes.clear();
        loopKeepDepths.clear();
        loopBreakStates.clear();
        valueFlow.clear();
        valueFlowScopes.clear();
        loopBreakValueStates.clear();
        accessMode = AccessMode::Read;
        flowTerminated = false;
        PushKeepScope();
        PushValueFlowScope();
        for (const auto& parameter : stmt->parameters) {
            const std::string name = parameter ? ExtractIdentifier(parameter->name.get()) : std::string{};
            const std::string type = parameter ? ResolveDeclaredType(*parameter) : "error";
            if (const auto declared = table.Declare(SymbolKind::Parameter, name, type))
                RegisterFlowSymbol(*declared, {InitializationState::Initialized,
                    IsPointerType(type) ? PointerValidity::Unknown : PointerValidity::NotPointer,
                    InvalidSymbolId,
                    IsTaskType(type) ? TaskState::Unknown : TaskState::NotTask});
            else Report("parameter '" + name + "' is already declared");
        }
        AcceptIfPresent(stmt->body, *this);
        PopValueFlowScope();
        PopKeepScope();
        keepLifetimes.clear();
        valueFlow.clear();
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
        const KeepLifetimeMap base = keepLifetimes;
        const ValueFlowMap baseValues = valueFlow;
        std::vector<KeepLifetimeMap> continuingPaths;
        std::vector<ValueFlowMap> continuingValuePaths;
        for (auto& branch : stmt->branches) {
            keepLifetimes = base;
            valueFlow = baseValues;
            flowTerminated = false;
            const Result condition = Evaluate(branch.condition.get());
            if (!IsConditionType(condition.type)) Report("if condition must be boolean-compatible");
            AcceptIfPresent(branch.body, *this);
            if (!flowTerminated) {
                continuingPaths.push_back(keepLifetimes);
                continuingValuePaths.push_back(valueFlow);
            }
        }
        if (stmt->elseBranch) {
            keepLifetimes = base;
            valueFlow = baseValues;
            flowTerminated = false;
            AcceptIfPresent(stmt->elseBranch, *this);
            if (!flowTerminated) {
                continuingPaths.push_back(keepLifetimes);
                continuingValuePaths.push_back(valueFlow);
            }
        }
        else {
            continuingPaths.push_back(base);
            continuingValuePaths.push_back(baseValues);
        }
        MergeKeepPaths(base, continuingPaths);
        MergeValueFlowPaths(baseValues, continuingValuePaths);
        flowTerminated = continuingPaths.empty();
    }

    void Analyzer::Visit(ForStmt* stmt) {
        if (phase == Phase::CollectDeclarations) return;
        table.EnterScope();
        PushKeepScope();
        PushValueFlowScope();
        AcceptAll(stmt->init, *this);
        if (stmt->condition) {
            const Result condition = Evaluate(stmt->condition.get());
            if (!IsConditionType(condition.type)) Report("for condition must be boolean-compatible");
        }
        const KeepLifetimeMap beforeLoop = keepLifetimes;
        const ValueFlowMap beforeLoopValues = valueFlow;
        loopKeepDepths.push_back(keepScopes.size());
        loopBreakStates.emplace_back();
        loopBreakValueStates.emplace_back();
        ++loopDepth;
        flowTerminated = false;
        AcceptIfPresent(stmt->body, *this);
        if (!flowTerminated) AcceptAll(stmt->update, *this);
        const bool bodyContinues = !flowTerminated;
        const KeepLifetimeMap afterBody = keepLifetimes;
        const ValueFlowMap afterBodyValues = valueFlow;
        --loopDepth;
        std::vector<KeepLifetimeMap> exits{beforeLoop};
        if (bodyContinues) exits.push_back(afterBody);
        exits.insert(exits.end(), loopBreakStates.back().begin(), loopBreakStates.back().end());
        std::vector<ValueFlowMap> valueExits{beforeLoopValues};
        if (bodyContinues) valueExits.push_back(afterBodyValues);
        valueExits.insert(valueExits.end(), loopBreakValueStates.back().begin(), loopBreakValueStates.back().end());
        loopBreakStates.pop_back();
        loopBreakValueStates.pop_back();
        loopKeepDepths.pop_back();
        MergeKeepPaths(beforeLoop, exits);
        MergeValueFlowPaths(beforeLoopValues, valueExits);
        flowTerminated = false;
        PopValueFlowScope();
        PopKeepScope();
        table.ExitScope();
    }

    void Analyzer::Visit(WhileStmt* stmt) {
        if (phase == Phase::CollectDeclarations) return;
        const Result condition = Evaluate(stmt->condition.get());
        if (!IsConditionType(condition.type)) Report("while condition must be boolean-compatible");
        const KeepLifetimeMap beforeLoop = keepLifetimes;
        const ValueFlowMap beforeLoopValues = valueFlow;
        loopKeepDepths.push_back(keepScopes.size());
        loopBreakStates.emplace_back();
        loopBreakValueStates.emplace_back();
        ++loopDepth;
        flowTerminated = false;
        AcceptIfPresent(stmt->body, *this);
        const bool bodyContinues = !flowTerminated;
        const KeepLifetimeMap afterBody = keepLifetimes;
        const ValueFlowMap afterBodyValues = valueFlow;
        --loopDepth;
        std::vector<KeepLifetimeMap> exits{beforeLoop};
        if (bodyContinues) exits.push_back(afterBody);
        exits.insert(exits.end(), loopBreakStates.back().begin(), loopBreakStates.back().end());
        std::vector<ValueFlowMap> valueExits{beforeLoopValues};
        if (bodyContinues) valueExits.push_back(afterBodyValues);
        valueExits.insert(valueExits.end(), loopBreakValueStates.back().begin(), loopBreakValueStates.back().end());
        loopBreakStates.pop_back();
        loopBreakValueStates.pop_back();
        loopKeepDepths.pop_back();
        MergeKeepPaths(beforeLoop, exits);
        MergeValueFlowPaths(beforeLoopValues, valueExits);
        flowTerminated = false;
    }

    void Analyzer::Visit(DoWhileStmt* stmt) {
        if (phase == Phase::CollectDeclarations) return;
        const KeepLifetimeMap beforeLoop = keepLifetimes;
        const ValueFlowMap beforeLoopValues = valueFlow;
        loopKeepDepths.push_back(keepScopes.size());
        loopBreakStates.emplace_back();
        loopBreakValueStates.emplace_back();
        ++loopDepth;
        flowTerminated = false;
        AcceptIfPresent(stmt->body, *this);
        const bool bodyContinues = !flowTerminated;
        const KeepLifetimeMap afterBody = keepLifetimes;
        const ValueFlowMap afterBodyValues = valueFlow;
        --loopDepth;
        const Result condition = Evaluate(stmt->condition.get());
        if (!IsConditionType(condition.type)) Report("do-while condition must be boolean-compatible");
        std::vector<KeepLifetimeMap> exits;
        if (bodyContinues) exits.push_back(afterBody);
        exits.insert(exits.end(), loopBreakStates.back().begin(), loopBreakStates.back().end());
        std::vector<ValueFlowMap> valueExits;
        if (bodyContinues) valueExits.push_back(afterBodyValues);
        valueExits.insert(valueExits.end(), loopBreakValueStates.back().begin(), loopBreakValueStates.back().end());
        if (exits.empty()) exits.push_back(beforeLoop);
        if (valueExits.empty()) valueExits.push_back(beforeLoopValues);
        loopBreakStates.pop_back();
        loopBreakValueStates.pop_back();
        loopKeepDepths.pop_back();
        MergeKeepPaths(beforeLoop, exits);
        MergeValueFlowPaths(beforeLoopValues, valueExits);
        flowTerminated = false;
    }

    void Analyzer::Visit(ForEachStmt* stmt) {
        if (phase == Phase::CollectDeclarations) return;
        table.EnterScope();
        PushKeepScope();
        PushValueFlowScope();
        const Result iterable = Evaluate(stmt->iterable.get());
        if (!iterable.type.ends_with("[]") && iterable.type != "error") Report("for-each source must be an array");
        else if (ArrayRank(iterable.type) != 1 && iterable.type != "error")
            Report("for-each currently requires a one-dimensional array or slice");
        AcceptIfPresent(stmt->var, *this);
        if (stmt->var) {
            if (const ExpressionInfo* variable = GetExpressionInfo(*stmt->var)) {
                const std::string elementType = ArrayRank(iterable.type) == 1
                    ? ArrayElementType(iterable.type) : "error";
                if (!IsAssignable(variable->type, elementType))
                    Report("for-each variable has type '" + variable->type +
                        "', expected '" + elementType + "'");
                if (auto flow = valueFlow.find(variable->symbol); flow != valueFlow.end())
                    flow->second.initialization = InitializationState::Initialized;
            }
        }
        const KeepLifetimeMap beforeLoop = keepLifetimes;
        const ValueFlowMap beforeLoopValues = valueFlow;
        loopKeepDepths.push_back(keepScopes.size());
        loopBreakStates.emplace_back();
        loopBreakValueStates.emplace_back();
        ++loopDepth;
        flowTerminated = false;
        AcceptIfPresent(stmt->body, *this);
        const bool bodyContinues = !flowTerminated;
        const KeepLifetimeMap afterBody = keepLifetimes;
        const ValueFlowMap afterBodyValues = valueFlow;
        --loopDepth;
        std::vector<KeepLifetimeMap> exits{beforeLoop};
        if (bodyContinues) exits.push_back(afterBody);
        exits.insert(exits.end(), loopBreakStates.back().begin(), loopBreakStates.back().end());
        std::vector<ValueFlowMap> valueExits{beforeLoopValues};
        if (bodyContinues) valueExits.push_back(afterBodyValues);
        valueExits.insert(valueExits.end(), loopBreakValueStates.back().begin(), loopBreakValueStates.back().end());
        loopBreakStates.pop_back();
        loopBreakValueStates.pop_back();
        loopKeepDepths.pop_back();
        MergeKeepPaths(beforeLoop, exits);
        MergeValueFlowPaths(beforeLoopValues, valueExits);
        flowTerminated = false;
        PopValueFlowScope();
        PopKeepScope();
        table.ExitScope();
    }

    void Analyzer::Visit(ContinueStmt* stmt) {
        (void)stmt;
        if (phase != Phase::ResolveBodies) return;
        if (loopDepth == 0) Report("continue statement is outside a loop");
        else {
            CheckKeepScopesFrom(loopKeepDepths.back(), "continue");
            CheckTaskScopesFrom(loopKeepDepths.back(), "continue");
            loopBreakValueStates.back().push_back(valueFlow);
            flowTerminated = true;
        }
    }

    void Analyzer::Visit(BreakStmt* stmt) {
        (void)stmt;
        if (phase != Phase::ResolveBodies) return;
        if (loopDepth == 0) Report("break statement is outside a loop");
        else {
            CheckKeepScopesFrom(loopKeepDepths.back(), "break");
            CheckTaskScopesFrom(loopKeepDepths.back(), "break");
            loopBreakStates.back().push_back(keepLifetimes);
            loopBreakValueStates.back().push_back(valueFlow);
            flowTerminated = true;
        }
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
