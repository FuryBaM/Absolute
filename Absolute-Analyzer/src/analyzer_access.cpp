#include "analyzer_internal.h"

namespace Absolute {
    void Analyzer::Visit(PrimitiveTypeExpr* expr) {
        if (typeContextDepth == 0) Report("type '" + expr->type + "' cannot be used as a value",
            "E_TYPE_USED_AS_VALUE");
        Save(expr, {InvalidSymbolId, expr->type, false});
    }

    void Analyzer::Visit(UserTypeExpr* expr) {
        const std::string type = ResolveType(expr->typeExpr.get());
        Save(expr, {InvalidSymbolId, type, false});
    }

    void Analyzer::Visit(PointerTypeExpr* expr) {
        const std::string pointee = ResolveType(expr->pointee.get());
        // `sub T` -- a qualifier with nothing to apply it to yet. It is kept as
        // written and applied when `T` becomes something, which is the only
        // moment it can be answered: `sub T` at `T = Node*` is `sub Node*`, and
        // at `T = int32` it is `int32`, because a value with no object has no
        // relation to an object's lifetime to weaken.
        if (expr->qualifiesBase) {
            if (phase == Phase::ResolveBodies && !IsOpenGenericParameter(pointee)) {
                std::string written(CanonicalOwnershipPrefix(expr->ownership));
                if (!written.empty()) written.pop_back();
                Report("'" + written + "' must qualify a pointer type or a generic "
                    "parameter, and '" + pointee + "' is neither",
                    "E_QUALIFIER_REQUIRES_POINTER");
                Save(expr, {InvalidSymbolId, pointee, false});
                return;
            }
            Save(expr, {InvalidSymbolId,
                std::string(CanonicalOwnershipPrefix(expr->ownership)) + pointee, false});
            return;
        }
        if (pointee == "void" && !expr->IsRaw())
            Report("managed pointers cannot point to void", "E_MANAGED_POINTER_TO_VOID");
        Save(expr, {InvalidSymbolId,
            CanonicalPointerName(pointee, expr->ownership), false});
    }

    void Analyzer::Visit(ArrayTypeExpr* expr) {
        const std::string element = ResolveType(expr->element.get());
        if (element == "void") Report("array element type cannot be void",
            "E_VOID_ARRAY_ELEMENT");
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
                    Report("unknown type '" + expr->name + "'", "E_UNKNOWN_TYPE");
                Save(expr, {InvalidSymbolId, type, false});
            }
            return;
        }
        SymbolId id = LookupSymbol(expr->name);
        const Symbol* symbol = table.Get(id);
        std::string expectedReturn;
        std::vector<std::string> expectedParameters;
        const bool expectedCFunction = ParseCFunctionType(
            expectedType, expectedReturn, expectedParameters);
        const bool expectedFunction = !expectedCFunction &&
            ParseFunctionType(expectedType, expectedReturn, expectedParameters);
        if (expectedFunction || expectedCFunction) {
            const std::vector<SymbolId> candidates = FindFunctionCandidates(expr->name);
            SymbolId matching = InvalidSymbolId;
            for (SymbolId candidateId : candidates) {
                const Symbol* candidate = table.Get(candidateId);
                if (!candidate || candidate->kind != SymbolKind::Function ||
                    !candidate->genericParameters.empty() ||
                    candidate->type != expectedReturn ||
                    candidate->parameterTypes != expectedParameters) continue;
                if (expectedCFunction &&
                    !(candidate->externalFunction || candidate->exportedFunction))
                    continue;
                if (expectedFunction &&
                    (candidate->externalFunction || candidate->exportedFunction))
                    continue;
                if (matching != InvalidSymbolId) {
                    Report("function value '" + expr->name + "' is ambiguous",
                        expectedCFunction ? "E_CFUNC_VALUE_AMBIGUOUS" : "E_FUNCTION_VALUE_AMBIGUOUS");
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
            Report("unknown object '" + expr->name + "'", "E_UNKNOWN_OBJECT");
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

            if (captured.valueReference) {
                Report("lambda cannot capture value-reference parameter '" + captured.name + "'",
                    "E_VALUE_REF_CAPTURE", captureId);
                context.capturedSymbols.insert(captureId);
                return true;
            }

            std::string closureReturn;
            std::vector<std::string> closureParameters;
            const bool nestedFunction = ParseFunctionType(
                captured.type, closureReturn, closureParameters);
            if (!nestedFunction && (IsPointerType(captured.type) ||
                IsTaskType(captured.type) || ArrayRank(captured.type) > 0 ||
                TypeOwnsUniqueResource(captured.type))) {
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
        const bool cFunctionValue = symbol->kind == SymbolKind::Function &&
            symbol->genericParameters.empty() &&
            (symbol->externalFunction || symbol->exportedFunction);
        const bool functionValue = symbol->kind == SymbolKind::Function &&
            symbol->genericParameters.empty() && !symbol->externalFunction &&
            !symbol->exportedFunction;
        if (functionValue && std::any_of(symbol->parameterTypes.begin(),
            symbol->parameterTypes.end(), [](const std::string& parameter) {
                return IsValueReferenceType(parameter);
            }))
            Report("function with reference parameters cannot be stored in a func value",
                "E_OWNERSHIP_FUNCTION_VALUE", id);
        if (cFunctionValue && !expectedCFunction && !expectedType.empty() &&
            ParseFunctionType(expectedType, expectedReturn, expectedParameters))
            Report("C ABI function '" + expr->name +
                "' cannot be stored in an Absolute func value; use cfunc<...>",
                "E_FUNCTION_VALUE_C_ABI", id);
        const bool value = functionValue || cFunctionValue ||
            symbol->kind == SymbolKind::Variable || symbol->kind == SymbolKind::Parameter ||
            symbol->kind == SymbolKind::Field || symbol->kind == SymbolKind::Property;
        if (!value) Report("object '" + expr->name + "' is not a value",
            "E_OBJECT_NOT_A_VALUE");
        ValueFlowState flow;
        if (const auto found = valueFlow.find(id); found != valueFlow.end()) flow = found->second;
        if (value && symbol->kind != SymbolKind::Property && accessMode == AccessMode::Read) {
            if (!expr->isOutArgument && flow.initialization == InitializationState::Uninitialized)
                Report("object '" + expr->name + "' is read before initialization",
                    "E_UNINITIALIZED_READ", id);
            else if (flow.initialization == InitializationState::MaybeUninitialized)
                Report("object '" + expr->name + "' is not initialized on every control-flow path",
                    "E_MAYBE_UNINITIALIZED_READ", id);
        }
        const std::string resolvedType = cFunctionValue
            ? CFunctionTypeName(symbol->type, symbol->parameterTypes)
            : (functionValue
                ? FunctionTypeName(symbol->type, symbol->parameterTypes) : symbol->type);
        if (symbol->kind == SymbolKind::Property && !capturedByCurrentLambda) {
            Save(expr, AccessorValue(id, resolvedType, symbol->canWrite));
            return;
        }
        Save(expr, {id, resolvedType,
            (functionValue || cFunctionValue) ? false :
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
                Report("unknown constructible type '" + typeName + "'",
                    "E_UNKNOWN_CONSTRUCTIBLE_TYPE");
            std::vector<Result> evaluated;
            evaluated.reserve(expr->arguments.size());
            for (const auto& argument : expr->arguments)
                evaluated.push_back(Evaluate(argument.get()));
            std::vector<MemberSignature> constructors = ConstructorsOf(typeName);
            const MemberSignature* selected = SelectConstructor(
                constructors, evaluated, typeName);
            std::vector<std::string> expected;
            if (selected) {
                RequireAccess(selected->access, typeName, typeName, selected->symbol);
                expected = selected->parameterTypes;
            }
            for (size_t i = 0; i < expr->arguments.size(); ++i) {
                const std::string expectedType = i < expected.size()
                    ? expected[i] : std::string{};
                const std::string expectedValueType = ValueReferenceBaseType(expectedType);
                const Result& argument = evaluated[i];
                if (i < expected.size() && !IsAssignable(expectedValueType, argument.type))
                    Report("constructor argument " + std::to_string(i + 1) + " has type '" + argument.type +
                        "', expected '" + expectedValueType + "'",
                            "E_CONSTRUCTOR_ARGUMENT_TYPE");
                CheckManagedMoveArgument(argument, expectedType, i, "constructor");
                if (IsValueReferenceType(expectedType) &&
                    !IsConstValueReferenceType(expectedType)) {
                    if (!argument.isLValue)
                        Report("mutable reference constructor argument " + std::to_string(i + 1) +
                            " requires an lvalue", "E_VALUE_REF_REQUIRES_LVALUE", argument.symbol);
                    else if (IsConstMutationTarget(expr->arguments[i].get(), argument))
                        Report("mutable reference constructor argument " + std::to_string(i + 1) +
                            " cannot borrow a const value", "E_VALUE_REF_CONST_ARGUMENT", argument.symbol);
                }
            }
            Save(expr, {InvalidSymbolId, typeName.empty() ? "error" : typeName, false});
            if (selected) {
                expressionInfo[expr].parameterTypes = expected;
                expressionInfo[expr].calleeSymbol = selected->symbol;
            }
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
            const bool cFunctionValue = ParseCFunctionType(
                callableValue.type, returnType, parameterTypes);
            if (!cFunctionValue &&
                !ParseFunctionType(callableValue.type, returnType, parameterTypes)) {
                Report("expression of type '" + callableValue.type + "' is not callable",
                    "E_NOT_CALLABLE");
                Save(expr, {InvalidSymbolId, "error", false});
                return;
            }
            if (expr->arguments.size() != parameterTypes.size())
                Report(std::string(cFunctionValue ? "cfunc" : "function value") +
                    " expects " + std::to_string(parameterTypes.size()) +
                    " argument(s), got " + std::to_string(expr->arguments.size()),
                    cFunctionValue ? "E_CFUNC_ARITY" : "E_FUNCTION_VALUE_ARITY");
            for (size_t index = 0; index < expr->arguments.size(); ++index) {
                const std::string expected = index < parameterTypes.size()
                    ? parameterTypes[index] : std::string{};
                const Result argument = EvaluateExpected(expr->arguments[index].get(), expected);
                if (!expected.empty() && !IsAssignable(expected, argument.type))
                    Report(std::string(cFunctionValue ? "cfunc" : "function value") +
                        " argument " + std::to_string(index + 1) +
                        " has type '" + argument.type + "', expected '" + expected + "'",
                        cFunctionValue ? "E_CFUNC_ARGUMENT" : "E_FUNCTION_VALUE_ARGUMENT");
                CheckManagedMoveArgument(argument, expected, index, "function value");
                if (IsTaskType(expected)) {
                    if (argument.taskState == TaskState::Awaited) {
                        Report("task argument " + std::to_string(index + 1) +
                            " has already been awaited or transferred",
                            "E_TASK_ALREADY_CONSUMED", argument.symbol);
                    }
                    if (auto flow = valueFlow.find(argument.symbol);
                        flow != valueFlow.end()) {
                        flow->second.taskState = TaskState::Awaited;
                    }
                }
            }
            Result callResult{callableValue.symbol, returnType, false,
                IsStrongManagedPointerType(returnType), false};
            callResult.createsArrayOwner = ArrayRank(returnType) > 0;
            Save(expr, std::move(callResult));
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

            if (callName == "tuple") {
                if (!explicitTypeArguments.empty())
                    Report("tuple literal does not accept explicit type arguments",
                        "E_TUPLE_TYPE_ARGUMENTS");
                if (arguments.size() < 2) {
                    Report("tuple literal requires at least two values",
                        "E_TUPLE_LITERAL_ARITY");
                    Save(expr, {table.Lookup(callName), "error", false});
                    return;
                }
                std::string tupleType = "tuple<";
                for (size_t index = 0; index < arguments.size(); ++index) {
                    const std::string& element = arguments[index].type;
                    if (element == "void" || element == "auto" ||
                        element == "dynamic")
                        Report("tuple elements require concrete non-void values",
                            "E_TUPLE_ELEMENT_TYPE");
                    if (TypeOwnsUniqueResource(element))
                        Report("tuple element type '" + element +
                            "' owns resources and is not supported yet",
                            "E_TUPLE_RESOURCE_ELEMENT", arguments[index].symbol);
                    if (index) tupleType += ",";
                    tupleType += element;
                }
                tupleType += ">";
                Save(expr, {table.Lookup(callName), std::move(tupleType), false});
                return;
            }

            if (callName == "print" || callName == "println") {
                for (size_t index = 0; index < arguments.size(); ++index) {
                    if (!IsPrintableType(arguments[index].type))
                        Report(callName + " argument " + std::to_string(index + 1) +
                            " has unsupported type '" + arguments[index].type + "'",
                                "E_UNSUPPORTED_ARGUMENT_TYPE");
                }
                Save(expr, {table.Lookup(callName), "void", false});
                return;
            }

            if (callName == "toString") {
                if (arguments.size() != 1) Report("toString expects exactly one argument",
                    "E_TOSTRING_ARGUMENTS");
                else if (!IsPrintableType(arguments.front().type))
                    Report("toString cannot convert type '" + arguments.front().type + "'",
                        "E_TOSTRING_UNSUPPORTED_TYPE");
                Save(expr, {table.Lookup(callName), "string", false});
                return;
            }

            if (callName == "assert") {
                if (arguments.empty() || arguments.size() > 2)
                    Report("assert expects a condition and an optional message",
                        "E_ASSERT_ARGUMENTS");
                else if (!IsConditionType(arguments.front().type))
                    Report("assert condition must be boolean-compatible",
                        "E_ASSERT_CONDITION_TYPE");
                if (arguments.size() == 2 && arguments[1].type != "string" && arguments[1].type != "error")
                    Report("assert message must be a string", "E_ASSERT_MESSAGE_TYPE");
                Save(expr, {table.Lookup(callName), "void", false});
                return;
            }

            if (callName == "debugBreak") {
                if (!arguments.empty())
                    Report("debugBreak expects no arguments",
                        "E_DEBUG_BREAK_ARGUMENT_COUNT");
                Save(expr, {table.Lookup(callName), "void", false});
                return;
            }

            if (callName == "unsafeArrayGet" || callName == "unsafeArraySet") {
                const bool isSet = callName == "unsafeArraySet";
                const size_t expectedCount = isSet ? 3 : 2;
                if (arguments.size() != expectedCount) {
                    Report(callName + " expects exactly " + std::to_string(expectedCount) +
                        " arguments", "E_UNSAFE_ARRAY_ARGUMENT_COUNT");
                    Save(expr, {table.Lookup(callName), isSet ? "void" : "error", false});
                    return;
                }
                const std::string& arrayType = arguments[0].type;
                if (ArrayRank(arrayType) != 1 && arrayType != "error")
                    Report(callName + " requires a one-dimensional array",
                        "E_UNSAFE_ARRAY_TYPE", arguments[0].symbol);
                if (!IsInteger(arguments[1].type) && arguments[1].type != "error")
                    Report(callName + " index must be an integer",
                        "E_UNSAFE_ARRAY_INDEX_TYPE", arguments[1].symbol);
                const std::string elementType = ArrayRank(arrayType) == 1
                    ? ArrayElementType(arrayType) : std::string("error");
                if (isSet && elementType != "error" &&
                    !IsAssignable(elementType, arguments[2].type))
                    Report("unsafeArraySet value has type '" + arguments[2].type +
                        "', expected '" + elementType + "'",
                        "E_UNSAFE_ARRAY_VALUE_TYPE", arguments[2].symbol);
                Save(expr, {table.Lookup(callName), isSet ? "void" : elementType, false});
                return;
            }

            // Reading a slot and clearing it in one step. `unsafeArrayGet`
            // reads without clearing, which for an element that owns something
            // leaves two handles to one object -- the one in the array and the
            // one the caller now holds. A take is what handing an element out
            // of a container actually means.
            if (callName == "unsafeArrayTake") {
                if (arguments.size() != 2) {
                    Report("unsafeArrayTake expects an array and an index",
                        "E_UNSAFE_ARRAY_ARGUMENT_COUNT");
                    Save(expr, {table.Lookup(callName), "error", false});
                    return;
                }
                const std::string& arrayType = arguments[0].type;
                if (ArrayRank(arrayType) != 1 && arrayType != "error")
                    Report("unsafeArrayTake requires a one-dimensional array",
                        "E_UNSAFE_ARRAY_TYPE", arguments[0].symbol);
                if (!IsInteger(arguments[1].type) && arguments[1].type != "error")
                    Report("unsafeArrayTake index must be an integer",
                        "E_UNSAFE_ARRAY_INDEX_TYPE", arguments[1].symbol);
                const std::string elementType = ArrayRank(arrayType) == 1
                    ? ArrayElementType(arrayType) : std::string("error");
                Result taken{table.Lookup(callName), elementType, false};
                // The slot no longer holds it, so what comes back is the owner
                // rather than another handle to the same object.
                taken.createsManagedOwner = IsStrongManagedPointerType(elementType);
                Save(expr, std::move(taken));
                return;
            }

            // Releasing a run of elements and clearing them. What releasing one
            // means is the element type's answer, so for elements that own
            // nothing this is nothing at all.
            if (callName == "unsafeArrayDrop") {
                if (arguments.size() != 3) {
                    Report("unsafeArrayDrop expects an array, a first index and "
                        "an element count", "E_UNSAFE_ARRAY_ARGUMENT_COUNT");
                    Save(expr, {table.Lookup(callName), "void", false});
                    return;
                }
                if (ArrayRank(arguments[0].type) != 1 && arguments[0].type != "error")
                    Report("unsafeArrayDrop requires a one-dimensional array",
                        "E_UNSAFE_ARRAY_TYPE", arguments[0].symbol);
                for (size_t index = 1; index < 3; ++index) {
                    if (!IsInteger(arguments[index].type) && arguments[index].type != "error")
                        Report("unsafeArrayDrop range must be integers",
                            "E_UNSAFE_ARRAY_INDEX_TYPE", arguments[index].symbol);
                }
                Save(expr, {table.Lookup(callName), "void", false});
                return;
            }

            // A bulk element copy. It exists because the alternative -- a
            // loop of unsafeArraySet/unsafeArrayGet -- is what a growing
            // collection does on every reallocation, and a scalar loop is not
            // what copying a block of memory should cost.
            if (callName == "unsafeArrayCopy" || callName == "unsafeArrayMove") {
                // The two differ in one place, and it is the place ownership
                // lives: a copy leaves the source holding what it held, a move
                // does not. Everything else about them is the same block of
                // memory going the same distance.
                const bool transfers = callName == "unsafeArrayMove";
                if (arguments.size() != 3) {
                    Report(callName + " expects a destination, a source and "
                        "an element count", "E_UNSAFE_ARRAY_COPY_ARGUMENT_COUNT");
                    Save(expr, {table.Lookup(callName), "void", false});
                    return;
                }
                for (size_t index = 0; index < 2; ++index) {
                    if (ArrayRank(arguments[index].type) != 1 &&
                        arguments[index].type != "error")
                        Report(callName + " requires one-dimensional arrays",
                            "E_UNSAFE_ARRAY_COPY_TYPE", arguments[index].symbol);
                }
                const std::string destination = ArrayRank(arguments[0].type) == 1
                    ? ArrayElementType(arguments[0].type) : std::string("error");
                const std::string source = ArrayRank(arguments[1].type) == 1
                    ? ArrayElementType(arguments[1].type) : std::string("error");
                if (destination != "error" && source != "error" &&
                    destination != source)
                    Report(callName + " requires the same element type on "
                        "both sides, got '" + destination + "' and '" + source +
                        "'", "E_UNSAFE_ARRAY_COPY_ELEMENT", arguments[1].symbol);
                // Elements that own something are refused: copying the bytes
                // would duplicate that ownership, the same reason `copy` of
                // such an array is refused.
                if (!transfers && destination != "error" && TypeOwnsUniqueResource(destination))
                    Report("unsafeArrayCopy cannot copy elements that own "
                        "something; that would duplicate the ownership -- "
                        "unsafeArrayMove transfers them instead",
                        "E_UNSAFE_ARRAY_COPY_OWNING", arguments[0].symbol);
                // The refusal above cannot see through a generic parameter, so
                // the element type is recorded and asked again per instantiation.
                if (!transfers && ArrayRank(arguments[0].type) == 1)
                    RecordGenericBodyFact(GenericBodyFact::Shape::CopiesElements,
                        destination, callName, expr);
                if (!IsInteger(arguments[2].type) && arguments[2].type != "error")
                    Report(callName + " count must be an integer",
                        "E_UNSAFE_ARRAY_COPY_COUNT", arguments[2].symbol);
                Save(expr, {table.Lookup(callName), "void", false});
                return;
            }

            if (callName == "unsafeArrayData") {
                if (arguments.size() != 1) {
                    Report("unsafeArrayData expects exactly one argument",
                        "E_UNSAFE_ARRAY_DATA_ARGUMENT_COUNT");
                    Save(expr, {table.Lookup(callName), "error", false});
                    return;
                }
                const std::string& arrayType = arguments.front().type;
                if (ArrayRank(arrayType) != 1 && arrayType != "error") {
                    Report("unsafeArrayData requires a one-dimensional array",
                        "E_UNSAFE_ARRAY_DATA_TYPE", arguments.front().symbol);
                    Save(expr, {table.Lookup(callName), "error", false});
                    return;
                }
                const std::string elementType = ArrayRank(arrayType) == 1
                    ? ArrayElementType(arrayType) : std::string("error");
                Result result = {
                    table.Lookup(callName), "raw " + elementType + "*", false};
                result.pointerValidity = PointerValidity::Live;
                // This intrinsic is the explicit unsafe escape hatch. Its
                // lifetime cannot be proven once the raw storage view leaves
                // the array expression, so no safe owner relation is claimed.
                result.pointerOwner = InvalidSymbolId;
                result.pointerRole = PointerRole::RawView;
                Save(expr, result);
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

            if (callName == "isLoaded") {
                if (arguments.size() != 1) {
                    Report("isLoaded expects exactly one library path",
                        "E_IS_LOADED_ARGUMENT_COUNT");
                }
                else if (arguments.front().type != "string" && arguments.front().type != "error") {
                    Report("isLoaded library path must be a string, got '" +
                        arguments.front().type + "'", "E_IS_LOADED_ARGUMENT_TYPE");
                }
                Save(expr, {table.Lookup(callName), "bool", false});
                return;
            }

            if (callName == "loadError") {
                if (!arguments.empty())
                    Report("loadError expects no arguments", "E_LOAD_ERROR_ARGUMENT_COUNT");
                Save(expr, {table.Lookup(callName), "string", false});
                return;
            }

            if (callName == "isOwner") {
                if (arguments.size() != 1) {
                    Report("isOwner expects exactly one argument",
                        "E_IS_OWNER_ARGUMENT_COUNT");
                }
                else if (!ParameterSupportsOwnership(arguments.front().type)) {
                    Report("isOwner requires a managed pointer, array, or "
                        "resource-owning value",
                        "E_IS_OWNER_ARGUMENT_TYPE", arguments.front().symbol);
                }
                Save(expr, {table.Lookup(callName), "bool", false});
                return;
            }

            if (callName == "move") {
                if (arguments.size() != 1) {
                    Report("move expects exactly one argument", "E_MOVE_ARGUMENTS");
                    Save(expr, {table.Lookup(callName), "error", false});
                    return;
                }
                const Result& argument = arguments.front();
                if (!argument.isLValue && argument.type != "error") {
                    Report("move expects an lvalue argument", "E_MOVE_REQUIRES_LVALUE");
                }
                const bool constSource = argument.isLValue &&
                    IsConstMutationTarget(expr->arguments.front().get(), argument);
                if (constSource)
                    Report("move cannot invalidate a const source",
                        "E_MOVE_CONST_SOURCE", argument.symbol);

                Symbol* source = table.Get(argument.symbol);
                if (source && source->rolePolymorphic &&
                    !ownerGuardedParameters.contains(source->id)) {
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
                const bool strongManaged = IsStrongManagedPointerType(argument.type);
                const bool arrayValue = ArrayRank(argument.type) > 0;
                const bool arrayOwner = arrayValue && source && source->ownsArrayStorage;
                const bool managedOwner = strongManaged && source && source->managedOwner;
                if (IsWeakPointerType(argument.type)) {
                    Report("weak managed pointers do not own a resource and cannot be moved",
                        "E_WEAK_MOVE", argument.symbol);
                }
                else if (IsValueReferenceType(argument.type) || (source && (source->valueReference || source->constValueReference))) {
                    Report("reference parameter or alias cannot be moved; move requires an owner place",
                        "E_REF_MOVE", argument.symbol);
                }
                else if (strongManaged && !managedOwner) {
                    Report("managed subscriber cannot be moved; move requires an owner",
                        "E_MANAGED_MOVE_REQUIRES_OWNER", argument.symbol);
                }
                else if (arrayValue && !arrayOwner) {
                    Report("borrowed array or slice cannot be moved; move requires an owning array",
                        "E_ARRAY_MOVE_REQUIRES_OWNER", argument.symbol);
                }
                if (argument.pointerValidity == PointerValidity::Deleted ||
                    argument.pointerValidity == PointerValidity::Expired ||
                    argument.pointerValidity == PointerValidity::MaybeInvalid) {
                    Report("move source is an invalid pointer",
                        "E_MOVE_INVALID_SOURCE", argument.symbol);
                }

                const bool validSource = argument.isLValue && !constSource &&
                    !IsWeakPointerType(argument.type) && (!strongManaged || managedOwner) &&
                    (!arrayValue || arrayOwner);
                if (validSource && argument.symbol != InvalidSymbolId) {
                    if (auto flow = valueFlow.find(argument.symbol); flow != valueFlow.end()) {
                        flow->second.initialization = InitializationState::Uninitialized;
                        flow->second.pointerValidity = PointerValidity::MaybeNull;
                    }
                }
                Result result = {table.Lookup(callName), argument.type, false};
                result.createsManagedOwner = managedOwner;
                result.initialization = InitializationState::Initialized;
                result.pointerValidity = IsPointerType(argument.type)
                    ? argument.pointerValidity : PointerValidity::NotPointer;
                result.pointerOwner = managedOwner
                    ? argument.symbol : argument.pointerOwner;
                result.isMoveResult = true;
                result.createsArrayOwner = arrayOwner;
                Save(expr, result);
                return;
            }


            if (callName == "adoptRaw") {
                if (arguments.empty() || arguments.size() > 2) {
                    Report("adoptRaw expects 1 or 2 arguments: raw T* and optional deleter", "E_ADOPT_RAW_ARGUMENTS");
                    Save(expr, {table.Lookup(callName), "error", false});
                    return;
                }
                const Result& argument = arguments.front();
                if (!IsRawPointerType(argument.type)) {
                    Report("adoptRaw requires a raw pointer 'raw T*'", "E_ADOPT_RAW_TYPE");
                    Save(expr, {table.Lookup(callName), "error", false});
                    return;
                }
                if (argument.symbol != InvalidSymbolId) {
                    if (auto flow = valueFlow.find(argument.symbol); flow != valueFlow.end()) {
                        flow->second.initialization = InitializationState::Uninitialized;
                        flow->second.pointerValidity = PointerValidity::MaybeNull;
                    }
                    if (auto keep = keepLifetimes.find(argument.symbol); keep != keepLifetimes.end()) {
                        keep->second.state = KeepState::Deleted;
                    }
                }
                std::string targetType = PointerPointee(argument.type) + "*";
                Result result = {table.Lookup(callName), targetType, false};
                result.createsManagedOwner = true;
                result.initialization = InitializationState::Initialized;
                result.pointerValidity = PointerValidity::Live;
                Save(expr, result);
                return;
            }

            if (callName == "retainRaw") {
                Report("retainRaw is unavailable in the deterministic unique-ownership "
                    "model; use adoptRaw(rawPointer) for ownership or borrowRaw(rawPointer) "
                    "for a non-owning raw view",
                    "E_RETAIN_RAW_UNSUPPORTED");
                Save(expr, {table.Lookup(callName), "error", false});
                return;
            }

            if (callName == "borrowRaw") {
                if (arguments.size() != 1) {
                    Report("borrowRaw expects exactly 1 argument", "E_BORROW_RAW_ARGUMENTS");
                    Save(expr, {table.Lookup(callName), "error", false});
                    return;
                }
                const Result& argument = arguments.front();
                if (!IsRawPointerType(argument.type)) {
                    Report("borrowRaw requires a raw pointer 'raw T*'", "E_BORROW_RAW_TYPE");
                    Save(expr, {table.Lookup(callName), "error", false});
                    return;
                }
                std::string targetType = argument.type;
                Result result = {table.Lookup(callName), targetType, false};
                result.initialization = InitializationState::Initialized;
                result.pointerValidity = PointerValidity::Live;
                result.pointerRole = PointerRole::RawView;
                Save(expr, result);
                return;
            }

            if (callName == "share") {
                Report("share is unavailable in Absolute's deterministic "
                    "unique-ownership model; transfer ownership with move(...) and "
                    "observe it with weak T*",
                    "E_SHARE_UNSUPPORTED");
                Save(expr, {table.Lookup(callName), "error", false});
                return;
            }

            if (callName == "seal") {
                if (!explicitTypeArguments.empty())
                    Report("seal does not accept explicit type arguments",
                        "E_SEAL_TYPE_ARGUMENTS");
                if (arguments.size() != 1) {
                    Report("seal expects exactly one moved managed owner",
                        "E_SEAL_ARGUMENT_COUNT");
                    Save(expr, {table.Lookup(callName), "raw void*", false});
                    return;
                }
                const Result& argument = arguments.front();
                if (!IsStrongManagedPointerType(argument.type) && argument.type != "error")
                    Report("seal expects a strong managed pointer, got '" +
                        argument.type + "'", "E_SEAL_ARGUMENT_TYPE", argument.symbol);
                else if ((!argument.isMoveResult || !argument.createsManagedOwner) &&
                    argument.type != "error")
                    Report("seal consumes ownership; pass move(owner)",
                        "E_SEAL_REQUIRES_MOVE", argument.symbol);
                Save(expr, {table.Lookup(callName), "raw void*", false, false, false,
                    InitializationState::Initialized, PointerValidity::Live});
                return;
            }

            if (callName == "unseal") {
                if (explicitTypeArguments.size() != 1) {
                    Report("unseal requires exactly one managed pointee type",
                        "E_UNSEAL_TYPE_ARGUMENT_COUNT");
                }
                if (arguments.size() != 1) {
                    Report("unseal expects exactly one raw capsule",
                        "E_UNSEAL_ARGUMENT_COUNT");
                }
                else if (arguments.front().type != "raw void*" &&
                    arguments.front().type != "error") {
                    Report("unseal expects a raw void* capsule, got '" +
                        arguments.front().type + "'", "E_UNSEAL_ARGUMENT_TYPE",
                        arguments.front().symbol);
                }
                const std::string pointee = explicitTypeArguments.size() == 1
                    ? explicitTypeArguments.front() : std::string("error");
                if (pointee == "void" || IsPointerType(pointee) || ArrayRank(pointee) > 0)
                    Report("unseal type argument must be a concrete managed pointee type",
                        "E_UNSEAL_POINTEE_TYPE");
                Save(expr, {table.Lookup(callName), pointee + "*", false, true, false,
                    InitializationState::Initialized, PointerValidity::Live});
                return;
            }

            if (callName == "copy") {
                if (arguments.size() != 1) {
                    Report("copy expects exactly one array, slice, or cloneable value argument",
                        "E_COPY_ARGUMENT_COUNT");
                    Save(expr, {table.Lookup(callName), "error", false});
                    return;
                }

                const Result& source = arguments.front();
                if (ArrayRank(source.type) > 0) {
                    // `copy` duplicates the buffer, not what the bytes in it
                    // refer to. For elements that own something that makes two
                    // arrays holding the same handles, and since an array owns
                    // its elements, whichever is released first destroys the
                    // objects the other still names. Refused rather than
                    // deep-copied: what a deep copy of an owner means is a
                    // decision this builtin does not get to make.
                    std::string element = source.type;
                    while (ArrayRank(element) > 0) element = ArrayElementType(element);
                    if (TypeOwnsUniqueResource(element)) {
                        Report("copy of an array whose elements own something would "
                            "duplicate that ownership; copy the elements one at a time "
                            "into an array you build yourself",
                            "E_COPY_OWNING_ELEMENTS", source.symbol);
                    }
                    Result copied{table.Lookup(callName), source.type, false};
                    copied.createsArrayOwner = true;
                    Save(expr, std::move(copied));
                    return;
                }
                if (source.type == "error") {
                    Save(expr, {table.Lookup(callName), "error", false});
                    return;
                }

                const std::string objectType = IsPointerType(source.type)
                    ? PointerPointee(source.type) : source.type;
                std::string definitionName = objectType;
                std::string genericBase;
                std::vector<std::string> genericArguments;
                if (ParseGenericTypeName(objectType, genericBase, genericArguments))
                    definitionName = genericBase;
                const auto definition = types.find(definitionName);
                if (definition == types.end() ||
                    (definition->second.kind != TypeKind::Struct &&
                        definition->second.kind != TypeKind::Class &&
                        definition->second.kind != TypeKind::Interface)) {
                    Report("copy expects an array, slice, or cloneable aggregate, got '" +
                        source.type + "'", "E_COPY_ARGUMENT_TYPE", source.symbol);
                    Save(expr, {table.Lookup(callName), "error", false});
                    return;
                }
                if (!source.isLValue) {
                    Report("copy of a cloneable value requires a stable lvalue source",
                        "E_CLONE_SOURCE_LVALUE", source.symbol);
                }
                if (source.pointerValidity == PointerValidity::Deleted ||
                    source.pointerValidity == PointerValidity::Expired) {
                    Report("copy source is an invalid pointer",
                        "E_INVALID_POINTER_ARGUMENT", source.symbol);
                }
                else if (source.pointerValidity == PointerValidity::MaybeInvalid) {
                    Report("copy source may be an invalid pointer",
                        "E_MAYBE_INVALID_POINTER_ARGUMENT", source.symbol);
                }

                const std::vector<MemberSignature> members = FindMembers(source.type, "clone");
                std::vector<const MemberSignature*> candidates;
                for (const MemberSignature& member : members) {
                    if (member.kind == SymbolKind::Method && !member.isStatic &&
                        member.parameterTypes.empty()) {
                        candidates.push_back(&member);
                    }
                }
                if (candidates.empty()) {
                    Report("type '" + source.type +
                        "' is not cloneable; declare 'public T clone() const'",
                        "E_COPY_NOT_CLONEABLE", source.symbol);
                    Save(expr, {table.Lookup(callName), "error", false});
                    return;
                }
                if (candidates.size() > 1) {
                    Report("clone() is ambiguous for type '" + source.type + "'",
                        "E_CLONE_AMBIGUOUS", source.symbol);
                    Save(expr, {table.Lookup(callName), "error", false});
                    return;
                }

                const MemberSignature& clone = *candidates.front();
                if (clone.access != AccessLevel::Public) {
                    Report("clone() used by copy(...) must be public",
                        "E_CLONE_ACCESS", clone.symbol);
                }
                if (!clone.isConst) {
                    Report("clone() used by copy(...) must be const",
                        "E_CLONE_CONST", clone.symbol);
                }

                const std::string expectedReturn =
                    definition->second.kind == TypeKind::Struct
                    ? objectType : objectType + "*";
                if (clone.type != expectedReturn) {
                    Report("clone() for type '" + objectType + "' must return '" +
                        expectedReturn + "', got '" + clone.type + "'",
                        "E_CLONE_RETURN_TYPE", clone.symbol);
                }

                Result cloned = {table.Lookup(callName), clone.type, false};
                cloned.createsManagedOwner = IsStrongManagedPointerType(clone.type);
                cloned.pointerValidity = IsPointerType(clone.type)
                    ? PointerValidity::Live : PointerValidity::NotPointer;
                cloned.isMoveResult = TypeOwnsUniqueResource(clone.type);
                Save(expr, cloned);
                return;
            }

            if (callName == "taskGroupAdd") {
                if (arguments.size() != 2) {
                    Report("taskGroupAdd expects a group handle and one task",
                        "E_TASK_GROUP_ARGUMENT_COUNT");
                }
                else {
                    if (arguments[0].type != "raw void*" &&
                        arguments[0].type != "error") {
                        Report("taskGroupAdd expects a raw TaskGroup handle",
                            "E_TASK_GROUP_HANDLE_TYPE", arguments[0].symbol);
                    }
                    if (!IsTaskType(arguments[1].type) &&
                        arguments[1].type != "error") {
                        Report("taskGroupAdd expects task<T>, got '" +
                            arguments[1].type + "'",
                            "E_TASK_GROUP_CHILD_TYPE", arguments[1].symbol);
                    }
                    else if (arguments[1].taskState == TaskState::Awaited) {
                        Report("task has already been awaited or transferred",
                            "E_TASK_ALREADY_CONSUMED", arguments[1].symbol);
                    }
                    if (auto flow = valueFlow.find(arguments[1].symbol);
                        flow != valueFlow.end()) {
                        flow->second.taskState = TaskState::Awaited;
                    }
                }
                Save(expr, {table.Lookup(callName), "bool", false});
                return;
            }

            if (arguments.empty()) {
                Report("format expects a string literal template", "E_FORMAT_TEMPLATE_LITERAL");
            }
            else {
                if (arguments.front().type != "string" && arguments.front().type != "error")
                    Report("format template must be a string", "E_FORMAT_TEMPLATE_TYPE");
                StringLiteralProbe literalProbe;
                expr->arguments.front()->Accept(literalProbe);
                if (!literalProbe.literal) Report("format template must be a string literal",
                    "E_FORMAT_TEMPLATE_LITERAL");
                else {
                    const std::optional<size_t> placeholders = CountFormatPlaceholders(literalProbe.literal->value);
                    if (!placeholders) Report("format template contains an unmatched brace",
                        "E_FORMAT_TEMPLATE_BRACES");
                    else if (*placeholders != arguments.size() - 1)
                        Report("format template expects " + std::to_string(*placeholders) +
                            " value(s), got " + std::to_string(arguments.size() - 1),
                                "E_FORMAT_VALUE_COUNT");
                }
                for (size_t index = 1; index < arguments.size(); ++index) {
                    if (!IsPrintableType(arguments[index].type))
                        Report("format value " + std::to_string(index) + " has unsupported type '" +
                            arguments[index].type + "'", "E_FORMAT_VALUE_TYPE");
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
                        "' has no method '" + memberCall->member + "'", "E_UNKNOWN_METHOD");
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
            if (candidates.empty()) Report("unknown function '" + callName + "'",
                "E_UNKNOWN_FUNCTION");
            else symbolId = SelectOverload(candidates, arguments, callName, explicitTypeArguments);
        }

        const Symbol* selected = table.Get(symbolId);
        if (selected && selected->kind == SymbolKind::Method)
            RequireAccess(selected->access, selected->memberOwner, selected->name, selected->id);
        if (selected) {
            const size_t extensionOffset =
                selected->extensionFunction && hasReceiver ? 1 : 0;
            const size_t effectiveArgumentCount = arguments.size() + extensionOffset;
            const size_t fixedCount = selected->variadicParameter &&
                !selected->parameterTypes.empty()
                ? selected->parameterTypes.size() - 1
                : selected->parameterTypes.size();
            for (size_t i = 0; i < arguments.size(); ++i) {
                const size_t parameterIndex = i + extensionOffset;
                if (parameterIndex < selected->parameterTypes.size() &&
                    dynamic_cast<ArrayExpr*>(expr->arguments[i].get())) {
                    std::string expectedType =
                        ValueReferenceBaseType(selected->parameterTypes[parameterIndex]);
                    if (selected->variadicParameter && parameterIndex >= fixedCount) {
                        const bool directArray = effectiveArgumentCount ==
                            selected->parameterTypes.size() &&
                            ArrayRank(arguments[i].type) == 1 &&
                            IsAssignable(ValueReferenceBaseType(
                                selected->parameterTypes.back()), arguments[i].type);
                        if (!directArray)
                            expectedType = ArrayElementType(ValueReferenceBaseType(
                                selected->parameterTypes.back()));
                    }
                    arguments[i] = EvaluateExpected(
                        expr->arguments[i].get(), expectedType);
                }
            }
        }
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
            if (selected && (selected->variadicParameter
                ? !selected->parameterTypes.empty()
                : parameterIndex < selected->parameterTypes.size())) {
                const size_t fixedCount = selected->variadicParameter &&
                    !selected->parameterTypes.empty()
                    ? selected->parameterTypes.size() - 1
                    : selected->parameterTypes.size();
                const size_t effectiveArgumentCount = arguments.size() +
                    (selected->extensionFunction && hasReceiver ? 1 : 0);
                std::string parameterType = parameterIndex < fixedCount
                    ? selected->parameterTypes[parameterIndex]
                    : selected->parameterTypes.back();
                if (selected->variadicParameter && parameterIndex >= fixedCount) {
                    const bool directArray = effectiveArgumentCount ==
                        selected->parameterTypes.size() &&
                        ArrayRank(argument.type) == 1 &&
                        IsAssignable(ValueReferenceBaseType(
                            selected->parameterTypes.back()), argument.type);
                    if (!directArray)
                        parameterType = ArrayElementType(ValueReferenceBaseType(
                            selected->parameterTypes.back()));
                }
                const std::string parameterValueType = ValueReferenceBaseType(parameterType);
                if (IsValueReferenceType(parameterType)) {
                    if (!IsConstValueReferenceType(parameterType)) {
                        if (!argument.isLValue)
                            Report("mutable reference argument " + std::to_string(i + 1) +
                                " requires an lvalue", "E_VALUE_REF_REQUIRES_LVALUE",
                                argument.symbol);
                        else if (IsConstMutationTarget(expr->arguments[i].get(), argument))
                            Report("mutable reference argument " + std::to_string(i + 1) +
                                " cannot borrow a const value", "E_VALUE_REF_CONST_ARGUMENT",
                                argument.symbol);
                        for (size_t other = 0; other < arguments.size(); ++other) {
                            if (other != i && argument.symbol != InvalidSymbolId &&
                                arguments[other].symbol == argument.symbol) {
                                Report("mutable reference argument " + std::to_string(i + 1) +
                                    " overlaps another argument", "E_VALUE_REF_ALIAS",
                                    argument.symbol);
                                break;
                            }
                        }
                        if (expr->arguments[i] && expr->arguments[i]->isOutArgument) {
                            if (argument.symbol != InvalidSymbolId) {
                                if (auto flow = valueFlow.find(argument.symbol); flow != valueFlow.end()) {
                                    flow->second.initialization = InitializationState::Initialized;
                                }
                            }
                        }
                    }
                }
                CheckManagedMoveArgument(argument, parameterType, i, "function");
                if (IsTaskType(parameterValueType)) {
                    if (argument.taskState == TaskState::Awaited) {
                        Report("task argument " + std::to_string(i + 1) +
                            " has already been awaited or transferred",
                            "E_TASK_ALREADY_CONSUMED", argument.symbol);
                    }
                    if (auto flow = valueFlow.find(argument.symbol);
                        flow != valueFlow.end()) {
                        flow->second.taskState = TaskState::Awaited;
                    }
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
        if (selected) {
            std::vector<Result> ownershipArguments;
            if (selected->extensionFunction && hasReceiver)
                ownershipArguments.push_back(receiver);
            ownershipArguments.insert(
                ownershipArguments.end(), arguments.begin(), arguments.end());
            RecordOwnershipCall(selected->id, ownershipArguments,
                selected->parameterTypes,
                "call to '" + selected->name + "'");
        }
        if (hasReceiver && (receiver.pointerValidity == PointerValidity::Deleted ||
            receiver.pointerValidity == PointerValidity::Expired))
            Report("extension or method receiver is an invalid pointer",
                "E_INVALID_POINTER_ARGUMENT", receiver.symbol);
        if (asyncCall && selected && selected->kind == SymbolKind::Method && !selected->isStatic) {
            auto* memberCall = dynamic_cast<MemberAccessExpr*>(expr->base.get());
            const bool implicitThis = !memberCall;
            if (implicitThis) {
                const SymbolId thisId = table.Lookup("this");
                const Symbol* thisSymbol = table.Get(thisId);
                if (!thisSymbol || !currentMethodConst) {
                    Report("an async instance method without an explicit receiver can only be spawned "
                        "inside a const method", "E_ASYNC_RECEIVER_NOT_STABLE", symbolId);
                }
                else {
                    receiver.symbol = thisId;
                    receiver.type = thisSymbol->type;
                    hasReceiver = true;
                }
            }

            const auto* receiverIdentifier = memberCall
                ? dynamic_cast<IdentifierExpr*>(memberCall->base.get()) : nullptr;
            const Symbol* receiverSymbol = table.Get(receiver.symbol);
            const bool namedReceiver = implicitThis || receiverIdentifier != nullptr;
            const bool thisReceiver = receiverSymbol && receiverSymbol->name == "this";
            if (!namedReceiver || !receiverSymbol)
                Report("async instance method receiver must be a named const variable",
                    "E_ASYNC_RECEIVER_NOT_STABLE", receiver.symbol);
            else if (!receiverSymbol->isConst)
                Report("async instance method receiver '" + receiverSymbol->name +
                    "' must be const until the task is awaited",
                    "E_ASYNC_RECEIVER_NOT_CONST", receiver.symbol);

            if (IsRawPointerType(receiver.type) && !thisReceiver)
                Report("async instance methods cannot borrow a raw pointer receiver",
                    "E_ASYNC_RECEIVER_RAW", receiver.symbol);
            if (IsManagedPointerType(receiver.type) &&
                (!receiver.referencesManagedOwner || !receiverSymbol || !receiverSymbol->managedOwner))
                Report("async instance method receiver must be a managed owner, not a subscriber",
                    "E_ASYNC_RECEIVER_OWNER", receiver.symbol);

            const std::string receiverValueType = IsPointerType(receiver.type)
                ? PointerPointee(receiver.type) : receiver.type;
            if (TypeOwnsUniqueResource(receiverValueType))
                Report("async instance method receiver type '" + receiverValueType +
                    "' owns resources and cannot cross the task lifetime boundary",
                    "E_ASYNC_RECEIVER_RESOURCES", receiver.symbol);
        }
        if (asyncCall && spawnContextDepth == 0)
            Report("async function must be started with spawn", "E_ASYNC_CALL_REQUIRES_SPAWN", symbolId);
        Result callResult{symbolId, returnType, false, IsStrongManagedPointerType(returnType), false,
            InitializationState::Initialized,
            IsPointerType(returnType) ? PointerValidity::Unknown : PointerValidity::NotPointer,
            InvalidSymbolId, TaskState::NotTask, false, asyncCall};
        callResult.createsArrayOwner = ArrayRank(returnType) > 0;
        Save(expr, std::move(callResult));
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
        // An index is read, whatever is being done to the element it selects.
        // The access mode belongs to the target of the statement -- write for
        // `a[i] = v`, delete for `delete a[i]`, address for `&a[i]` -- and
        // letting it reach the index expression made the index inherit the
        // target's rules: `a[i % a.length] = v` was refused because `length`
        // is read-only, which it is, and which has nothing to do with reading
        // it to compute an index.
        const AccessMode previousAccess = accessMode;
        accessMode = AccessMode::Read;
        for (const auto& index : expr->indexes) {
            if (!index) {
                Report("array access requires an index", "E_ARRAY_INDEX_MISSING");
                continue;
            }
            const Result indexResult = Evaluate(index.get());
            indexes.push_back(indexResult);
            if (rank == 0) continue;
            if (!IsInteger(indexResult.type) && indexResult.type != "error")
                Report("array index must be an integer, got '" + indexResult.type + "'",
                    "E_ARRAY_INDEX_TYPE");
        }
        accessMode = previousAccess;
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
            if (!selected && IsRawPointerType(base.type)) {
                // `p[i]` on a raw pointer is `*(p + i)`, the spelling every
                // buffer walk reaches for. It was refused as "not an array and
                // has no matching indexer" even though the equivalent
                // arithmetic was accepted. Reached only after the indexer
                // search fails, so a raw pointer to a type that declares an
                // indexer keeps calling it. Raw only: a managed pointer has no
                // arithmetic, and one index into it would walk off its own
                // allocation.
                if (expr->indexes.size() != 1)
                    Report("a raw pointer takes exactly one index", "E_POINTER_INDEX_COUNT");
                else if (!indexes.empty() && !IsInteger(indexes.front().type) &&
                    indexes.front().type != "error")
                    Report("pointer index must be an integer, got '" +
                        indexes.front().type + "'", "E_POINTER_INDEX_TYPE");
                if (base.pointerValidity == PointerValidity::Null)
                    Report("null pointer is dereferenced", "E_NULL_DEREFERENCE", base.symbol);
                else if (base.pointerValidity == PointerValidity::Deleted)
                    Report("deleted pointer is dereferenced", "E_USE_AFTER_DELETE", base.symbol);
                Save(expr, {InvalidSymbolId, PointerPointee(base.type), true});
                return;
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
            Save(expr, AccessorValue(
                selected->symbol, selected->type, selected->canWrite));
            expressionInfo[expr].parameterTypes = selected->parameterTypes;
        }
        else if (expr->indexes.size() > rank) {
            Report("array access provides " + std::to_string(expr->indexes.size()) +
                " index(es), but the array has " + std::to_string(rank) + " dimension(s)",
                    "E_ARRAY_INDEX_COUNT");
            Save(expr, {base.symbol, "error", false});
        }
        else {
            Save(expr, {base.symbol, ArrayElementType(base.type, expr->indexes.size()), true});
        }
    }

    void Analyzer::Visit(SliceExpr* expr) {
        const Result base = Evaluate(expr->base.get());
        const size_t rank = ArrayRank(base.type);
        if (rank == 0 && base.type != "error") {
            Report("slice operation requires an array type", "E_SLICE_REQUIRES_ARRAY");
        }
        if (expr->ranges.size() > rank && base.type != "error") {
            Report("slice specifies " + std::to_string(expr->ranges.size()) +
                " range(s), but array has rank " + std::to_string(rank), "E_SLICE_RANGE_COUNT");
        }
        // Bounds are read for the same reason indexes are.
        const AccessMode previousAccess = accessMode;
        accessMode = AccessMode::Read;
        for (const auto& range : expr->ranges) {
            for (Expression* bound : {range.begin.get(), range.end.get()}) {
                if (!bound) continue;
                const Result resolved = Evaluate(bound);
                if (!IsInteger(resolved.type) && resolved.type != "error")
                    Report("slice bounds must be integers", "E_SLICE_BOUND_TYPE");
            }
        }
        accessMode = previousAccess;
        Save(expr, {base.symbol, rank > 0 ? base.type : "error", false});
    }
}
