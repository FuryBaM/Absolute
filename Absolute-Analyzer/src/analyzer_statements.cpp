#include "analyzer_internal.h"

namespace Absolute {
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
        else if (!currentType.empty() && types[currentType].kind == TypeKind::Interface) {
            const std::string returnType = ResolveType(stmt->returnType.get());
            if (!IsKnownType(returnType))
                Report("unknown return type '" + returnType + "' of interface method '" +
                    currentType + "." + stmt->name->value + "'");
            for (const auto& parameter : stmt->parameters) {
                const std::string parameterType = ResolveDeclaredType(*parameter);
                if (!IsKnownType(parameterType))
                    Report("unknown parameter type '" + parameterType + "' of interface method '" +
                        currentType + "." + stmt->name->value + "'");
                if (parameter->value)
                    Report("interface methods cannot declare default parameter values");
            }
            if (stmt->body) Report("interface methods cannot have a body");
            if (HasModifier(*stmt, "static") || HasModifier(*stmt, "extension") ||
                HasModifier(*stmt, "async") || HasModifier(*stmt, "override") ||
                HasModifier(*stmt, "sealed"))
                Report("interface method '" + currentType + "." + stmt->name->value +
                    "' has an unsupported modifier");
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

    void Analyzer::Visit(OpaquePluginStmt* stmt) {
        if (phase != Phase::ResolveBodies || !stmt || !stmt->node.vtable ||
            !stmt->node.vtable->validate) return;
        AbsoluteOpaqueValidationContextV1 context{
            ABSOLUTE_SYNTAX_PLUGIN_ABI_VERSION,
            static_cast<uint32_t>(functionDepth),
            currentNamespace.c_str()
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
