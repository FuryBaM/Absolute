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
        const SymbolId id = LookupSymbol(expr->name);
        const Symbol* symbol = table.Get(id);
        if (!symbol) {
            Report("unknown object '" + expr->name + "'");
            Save(expr, {InvalidSymbolId, "error", false});
            return;
        }
        if (currentMethodStatic && symbol->kind == SymbolKind::Field && !symbol->isStatic)
            Report("static method cannot access instance field '" + expr->name + "'",
                "E_STATIC_INSTANCE_ACCESS", id);
        if ((symbol->kind == SymbolKind::Field || symbol->kind == SymbolKind::Method) &&
            !symbol->memberOwner.empty())
            RequireAccess(symbol->access, symbol->memberOwner, expr->name, id);
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
            for (const MemberSignature& member : members)
                if (member.kind == SymbolKind::Method && member.isStatic == typeReceiver)
                    methodCandidates.push_back(member.symbol);
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
                    if (member.kind == SymbolKind::Method && member.isStatic)
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

}
