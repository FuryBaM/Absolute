#include "analyzer_internal.h"

namespace Absolute {
    void Analyzer::Visit(AssignmentExpr* expr) {
        const AccessMode previousAccess = accessMode;
        accessMode = AccessMode::Write;
        const Result target = Evaluate(expr->target.get());
        accessMode = previousAccess;
        if (expr->op != "=") {
            const Symbol* symbol = table.Get(target.symbol);
            if (symbol && (symbol->kind == SymbolKind::Property ||
                symbol->kind == SymbolKind::Indexer) && !symbol->canRead)
                Report("compound assignment requires a readable property or indexer",
                    "E_PROPERTY_COMPOUND_REQUIRES_GETTER", target.symbol);
        }
        const Result value = EvaluateExpected(expr->value.get(), target.type);
        const Symbol* targetSymbol = table.Get(target.symbol);
        if (!target.isLValue) Report("assignment target is not assignable");
        CheckMutableTarget(expr->target.get(), target, "assignment");
        if (ArrayRank(target.type) > 0 &&
            dynamic_cast<MemberAccessExpr*>(expr->target.get()) == nullptr &&
            (!targetSymbol || (targetSymbol->kind != SymbolKind::Field &&
                targetSymbol->kind != SymbolKind::Property)))
            Report("array variables cannot be reassigned; assign an element or declare a new array view");
        if (ArrayRank(target.type) > 0 && expr->op != "=")
            Report("array fields only support direct '=' assignment");
        if (!IsAssignable(target.type, value.type))
            Report("cannot assign '" + value.type + "' to '" + target.type + "'");
        if (IsTaskType(target.type))
            Report("tasks cannot be reassigned or copied", "E_TASK_ASSIGNMENT", target.symbol);
        const bool owningField = targetSymbol &&
            (targetSymbol->kind == SymbolKind::Field ||
             targetSymbol->kind == SymbolKind::Property);
        if (owningField && IsManagedPointerType(target.type) &&
            value.type != "null" && !value.createsManagedOwner) {
            Report("managed resource fields require a fresh owner or null; "
                "store a copy/owner instead of a subscriber",
                "E_RESOURCE_FIELD_REQUIRES_OWNER", target.symbol);
        }
        if (owningField && ArrayRank(target.type) > 0) {
            bool transfersOwner = IsExplicitArrayCopy(expr->value.get());
            if (const Symbol* source = table.Get(value.symbol)) {
                transfersOwner = transfersOwner || source->scopeDepth == 0 ||
                    source->kind == SymbolKind::Function || source->kind == SymbolKind::Method;
            }
            if (!transfersOwner)
                Report("array resource fields require copy(...), a returned array, or a global view",
                    "E_ARRAY_FIELD_REQUIRES_OWNER", target.symbol);
        }
        bool transfersAggregateOwner = value.isMoveResult;
        if (const Symbol* source = table.Get(value.symbol)) {
            transfersAggregateOwner = transfersAggregateOwner ||
                source->kind == SymbolKind::Function || source->kind == SymbolKind::Method;
        }
        if (ArrayRank(target.type) == 0 && !IsPointerType(target.type) &&
            TypeOwnsResources(target.type) && !transfersAggregateOwner) {
            Report("resource-owning aggregate '" + target.type +
                "' cannot be copied; use move(...) for lvalues",
                "E_RESOURCE_AGGREGATE_COPY", target.symbol);
        }
        if (IsRawPointerType(target.type) && value.type != "error" && value.type != "null") {
            const Symbol* owner = table.Get(value.pointerOwner);
            if (owner && owner->scopeDepth > 0) {
                bool escapes = false;
                if (!targetSymbol) escapes = true;
                else if (targetSymbol->scopeDepth == 0) escapes = true;
                else if (owningField) escapes = true;
                else if (targetSymbol->scopeDepth < owner->scopeDepth) escapes = true;

                if (escapes) {
                    Report("cannot assign a raw pointer to a local variable to a longer-lived location",
                           "E_RAW_ESCAPE_LOCAL", value.symbol);
                }
            }
        }
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
        const bool fieldDeclaration = !currentType.empty() && functionDepth == 0;
        if (phase == Phase::CollectDeclarations) {
            if (currentType.empty()) {
                const auto declared = table.Declare(SymbolKind::Variable, declarationName, type);
                if (!declared)
                    Report("object '" + declarationName + "' is already declared in this scope");
                else table.Get(*declared)->isConst = expr->isConst;
            }
            else DeclareMember(currentType, name,
                {SymbolKind::Field, type, {}, InvalidSymbolId, expr->isConst, expr->isStatic,
                    pendingMemberAccess});
            Save(expr, {InvalidSymbolId, type, false});
            return;
        }
        if (expr->isStatic && !fieldDeclaration)
            Report("static field '" + name + "' must be declared inside a class, struct, or interface",
                "E_STATIC_NON_MEMBER_FIELD");
        if (expr->isStatic && !currentType.empty() &&
            !types[currentType].genericParameters.empty())
            Report("static members of generic types are not implemented yet",
                "E_STATIC_GENERIC_UNSUPPORTED");
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
        if (expr->isStatic) {
            const auto definition = types.find(type);
            const bool enumType = definition != types.end() && definition->second.kind == TypeKind::Enum;
            const bool scalarType = (PrimitiveStringToEnum(type).has_value() && type != "void" &&
                type != "auto" && type != "dynamic") || enumType || IsRawPointerType(type);
            if (!scalarType)
                Report("static field '" + currentType + "." + name +
                    "' currently requires a primitive, enum, string, or raw pointer type",
                    "E_STATIC_FIELD_TYPE");
            const auto constantInitializer = [](Expression* expression) {
                if (!expression) return true;
                if (dynamic_cast<NumberLiteralExpr*>(expression) ||
                    dynamic_cast<BooleanLiteralExpr*>(expression) ||
                    dynamic_cast<CharLiteralExpr*>(expression) ||
                    dynamic_cast<StringLiteralExpr*>(expression) ||
                    dynamic_cast<NullExpr*>(expression)) return true;
                const auto* unary = dynamic_cast<PrefixUnaryExpr*>(expression);
                return unary && unary->op == "-" &&
                    dynamic_cast<NumberLiteralExpr*>(unary->operand.get());
            };
            if (!constantInitializer(expr->value.get()))
                Report("static field initializer must be a constant literal",
                    "E_STATIC_FIELD_INITIALIZER");
            if (expr->isConst && !expr->value)
                Report("const static field '" + name + "' requires an initializer",
                    "E_CONST_REQUIRES_INITIALIZER");
        }
        if (IsTaskType(type) && expr->value && !value.createsTask)
            Report("task variable '" + name + "' requires a spawn initializer",
                "E_TASK_SPAWN_REQUIRED");
        if (IsTaskType(type) && !expr->value)
            Report("task variable '" + name + "' requires a spawn initializer",
                "E_TASK_SPAWN_REQUIRED");
        if (expr->isConst && currentType.empty() && !expr->value)
            Report("const variable '" + name + "' requires an initializer",
                "E_CONST_REQUIRES_INITIALIZER");

        const bool globalDeclaration = currentType.empty() && table.ScopeDepth() == 0;
        SymbolId id = fieldDeclaration ? table.Lookup(name) :
            (globalDeclaration ? table.Lookup(declarationName) : table.LookupCurrent(name));
        if (!fieldDeclaration && !globalDeclaration) {
            const auto declared = table.Declare(SymbolKind::Variable, name, type);
            if (!declared) Report("object '" + name + "' is already declared in this scope");
            else id = *declared;
        }
        if (Symbol* symbol = table.Get(id)) {
            symbol->isConst = expr->isConst;
            if (ArrayRank(type) > 0) {
                bool storageEscapes = globalDeclaration || IsExplicitArrayCopy(expr->value.get());
                if (const Symbol* source = table.Get(value.symbol)) {
                    storageEscapes = storageEscapes || source->arrayStorageEscapes ||
                        source->scopeDepth == 0 || source->kind == SymbolKind::Function ||
                        source->kind == SymbolKind::Method;
                }
                symbol->arrayStorageEscapes = storageEscapes;
            }
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
        std::string defName = type;
        std::string genBase;
        std::vector<std::string> genArgs;
        if (ParseGenericTypeName(type, genBase, genArgs)) defName = genBase;
        const bool isStructType = types.contains(defName) && types.at(defName).kind == TypeKind::Struct;
        flow.initialization = (expr->value || arrayRank > 0 || isStructType)
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

        const SymbolId nonFieldQualifiedId = LookupSymbol(qualifiedName);
        if (const Symbol* symbol = table.Get(nonFieldQualifiedId);
            symbol && symbol->kind != SymbolKind::Field && symbol->kind != SymbolKind::Property) {
            const bool isValue = symbol->kind == SymbolKind::Variable ||
                symbol->kind == SymbolKind::Parameter;
            if (!isValue && symbol->kind != SymbolKind::Function && symbol->kind != SymbolKind::Method)
                Report("object '" + qualifiedName + "' is not a value");
            callable = symbol->kind == SymbolKind::Function || symbol->kind == SymbolKind::Method;
            callableParameters = symbol->parameterTypes;
            Save(expr, {nonFieldQualifiedId, symbol->type, isValue});
            return;
        }

        const std::string typeReceiverName = ResolveTypeReference(
            ExtractQualifiedName(expr->base.get()));
        if (types.contains(typeReceiverName)) {
            const auto members = FindMembers(typeReceiverName, expr->member);
            const auto field = std::find_if(members.begin(), members.end(), [](const MemberSignature& member) {
                return member.kind == SymbolKind::Field && member.isStatic;
            });
            if (field != members.end()) {
                RequireAccess(*field, expr->member);
                callable = false;
                callableParameters.clear();
                Save(expr, {field->symbol, field->type, true});
                return;
            }
            if (std::any_of(members.begin(), members.end(), [](const MemberSignature& member) {
                return member.kind == SymbolKind::Field && !member.isStatic;
            }) || std::any_of(members.begin(), members.end(), [](const MemberSignature& member) {
                return member.kind == SymbolKind::Property && !member.isStatic;
            }))
                Report("instance member '" + expr->member + "' requires an object",
                    "E_INSTANCE_MEMBER_ON_TYPE");
            else
                Report("type '" + typeReceiverName + "' has no static field '" + expr->member + "'");
            Save(expr, {InvalidSymbolId, "error", false});
            return;
        }

        const SymbolId qualifiedId = LookupSymbol(qualifiedName);
        if (const Symbol* symbol = table.Get(qualifiedId)) {
            const bool isValue = symbol->kind == SymbolKind::Variable || symbol->kind == SymbolKind::Parameter ||
                symbol->kind == SymbolKind::Field || symbol->kind == SymbolKind::Property;
            if (!isValue && symbol->kind != SymbolKind::Function && symbol->kind != SymbolKind::Method)
                Report("object '" + qualifiedName + "' is not a value");
            callable = symbol->kind == SymbolKind::Function || symbol->kind == SymbolKind::Method;
            callableParameters = symbol->parameterTypes;
            if (symbol->kind == SymbolKind::Property) {
                const AccessLevel accessorAccess = accessMode == AccessMode::Write
                    ? symbol->writeAccess : symbol->readAccess;
                RequireAccess(accessorAccess, symbol->memberOwner, expr->member, symbol->id);
                if (accessMode == AccessMode::Read && !symbol->canRead)
                    Report("property '" + expr->member + "' is write-only",
                        "E_PROPERTY_WRITE_ONLY", symbol->id);
                else if (accessMode == AccessMode::Write && !symbol->canWrite)
                    Report("property '" + expr->member + "' is read-only",
                        "E_PROPERTY_READ_ONLY", symbol->id);
                else if (accessMode == AccessMode::Address || accessMode == AccessMode::Delete)
                    Report("property '" + expr->member + "' has no addressable storage",
                        "E_PROPERTY_NOT_ADDRESSABLE", symbol->id);
            }
            Save(expr, {qualifiedId, symbol->type,
                symbol->kind == SymbolKind::Property ? symbol->canWrite : isValue});
            return;
        }

        const Result base = Evaluate(expr->base.get());
        if (ArrayRank(base.type) > 0) {
            if (expr->member == "length" || expr->member == "count") {
                if (accessMode == AccessMode::Write || accessMode == AccessMode::Address || accessMode == AccessMode::Delete) {
                    Report("array property '" + expr->member + "' is read-only", "E_PROPERTY_READ_ONLY");
                }
                callable = false;
                callableParameters.clear();
                Save(expr, {InvalidSymbolId, "int32", false});
                return;
            }
        }
        if (base.pointerValidity == PointerValidity::Deleted ||
            base.pointerValidity == PointerValidity::Expired) {

            Report("member access uses an invalid pointer", "E_INVALID_POINTER_ACCESS", base.symbol);
        }
        else if (base.pointerValidity == PointerValidity::MaybeInvalid) {
            Report("member access may use an invalid pointer",
                "E_MAYBE_INVALID_POINTER_ACCESS", base.symbol);
        }
        const auto members = FindMembers(base.type, expr->member);
        const auto property = std::find_if(members.begin(), members.end(), [](const MemberSignature& member) {
            return member.kind == SymbolKind::Property && !member.isStatic;
        });
        if (property != members.end()) {
            RequireAccess(accessMode == AccessMode::Write
                ? property->writeAccess : property->readAccess, property->owner,
                expr->member, property->symbol);
            if (accessMode == AccessMode::Read && !property->canRead)
                Report("property '" + expr->member + "' is write-only",
                    "E_PROPERTY_WRITE_ONLY", property->symbol);
            else if (accessMode == AccessMode::Write && !property->canWrite)
                Report("property '" + expr->member + "' is read-only",
                    "E_PROPERTY_READ_ONLY", property->symbol);
            else if (accessMode == AccessMode::Address || accessMode == AccessMode::Delete)
                Report("property '" + expr->member + "' has no addressable storage",
                    "E_PROPERTY_NOT_ADDRESSABLE", property->symbol);
            callable = false;
            callableParameters.clear();
            Save(expr, {property->symbol, property->type, property->canWrite});
            return;
        }
        const auto field = std::find_if(members.begin(), members.end(), [](const MemberSignature& member) {
            return member.kind == SymbolKind::Field && !member.isStatic;
        });
        if (field == members.end()) {
            if (std::any_of(members.begin(), members.end(), [](const MemberSignature& member) {
                return member.kind == SymbolKind::Field && member.isStatic;
            }))
                Report("static field '" + expr->member + "' requires a type receiver",
                    "E_STATIC_MEMBER_ON_OBJECT");
            else Report("type '" + base.type + "' has no member '" + expr->member + "'");
            Save(expr, {InvalidSymbolId, "error", false});
            return;
        }
        RequireAccess(*field, expr->member);
        callable = false;
        callableParameters.clear();
        Save(expr, {field->symbol, field->type, true});
    }

    void Analyzer::Visit(CastExpr* expr) {
        const std::string target = ResolveType(expr->typeName.get());
        const Result base = Evaluate(expr->base.get());

        const auto runtimeType = [&](const std::string& type) {
            const std::string pointee = IsPointerType(type) ? PointerPointee(type) : type;
            std::string definition = pointee;
            std::string genericBase;
            std::vector<std::string> genericArguments;
            if (ParseGenericTypeName(pointee, genericBase, genericArguments)) definition = genericBase;
            const auto found = types.find(definition);
            return found != types.end() &&
                (found->second.kind == TypeKind::Class || found->second.kind == TypeKind::Interface);
        };
        const bool sourceReference = IsPointerType(base.type) && runtimeType(base.type);
        const bool targetReference = runtimeType(target);

        if (expr->operation == "is") {
            if (!sourceReference)
                Report("left operand of 'is' must be a class or interface pointer, got '" +
                    base.type + "'", "E_TYPE_TEST_SOURCE");
            if (!targetReference)
                Report("target of 'is' must be a class or interface type, got '" + target + "'",
                    "E_TYPE_TEST_TARGET");
            Save(expr, {InvalidSymbolId, "bool", false});
            return;
        }

        if (sourceReference || targetReference) {
            if (!sourceReference || !targetReference) {
                Report("safe reference cast requires class or interface pointer types, got '" +
                    base.type + "' and '" + target + "'", "E_SAFE_CAST_TYPE");
                Save(expr, {InvalidSymbolId, "error", false});
                return;
            }
            std::string resultType = target;
            if (!IsPointerType(resultType))
                resultType = (IsRawPointerType(base.type) ? "raw " : "") + target + "*";
            else if (IsRawPointerType(resultType) != IsRawPointerType(base.type))
                Report("safe cast cannot change pointer ownership mode from '" + base.type +
                    "' to '" + resultType + "'", "E_SAFE_CAST_OWNERSHIP");
            Result cast{InvalidSymbolId, resultType, false,
                base.createsManagedOwner, base.referencesManagedOwner,
                InitializationState::Initialized,
                base.pointerValidity == PointerValidity::Null
                    ? PointerValidity::Null : PointerValidity::MaybeNull,
                base.pointerOwner};
            cast.createsRawOwner = base.createsRawOwner;
            Save(expr, std::move(cast));
            return;
        }

        if (!IsAssignable(target, base.type) && !(IsNumeric(target) && IsNumeric(base.type)))
            Report("cannot cast '" + base.type + "' to '" + target + "'");
        Save(expr, {InvalidSymbolId, target, false});
    }

    void Analyzer::Visit(ConstructorCallExpr* expr) {
        const std::string constructedType = ResolveType(expr->constructName.get());
        if (ArrayRank(constructedType) > 0) {
            if (!expr->arguments.empty()) {
                EvaluateExpected(expr->arguments[0].get(), "int64");
            }
            Result allocation{InvalidSymbolId, constructedType, false,
                true, false, InitializationState::Initialized,
                PointerValidity::Live, InvalidSymbolId};
            Save(expr, std::move(allocation));
            return;
        }
        const bool rawAllocation = expr->raw ||
            (IsRawPointerType(expectedType) && PointerPointee(expectedType) == constructedType);
        if (!IsKnownType(constructedType) || constructedType == "void" || constructedType == "auto" ||
            constructedType == "dynamic")
            Report("cannot allocate type '" + constructedType + "'");
        const bool primitive = PrimitiveStringToEnum(constructedType).has_value();
        std::string definitionName = constructedType;
        std::unordered_map<std::string, std::string> substitutions;
        std::string genericBase;
        std::vector<std::string> genericArguments;
        if (ParseGenericTypeName(constructedType, genericBase, genericArguments)) {
            definitionName = genericBase;
            const auto definition = types.find(definitionName);
            if (definition != types.end() &&
                definition->second.genericParameters.size() == genericArguments.size())
                for (size_t index = 0; index < genericArguments.size(); ++index)
                    substitutions.emplace(definition->second.genericParameters[index],
                        genericArguments[index]);
        }
        if (const auto found = types.find(definitionName);
            found != types.end() && found->second.kind == TypeKind::Interface)
            Report("cannot instantiate interface '" + constructedType + "'");
        if (expr->arguments.size() > 1 && primitive)
            Report("primitive allocation accepts at most one initializer");
        std::vector<std::string> parameters;
        if (!primitive) {
            const auto found = types.find(definitionName);
            if (found != types.end() && found->second.constructor) {
                RequireAccess(found->second.constructor->access, definitionName,
                    definitionName, found->second.constructor->symbol);
                parameters = found->second.constructor->parameterTypes;
                for (std::string& parameter : parameters)
                    parameter = SubstituteGenericType(parameter, substitutions);
            }
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
        CheckMutableTarget(expr->target.get(), target, "delete");
        if (!IsPointerType(target.type) && target.type != "error")
            Report("delete requires a pointer, got '" + target.type + "'");
        if (!target.isLValue) Report("delete target must be an assignable pointer variable");
        for (const auto& scope : deferredKeepScopes) {
            if (scope.contains(target.symbol)) {
                Report("pointer is already scheduled for deletion by defer",
                    "E_DELETE_AFTER_DEFER", target.symbol);
                break;
            }
        }
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
        const bool fieldDeclaration = !currentType.empty() && functionDepth == 0;
        if (phase == Phase::CollectDeclarations) {
            if (currentType.empty()) {
                const auto declared = table.Declare(SymbolKind::Variable, declarationName, type);
                if (!declared)
                    Report("object '" + declarationName + "' is already declared in this scope");
                else table.Get(*declared)->isConst = expr->isConst;
            }
            else DeclareMember(currentType, name,
                {SymbolKind::Field, type, {}, InvalidSymbolId, expr->isConst, expr->isStatic,
                    pendingMemberAccess});
            Save(expr, {InvalidSymbolId, type, false});
            return;
        }
        if (expr->isStatic && !fieldDeclaration)
            Report("static field '" + name + "' must be declared inside a class or struct",
                "E_STATIC_NON_MEMBER_FIELD");
        if (expr->isStatic) {
            Report("static field '" + currentType + "." + name +
                "' currently requires a primitive, enum, string, or raw pointer type",
                "E_STATIC_FIELD_TYPE");
            if (!currentType.empty() && !types[currentType].genericParameters.empty())
                Report("static members of generic types are not implemented yet",
                    "E_STATIC_GENERIC_UNSUPPORTED");
        }
        std::string definitionName = type;
        std::string genericBase;
        std::vector<std::string> genericArguments;
        if (ParseGenericTypeName(type, genericBase, genericArguments)) definitionName = genericBase;
        if (!IsKnownType(type)) Report("unknown object type '" + type + "'");
        else if (const auto definition = types.find(definitionName);
            definition != types.end() && definition->second.kind == TypeKind::Interface)
            Report("interface '" + type + "' must be used through raw or managed pointer");
        else if (!expr->value) {
            if (const auto definition = types.find(definitionName);
                definition != types.end() && definition->second.constructor)
                RequireAccess(definition->second.constructor->access, definitionName,
                    definitionName, definition->second.constructor->symbol);
        }
        Result value;
        if (expr->value) value = Evaluate(expr->value.get());
        if (expr->value && !IsAssignable(type, value.type))
            Report("initializer of '" + name + "' has type '" + value.type + "', expected '" + type + "'");
        bool transfersAggregateOwner = value.isMoveResult;
        if (const Symbol* source = table.Get(value.symbol)) {
            transfersAggregateOwner = transfersAggregateOwner ||
                source->kind == SymbolKind::Function || source->kind == SymbolKind::Method;
        }
        if (expr->value && ArrayRank(type) == 0 && !IsPointerType(type) &&
            TypeOwnsResources(type) && !transfersAggregateOwner)
            Report("resource-owning aggregate '" + type +
                "' cannot be copied into '" + name +
                "'; use move(...) for lvalues",
                "E_RESOURCE_AGGREGATE_COPY", value.symbol);
        if (expr->isConst && currentType.empty() && !expr->value)
            Report("const variable '" + name + "' requires an initializer",
                "E_CONST_REQUIRES_INITIALIZER");
        const bool existingDeclaration = (!currentType.empty() && functionDepth == 0) ||
            (currentType.empty() && table.ScopeDepth() == 0);
        SymbolId id = (!currentType.empty() && functionDepth == 0) ? table.Lookup(name) :
            (existingDeclaration ? table.Lookup(declarationName) : table.LookupCurrent(name));
        if (!existingDeclaration) {
            const auto declared = table.Declare(SymbolKind::Variable, name, type);
            if (!declared) Report("object '" + name + "' is already declared in this scope");
            else id = *declared;
        }
        if (Symbol* symbol = table.Get(id)) symbol->isConst = expr->isConst;
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
            for (const auto& scope : deferredTaskScopes) {
                if (scope.contains(operand.symbol)) {
                    Report("task is already scheduled for await by defer",
                        "E_AWAIT_AFTER_DEFER", operand.symbol);
                    break;
                }
            }
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
        else if (expr->op == "++" || expr->op == "--") accessMode = AccessMode::Write;
        const Result operand = Evaluate(expr->operand.get());
        accessMode = previousAccess;
        if (expr->op == "&") {
            if (!operand.isLValue) Report("operator '&' requires an assignable value");
            CheckMutableTarget(expr->operand.get(), operand, "taking a mutable address");
            Save(expr, {InvalidSymbolId, "raw " + operand.type + "*", false, false, false,
                InitializationState::Initialized, PointerValidity::Live, operand.symbol});
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
        if (expr->op == "++" || expr->op == "--") {
            if (!operand.isLValue || !IsNumeric(operand.type))
                Report("operator '" + expr->op + "' requires an assignable numeric operand");
            const Symbol* symbol = table.Get(operand.symbol);
            if (symbol && (symbol->kind == SymbolKind::Property ||
                symbol->kind == SymbolKind::Indexer) && !symbol->canRead)
                Report("operator '" + expr->op + "' requires a readable property or indexer",
                    "E_PROPERTY_COMPOUND_REQUIRES_GETTER", operand.symbol);
            CheckMutableTarget(expr->operand.get(), operand, "operator '" + expr->op + "'");
        }
        else if (expr->op == "!" && !IsConditionType(operand.type)) Report("operator '!' requires a boolean-compatible operand");
        else if ((expr->op == "+" || expr->op == "-") && !IsNumeric(operand.type)) Report("unary numeric operator requires a number");
        else if (expr->op == "~" && !IsInteger(operand.type)) Report("operator '~' requires an integer");
        Save(expr, {operand.symbol, expr->op == "!" ? "bool" : operand.type, false});
    }

    void Analyzer::Visit(PostfixUnaryExpr* expr) {
        const AccessMode previousAccess = accessMode;
        accessMode = AccessMode::Write;
        const Result operand = Evaluate(expr->operand.get());
        accessMode = previousAccess;
        if (!operand.isLValue || !IsNumeric(operand.type))
            Report("operator '" + expr->op + "' requires an assignable numeric operand");
        const Symbol* symbol = table.Get(operand.symbol);
        if (symbol && (symbol->kind == SymbolKind::Property ||
            symbol->kind == SymbolKind::Indexer) && !symbol->canRead)
            Report("operator '" + expr->op + "' requires a readable property or indexer",
                "E_PROPERTY_COMPOUND_REQUIRES_GETTER", operand.symbol);
        CheckMutableTarget(expr->operand.get(), operand, "operator '" + expr->op + "'");
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
        if (typeContext && templateName == "func") {
            if (expr->types.empty()) {
                Report("func requires a return type", "E_FUNCTION_TYPE_ARITY");
                Save(expr, {InvalidSymbolId, "error", false});
                return;
            }
            std::vector<std::string> types;
            types.reserve(expr->types.size());
            for (const auto& type : expr->types) types.push_back(ResolveType(type.get()));
            if (types.front() == "auto" || types.front() == "dynamic")
                Report("func return type must be concrete", "E_FUNCTION_TYPE_RETURN");
            for (size_t index = 1; index < types.size(); ++index)
                if (types[index] == "void" || types[index] == "auto" || types[index] == "dynamic")
                    Report("func parameter types must be concrete non-void types",
                        "E_FUNCTION_TYPE_PARAMETER");
            Save(expr, {InvalidSymbolId,
                FunctionTypeName(types.front(), std::vector<std::string>(types.begin() + 1, types.end())),
                false});
            return;
        }
        if (typeContext) {
            const std::string base = ResolveTypeReference(ExtractQualifiedName(expr->base.get()));
            std::vector<std::string> arguments;
            arguments.reserve(expr->types.size());
            for (const auto& type : expr->types) arguments.push_back(ResolveType(type.get()));
            const auto definition = types.find(base);
            if (definition == types.end() || definition->second.genericParameters.empty()) {
                Report("type '" + base + "' is not generic", "E_NOT_GENERIC_TYPE");
                Save(expr, {InvalidSymbolId, "error", false});
                return;
            }
            if (definition->second.genericParameters.size() != arguments.size()) {
                Report("generic type '" + base + "' expects " +
                    std::to_string(definition->second.genericParameters.size()) +
                    " type argument(s), got " + std::to_string(arguments.size()),
                    "E_GENERIC_TYPE_ARITY");
                Save(expr, {InvalidSymbolId, "error", false});
                return;
            }
            std::string specialized = base + "<";
            for (size_t index = 0; index < arguments.size(); ++index) {
                if (index) specialized += ",";
                specialized += arguments[index];
            }
            specialized += ">";
            instantiatedGenericTypes.insert(specialized);
            Save(expr, {InvalidSymbolId, specialized, false});
            return;
        }
        else {
            const Result baseResult = Evaluate(expr->base.get());
            for (const auto& type : expr->types) ResolveType(type.get());
            Save(expr, {baseResult.symbol, baseResult.type, false});
        }
    }

    void Analyzer::Visit(LambdaExpr* expr) {
        std::vector<std::string> parameterTypes;
        parameterTypes.reserve(expr->parameters.size());
        table.EnterScope();
        lambdaScopeDepths.push_back(table.ScopeDepth());
        ++functionDepth;
        for (const auto& parameter : expr->parameters) {
            if (!parameter) continue;
            const std::string name = ExtractIdentifier(parameter->name.get());
            const std::string type = ResolveDeclaredType(*parameter);
            parameterTypes.push_back(type);
            if (name.empty()) Report("lambda parameter requires a name", "E_LAMBDA_PARAMETER");
            else if (!table.Declare(SymbolKind::Parameter, name, type))
                Report("duplicate lambda parameter '" + name + "'", "E_LAMBDA_PARAMETER");
            if (parameter->value)
                Report("lambda parameters cannot have default values", "E_LAMBDA_DEFAULT_PARAMETER");
        }
        const Result body = Evaluate(expr->body.get());
        --functionDepth;
        lambdaScopeDepths.pop_back();
        table.ExitScope();
        if (body.type == "void")
            Report("an expression lambda cannot return void", "E_LAMBDA_VOID_EXPRESSION");
        const std::string type = FunctionTypeName(body.type, parameterTypes);
        std::string expectedReturn;
        std::vector<std::string> expectedParameters;
        if (ParseFunctionType(expectedType, expectedReturn, expectedParameters)) {
            if (expectedParameters != parameterTypes)
                Report("lambda parameter types do not match expected type '" + expectedType + "'",
                    "E_LAMBDA_SIGNATURE");
            if (!IsAssignable(expectedReturn, body.type))
                Report("lambda returns '" + body.type + "', expected '" + expectedReturn + "'",
                    "E_LAMBDA_RETURN");
        }
        Save(expr, {InvalidSymbolId, type, false});
    }

}
