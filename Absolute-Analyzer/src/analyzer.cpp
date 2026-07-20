#include "analyzer_internal.h"

namespace Absolute {
    bool Analyzer::Analyze() {
        table.Reset();
        types.clear();
        functionOverloads.clear();
        extensionMethods.clear();
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

    const Symbol* Analyzer::FindFunctionSymbol(
        const std::string& name, const std::vector<std::string>& parameterTypes) const {
        const auto found = functionOverloads.find(name);
        if (found == functionOverloads.end()) return nullptr;
        for (const SymbolId id : found->second) {
            const Symbol* symbol = table.Get(id);
            if (symbol && symbol->parameterTypes == parameterTypes) return symbol;
        }
        return nullptr;
    }

    size_t Analyzer::FunctionOverloadCount(const std::string& name) const {
        const auto found = functionOverloads.find(name);
        return found == functionOverloads.end() ? 0 : found->second.size();
    }

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
        if (name == "main" && functionOverloads.contains(name))
            Report("entry function 'main' cannot be overloaded", "E_MAIN_OVERLOAD");
        if (statement.IsExternal() && functionOverloads.contains(name) &&
            std::any_of(functionOverloads[name].begin(), functionOverloads[name].end(), [&](SymbolId id) {
                const Symbol* existing = table.Get(id);
                return existing && existing->externalFunction;
            }))
            Report("extern function '" + name + "' cannot be overloaded", "E_EXTERN_OVERLOAD");
        const auto declared = table.Declare(SymbolKind::Function, name,
            returnType, ResolveParameterTypes(statement.parameters));
        if (!declared) Report("function '" + name + "' already has an overload with this signature");
        else if (Symbol* symbol = table.Get(*declared)) {
            symbol->asyncFunction = HasModifier(statement, "async");
            symbol->extensionFunction = HasModifier(statement, "extension");
            symbol->externalFunction = statement.IsExternal();
            functionOverloads[name].push_back(*declared);
            if (symbol->extensionFunction)
                extensionMethods[statement.name->value].push_back(*declared);
        }
    }

    void Analyzer::ResolveFunction(FunctionDeclStmt& statement, SymbolKind kind) {
        if (!statement.name || !statement.returnType) return;
        const std::string returnType = ResolveType(statement.returnType.get());
        if (!IsKnownType(returnType))
            Report("unknown return type '" + returnType + "' of function '" + statement.name->value + "'");

        if (statement.IsExternal() && kind == SymbolKind::Method)
            Report("extern functions cannot be class or struct members");
        if (HasModifier(statement, "extension") && kind != SymbolKind::Function)
            Report("extension methods must be namespace or global functions", "E_EXTENSION_MEMBER");
        if (HasModifier(statement, "extension") && statement.parameters.empty())
            Report("extension method '" + statement.name->value + "' requires a receiver parameter",
                "E_EXTENSION_RECEIVER");
        if (HasModifier(statement, "extension") && statement.IsExternal())
            Report("extern functions cannot be extension methods", "E_EXTENSION_EXTERN");
        if (HasModifier(statement, "extension") && HasModifier(statement, "async"))
            Report("async extension methods are not implemented", "E_EXTENSION_ASYNC");
        if (HasModifier(statement, "extension") && !statement.parameters.empty()) {
            const std::string receiverType = ResolveDeclaredType(*statement.parameters.front());
            if (receiverType == "void" || receiverType == "auto" || receiverType == "dynamic" ||
                receiverType == "error")
                Report("extension method receiver requires a concrete type", "E_EXTENSION_RECEIVER_TYPE");
        }
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

    void Analyzer::DeclareType(const std::string& name, TypeKind kind) {
        const std::string qualifiedName = Qualify(name);
        if (types.contains(qualifiedName)) {
            Report("type '" + qualifiedName + "' is already declared");
            return;
        }
        TypeDefinition definition;
        definition.kind = kind;
        types.emplace(qualifiedName, std::move(definition));
        if (!table.Declare(SymbolKind::Type, qualifiedName, qualifiedName))
            Report("object '" + qualifiedName + "' is already declared in this scope");
    }

    void Analyzer::DeclareMember(const std::string& owner, std::string name, MemberSignature signature) {
        auto& members = types[owner].members;
        auto& overloads = members[name];
        const bool method = signature.kind == SymbolKind::Method;
        const bool collision = std::any_of(overloads.begin(), overloads.end(), [&](const MemberSignature& existing) {
            return !method || existing.kind != SymbolKind::Method ||
                existing.parameterTypes == signature.parameterTypes;
        });
        if (collision) {
            Report("member '" + owner + "." + name + "' already has this signature");
            return;
        }
        const auto declared = table.Declare(signature.kind, owner + "." + name,
            signature.type, signature.parameterTypes);
        if (!declared) {
            Report("member '" + owner + "." + name + "' already has this signature");
            return;
        }
        signature.symbol = *declared;
        overloads.push_back(std::move(signature));
    }

    std::vector<Analyzer::MemberSignature> Analyzer::FindMembers(
        const std::string& owner, const std::string& name) const {
        const std::string objectType = IsPointerType(owner) ? PointerPointee(owner) : owner;
        const auto type = types.find(objectType);
        if (type == types.end()) return {};
        std::vector<MemberSignature> result;
        for (const std::string& parent : type->second.parents) {
            auto inherited = FindMembers(parent, name);
            result.insert(result.end(), inherited.begin(), inherited.end());
        }
        if (const auto member = type->second.members.find(name); member != type->second.members.end()) {
            for (const MemberSignature& declared : member->second) {
                if (declared.kind == SymbolKind::Method) {
                    result.erase(std::remove_if(result.begin(), result.end(), [&](const MemberSignature& inherited) {
                        return inherited.kind == SymbolKind::Method &&
                            inherited.parameterTypes == declared.parameterTypes;
                    }), result.end());
                }
                result.push_back(declared);
            }
        }
        return result;
    }

    std::unordered_map<std::string, std::vector<Analyzer::MemberSignature>> Analyzer::VisibleMembers(
        const std::string& owner) const {
        std::unordered_map<std::string, std::vector<MemberSignature>> result;
        const auto found = types.find(owner);
        if (found == types.end()) return result;
        for (const std::string& parent : found->second.parents) {
            auto inherited = VisibleMembers(parent);
            for (auto& [name, signatures] : inherited)
                result[name].insert(result[name].end(), signatures.begin(), signatures.end());
        }
        for (const auto& [name, signatures] : found->second.members) {
            for (const MemberSignature& declared : signatures) {
                if (declared.kind == SymbolKind::Method) {
                    auto& visible = result[name];
                    visible.erase(std::remove_if(visible.begin(), visible.end(), [&](const MemberSignature& inherited) {
                        return inherited.kind == SymbolKind::Method &&
                            inherited.parameterTypes == declared.parameterTypes;
                    }), visible.end());
                }
                result[name].push_back(declared);
            }
        }
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

    std::vector<SymbolId> Analyzer::FindFunctionCandidates(const std::string& name) const {
        if (name.empty()) return {};
        const auto lookup = [&](const std::string& candidate) -> std::vector<SymbolId> {
            const auto found = functionOverloads.find(candidate);
            return found == functionOverloads.end() ? std::vector<SymbolId>{} : found->second;
        };
        if (auto direct = lookup(name); !direct.empty()) return direct;
        if (name.find('.') != std::string::npos) {
            if (!currentNamespace.empty())
                if (auto nested = lookup(currentNamespace + "." + name); !nested.empty()) return nested;
            return {};
        }
        std::string scope = currentNamespace;
        while (!scope.empty()) {
            if (auto scoped = lookup(scope + "." + name); !scoped.empty()) return scoped;
            const size_t separator = scope.rfind('.');
            if (separator == std::string::npos) break;
            scope.resize(separator);
        }
        for (const std::string& imported : importedNamespaces)
            if (auto importedCandidates = lookup(imported + "." + name); !importedCandidates.empty())
                return importedCandidates;
        return {};
    }

    std::vector<SymbolId> Analyzer::FindExtensionCandidates(const std::string& name) const {
        const auto found = extensionMethods.find(name);
        if (found == extensionMethods.end()) return {};
        std::vector<SymbolId> result;
        for (const SymbolId id : found->second) {
            const Symbol* symbol = table.Get(id);
            if (!symbol) continue;
            const size_t separator = symbol->name.rfind('.');
            if (separator == std::string::npos) {
                result.push_back(id);
                continue;
            }
            const std::string ownerNamespace = symbol->name.substr(0, separator);
            if (currentNamespace == ownerNamespace ||
                currentNamespace.starts_with(ownerNamespace + ".") ||
                importedNamespaces.contains(ownerNamespace))
                result.push_back(id);
        }
        return result;
    }

    int Analyzer::ConversionCost(const std::string& target, const std::string& source) const {
        if (target == source) return 0;
        if (!IsAssignable(target, source)) return -1;
        if (target == "dynamic") return 4;
        if (source == "null") return 3;
        if (IsNumeric(target) && IsNumeric(source)) {
            const bool targetFloating = target == "float" || target == "double";
            const bool sourceFloating = source == "float" || source == "double";
            return targetFloating == sourceFloating ? 1 : 2;
        }
        return 1;
    }

    SymbolId Analyzer::SelectOverload(const std::vector<SymbolId>& candidates,
        const std::vector<Result>& arguments, const std::string& displayName) {
        SymbolId best = InvalidSymbolId;
        int bestCost = std::numeric_limits<int>::max();
        bool ambiguous = false;
        for (const SymbolId id : candidates) {
            const Symbol* candidate = table.Get(id);
            if (!candidate || candidate->parameterTypes.size() != arguments.size()) continue;
            int cost = 0;
            bool applicable = true;
            for (size_t index = 0; index < arguments.size(); ++index) {
                const int conversion = ConversionCost(candidate->parameterTypes[index], arguments[index].type);
                if (conversion < 0) {
                    applicable = false;
                    break;
                }
                cost += conversion;
            }
            if (!applicable) continue;
            if (cost < bestCost) {
                best = id;
                bestCost = cost;
                ambiguous = false;
            }
            else if (cost == bestCost) ambiguous = true;
        }
        if (best == InvalidSymbolId) {
            std::string typesText;
            for (size_t index = 0; index < arguments.size(); ++index) {
                if (index) typesText += ", ";
                typesText += arguments[index].type;
            }
            Report("no overload of '" + displayName + "' accepts (" + typesText + ")",
                "E_NO_MATCHING_OVERLOAD");
            return InvalidSymbolId;
        }
        if (ambiguous) {
            Report("call to overloaded function '" + displayName + "' is ambiguous",
                "E_AMBIGUOUS_OVERLOAD");
            return InvalidSymbolId;
        }
        return best;
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

}
