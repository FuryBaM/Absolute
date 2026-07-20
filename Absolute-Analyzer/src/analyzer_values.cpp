#include "analyzer_internal.h"

namespace Absolute {
    void Analyzer::Visit(AssignmentExpr* expr) {
        const AccessMode previousAccess = accessMode;
        accessMode = expr->op == "=" ? AccessMode::Write : AccessMode::Read;
        const Result target = Evaluate(expr->target.get());
        accessMode = previousAccess;
        const Result value = EvaluateExpected(expr->value.get(), target.type);
        if (!target.isLValue) Report("assignment target is not assignable");
        if (ArrayRank(target.type) > 0 &&
            dynamic_cast<MemberAccessExpr*>(expr->target.get()) == nullptr)
            Report("array variables cannot be reassigned; assign an element or declare a new array view");
        if (ArrayRank(target.type) > 0 && expr->op != "=")
            Report("array fields only support direct '=' assignment");
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
        const auto members = FindMembers(base.type, expr->member);
        const auto field = std::find_if(members.begin(), members.end(), [](const MemberSignature& member) {
            return member.kind == SymbolKind::Field;
        });
        if (field == members.end()) {
            Report("type '" + base.type + "' has no member '" + expr->member + "'");
            Save(expr, {InvalidSymbolId, "error", false});
            return;
        }
        callable = false;
        callableParameters.clear();
        Save(expr, {field->symbol, field->type, true});
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
        if (const auto found = types.find(constructedType);
            found != types.end() && found->second.kind == TypeKind::Interface)
            Report("cannot instantiate interface '" + constructedType + "'");
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
        else if (types[type].kind == TypeKind::Interface)
            Report("interface '" + type + "' must be used through raw or managed pointer");
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

}
