#include "analyzer_internal.h"

namespace Absolute {
    namespace {
        struct OpaqueAttributeViews {
            std::vector<std::vector<AbsoluteAttributeArgumentV1>> argumentStorage;
            std::vector<AbsoluteAttributeV1> attributes;

            explicit OpaqueAttributeViews(const Statement& statement) {
                argumentStorage.resize(statement.attributes.size());
                attributes.reserve(statement.attributes.size());
                for (size_t index = 0; index < statement.attributes.size(); ++index) {
                    const Attribute& source = statement.attributes[index];
                    auto& arguments = argumentStorage[index];
                    arguments.reserve(source.arguments.size());
                    for (const AttributeArgument& argument : source.arguments) {
                        arguments.push_back({
                            argument.name.empty() ? nullptr : argument.name.c_str(),
                            argument.name.size(),
                            static_cast<uint32_t>(argument.value.kind),
                            argument.value.text.c_str(),
                            argument.value.text.size()
                        });
                    }
                    attributes.push_back({source.name.c_str(), source.name.size(),
                        arguments.size(), arguments.empty() ? nullptr : arguments.data()});
                }
            }
        };
    }

    void Analyzer::Visit(SingleStatement* stmt) {
        if (phase == Phase::ResolveBodies && !stmt->attributes.empty())
            ValidateAttributes(*stmt, "statement", false);
        const bool memberDeclaration = !currentType.empty() && functionDepth == 0 &&
            dynamic_cast<InstanceDeclExpr*>(stmt->expr.get()) != nullptr;
        if (phase == Phase::ResolveBodies)
            ValidateAccessModifiers(*stmt, memberDeclaration,
                memberDeclaration ? "field declaration" : "statement");
        const AccessLevel oldAccess = pendingMemberAccess;
        if (memberDeclaration) pendingMemberAccess = DeclaredAccess(*stmt);
        AcceptIfPresent(stmt->expr, *this);
        pendingMemberAccess = oldAccess;
        if (phase == Phase::ResolveBodies && stmt->expr) {
            const ExpressionInfo* info = GetExpressionInfo(*stmt->expr);
            if (info && info->isMoveResult)
                Report("move result must be consumed by an assignment, field store, "
                    "argument, or return", "E_MOVE_RESULT_UNUSED", info->symbol);
            // The same rule for the other ways to produce a managed or raw
            // owner. A discarded move was already refused while `new Box();`
            // and a call returning an owner were accepted and leaked -- the
            // program ran, and the leak checker aborted it at exit with a
            // handle number and no idea where it came from. Arrays are not
            // included: the backend already releases an array temporary at the
            // end of the statement, which tests/array-copy.abs relies on.
            else if (info && (info->createsManagedOwner || info->createsRawOwner))
                Report("this expression produces an owner that nothing takes; "
                    "bind it to a variable, pass it on, return it, or delete it",
                    "E_OWNING_RESULT_DISCARDED", info->symbol);
        }
    }

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
            else if (stmt->name && stmt->returnType) {
                DeclareMember(currentType, stmt->name->value,
                    {SymbolKind::Method, ResolveType(stmt->returnType.get()),
                        ResolveParameterTypes(stmt->parameters), InvalidSymbolId,
                        HasModifier(*stmt, "const"), HasModifier(*stmt, "static"),
                        DeclaredAccess(*stmt)});
                auto& overloads = types[currentType].members[stmt->name->value];
                if (!overloads.empty()) {
                    Symbol* symbol = table.Get(overloads.back().symbol);
                    if (symbol) {
                        symbol->asyncFunction = HasModifier(*stmt, "async");
                        symbol->variadicParameter = !stmt->parameters.empty() &&
                            stmt->parameters.back()->isParams;
                        symbol->genericParameters = types[currentType].genericParameters;
                        for (const Token& parameter : stmt->templateParams)
                            symbol->genericParameters.push_back(parameter.value);
                        RecordParameterDefaults(*symbol, stmt->parameters);
                        functionDeclarations[symbol->id] = stmt;
                    }
                }
            }
        }
        else if (!currentType.empty() && types[currentType].kind == TypeKind::Interface) {
            ValidateAccessModifiers(*stmt, true, "interface method");
            if (DeclaredAccess(*stmt) != AccessLevel::Public)
                Report("interface method '" + currentType + "." + stmt->name->value +
                    "' must be public", "E_INTERFACE_METHOD_ACCESS");
            ValidateAttributes(*stmt, "interface method", true);
            const std::string returnType = ResolveType(stmt->returnType.get());
            if (!IsKnownType(returnType))
                Report("unknown return type '" + returnType + "' of interface method '" +
                    currentType + "." + stmt->name->value + "'", "E_UNKNOWN_RETURN_TYPE");
            for (const auto& parameter : stmt->parameters) {
                const std::string parameterType = ResolveDeclaredType(*parameter);
                ValidateValueReferenceParameter(*parameter, parameterType,
                    currentType + "." + stmt->name->value);
                if (!IsKnownType(parameterType))
                    Report("unknown parameter type '" + parameterType + "' of interface method '" +
                        currentType + "." + stmt->name->value + "'",
                            "E_UNKNOWN_PARAMETER_TYPE");
                if (parameter->value)
                    Report("interface methods cannot declare default parameter values",
                        "E_INTERFACE_DEFAULT_ARGUMENT");
            }
            const bool staticMethod = HasModifier(*stmt, "static");
            if (HasModifier(*stmt, "extension") || HasModifier(*stmt, "async") ||
                HasModifier(*stmt, "override") ||
                HasModifier(*stmt, "sealed"))
                Report("interface method '" + currentType + "." + stmt->name->value +
                    "' has an unsupported modifier", "E_INTERFACE_METHOD_MODIFIER");
            if (staticMethod && !stmt->body)
                Report("static interface method '" + currentType + "." +
                    stmt->name->value + "' requires a body",
                    "E_STATIC_INTERFACE_METHOD_BODY");
            if (stmt->body) ResolveFunction(*stmt, SymbolKind::Method);
        }
        else {
            ValidateAccessModifiers(*stmt, !currentType.empty(),
                currentType.empty() ? "function" : "method");
            if (!currentType.empty() && types[currentType].kind == TypeKind::Struct &&
                DeclaredAccess(*stmt) == AccessLevel::Protected)
                Report("struct method cannot be protected", "E_PROTECTED_STRUCT_MEMBER");
            if (!currentType.empty() && !stmt->templateParams.empty())
                Report("independently generic methods are not implemented yet; use the generic parameters of the containing type",
                    "E_GENERIC_METHOD_UNSUPPORTED");
            ValidateAttributes(*stmt, currentType.empty() ? "function" : "method", true);
            ResolveFunction(*stmt, currentType.empty() ? SymbolKind::Function : SymbolKind::Method);
        }
    }

    void Analyzer::Visit(ReturnStmt* stmt) {
        if (phase == Phase::CollectDeclarations) return;
        if (finallyDepth > 0)
            Report("return is not allowed inside finally", "E_FINALLY_CONTROL_TRANSFER");
        if (deferDepth > 0)
            Report("return is not allowed inside defer", "E_DEFER_CONTROL_TRANSFER");
        if (functionDepth == 0) Report("return statement is outside a function",
            "E_RETURN_OUTSIDE_FUNCTION");
        const Result value = EvaluateExpected(stmt->expr.get(), currentReturnType);
        if (Symbol* source = table.Get(value.symbol);
            source && source->rolePolymorphic &&
            !ownerGuardedParameters.contains(source->id) &&
            ParameterSupportsOwnership(currentReturnType)) {
            if (Symbol* callable = table.Get(source->callableOwner)) {
                if (callable->parameterRequiresOwner.size() <=
                    source->parameterIndex)
                    callable->parameterRequiresOwner.resize(
                        source->parameterIndex + 1, false);
                callable->parameterRequiresOwner[
                    source->parameterIndex] = true;
                source->requiresOwner = true;
            }
        }
        if (!IsAssignable(currentReturnType, value.type))
            Report("return type '" + value.type + "' does not match '" + currentReturnType + "'",
                "E_RETURN_TYPE_MISMATCH");
        // An expression that produced the value can hand it on: nothing is
        // copied, because there is no other name for it. Asking whether the
        // value's symbol is a callable answers that for a direct call and
        // loses it for everything else -- a conditional of two calls is two
        // values, each produced by the arm that ran, and refusing it said
        // "use move(...)" about a value no name holds.
        bool transfersAggregateOwner =
            value.isMoveResult || value.producesFreshValue;
        if (const Symbol* source = table.Get(value.symbol)) {
            transfersAggregateOwner = transfersAggregateOwner ||
                source->kind == SymbolKind::Function || source->kind == SymbolKind::Method;
        }
        if (ArrayRank(currentReturnType) == 0 && !IsPointerType(currentReturnType) &&
            TypeOwnsResources(currentReturnType) && !transfersAggregateOwner) {
            Report("resource-owning aggregate '" + currentReturnType +
                "' cannot be returned by value; use move(...) for lvalues",
                "E_RESOURCE_AGGREGATE_RETURN", value.symbol);
        }
        if (ArrayRank(currentReturnType) > 0 && value.type != "error") {
            bool storageEscapes = IsExplicitArrayCopy(stmt->expr.get()) ||
                value.createsArrayOwner || value.isMoveResult;
            if (const Symbol* symbol = table.Get(value.symbol)) {
                storageEscapes = storageEscapes || symbol->arrayStorageEscapes ||
                    symbol->scopeDepth == 0 || symbol->kind == SymbolKind::Function ||
                    symbol->kind == SymbolKind::Method;
            }
            if (!storageEscapes)
                Report("returning a local array or borrowed slice requires copy(...)",
                    "E_ARRAY_RETURN_REQUIRES_COPY", value.symbol);
        }
        if (value.pointerValidity == PointerValidity::Deleted || value.pointerValidity == PointerValidity::Expired)
            Report("return expression contains an invalid pointer", "E_RETURN_INVALID_POINTER", value.symbol);
        else if (value.pointerValidity == PointerValidity::MaybeInvalid)
            Report("return expression may contain an invalid pointer", "E_RETURN_MAYBE_INVALID_POINTER", value.symbol);
        if (IsStrongManagedPointerType(currentReturnType) && value.type != "error" &&
            value.type != "null" &&
            !value.createsManagedOwner && !value.referencesManagedOwner)
            Report("a managed pointer return must transfer an owner; subscribers and aggregate fields cannot escape their owner",
                "E_MANAGED_RETURN_REQUIRES_OWNER", value.symbol);
        // The same rule, for a return type that is still a generic parameter.
        else if (!IsStrongManagedPointerType(currentReturnType) &&
            value.type != "error" && value.type != "null" &&
            !value.createsManagedOwner && !value.referencesManagedOwner) {
            if (const Symbol* returned = table.Get(value.symbol);
                returned && returned->kind == SymbolKind::Field)
                RecordGenericBodyFact(GenericBodyFact::Shape::ReturnsField,
                    currentReturnType, returned->name, stmt);
        }
        if (IsWeakPointerType(currentReturnType) && value.type != "error" &&
            value.type != "null") {
            const Symbol* owner = table.Get(value.pointerOwner);
            if (value.createsManagedOwner ||
                (owner && owner->managedOwner && !owner->rolePolymorphic &&
                    owner->scopeDepth > 0))
                Report("returning a weak reference to a local owner would produce an immediately expired handle",
                    "E_WEAK_RETURN_LOCAL_OWNER", value.symbol);
        }
        if (IsRawPointerType(currentReturnType) && value.type != "error" && value.type != "null") {
            if (const Symbol* owner = table.Get(value.pointerOwner)) {
                if (owner->scopeDepth > 0)
                    Report("cannot return a raw pointer to a local variable", "E_RAW_RETURN_LOCAL", value.symbol);
            }
        }
        const size_t keepBoundary = lambdaFunctionBoundaries.empty()
            ? 0 : lambdaFunctionBoundaries.back().keepScopeDepth;
        const size_t valueBoundary = lambdaFunctionBoundaries.empty()
            ? 0 : lambdaFunctionBoundaries.back().valueFlowScopeDepth;
        CheckKeepScopesFrom(keepBoundary, "return");
        CheckTaskScopesFrom(valueBoundary, "return");
        flowTerminated = true;
    }

    void Analyzer::Visit(AssignmentStmt* stmt) { if (phase == Phase::ResolveBodies) AcceptIfPresent(stmt->expr, *this); }
    void Analyzer::Visit(VarDeclStmt* stmt) {
        if (phase == Phase::ResolveBodies && !stmt->attributes.empty())
            ValidateAttributes(*stmt, functionDepth == 0 ? "variable declaration" : "local declaration", false);
        const bool memberDeclaration = !currentType.empty() && functionDepth == 0;
        if (phase == Phase::ResolveBodies) {
            ValidateAccessModifiers(*stmt, memberDeclaration,
                memberDeclaration ? "field declaration" : "variable declaration");
            if (memberDeclaration && types[currentType].kind == TypeKind::Struct &&
                DeclaredAccess(*stmt) == AccessLevel::Protected)
                Report("struct field cannot be protected", "E_PROTECTED_STRUCT_MEMBER");
            if (memberDeclaration && types[currentType].kind == TypeKind::Interface) {
                if (!stmt->expr || !stmt->expr->isStatic)
                    Report("interface fields must be static", "E_INTERFACE_INSTANCE_FIELD");
                if (DeclaredAccess(*stmt) != AccessLevel::Public)
                    Report("interface static fields must be public",
                        "E_INTERFACE_STATIC_FIELD_ACCESS");
            }
        }
        const AccessLevel oldAccess = pendingMemberAccess;
        if (memberDeclaration) pendingMemberAccess = DeclaredAccess(*stmt);
        AcceptIfPresent(stmt->expr, *this);
        pendingMemberAccess = oldAccess;
        if (phase != Phase::ResolveBodies || functionDepth == 0 || !stmt->expr) return;
        const ExpressionInfo* declaration = GetExpressionInfo(*stmt->expr);
        if (declaration) ResolveTypeReference(declaration->type);
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
            if (!IsConditionType(condition.type)) Report("if condition must be boolean-compatible",
                "E_CONDITION_TYPE");
            const SymbolId guardedOwner =
                OwnerGuardParameter(branch.condition.get());
            if (guardedOwner != InvalidSymbolId)
                ownerGuardedParameters.insert(guardedOwner);
            AcceptIfPresent(branch.body, *this);
            if (guardedOwner != InvalidSymbolId)
                ownerGuardedParameters.erase(guardedOwner);
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

    void Analyzer::Visit(SwitchStmt* stmt) {
        if (phase == Phase::CollectDeclarations) return;
        ++switchDepth;
        const Result selector = Evaluate(stmt->value.get());
        const auto enumType = types.find(selector.type);
        const bool selectsEnum = enumType != types.end() && enumType->second.kind == TypeKind::Enum;
        if (!IsInteger(selector.type) && selector.type != "bool" && !selectsEnum &&
            selector.type != "error") {
            Report((stmt->exhaustive ? "match" : "switch") +
                std::string(" value must be an integer, bool, char, or enum"),
                "E_SWITCH_TYPE");
        }

        const KeepLifetimeMap base = keepLifetimes;
        const ValueFlowMap baseValues = valueFlow;
        std::vector<KeepLifetimeMap> continuingPaths;
        std::vector<ValueFlowMap> continuingValuePaths;
        std::unordered_set<std::string> labels;
        std::unordered_set<std::string> enumCoverage;
        bool coversTrue = false;
        bool coversFalse = false;

        const auto constantKey = [&](Expression* expression) -> std::optional<std::string> {
            if (const auto* boolean = dynamic_cast<BooleanLiteralExpr*>(expression))
                return std::string("bool:") + (boolean->value ? "true" : "false");
            if (const auto* number = dynamic_cast<NumberLiteralExpr*>(expression)) {
                if (IsFloatingLiteral(number->value)) return std::nullopt;
                return "integer:" + std::to_string(std::stoll(number->value));
            }
            if (const auto* character = dynamic_cast<CharLiteralExpr*>(expression))
                return "integer:" + std::to_string(static_cast<unsigned char>(character->value));
            if (const auto* unary = dynamic_cast<PrefixUnaryExpr*>(expression)) {
                if ((unary->op == "+" || unary->op == "-") &&
                    dynamic_cast<NumberLiteralExpr*>(unary->operand.get())) {
                    const auto* number = static_cast<NumberLiteralExpr*>(unary->operand.get());
                    if (IsFloatingLiteral(number->value)) return std::nullopt;
                    const std::int64_t magnitude = std::stoll(number->value);
                    return "integer:" + std::to_string(unary->op == "-" ? -magnitude : magnitude);
                }
            }
            if (dynamic_cast<MemberAccessExpr*>(expression)) {
                const std::string name = ExtractQualifiedName(expression);
                if (!name.empty()) return "enum:" + name;
            }
            return std::nullopt;
        };

        for (auto& branch : stmt->cases) {
            keepLifetimes = base;
            valueFlow = baseValues;
            flowTerminated = false;
            const Result label = Evaluate(branch.label.get());
            if (!IsAssignable(selector.type, label.type))
                Report("case label has type '" + label.type + "', expected '" + selector.type + "'",
                    "E_SWITCH_CASE_TYPE");
            auto key = constantKey(branch.label.get());
            if (dynamic_cast<MemberAccessExpr*>(branch.label.get())) {
                if (const Symbol* symbol = table.Get(label.symbol)) key = "enum:" + symbol->name;
            }
            if (!key) {
                Report("case label must be a compile-time bool, integer, char, or enum constant",
                    "E_SWITCH_CASE_CONSTANT");
            }
            else if (!labels.insert(*key).second) {
                // The key is an internal encoding -- "integer:1", "enum:Color.Red"
                // -- that exists to make two spellings of the same constant
                // compare equal. Reporting it verbatim showed the user a prefix
                // their program never mentions.
                const size_t separator = key->find(':');
                Report("duplicate case label '" +
                    (separator == std::string::npos ? *key : key->substr(separator + 1)) + "'",
                    "E_DUPLICATE_SWITCH_CASE");
            }
            else if (*key == "bool:true") coversTrue = true;
            else if (*key == "bool:false") coversFalse = true;
            else if (key->starts_with("enum:")) enumCoverage.insert(key->substr(5));

            AcceptIfPresent(branch.body, *this);
            if (!flowTerminated) {
                continuingPaths.push_back(keepLifetimes);
                continuingValuePaths.push_back(valueFlow);
            }
        }

        if (stmt->defaultCase) {
            keepLifetimes = base;
            valueFlow = baseValues;
            flowTerminated = false;
            AcceptIfPresent(stmt->defaultCase, *this);
            if (!flowTerminated) {
                continuingPaths.push_back(keepLifetimes);
                continuingValuePaths.push_back(valueFlow);
            }
        }

        bool exhaustive = stmt->defaultCase != nullptr;
        if (!exhaustive && selector.type == "bool") exhaustive = coversTrue && coversFalse;
        if (!exhaustive && selectsEnum) {
            exhaustive = std::all_of(enumType->second.enumMembers.begin(),
                enumType->second.enumMembers.end(), [&](const std::string& member) {
                    return enumCoverage.contains(selector.type + "." + member);
                });
        }
        if (stmt->exhaustive && !exhaustive) {
            if (selector.type == "bool")
                Report("non-exhaustive match: bool requires both true and false cases",
                    "E_NON_EXHAUSTIVE_MATCH");
            else if (selectsEnum) {
                std::string missing;
                for (const std::string& member : enumType->second.enumMembers) {
                    if (enumCoverage.contains(selector.type + "." + member)) continue;
                    if (!missing.empty()) missing += ", ";
                    missing += selector.type + "." + member;
                }
                Report("non-exhaustive match; missing: " + missing, "E_NON_EXHAUSTIVE_MATCH");
            }
            else Report("non-exhaustive match requires a default branch", "E_NON_EXHAUSTIVE_MATCH");
        }
        if (!exhaustive) {
            continuingPaths.push_back(base);
            continuingValuePaths.push_back(baseValues);
        }
        MergeKeepPaths(base, continuingPaths);
        MergeValueFlowPaths(baseValues, continuingValuePaths);
        flowTerminated = exhaustive && continuingPaths.empty();
        --switchDepth;
    }

    void Analyzer::Visit(ThrowStmt* stmt) {
        usesExceptions = true;
        if (phase == Phase::CollectDeclarations) return;
        if (currentFunctionNoThrow)
            Report("nothrow callable cannot throw", "E_NOTHROW_THROWS");
        if (finallyDepth > 0)
            Report("throw is not allowed inside finally", "E_FINALLY_CONTROL_TRANSFER");
        if (deferDepth > 0)
            Report("throw is not allowed inside defer", "E_DEFER_CONTROL_TRANSFER");
        if (!stmt->value) {
            if (catchDepth == 0) Report("rethrow is only valid inside catch", "E_RETHROW_OUTSIDE_CATCH");
        }
        else {
            const Result exception = Evaluate(stmt->value.get());
            const bool managed = IsManagedPointerType(exception.type);
            const std::string pointee = managed ? PointerPointee(exception.type) : std::string{};
            if (!managed || !IsDerivedFrom(pointee, "Error"))
                Report("throw requires a managed pointer to Error or a derived class",
                    "E_INVALID_THROW_TYPE", exception.symbol);
            else if (!exception.createsManagedOwner &&
                (!exception.referencesManagedOwner ||
                    exception.pointerOwner == InvalidSymbolId ||
                    exception.pointerOwner != exception.symbol))
                Report("throw must transfer an owned exception; a subscriber cannot be thrown",
                    "E_THROW_SUBSCRIBER", exception.symbol);
            else if (!exception.createsManagedOwner &&
                exception.pointerOwner != InvalidSymbolId) {
                for (auto& transferred : exceptionTransferredOwners)
                    transferred.insert(exception.pointerOwner);
            }
        }
        const size_t keepBoundary = lambdaFunctionBoundaries.empty()
            ? 0 : lambdaFunctionBoundaries.back().keepScopeDepth;
        const size_t valueBoundary = lambdaFunctionBoundaries.empty()
            ? 0 : lambdaFunctionBoundaries.back().valueFlowScopeDepth;
        CheckKeepScopesFrom(keepBoundary, "throw");
        CheckTaskScopesFrom(valueBoundary, "throw");
        flowTerminated = true;
    }

    void Analyzer::Visit(TryStmt* stmt) {
        usesExceptions = true;
        if (phase == Phase::CollectDeclarations) return;
        const KeepLifetimeMap base = keepLifetimes;
        const ValueFlowMap baseValues = valueFlow;
        std::vector<KeepLifetimeMap> continuingPaths;
        std::vector<ValueFlowMap> continuingValuePaths;
        exceptionTransferredOwners.emplace_back();

        keepLifetimes = base;
        valueFlow = baseValues;
        flowTerminated = false;
        AcceptIfPresent(stmt->body, *this);
        if (!flowTerminated) {
            continuingPaths.push_back(keepLifetimes);
            continuingValuePaths.push_back(valueFlow);
        }
        const std::unordered_set<SymbolId> transferredOwners =
            std::move(exceptionTransferredOwners.back());
        exceptionTransferredOwners.pop_back();

        std::vector<std::string> previousCatchTypes;
        for (auto& clause : stmt->catches) {
            keepLifetimes = base;
            valueFlow = baseValues;
            for (const SymbolId owner : transferredOwners) {
                if (auto found = valueFlow.find(owner); found != valueFlow.end())
                    found->second.pointerValidity = PointerValidity::Expired;
            }
            flowTerminated = false;
            table.EnterScope();
            PushKeepScope();
            PushValueFlowScope();

            const std::string type = clause.parameter
                ? ResolveDeclaredType(*clause.parameter) : "error";
            const std::string name = clause.parameter
                ? ExtractIdentifier(clause.parameter->name.get()) : std::string{};
            const bool managed = IsManagedPointerType(type);
            const std::string pointee = managed ? PointerPointee(type) : std::string{};
            if (!managed || !IsDerivedFrom(pointee, "Error"))
                Report("catch parameter must be a managed pointer to Error or a derived class",
                    "E_INVALID_CATCH_TYPE");
            for (const std::string& previous : previousCatchTypes) {
                if (managed && IsDerivedFrom(pointee, previous)) {
                    Report("catch for '" + type + "' is unreachable after catch for '" + previous + "*'",
                        "E_UNREACHABLE_CATCH");
                    break;
                }
            }
            if (managed) previousCatchTypes.push_back(pointee);

            SymbolId id = InvalidSymbolId;
            if (name.empty()) Report("catch parameter requires a name", "E_INVALID_CATCH_PARAMETER");
            else if (const auto declared = table.Declare(SymbolKind::Variable, name, type)) {
                id = *declared;
                if (Symbol* symbol = table.Get(id)) symbol->managedOwner = true;
                RegisterFlowSymbol(id, {InitializationState::Initialized,
                    PointerValidity::Live, id, TaskState::NotTask});
            }
            else Report("catch parameter '" + name + "' is already declared",
                "E_DUPLICATE_DECLARATION");
            if (clause.parameter)
                Save(clause.parameter.get(), {id, type, true, false, true,
                    InitializationState::Initialized, PointerValidity::Live, id});

            ++catchDepth;
            AcceptIfPresent(clause.body, *this);
            --catchDepth;
            if (!flowTerminated) {
                continuingPaths.push_back(keepLifetimes);
                continuingValuePaths.push_back(valueFlow);
            }
            PopValueFlowScope();
            PopKeepScope();
            table.ExitScope();
        }

        const bool allPathsTerminate = continuingPaths.empty();
        if (allPathsTerminate) {
            keepLifetimes = base;
            valueFlow = baseValues;
        }
        else {
            MergeKeepPaths(base, continuingPaths);
            MergeValueFlowPaths(baseValues, continuingValuePaths);
        }
        flowTerminated = allPathsTerminate;

        if (stmt->finallyBody) {
            const bool terminatedBeforeFinally = flowTerminated;
            flowTerminated = false;
            ++finallyDepth;
            AcceptIfPresent(stmt->finallyBody, *this);
            --finallyDepth;
            if (!flowTerminated) flowTerminated = terminatedBeforeFinally;
        }
    }

    void Analyzer::Visit(DeferStmt* stmt) {
        if (phase == Phase::CollectDeclarations) return;
        if (functionDepth == 0) {
            Report("defer is only allowed inside a function", "E_DEFER_OUTSIDE_FUNCTION");
            return;
        }
        if (keepScopes.empty() || valueFlowScopes.empty()) {
            Report("defer requires an active lexical scope", "E_DEFER_OUTSIDE_SCOPE");
            return;
        }

        const KeepLifetimeMap beforeKeep = keepLifetimes;
        const ValueFlowMap beforeValues = valueFlow;
        const bool beforeTerminated = flowTerminated;
        flowTerminated = false;
        ++deferDepth;
        AcceptIfPresent(stmt->body, *this);
        --deferDepth;

        for (const auto& [id, before] : beforeKeep) {
            const auto after = keepLifetimes.find(id);
            if (before.state != KeepState::Deleted && after != keepLifetimes.end() &&
                after->second.state == KeepState::Deleted) {
                bool alreadyDeferred = false;
                for (const auto& scope : deferredKeepScopes)
                    alreadyDeferred = alreadyDeferred || scope.contains(id);
                if (alreadyDeferred)
                    Report("raw owner '" + before.name + "' is deleted by more than one defer",
                        "E_DEFER_DOUBLE_CLEANUP", id);
                deferredKeepScopes.back().insert(id);
            }
        }
        for (const auto& [id, before] : beforeValues) {
            const auto after = valueFlow.find(id);
            if (before.taskState == TaskState::Pending && after != valueFlow.end() &&
                after->second.taskState == TaskState::Awaited) {
                bool alreadyDeferred = false;
                for (const auto& scope : deferredTaskScopes)
                    alreadyDeferred = alreadyDeferred || scope.contains(id);
                if (alreadyDeferred)
                    Report("task is awaited by more than one defer",
                        "E_DEFER_DOUBLE_AWAIT", id);
                deferredTaskScopes.back().insert(id);
            }
        }

        keepLifetimes = beforeKeep;
        valueFlow = beforeValues;
        flowTerminated = beforeTerminated;
    }

    void Analyzer::Visit(ForStmt* stmt) {
        if (phase == Phase::CollectDeclarations) return;
        table.EnterScope();
        PushKeepScope();
        PushValueFlowScope();
        AcceptAll(stmt->init, *this);
        if (stmt->condition) {
            const Result condition = Evaluate(stmt->condition.get());
            if (!IsConditionType(condition.type)) Report("for condition must be boolean-compatible",
                "E_CONDITION_TYPE");
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
        if (!IsConditionType(condition.type)) Report("while condition must be boolean-compatible",
            "E_CONDITION_TYPE");
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
        if (!IsConditionType(condition.type)) Report("do-while condition must be boolean-compatible",
            "E_CONDITION_TYPE");
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
        bool isArray = iterable.type.ends_with("[]");
        std::string elementType = "error";
        
        if (isArray) {
            if (ArrayRank(iterable.type) != 1 && iterable.type != "error")
                Report("for-each currently requires a one-dimensional array or slice",
                    "E_FOREACH_SOURCE_RANK");
            else if (iterable.type != "error") elementType = ArrayElementType(iterable.type);
        } else if (iterable.type != "error") {
            const auto iterateMembers = FindMembers(iterable.type, "iterate");
            const auto iterateMethod = std::find_if(iterateMembers.begin(), iterateMembers.end(),
                [](const MemberSignature& m) { return m.kind == SymbolKind::Method; });
            
            if (iterateMethod == iterateMembers.end()) {
                Report("for-each source '" + iterable.type + "' requires an 'iterate()' method or must be an array",
                    "E_FOREACH_SOURCE_NOT_ITERABLE");
            } else {
                if (iterateMethod->parameterTypes.size() > 0)
                    Report("'iterate' method on '" + iterable.type + "' must take 0 arguments",
                        "E_ITERATE_SIGNATURE");
                std::string iteratorType = iterateMethod->type;
                
                const auto nextMembers = FindMembers(iteratorType, "next");
                const auto nextMethod = std::find_if(nextMembers.begin(), nextMembers.end(),
                    [](const MemberSignature& m) { return m.kind == SymbolKind::Method; });
                if (nextMethod == nextMembers.end())
                    Report("iterator '" + iteratorType + "' requires a 'next()' method",
                        "E_ITERATOR_MISSING_NEXT");
                else if (nextMethod->type != "bool")
                    Report("'next()' method on '" + iteratorType + "' must return bool",
                        "E_ITERATOR_NEXT_RESULT");
                else if (nextMethod->parameterTypes.size() > 0)
                    Report("'next()' method on '" + iteratorType + "' must take 0 arguments",
                        "E_ITERATOR_NEXT_SIGNATURE");
                
                const auto valueMembers = FindMembers(iteratorType, "value");
                const auto valueProperty = std::find_if(valueMembers.begin(), valueMembers.end(),
                    [](const MemberSignature& m) { return m.kind == SymbolKind::Property; });
                const auto valueMethod = std::find_if(valueMembers.begin(), valueMembers.end(),
                    [](const MemberSignature& m) { return m.kind == SymbolKind::Method; });
                
                if (valueProperty != valueMembers.end()) {
                    elementType = valueProperty->type;
                } else if (valueMethod != valueMembers.end()) {
                    if (valueMethod->parameterTypes.size() > 0)
                        Report("'value()' method on '" + iteratorType + "' must take 0 arguments",
                            "E_ITERATOR_VALUE_SIGNATURE");
                    elementType = valueMethod->type;
                } else {
                    Report("iterator '" + iteratorType + "' requires a 'value' property or 'value()' method",
                        "E_ITERATOR_MISSING_VALUE");
                }
            }
        }
        
        AcceptIfPresent(stmt->var, *this);
        if (stmt->var) {
            if (const ExpressionInfo* variable = GetExpressionInfo(*stmt->var)) {
                if (!IsAssignable(variable->type, elementType))
                    Report("for-each variable has type '" + variable->type +
                        "', expected '" + elementType + "'", "E_FOREACH_VARIABLE_TYPE");
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
        if (finallyDepth > 0)
            Report("continue is not allowed inside finally", "E_FINALLY_CONTROL_TRANSFER");
        if (deferDepth > 0)
            Report("continue is not allowed inside defer", "E_DEFER_CONTROL_TRANSFER");
        if (loopDepth == 0) Report("continue statement is outside a loop",
            "E_CONTINUE_OUTSIDE_LOOP");
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
        if (finallyDepth > 0)
            Report("break is not allowed inside finally", "E_FINALLY_CONTROL_TRANSFER");
        if (deferDepth > 0)
            Report("break is not allowed inside defer", "E_DEFER_CONTROL_TRANSFER");
        // Naming the switch explicitly, because a C habit puts break at the end
        // of every arm and "outside a loop" does not explain why it is refused.
        if (loopDepth == 0)
            Report(switchDepth > 0
                ? std::string("break statement is outside a loop; switch and match cases "
                    "do not fall through, so break is unnecessary")
                : std::string("break statement is outside a loop"), "E_BREAK_OUTSIDE_LOOP");
        else {
            CheckKeepScopesFrom(loopKeepDepths.back(), "break");
            CheckTaskScopesFrom(loopKeepDepths.back(), "break");
            loopBreakStates.back().push_back(keepLifetimes);
            loopBreakValueStates.back().push_back(valueFlow);
            flowTerminated = true;
        }
    }

    void Analyzer::Visit(TypeAliasStmt* stmt) {
        const std::string name = Qualify(stmt->name);
        if (phase == Phase::CollectTypeNames) {
            if (types.contains(name) || typeAliases.contains(name)) {
                Report("type or alias '" + name + "' is already declared", "E_DUPLICATE_TYPE_ALIAS");
                return;
            }
            typeAliases.emplace(name, TypeAliasDefinition{
                stmt->target.get(), currentNamespace, {}});
            if (!table.Declare(SymbolKind::Type, name, name))
                Report("object '" + name + "' is already declared in this scope",
                    "E_DUPLICATE_TYPE_ALIAS");
            return;
        }
        if (phase == Phase::CollectDeclarations) return;
        ValidateAttributes(*stmt, "type alias", false);
        if (!stmt->modifiers.empty())
            Report("type alias '" + name + "' cannot have modifiers", "E_TYPE_ALIAS_MODIFIER");
        const std::string target = ResolveTypeAlias(name);
        if (target == "error" || !IsKnownType(target))
            Report("type alias '" + name + "' refers to unknown type '" + target + "'",
                "E_UNKNOWN_ALIAS_TARGET");
    }

    void Analyzer::Visit(ImportStmt* stmt) {
        if (phase == Phase::ResolveBodies && !stmt->attributes.empty())
            ValidateAttributes(*stmt, "import declaration", false);

        std::string clean = stmt->target;
        if (stmt->isFile) {
            if (clean.starts_with("./")) clean = clean.substr(2);
            while (clean.starts_with("../")) clean = clean.substr(3);
            if (clean.ends_with(".abs")) clean = clean.substr(0, clean.size() - 4);
            std::replace(clean.begin(), clean.end(), '/', '.');
            std::replace(clean.begin(), clean.end(), '\\', '.');
        }

        std::vector<std::string> candidates;
        candidates.push_back(clean);
        std::string curr = clean;
        while (true) {
            size_t dotPos = curr.rfind('.');
            if (dotPos == std::string::npos) break;
            curr = curr.substr(0, dotPos);
            if (!curr.empty()) candidates.push_back(curr);
        }

        for (const std::string& ns : candidates) {
            importedNamespaces.insert(ns);
        }

        if (phase == Phase::ResolveBodies) {
            bool foundAny = stmt->isFile;
            if (!foundAny) {
                for (const std::string& ns : candidates) {
                    if (namespaces.contains(ns)) {
                        foundAny = true;
                        break;
                    }
                }
            }
            if (!foundAny) {
                Report("unknown imported namespace '" + stmt->target + "'",
                    "E_UNKNOWN_NAMESPACE");
            }
        }
    }

    void Analyzer::Visit(NamespaceDeclStmt* stmt) {
        if (phase == Phase::ResolveBodies && !stmt->attributes.empty())
            ValidateAttributes(*stmt, "namespace declaration", false);
        const std::string oldNamespace = currentNamespace;
        const std::string namespaceName = Qualify(stmt->name);
        if (phase == Phase::CollectTypeNames && namespaces.insert(namespaceName).second) {
            if (!table.Declare(SymbolKind::Namespace, namespaceName, namespaceName))
                Report("object '" + namespaceName + "' is already declared in this scope",
                    "E_DUPLICATE_DECLARATION");
        }
        currentNamespace = namespaceName;
        if (stmt->body) {
            if (phase == Phase::CollectTypeNames) {
                for (const auto& statement : stmt->body->statements)
                    if (statement) CollectTypeName(*statement);
            }
            else AcceptAll(stmt->body->statements, *this);
        }
        currentNamespace = oldNamespace;
    }

    void Analyzer::Visit(OpaquePluginStmt* stmt) {
        if (phase != Phase::ResolveBodies || !stmt) return;
        ValidateAttributes(*stmt, "plugin declaration", false);
        if (!stmt->node.vtable || !stmt->node.vtable->validate) return;
        OpaqueAttributeViews attributeViews(*stmt);
        AbsoluteOpaqueValidationContextV1 context{
            ABSOLUTE_SYNTAX_PLUGIN_ABI_VERSION,
            static_cast<uint32_t>(functionDepth),
            currentNamespace.c_str(),
            attributeViews.attributes.size(),
            attributeViews.attributes.empty() ? nullptr : attributeViews.attributes.data()
        };
        const char* errorMessage = nullptr;
        int32_t valid = 0;
        try {
            valid = stmt->node.vtable->validate(stmt->node.payload, &context, &errorMessage);
        }
        catch (const std::exception& error) {
            Report("opaque plugin '" + stmt->pluginName + "' validator threw: " + error.what(),
                "E_OPAQUE_PLUGIN_VALIDATE");
            return;
        }
        catch (...) {
            Report("opaque plugin '" + stmt->pluginName + "' validator threw an unknown exception",
                "E_OPAQUE_PLUGIN_VALIDATE");
            return;
        }
        if (!valid)
            Report("opaque plugin '" + stmt->pluginName + "' rejected '" + stmt->keyword +
                "': " + (errorMessage ? errorMessage : "validation failed"),
                "E_OPAQUE_PLUGIN_VALIDATE");
    }
}
