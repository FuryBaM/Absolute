#include "analyzer_internal.h"

namespace Absolute {
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
            const auto generic = types.find(type);
            if (phase == Phase::ResolveBodies && generic != types.end() &&
                !generic->second.genericParameters.empty()) {
                Report("generic type '" + type + "' requires type arguments",
                    "E_GENERIC_TYPE_ARGUMENTS_REQUIRED");
                Save(expr, {InvalidSymbolId, "error", false});
            }
            else {
                if (phase == Phase::ResolveBodies && !IsKnownType(type))
                    Report("unknown type '" + expr->name + "'");
                Save(expr, {InvalidSymbolId, type, false});
            }
            return;
        }
        SymbolId id = LookupSymbol(expr->name);
        const Symbol* symbol = table.Get(id);
        std::string expectedReturn;
        std::vector<std::string> expectedParameters;
        if (ParseFunctionType(expectedType, expectedReturn, expectedParameters)) {
            const std::vector<SymbolId> candidates = FindFunctionCandidates(expr->name);
            SymbolId matching = InvalidSymbolId;
            for (SymbolId candidateId : candidates) {
                const Symbol* candidate = table.Get(candidateId);
                if (!candidate || candidate->kind != SymbolKind::Function ||
                    !candidate->genericParameters.empty() ||
                    candidate->type != expectedReturn ||
                    candidate->parameterTypes != expectedParameters) continue;
                if (matching != InvalidSymbolId) {
                    Report("function value '" + expr->name + "' is ambiguous",
                        "E_FUNCTION_VALUE_AMBIGUOUS");
                    matching = InvalidSymbolId;
                    break;
                }
                matching = candidateId;
            }
            if (matching != InvalidSymbolId) {
                id = matching;
                symbol = table.Get(id);
            }
        }
        if (!symbol) {
            Report("unknown object '" + expr->name + "'");
            Save(expr, {InvalidSymbolId, "error", false});
            return;
        }
        if (currentMethodStatic &&
            (symbol->kind == SymbolKind::Field || symbol->kind == SymbolKind::Property) &&
            !symbol->isStatic)
            Report("static method cannot access instance member '" + expr->name + "'",
                "E_STATIC_INSTANCE_ACCESS", id);
        if ((symbol->kind == SymbolKind::Field || symbol->kind == SymbolKind::Property ||
            symbol->kind == SymbolKind::Method) &&
            !symbol->memberOwner.empty())
            RequireAccess(symbol->kind == SymbolKind::Property && accessMode == AccessMode::Write
                ? symbol->writeAccess
                : (symbol->kind == SymbolKind::Property ? symbol->readAccess : symbol->access),
                symbol->memberOwner, expr->name, id);
        if (symbol->kind == SymbolKind::Property) {
            if (accessMode == AccessMode::Read && !symbol->canRead)
                Report("property '" + expr->name + "' is write-only", "E_PROPERTY_WRITE_ONLY", id);
            else if (accessMode == AccessMode::Write && !symbol->canWrite)
                Report("property '" + expr->name + "' is read-only", "E_PROPERTY_READ_ONLY", id);
            else if (accessMode == AccessMode::Address || accessMode == AccessMode::Delete)
                Report("property '" + expr->name + "' has no addressable storage",
                    "E_PROPERTY_NOT_ADDRESSABLE", id);
        }
        bool capturedByCurrentLambda = false;
        const auto recordCapture = [&](LambdaContext& context, SymbolId captureId,
            const Symbol& captured) {
            if (captured.scopeDepth == 0 || captured.scopeDepth >= context.scopeDepth)
                return false;
            if (context.capturedSymbols.contains(captureId)) return true;

            std::string closureReturn;
            std::vector<std::string> closureParameters;
            const bool nestedFunction = ParseFunctionType(
                captured.type, closureReturn, closureParameters);
            if (!nestedFunction && (IsPointerType(captured.type) ||
                IsTaskType(captured.type) || ArrayRank(captured.type) > 0 ||
                TypeOwnsResources(captured.type))) {
                Report("lambda cannot safely capture resource value '" + captured.name +
                    "' of type '" + captured.type + "' by value",
                    "E_LAMBDA_CAPTURE_RESOURCE", captureId);
                context.capturedSymbols.insert(captureId);
                return true;
            }
            context.capturedSymbols.insert(captureId);
            context.captures.push_back({captureId, captured.name, captured.type});
            return true;
        };

        if (!lambdaContexts.empty()) {
            if (symbol->kind == SymbolKind::Variable ||
                symbol->kind == SymbolKind::Parameter) {
                for (LambdaContext& context : lambdaContexts)
                    if (recordCapture(context, id, *symbol) &&
                        &context == &lambdaContexts.back())
                        capturedByCurrentLambda = true;
            }
            else if ((symbol->kind == SymbolKind::Field ||
                symbol->kind == SymbolKind::Property) && !symbol->isStatic) {
                const SymbolId thisId = table.Lookup("this");
                const Symbol* thisSymbol = table.Get(thisId);
                if (thisSymbol) {
                    for (LambdaContext& context : lambdaContexts)
                        if (recordCapture(context, thisId, *thisSymbol) &&
                            &context == &lambdaContexts.back())
                            capturedByCurrentLambda = true;
                }
            }
        }
        if (capturedByCurrentLambda && accessMode != AccessMode::Read)
            Report("captured value '" + expr->name +
                "' is immutable; captures use by-value semantics",
                "E_LAMBDA_CAPTURE_MUTATION", id);
        const bool functionValue = symbol->kind == SymbolKind::Function &&
            symbol->genericParameters.empty() && !symbol->externalFunction &&
            !symbol->exportedFunction;
        if (symbol->kind == SymbolKind::Function &&
            (symbol->externalFunction || symbol->exportedFunction))
            Report("C ABI function '" + expr->name +
                "' cannot be stored in an Absolute func value", "E_FUNCTION_VALUE_C_ABI", id);
        const bool value = functionValue || symbol->kind == SymbolKind::Variable || symbol->kind == SymbolKind::Parameter ||
            symbol->kind == SymbolKind::Field || symbol->kind == SymbolKind::Property;
        if (!value) Report("object '" + expr->name + "' is not a value");
        ValueFlowState flow;
        if (const auto found = valueFlow.find(id); found != valueFlow.end()) flow = found->second;
        if (value && symbol->kind != SymbolKind::Property && accessMode == AccessMode::Read) {
            if (flow.initialization == InitializationState::Uninitialized)
                Report("object '" + expr->name + "' is read before initialization",
                    "E_UNINITIALIZED_READ", id);
            else if (flow.initialization == InitializationState::MaybeUninitialized)
                Report("object '" + expr->name + "' is not initialized on every control-flow path",
                    "E_MAYBE_UNINITIALIZED_READ", id);
        }
        const std::string resolvedType = functionValue
            ? FunctionTypeName(symbol->type, symbol->parameterTypes) : symbol->type;
        Save(expr, {id, resolvedType,
            functionValue ? false :
                (capturedByCurrentLambda ? false :
                    (symbol->kind == SymbolKind::Property ? symbol->canWrite : value)), false,
            value && symbol->kind == SymbolKind::Variable &&
                IsManagedPointerType(symbol->type) && symbol->managedOwner,
            flow.initialization, flow.pointerValidity, flow.pointerOwner,
            flow.taskState});
    }

    void Analyzer::Visit(FunctionCallExpr* expr) {
        std::vector<std::string> explicitTypeArguments;
        if (auto* specialization = dynamic_cast<TemplateExpr*>(expr->base.get())) {
            explicitTypeArguments.reserve(specialization->types.size());
            for (const auto& type : specialization->types)
                explicitTypeArguments.push_back(ResolveType(type.get()));
        }
        if (constructorContextDepth > 0) {
            const std::string typeName = ExtractIdentifier(expr->base.get());
            if (!IsKnownType(typeName) || !types.contains(typeName))
                Report("unknown constructible type '" + typeName + "'");
            const auto found = types.find(typeName);
            const std::vector<std::string> expected = found != types.end() && found->second.constructor
                ? found->second.constructor->parameterTypes : std::vector<std::string>{};
            if (found != types.end() && found->second.constructor)
                RequireAccess(found->second.constructor->access, typeName,
                    typeName, found->second.constructor->symbol);
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

        const Symbol* valueSymbol = nullptr;
        if (probe.identifierExpr) {
            const Symbol* candidate = table.Get(LookupSymbol(probe.identifierExpr->name));
            if (candidate && (candidate->kind == SymbolKind::Variable ||
                candidate->kind == SymbolKind::Parameter)) valueSymbol = candidate;
        }
        if (!probe.isMember && (valueSymbol || dynamic_cast<LambdaExpr*>(expr->base.get()))) {
            const Result callableValue = Evaluate(expr->base.get());
            std::string returnType;
            std::vector<std::string> parameterTypes;
            if (!ParseFunctionType(callableValue.type, returnType, parameterTypes)) {
                Report("expression of type '" + callableValue.type + "' is not callable",
                    "E_NOT_CALLABLE");
                Save(expr, {InvalidSymbolId, "error", false});
                return;
            }
            if (expr->arguments.size() != parameterTypes.size())
                Report("function value expects " + std::to_string(parameterTypes.size()) +
                    " argument(s), got " + std::to_string(expr->arguments.size()),
                    "E_FUNCTION_VALUE_ARITY");
            for (size_t index = 0; index < expr->arguments.size(); ++index) {
                const std::string expected = index < parameterTypes.size()
                    ? parameterTypes[index] : std::string{};
                const Result argument = EvaluateExpected(expr->arguments[index].get(), expected);
                if (!expected.empty() && !IsAssignable(expected, argument.type))
                    Report("function value argument " + std::to_string(index + 1) +
                        " has type '" + argument.type + "', expected '" + expected + "'",
                        "E_FUNCTION_VALUE_ARGUMENT");
            }
            Save(expr, {callableValue.symbol, returnType, false,
                IsManagedPointerType(returnType), false});
            expressionInfo[expr].parameterTypes = parameterTypes;
            return;
        }

        if (!probe.isMember && callName == "base") {
            for (const auto& argument : expr->arguments) Evaluate(argument.get());
            Report(currentConstructor
                ? "base(...) must be the first statement of a constructor"
                : "base(...) is only allowed as the first statement of a constructor",
                currentConstructor ? "E_BASE_CALL_POSITION" : "E_BASE_CALL_OUTSIDE_CONSTRUCTOR");
            Save(expr, {InvalidSymbolId, "error", false});
            return;
        }

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

            if (callName == "load") {
                if (arguments.size() != 1) {
                    Report("load expects exactly one library path", "E_LOAD_ARGUMENT_COUNT");
                }
                else if (arguments.front().type != "string" && arguments.front().type != "error") {
                    Report("load library path must be a string, got '" +
                        arguments.front().type + "'", "E_LOAD_ARGUMENT_TYPE");
                }
                Save(expr, {table.Lookup(callName), "bool", false});
                return;
            }

            if (callName == "move") {
                if (arguments.size() != 1) {
                    Report("move expects exactly one argument");
                    Save(expr, {table.Lookup(callName), "error", false});
                    return;
                }
                const Result& argument = arguments.front();
                if (!argument.isLValue && argument.type != "error") {
                    Report("move expects an lvalue argument");
                }
                if (argument.symbol != InvalidSymbolId) {
                    if (auto flow = valueFlow.find(argument.symbol); flow != valueFlow.end()) {
                        flow->second.initialization = InitializationState::Uninitialized;
                        flow->second.pointerValidity = PointerValidity::MaybeNull;
                    }
                }
                Result result = {table.Lookup(callName), argument.type, false};
                result.isMoveResult = true;
                Save(expr, result);
                return;
            }

            if (callName == "copy") {
                if (arguments.size() != 1) {
                    Report("copy expects exactly one array or slice argument", "E_COPY_ARGUMENT_COUNT");
                    Save(expr, {table.Lookup(callName), "error", false});
                    return;
                }
                if (ArrayRank(arguments.front().type) == 0 && arguments.front().type != "error") {
                    Report("copy expects an array or slice, got '" + arguments.front().type + "'",
                        "E_COPY_ARGUMENT_TYPE");
                    Save(expr, {table.Lookup(callName), "error", false});
                    return;
                }
                Save(expr, {table.Lookup(callName), arguments.front().type, false});
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

        std::vector<Result> arguments;
        arguments.reserve(expr->arguments.size());
        SymbolId symbolId = InvalidSymbolId;
        Result receiver;
        bool hasReceiver = false;

        const std::vector<SymbolId> qualifiedCandidates = FindFunctionCandidates(qualifiedCallName);
        if (!qualifiedCandidates.empty()) {
            for (const auto& argument : expr->arguments) arguments.push_back(Evaluate(argument.get()));
            symbolId = SelectOverload(
                qualifiedCandidates, arguments, qualifiedCallName, explicitTypeArguments);
        }
        else if (auto* memberCall = dynamic_cast<MemberAccessExpr*>(expr->base.get())) {
            const std::string ownerName = ResolveTypeReference(
                ExtractQualifiedName(memberCall->base.get()));
            const bool typeReceiver = types.contains(ownerName);
            if (!typeReceiver) {
                receiver = Evaluate(memberCall->base.get());
                hasReceiver = true;
            }
            for (const auto& argument : expr->arguments) arguments.push_back(Evaluate(argument.get()));

            const auto members = FindMembers(typeReceiver ? ownerName : receiver.type, memberCall->member);
            std::vector<SymbolId> methodCandidates;
            std::vector<std::vector<std::string>> candidateSignatures;
            for (const MemberSignature& member : members) {
                if (member.kind == SymbolKind::Method && member.isStatic == typeReceiver &&
                    std::find(candidateSignatures.begin(), candidateSignatures.end(),
                        member.parameterTypes) == candidateSignatures.end()) {
                    methodCandidates.push_back(member.symbol);
                    candidateSignatures.push_back(member.parameterTypes);
                }
            }
            if (!methodCandidates.empty()) {
                symbolId = SelectOverload(
                    methodCandidates, arguments, memberCall->member, explicitTypeArguments);
            }
            else if (std::any_of(members.begin(), members.end(), [&](const MemberSignature& member) {
                return member.kind == SymbolKind::Method && member.isStatic != typeReceiver;
            })) {
                Report(typeReceiver
                    ? "instance method '" + memberCall->member + "' requires an object"
                    : "static method '" + memberCall->member + "' requires a type receiver",
                    typeReceiver ? "E_INSTANCE_MEMBER_ON_TYPE" : "E_STATIC_MEMBER_ON_OBJECT");
            }
            else {
                const auto extensions = typeReceiver
                    ? std::vector<SymbolId>{} : FindExtensionCandidates(memberCall->member);
                if (!extensions.empty()) {
                    std::vector<Result> extensionArguments;
                    extensionArguments.reserve(arguments.size() + 1);
                    extensionArguments.push_back(receiver);
                    extensionArguments.insert(extensionArguments.end(), arguments.begin(), arguments.end());
                    symbolId = SelectOverload(
                        extensions, extensionArguments, memberCall->member, explicitTypeArguments);
                }
                else {
                    Report("type '" + (typeReceiver ? ownerName : receiver.type) +
                        "' has no method '" + memberCall->member + "'");
                }
            }
        }
        else {
            for (const auto& argument : expr->arguments) arguments.push_back(Evaluate(argument.get()));
            std::vector<SymbolId> candidates = FindFunctionCandidates(callName);
            if (candidates.empty() && !currentType.empty()) {
                for (const MemberSignature& member : FindMembers(currentType, callName))
                    if (member.kind == SymbolKind::Method && (member.isStatic || !currentMethodStatic))
                        candidates.push_back(member.symbol);
            }
            if (candidates.empty()) Report("unknown function '" + callName + "'");
            else symbolId = SelectOverload(candidates, arguments, callName, explicitTypeArguments);
        }

        const Symbol* selected = table.Get(symbolId);
        if (selected && selected->kind == SymbolKind::Method)
            RequireAccess(selected->access, selected->memberOwner, selected->name, selected->id);
        const std::string returnType = selected ? selected->type : "error";
        const bool asyncCall = selected && selected->asyncFunction;
        if (currentMethodConst && selected && selected->kind == SymbolKind::Method &&
            !selected->isStatic &&
            !selected->isConst) {
            Report("const method cannot call non-const method '" + selected->name + "'",
                "E_CONST_METHOD_CALL", selected->id);
        }
        for (size_t i = 0; i < arguments.size(); ++i) {
            const Result& argument = arguments[i];
            const size_t parameterIndex = i +
                (selected && selected->extensionFunction && hasReceiver ? 1 : 0);
            if (selected && parameterIndex < selected->parameterTypes.size()) {
                const std::string& parameterType = selected->parameterTypes[parameterIndex];
                bool transfersAggregateOwner = argument.isMoveResult;
                if (const Symbol* source = table.Get(argument.symbol)) {
                    transfersAggregateOwner = transfersAggregateOwner ||
                        source->kind == SymbolKind::Function || source->kind == SymbolKind::Method;
                }
                if (ArrayRank(parameterType) == 0 && !IsPointerType(parameterType) &&
                    TypeOwnsResources(parameterType) && !transfersAggregateOwner) {
                    Report("resource-owning aggregate argument '" + parameterType +
                        "' cannot be copied by value; use move(...) for lvalues",
                        "E_RESOURCE_AGGREGATE_ARGUMENT", argument.symbol);
                }
            }
            if (argument.pointerValidity == PointerValidity::Deleted ||
                argument.pointerValidity == PointerValidity::Expired)
                Report("argument " + std::to_string(i + 1) + " passes an invalid pointer",
                    "E_INVALID_POINTER_ARGUMENT", argument.symbol);
            else if (argument.pointerValidity == PointerValidity::MaybeInvalid)
                Report("argument " + std::to_string(i + 1) + " may pass an invalid pointer",
                    "E_MAYBE_INVALID_POINTER_ARGUMENT", argument.symbol);
        }
        if (hasReceiver && (receiver.pointerValidity == PointerValidity::Deleted ||
            receiver.pointerValidity == PointerValidity::Expired))
            Report("extension or method receiver is an invalid pointer",
                "E_INVALID_POINTER_ARGUMENT", receiver.symbol);
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
        std::vector<Result> indexes;
        indexes.reserve(expr->indexes.size());
        for (const auto& index : expr->indexes) {
            if (!index) {
                Report("array access requires an index");
                continue;
            }
            const Result indexResult = Evaluate(index.get());
            indexes.push_back(indexResult);
            if (rank == 0) continue;
            if (!IsInteger(indexResult.type) && indexResult.type != "error")
                Report("array index must be an integer, got '" + indexResult.type + "'");
        }
        if (rank == 0) {
            const auto members = FindMembers(base.type, IndexerMemberName());
            const MemberSignature* selected = nullptr;
            int bestCost = std::numeric_limits<int>::max();
            bool ambiguous = false;
            for (const MemberSignature& member : members) {
                if (member.kind != SymbolKind::Indexer || member.isStatic ||
                    member.parameterTypes.size() != indexes.size()) continue;
                int cost = 0;
                bool compatible = true;
                for (size_t index = 0; index < indexes.size(); ++index) {
                    const int conversion = ConversionCost(
                        member.parameterTypes[index], indexes[index].type);
                    if (conversion < 0) {
                        compatible = false;
                        break;
                    }
                    cost += conversion;
                }
                if (!compatible) continue;
                if (cost < bestCost) {
                    selected = &member;
                    bestCost = cost;
                    ambiguous = false;
                }
                else if (cost == bestCost) ambiguous = true;
            }
            if (!selected) {
                Report("object of type '" + base.type +
                    "' is not an array and has no matching indexer", "E_INDEXER_NOT_FOUND");
                Save(expr, {base.symbol, "error", false});
                return;
            }
            if (ambiguous) {
                Report("indexer access on type '" + base.type + "' is ambiguous",
                    "E_AMBIGUOUS_INDEXER", selected->symbol);
            }
            if (accessMode == AccessMode::Read) {
                if (!selected->canRead)
                    Report("indexer of type '" + base.type + "' is write-only",
                        "E_INDEXER_WRITE_ONLY", selected->symbol);
                else RequireAccess(selected->readAccess, selected->owner,
                    "this", selected->symbol);
            }
            else if (accessMode == AccessMode::Write) {
                if (!selected->canWrite)
                    Report("indexer of type '" + base.type + "' is read-only",
                        "E_INDEXER_READ_ONLY", selected->symbol);
                else RequireAccess(selected->writeAccess, selected->owner,
                    "this", selected->symbol);
            }
            else if (accessMode == AccessMode::Address || accessMode == AccessMode::Delete) {
                Report("indexers have no addressable storage",
                    "E_INDEXER_NOT_ADDRESSABLE", selected->symbol);
            }
            Save(expr, {selected->symbol, selected->type, selected->canWrite});
            expressionInfo[expr].parameterTypes = selected->parameterTypes;
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

}
