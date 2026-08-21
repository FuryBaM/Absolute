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
        if (!target.isLValue) Report("assignment target is not assignable",
            "E_TARGET_NOT_ASSIGNABLE");
        CheckMutableTarget(expr->target.get(), target, "assignment");
        if (ArrayRank(target.type) > 0 &&
            dynamic_cast<MemberAccessExpr*>(expr->target.get()) == nullptr &&
            (!targetSymbol || (targetSymbol->kind != SymbolKind::Field &&
                targetSymbol->kind != SymbolKind::Property)))
            Report("array variables cannot be reassigned; assign an element or declare a new array view",
                "E_ARRAY_VARIABLE_REASSIGNED");
        if (ArrayRank(target.type) > 0 && expr->op != "=")
            Report("array fields only support direct '=' assignment",
                "E_ARRAY_FIELD_COMPOUND_ASSIGNMENT");
        // A compound assignment stores the result of `target op value`, not
        // the value itself, and for a raw pointer those differ: `p += 2` adds
        // two elements and stores a pointer. Checking the value against the
        // target rejected the whole notation, even though `p = p + 2` was
        // accepted and lowered correctly.
        const bool pointerCompound = expr->op != "=" && IsRawPointerType(target.type);
        const bool pointerStep = pointerCompound &&
            (expr->op == "+=" || expr->op == "-=") && IsInteger(value.type);
        if (pointerCompound) {
            // One message, not two: the generic check would add "cannot assign
            // 'int32' to 'raw int32*'", which is exactly the wrong reading of
            // `p *= 2` -- nothing is being assigned to the pointer.
            if (!pointerStep)
                Report("a raw pointer supports only '+=' and '-=' with an integer step, "
                    "but the operator is '" + expr->op + "' and the step has type '" +
                    value.type + "'", "E_RAW_POINTER_COMPOUND_ASSIGNMENT");
        }
        else if (!IsAssignable(target.type, value.type))
            Report("cannot assign '" + value.type + "' to '" + target.type + "'",
                "E_ASSIGNMENT_TYPE_MISMATCH");
        // The compound form lowers through the same shift as `a = a << n`, so
        // it has the same undefined case and gets the same answer here.
        if ((expr->op == "<<=" || expr->op == ">>=") &&
            IsInteger(target.type) && IsInteger(value.type))
            CheckShiftAmount(expr->value.get(), CommonType(target.type, value.type), expr->op);
        if (IsTaskType(target.type))
            Report("tasks cannot be reassigned or copied", "E_TASK_ASSIGNMENT", target.symbol);
        const bool owningField = targetSymbol &&
            (targetSymbol->kind == SymbolKind::Field ||
             targetSymbol->kind == SymbolKind::Property);
        // Inside a generic body the field's type is `T`, so the rule below
        // cannot see a pointer and does not run. Record what was seen; each
        // instantiation substitutes the type and asks the rule then.
        if (owningField && !value.createsManagedOwner && value.type != "null" &&
            !IsStrongManagedPointerType(target.type)) {
            RecordGenericBodyFact(GenericBodyFact::Shape::FieldFromNonOwner,
                target.type, targetSymbol->name, expr);
        }
        if (owningField && IsStrongManagedPointerType(target.type) && value.type != "null") {
            bool createsOwnershipCycle = false;
            if (auto* member = dynamic_cast<MemberAccessExpr*>(expr->target.get())) {
                Expression* rootExpr = member->base.get();
                while (auto* parentMember = dynamic_cast<MemberAccessExpr*>(rootExpr)) {
                    rootExpr = parentMember->base.get();
                }
                const SymbolId root = LookupSymbol(ExtractIdentifier(rootExpr));
                SymbolId targetOwner = root;
                if (const auto flow = valueFlow.find(root);
                    flow != valueFlow.end() && flow->second.pointerOwner != InvalidSymbolId)
                    targetOwner = flow->second.pointerOwner;
                createsOwnershipCycle = targetOwner != InvalidSymbolId &&
                    (value.pointerOwner == targetOwner || value.symbol == targetOwner);
            }
            if (createsOwnershipCycle) {
                Report("owning field assignment would create a strong managed ownership cycle; "
                    "use weak T* for the back-edge",
                    "E_MANAGED_OWNERSHIP_CYCLE", target.symbol);
            }
            else if (!value.createsManagedOwner) {
                Report("managed resource fields require a fresh owner or null; "
                    "store a copy/owner instead of a subscriber",
                    "E_RESOURCE_FIELD_REQUIRES_OWNER", target.symbol);
            }
        }
        // A slot of an array is a place like a field, and the same rule
        // governs it. A handle read out of somewhere else is a second handle
        // to one object, and an array that owns its elements cannot hold one:
        // whichever of the two is released first, the other is wrong.
        // `unsafeArrayTake` is how an element leaves a slot, and it reports a
        // fresh owner, so it satisfies this the way `copy` and `move` do.
        const bool owningSlot =
            dynamic_cast<ArrayAccessExpr*>(expr->target.get()) != nullptr;
        if (owningSlot && !value.createsManagedOwner && value.type != "null" &&
            !IsStrongManagedPointerType(target.type)) {
            const Symbol* array = table.Get(target.symbol);
            RecordGenericBodyFact(GenericBodyFact::Shape::ElementFromNonOwner,
                target.type, array ? array->name : target.type, expr);
        }
        if (owningSlot && IsStrongManagedPointerType(target.type) &&
            value.type != "null" && !value.createsManagedOwner) {
            Report("managed array elements require a fresh owner or null; "
                "store a copy/owner or take the element out of its slot "
                "instead of reading it",
                "E_RESOURCE_ELEMENT_REQUIRES_OWNER", target.symbol);
        }
        if (IsWeakPointerType(target.type) && value.createsManagedOwner) {
            Report("weak pointer cannot take ownership of a fresh managed allocation; "
                "bind the allocation to a managed owner first",
                "E_WEAK_REQUIRES_EXISTING_OWNER", target.symbol);
        }
        // A subscriber says someone else owns what it names, and a fresh
        // allocation is owned by nobody -- so nothing would ever release it.
        // The same mistake `weak` refuses just above, refused for the other
        // qualifier that means the same thing about ownership. `T* b = a;`
        // stays legal: an unqualified name takes a subscriber when what it is
        // given already has an owner, and this asks whether it does.
        if (IsSubscriberPointerType(target.type) && value.createsManagedOwner) {
            Report("subscriber cannot take ownership of a fresh managed allocation; "
                "bind the allocation to a managed owner first",
                "E_SUBSCRIBER_REQUIRES_EXISTING_OWNER", target.symbol);
        }
        if (owningField && ArrayRank(target.type) > 0) {
            bool transfersOwner = value.createsArrayOwner ||
                IsExplicitArrayCopy(expr->value.get());
            if (const Symbol* source = table.Get(value.symbol)) {
                transfersOwner = transfersOwner || source->scopeDepth == 0 ||
                    source->kind == SymbolKind::Function || source->kind == SymbolKind::Method;
            }
            if (!transfersOwner)
                Report("array resource fields require copy(...), move(...), a returned array, or a global view",
                    "E_ARRAY_FIELD_REQUIRES_OWNER", target.symbol);
        }
        if (ArrayRank(target.type) > 0) {
            if (Symbol* symbol = table.Get(target.symbol))
                symbol->ownsArrayStorage = symbol->ownsArrayStorage ||
                    value.createsArrayOwner;
        }
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
        if (ArrayRank(target.type) == 0 && !IsPointerType(target.type) &&
            TypeOwnsUniqueResource(target.type) && !transfersAggregateOwner) {
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
        if (IsStrongManagedPointerType(target.type)) {
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
        if (value.isMoveResult && value.createsManagedOwner) {
            SymbolId nextOwner = owningField ? InvalidSymbolId : target.symbol;
            if (owningField) {
                if (auto* member = dynamic_cast<MemberAccessExpr*>(expr->target.get())) {
                    const SymbolId root = LookupSymbol(ExtractIdentifier(member->base.get()));
                    nextOwner = root;
                    if (const auto flow = valueFlow.find(root);
                        flow != valueFlow.end() && flow->second.pointerOwner != InvalidSymbolId)
                        nextOwner = flow->second.pointerOwner;
                }
            }
            TransferManagedAliases(value.pointerOwner, nextOwner);
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
        Expression* baseDeclarator = expr->name.get();
        std::vector<Expression*> declaratorIndexes;
        while (auto* access = dynamic_cast<ArrayAccessExpr*>(baseDeclarator)) {
            for (const auto& index : access->indexes) {
                declaratorIndexes.push_back(index.get());
            }
            baseDeclarator = access->base.get();
        }
        std::reverse(declaratorIndexes.begin(), declaratorIndexes.end());

        const std::string name = ExtractIdentifier(baseDeclarator);
        const std::string declarationName = currentType.empty() && functionDepth == 0 ? Qualify(name) : name;
        std::string type = ResolveType(expr->type.get());
        const size_t arrayRank = declaratorIndexes.size();
        if (arrayRank > 0) type = ArrayType(std::move(type), arrayRank);
        const bool fieldDeclaration = !currentType.empty() && functionDepth == 0;
        // An instance field's initializer was parsed, collected, and then
        // dropped on the floor: the backend zero-initializes an object's
        // storage and nothing ever ran what was written here, so
        // `private int64 n = 5;` read back 0 and a string field read back
        // null. A missing feature that fails loudly is not the same problem as
        // a wrong answer nobody notices, so it says so. Reported while
        // declarations are collected, which is the phase a field's declaration
        // is visited in.
        //
        // Not implemented rather than unwanted: what it would mean for a
        // struct is an open question -- a struct's storage is made without a
        // constructor, and an element of `new S[n]` is zeroed with nothing to
        // run -- and answering it is a language decision rather than a backend
        // one. See section 2 of docs/known-defects.md.
        if (fieldDeclaration && !expr->isStatic && expr->value &&
            phase == Phase::CollectDeclarations)
            Report("field '" + name + "' cannot have an initializer here; "
                "assign it in a constructor",
                "E_FIELD_INITIALIZER_UNSUPPORTED");
        if (phase == Phase::CollectDeclarations) {
            if (currentType.empty()) {
                const auto declared = table.Declare(SymbolKind::Variable, declarationName, type);
                if (!declared)
                    Report("object '" + declarationName + "' is already declared in this scope",
                        "E_DUPLICATE_DECLARATION");
                else table.Get(*declared)->isConst = expr->isConst;
            }
            else DeclareMember(currentType, name,
                {SymbolKind::Field, type, {}, InvalidSymbolId, expr->isConst, expr->isStatic,
                    pendingMemberAccess});
            Save(expr, {InvalidSymbolId, type, false});
            return;
        }
        // Module scope has no destruction point: the storage is created once and
        // lives until the process ends, so nothing defines when an owner there
        // would be released or in what order against other globals. Refuse it
        // here, where the message can carry a file, a line and a column;
        // CodeGen keeps the same refusal as a backstop for paths that reach it
        // without semantic analysis.
        // Only heap owners: a global array has static storage and no
        // destructor to schedule, which is why arrays have always been
        // allowed here and stay allowed.
        if (currentType.empty() && functionDepth == 0 && ArrayRank(type) == 0 &&
            (IsManagedPointerType(type) || IsWeakPointerType(type) ||
             IsTaskType(type)))
            Report("'" + name + "' owns a resource and cannot be declared at module scope; "
                "module storage is never destroyed, so its destruction point and order "
                "would be undefined",
                "E_MODULE_SCOPE_OWNER");
        if (expr->isStatic && !fieldDeclaration)
            Report("static field '" + name + "' must be declared inside a class, struct, or interface",
                "E_STATIC_NON_MEMBER_FIELD");
        if (expr->isStatic && !currentType.empty() &&
            !types[currentType].genericParameters.empty())
            Report("static members of generic types are not implemented yet",
                "E_STATIC_GENERIC_UNSUPPORTED");
        if (!IsKnownType(type)) Report("unknown type '" + type + "' of variable '" + name + "'",
            "E_UNKNOWN_TYPE");
        std::optional<std::vector<size_t>> initializerShape;
        if (arrayRank > 0) {
            for (Expression* size : declaratorIndexes) {
                if (!size) continue;
                const Result resolved = Evaluate(size);
                if (!IsInteger(resolved.type) && resolved.type != "error")
                    Report("array size must be an integer, got '" + resolved.type + "'",
                        "E_ARRAY_SIZE_TYPE");
                if (currentType.empty() && functionDepth == 0 &&
                    !dynamic_cast<const NumberLiteralExpr*>(size))
                    Report("global array dimensions must be constant integer literals",
                        "E_GLOBAL_ARRAY_SIZE_NOT_CONSTANT");
                if (const auto* literal = dynamic_cast<const NumberLiteralExpr*>(size)) {
                    try {
                        if (std::stoll(literal->value) <= 0) Report("array size must be greater than zero",
                            "E_ARRAY_SIZE_NOT_POSITIVE");
                    }
                    catch (const std::exception&) {
                        Report("array size is outside the supported integer range",
                            "E_ARRAY_SIZE_OUT_OF_RANGE");
                    }
                }
            }
            if (const auto* literal = dynamic_cast<const ArrayExpr*>(expr->value.get())) {
                initializerShape = InferArrayStorageShape(*literal, arrayRank);
                if (!initializerShape) Report("array initializer must be rectangular",
                    "E_ARRAY_INITIALIZER_NOT_RECTANGULAR");
                else if (initializerShape->size() != arrayRank)
                    Report("array initializer has " + std::to_string(initializerShape->size()) +
                        " dimension(s), expected " + std::to_string(arrayRank),
                            "E_ARRAY_INITIALIZER_RANK");
            }
            for (size_t dimension = 0; dimension < declaratorIndexes.size(); ++dimension) {
                Expression* size = declaratorIndexes[dimension];
                if (!size) {
                    if (!initializerShape || dimension >= initializerShape->size())
                        Report("array dimension " + std::to_string(dimension + 1) +
                            " requires a size or an initializer", "E_ARRAY_DIMENSION_MISSING");
                    continue;
                }
                if (initializerShape && dimension < initializerShape->size()) {
                    if (const auto* literal = dynamic_cast<const NumberLiteralExpr*>(size)) {
                        try {
                            if (static_cast<size_t>(std::stoull(literal->value)) != (*initializerShape)[dimension])
                                Report("array initializer size does not match dimension " +
                                    std::to_string(dimension + 1), "E_ARRAY_INITIALIZER_SIZE");
                        }
                        catch (const std::exception&) {
                        }
                    }
                }
            }
            if (expr->value && !dynamic_cast<ArrayExpr*>(expr->value.get()))
                Report("array variable '" + name + "' requires an array literal initializer",
                    "E_ARRAY_REQUIRES_LITERAL");
        }
        Result value;
        if (expr->value) value = EvaluateExpected(expr->value.get(), type == "auto" ? std::string{} : type);
        if (type == "auto") {
            if (!expr->value) Report("auto variable '" + name + "' requires an initializer",
                "E_AUTO_REQUIRES_INITIALIZER");
            else type = value.type;
        }
        else if (expr->value && !IsAssignable(type, value.type))
            Report("initializer of '" + name + "' has type '" + value.type + "', expected '" + type + "'",
                "E_INITIALIZER_TYPE_MISMATCH");
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
        // The same refusal an instance declaration carries. A declaration whose
        // type is written with generic arguments parses as this one instead,
        // so `Cell<Node*> second = first;` was two names for one owner and
        // nothing said so -- while the same thing spelled without the angle
        // brackets was refused.
        {
            bool transfersAggregateOwner =
                value.isMoveResult || value.producesFreshValue;
            if (const Symbol* source = table.Get(value.symbol)) {
                transfersAggregateOwner = transfersAggregateOwner ||
                    source->kind == SymbolKind::Function ||
                    source->kind == SymbolKind::Method;
            }
            if (expr->value && ArrayRank(type) == 0 && !IsPointerType(type) &&
                TypeOwnsUniqueResource(type) && !transfersAggregateOwner)
                Report("resource-owning aggregate '" + type +
                    "' cannot be copied into '" + name +
                    "'; use move(...) for lvalues",
                    "E_RESOURCE_AGGREGATE_COPY", value.symbol);
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
            if (!declared) Report("object '" + name + "' is already declared in this scope",
                "E_DUPLICATE_DECLARATION");
            else id = *declared;
        }
        if (Symbol* symbol = table.Get(id)) {
            symbol->isConst = expr->isConst;
            if (ArrayRank(type) > 0) {
                // A declaration that provides the storage itself keeps it: a
                // sized declarator is the frame's, a global is the module's,
                // and neither is freed by whoever holds the name. So an
                // initializer's answer -- that it made an owner -- counts only
                // where the initializer is what made the storage.
                const bool declarationOwnsStorage =
                    !declaratorIndexes.empty() || globalDeclaration;
                const bool initializerMadeStorage = !declarationOwnsStorage &&
                    (value.createsArrayOwner || IsExplicitArrayCopy(expr->value.get()));
                bool storageEscapes = globalDeclaration || initializerMadeStorage;
                if (const Symbol* source = table.Get(value.symbol)) {
                    storageEscapes = storageEscapes || source->arrayStorageEscapes ||
                        source->scopeDepth == 0 || source->kind == SymbolKind::Function ||
                        source->kind == SymbolKind::Method;
                }
                symbol->arrayStorageEscapes = storageEscapes;
                symbol->ownsArrayStorage = initializerMadeStorage;
            }
        }
        if (IsWeakPointerType(type) && value.createsManagedOwner) {
            Report("weak pointer '" + name +
                "' cannot own a fresh managed allocation; declare a managed owner first",
                "E_WEAK_REQUIRES_EXISTING_OWNER", id);
        }
        if (IsSubscriberPointerType(type) && value.createsManagedOwner) {
            Report("subscriber '" + name +
                "' cannot own a fresh managed allocation; declare a managed owner first",
                "E_SUBSCRIBER_REQUIRES_EXISTING_OWNER", id);
        }
        // `T* b = a;` stays legal: it takes a subscriber, and `isOwner()`
        // answers which of the two a value is. `sub T*` is there to let the
        // distinction be written down where it matters -- a container element,
        // a field, a generic argument -- not to forbid the short form.
        const bool borrowsExistingOwner = expr->value && value.type != "null" &&
            !value.createsManagedOwner && value.pointerOwner != InvalidSymbolId;
        if (IsManagedPointerType(type)) {
            if (Symbol* symbol = table.Get(id)) {
                symbol->managedOwner = IsStrongManagedPointerType(type) && value.createsManagedOwner;
                symbol->managedBorrower = IsWeakPointerType(type) ||
                    IsSubscriberPointerType(type) || borrowsExistingOwner;
            }
        }
        else if (Symbol* symbol = table.Get(id)) symbol->type = type;
        if (value.isMoveResult && value.createsManagedOwner)
            TransferManagedAliases(value.pointerOwner, id);
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
            if (phase == Phase::ResolveBodies && !IsKnownType(type)) Report("unknown type '" + qualifiedName + "'",
                "E_UNKNOWN_TYPE");
            Save(expr, {InvalidSymbolId, type, false});
            return;
        }

        const SymbolId nonFieldQualifiedId = LookupSymbol(qualifiedName);
        if (const Symbol* symbol = table.Get(nonFieldQualifiedId);
            symbol && symbol->kind != SymbolKind::Field && symbol->kind != SymbolKind::Property) {
            const bool isValue = symbol->kind == SymbolKind::Variable ||
                symbol->kind == SymbolKind::Parameter;
            if (!isValue && symbol->kind != SymbolKind::Function && symbol->kind != SymbolKind::Method)
                Report("object '" + qualifiedName + "' is not a value", "E_OBJECT_NOT_A_VALUE");
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
                Report("type '" + typeReceiverName + "' has no static field '" + expr->member + "'",
                    "E_UNKNOWN_STATIC_FIELD");
            Save(expr, {InvalidSymbolId, "error", false});
            return;
        }

        const SymbolId qualifiedId = LookupSymbol(qualifiedName);
        if (const Symbol* symbol = table.Get(qualifiedId)) {
            const bool isValue = symbol->kind == SymbolKind::Variable || symbol->kind == SymbolKind::Parameter ||
                symbol->kind == SymbolKind::Field || symbol->kind == SymbolKind::Property;
            if (!isValue && symbol->kind != SymbolKind::Function && symbol->kind != SymbolKind::Method)
                Report("object '" + qualifiedName + "' is not a value", "E_OBJECT_NOT_A_VALUE");
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
            if (symbol->kind == SymbolKind::Property) {
                Save(expr, AccessorValue(qualifiedId, symbol->type, symbol->canWrite));
                return;
            }
            Save(expr, {qualifiedId, symbol->type, isValue});
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
        std::string tupleBase;
        std::vector<std::string> tupleElements;
        if (ParseGenericTypeName(base.type, tupleBase, tupleElements) &&
            tupleBase == "tuple") {
            if (expr->member == "length" || expr->member == "count") {
                if (accessMode != AccessMode::Read)
                    Report("tuple property '" + expr->member + "' is read-only",
                        "E_PROPERTY_READ_ONLY");
                callable = false;
                callableParameters.clear();
                Save(expr, {InvalidSymbolId, "int32", false});
                return;
            }
            if (expr->member.starts_with("item")) {
                const std::string suffix = expr->member.substr(4);
                const bool digits = !suffix.empty() &&
                    std::all_of(suffix.begin(), suffix.end(),
                        [](unsigned char value) { return std::isdigit(value); });
                size_t index = tupleElements.size();
                if (digits) {
                    try { index = static_cast<size_t>(std::stoull(suffix)); }
                    catch (...) { index = tupleElements.size(); }
                }
                if (index < tupleElements.size()) {
                    callable = false;
                    callableParameters.clear();
                    Save(expr, {base.symbol, tupleElements[index], base.isLValue});
                    return;
                }
            }
            Report("tuple type '" + base.type + "' has no member '" +
                expr->member + "'", "E_TUPLE_MEMBER");
            Save(expr, {InvalidSymbolId, "error", false});
            return;
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
            Save(expr, AccessorValue(
                property->symbol, property->type, property->canWrite));
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
            else Report("type '" + base.type + "' has no member '" + expr->member + "'",
                "E_UNKNOWN_MEMBER");
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
                resultType = (IsRawPointerType(base.type) ? "raw " :
                    (IsWeakPointerType(base.type) ? "weak " : "")) + target + "*";
            else if (!((IsRawPointerType(resultType) && IsRawPointerType(base.type)) ||
                (IsWeakPointerType(resultType) && IsManagedPointerType(base.type)) ||
                (IsStrongManagedPointerType(resultType) &&
                    IsStrongManagedPointerType(base.type))))
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

        const bool pointerToInt = IsInteger(target) && IsRawPointerType(base.type);
        const bool intToPointer = IsRawPointerType(target) && IsInteger(base.type);
        // A member may be given a number that means something outside the
        // program -- an HTTP status, a C constant -- so the number has to be
        // readable. Only this direction: making an enum out of an arbitrary
        // integer would produce values no member stands for, and `match` is
        // exhaustive precisely because that cannot happen.
        const bool enumToInt = IsInteger(target) && IsEnumType(base.type);
        if (!IsAssignable(target, base.type) &&
            !(IsNumeric(target) && IsNumeric(base.type)) &&
            !pointerToInt && !intToPointer && !enumToInt)
            Report("cannot cast '" + base.type + "' to '" + target + "'", "E_INVALID_CAST");
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
            allocation.createsArrayOwner = true;
            Save(expr, std::move(allocation));
            return;
        }
        const bool rawAllocation = expr->raw ||
            (IsRawPointerType(expectedType) && PointerPointee(expectedType) == constructedType);
        if (!IsKnownType(constructedType) || constructedType == "void" || constructedType == "auto" ||
            constructedType == "dynamic")
            Report("cannot allocate type '" + constructedType + "'", "E_UNALLOCATABLE_TYPE");
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
            Report("cannot instantiate interface '" + constructedType + "'",
                "E_INTERFACE_INSTANTIATION");
        if (expr->arguments.size() > 1 && primitive)
            Report("primitive allocation accepts at most one initializer",
                "E_PRIMITIVE_ALLOCATION_ARGUMENTS");
        std::vector<std::string> parameters;
        if (!primitive) {
            std::vector<Result> evaluated;
            evaluated.reserve(expr->arguments.size());
            for (const auto& argument : expr->arguments)
                evaluated.push_back(Evaluate(argument.get()));
            std::vector<MemberSignature> constructors = ConstructorsOf(constructedType);
            const MemberSignature* selected = SelectConstructor(
                constructors, evaluated, constructedType, substitutions);
            if (selected) {
                RequireAccess(selected->access, definitionName,
                    definitionName, selected->symbol);
                parameters = selected->parameterTypes;
                for (std::string& parameter : parameters)
                    parameter = SubstituteGenericType(parameter, substitutions);
            }
            for (size_t index = 0; index < expr->arguments.size(); ++index) {
                const std::string declaredExpected = index < parameters.size()
                    ? parameters[index] : std::string{};
                const std::string expected =
                    ValueReferenceBaseType(declaredExpected);
                const Result& value = evaluated[index];
                if (!expected.empty() && !IsAssignable(expected, value.type))
                    Report("constructor argument " + std::to_string(index + 1) + " has type '" +
                        value.type + "', expected '" + expected + "'",
                            "E_CONSTRUCTOR_ARGUMENT_TYPE");
                CheckManagedArgumentOwnership(value, declaredExpected, index, "constructor");
            }
            if (selected)
                RecordOwnershipCall(selected->symbol, evaluated, parameters,
                    "constructor '" + constructedType + "'");
            Result allocation{InvalidSymbolId,
                (rawAllocation ? "raw " : "") + constructedType + "*", false,
                !rawAllocation, false, InitializationState::Initialized,
                PointerValidity::Live, InvalidSymbolId};
            allocation.createsRawOwner = rawAllocation;
            Save(expr, std::move(allocation));
            if (!parameters.empty() || selected) {
                expressionInfo[expr].parameterTypes = parameters;
                if (selected) expressionInfo[expr].calleeSymbol = selected->symbol;
            }
            return;
        }
        for (size_t index = 0; index < expr->arguments.size(); ++index) {
            const std::string expected = constructedType;
            const Result value = EvaluateExpected(expr->arguments[index].get(), expected);
            if (!expected.empty() && !IsAssignable(expected, value.type))
                Report("constructor argument " + std::to_string(index + 1) + " has type '" +
                    value.type + "', expected '" + expected + "'",
                        "E_CONSTRUCTOR_ARGUMENT_TYPE");
            CheckManagedArgumentOwnership(value, expected, index, "constructor");
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
        // A bare generic parameter is not a non-pointer, it is a name that is
        // not a type yet. Judging its shape here would refuse `delete held`
        // for every `T` including the ones it is written for; the instantiation
        // knows what `T` became and answers there instead.
        const bool parameterTarget = IsCurrentGenericParameter(target.type);
        if (parameterTarget) {
            const Symbol* named = table.Get(target.symbol);
            RecordGenericBodyFact(GenericBodyFact::Shape::DeletesValue,
                target.type, named ? named->name : target.type, expr);
        } else if (!IsPointerType(target.type) && target.type != "error")
            Report("delete requires a pointer, got '" + target.type + "'",
                "E_DELETE_REQUIRES_POINTER");
        if (!target.isLValue) Report("delete target must be an assignable pointer variable",
            "E_DELETE_REQUIRES_VARIABLE");
        for (const auto& scope : deferredKeepScopes) {
            if (scope.contains(target.symbol)) {
                Report("pointer is already scheduled for deletion by defer",
                    "E_DELETE_AFTER_DEFER", target.symbol);
                break;
            }
        }
        const auto keep = keepLifetimes.find(target.symbol);
        Symbol* targetSymbol = table.Get(target.symbol);
        if (targetSymbol && targetSymbol->rolePolymorphic &&
            !ownerGuardedParameters.contains(targetSymbol->id)) {
            if (Symbol* callable = table.Get(targetSymbol->callableOwner)) {
                if (callable->parameterRequiresOwner.size() <=
                    targetSymbol->parameterIndex)
                    callable->parameterRequiresOwner.resize(
                        targetSymbol->parameterIndex + 1, false);
                callable->parameterRequiresOwner[
                    targetSymbol->parameterIndex] = true;
                targetSymbol->requiresOwner = true;
            }
        }
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

        if (IsWeakPointerType(target.type))
            Report("weak managed pointer cannot be deleted; delete its owner instead",
                "E_WEAK_DELETE", target.symbol);
        if (IsValueReferenceType(target.type) || (targetSymbol && (targetSymbol->valueReference || targetSymbol->constValueReference)))
            Report("reference parameter or alias cannot be deleted",
                "E_REF_DELETE", target.symbol);
        if (IsManagedPointerType(target.type) && target.pointerValidity != PointerValidity::Null &&
            target.pointerOwner != target.symbol) {
            // A field is the one shape where "delete its owner instead" names
            // the wrong thing: the field *is* the owner, and what deletes it is
            // deleting the object that holds it. Saying "subscriber" there
            // sent the author looking for a second name that does not exist.
            const Symbol* deleted = table.Get(target.symbol);
            if (deleted && deleted->kind == SymbolKind::Field)
                Report("field '" + deleted->name + "' is released with the object "
                    "that holds it, so it cannot be deleted by hand",
                    "E_DELETE_OWNED_FIELD", target.symbol);
            else
                Report("managed subscriber cannot be deleted; delete its owner instead",
                    "E_DELETE_SUBSCRIBER", target.symbol);
        }
        // The same rule, for a field whose type is still a generic parameter.
        else if (!IsManagedPointerType(target.type)) {
            if (const Symbol* deleted = table.Get(target.symbol);
                deleted && deleted->kind == SymbolKind::Field)
                RecordGenericBodyFact(GenericBodyFact::Shape::DeletesField,
                    target.type, deleted->name, expr);
        }

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
        if (fieldDeclaration && !expr->isStatic && expr->value &&
            phase == Phase::CollectDeclarations)
            Report("field '" + name + "' cannot have an initializer here; "
                "assign it in a constructor",
                "E_FIELD_INITIALIZER_UNSUPPORTED");
        if (phase == Phase::CollectDeclarations) {
            if (currentType.empty()) {
                const auto declared = table.Declare(SymbolKind::Variable, declarationName, type);
                if (!declared)
                    Report("object '" + declarationName + "' is already declared in this scope",
                        "E_DUPLICATE_DECLARATION");
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
        if (!IsKnownType(type)) Report("unknown object type '" + type + "'", "E_UNKNOWN_TYPE");
        else if (const auto definition = types.find(definitionName);
            definition != types.end() && definition->second.kind == TypeKind::Interface)
            Report("interface '" + type + "' must be used through raw or managed pointer",
                "E_INTERFACE_REQUIRES_POINTER");
        else if (!expr->value) {
            // Default stack construction uses the zero-argument constructor.
            if (const auto definition = types.find(definitionName);
                definition != types.end()) {
                for (const MemberSignature& constructor : definition->second.constructors) {
                    if (!constructor.parameterTypes.empty()) continue;
                    RequireAccess(constructor.access, definitionName,
                        definitionName, constructor.symbol);
                    break;
                }
            }
        }
        Result value;
        if (expr->value) value = Evaluate(expr->value.get());
        if (expr->value && !IsAssignable(type, value.type))
            Report("initializer of '" + name + "' has type '" + value.type + "', expected '" + type + "'",
                "E_INITIALIZER_TYPE_MISMATCH");
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
        if (expr->value && ArrayRank(type) == 0 && !IsPointerType(type) &&
            TypeOwnsUniqueResource(type) && !transfersAggregateOwner)
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
            if (!declared) Report("object '" + name + "' is already declared in this scope",
                "E_DUPLICATE_DECLARATION");
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
        // A minus in front of a literal belongs to the constant, not to a
        // separate operation, so the range check has to see them together:
        // -2147483648 is an int32 and 2147483648 on its own is not.
        const int previousSign = literalSign;
        if ((expr->op == "-" || expr->op == "+") &&
            dynamic_cast<NumberLiteralExpr*>(expr->operand.get()))
            literalSign = expr->op == "-" ? -previousSign : previousSign;
        const Result operand = Evaluate(expr->operand.get());
        literalSign = previousSign;
        accessMode = previousAccess;
        if (expr->op == "&") {
            if (!operand.isLValue) Report("operator '&' requires an assignable value",
                "E_ADDRESS_REQUIRES_LVALUE");
            Expression* root = expr->operand.get();
            while (root) {
                if (auto* member = dynamic_cast<MemberAccessExpr*>(root)) {
                    root = member->base.get();
                    continue;
                }
                if (auto* array = dynamic_cast<ArrayAccessExpr*>(root)) {
                    root = array->base.get();
                    continue;
                }
                break;
            }
            const ExpressionInfo* rootInfo = root ? GetExpressionInfo(*root) : nullptr;
            const Symbol* rootSymbol = rootInfo ? table.Get(rootInfo->symbol) : nullptr;
            if (rootSymbol && rootSymbol->valueReference)
                Report("a value-reference parameter cannot be converted to a raw pointer",
                    "E_VALUE_REF_ADDRESS_ESCAPE", rootSymbol->id);
            CheckMutableTarget(expr->operand.get(), operand, "taking a mutable address");
            Save(expr, {InvalidSymbolId, "raw " + operand.type + "*", false, false, false,
                InitializationState::Initialized, PointerValidity::Live, operand.symbol});
            return;
        }
        if (expr->op == "*") {
            if (!IsPointerType(operand.type)) {
                Report("operator '*' requires a pointer", "E_DEREFERENCE_REQUIRES_POINTER");
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
            // A raw pointer steps by one element, the same rule `p + 1`
            // already follows. Only raw: a managed pointer has no arithmetic
            // at all, and stepping one would walk off its own allocation.
            if (!operand.isLValue ||
                (!IsNumeric(operand.type) && !IsRawPointerType(operand.type)))
                Report("operator '" + expr->op +
                    "' requires an assignable numeric or raw pointer operand",
                        "E_INCREMENT_OPERAND");
            const Symbol* symbol = table.Get(operand.symbol);
            if (symbol && (symbol->kind == SymbolKind::Property ||
                symbol->kind == SymbolKind::Indexer) && !symbol->canRead)
                Report("operator '" + expr->op + "' requires a readable property or indexer",
                    "E_PROPERTY_COMPOUND_REQUIRES_GETTER", operand.symbol);
            CheckMutableTarget(expr->operand.get(), operand, "operator '" + expr->op + "'");
        }
        else if (expr->op == "!" && !IsConditionType(operand.type)) Report("operator '!' requires a boolean-compatible operand",
            "E_LOGICAL_NOT_OPERAND");
        else if ((expr->op == "+" || expr->op == "-") && !IsNumeric(operand.type)) Report("unary numeric operator requires a number",
            "E_UNARY_NUMERIC_OPERAND");
        else if (expr->op == "~" && !IsInteger(operand.type)) Report("operator '~' requires an integer",
            "E_COMPLEMENT_OPERAND");
        Save(expr, {operand.symbol, expr->op == "!" ? "bool" : operand.type, false});
    }

    void Analyzer::Visit(PostfixUnaryExpr* expr) {
        const AccessMode previousAccess = accessMode;
        accessMode = AccessMode::Write;
        const Result operand = Evaluate(expr->operand.get());
        accessMode = previousAccess;
        if (!operand.isLValue ||
            (!IsNumeric(operand.type) && !IsRawPointerType(operand.type)))
            Report("operator '" + expr->op +
                "' requires an assignable numeric or raw pointer operand",
                    "E_INCREMENT_OPERAND");
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
        if (typeContext && templateName == "tuple") {
            if (expr->types.size() < 2) {
                Report("tuple requires at least two element types",
                    "E_TUPLE_TYPE_ARITY");
                Save(expr, {InvalidSymbolId, "error", false});
                return;
            }
            std::vector<std::string> elements;
            elements.reserve(expr->types.size());
            for (const auto& type : expr->types) {
                const std::string element = ResolveType(type.get());
                if (element == "void" || element == "auto" ||
                    element == "dynamic")
                    Report("tuple elements require concrete non-void types",
                        "E_TUPLE_ELEMENT_TYPE");
                if (TypeOwnsUniqueResource(element))
                    Report("tuple element type '" + element +
                        "' owns resources and is not supported yet",
                        "E_TUPLE_RESOURCE_ELEMENT");
                elements.push_back(element);
            }
            std::string tupleType = "tuple<";
            for (size_t index = 0; index < elements.size(); ++index) {
                if (index) tupleType += ",";
                tupleType += elements[index];
            }
            tupleType += ">";
            Save(expr, {InvalidSymbolId, std::move(tupleType), false});
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
        if (typeContext && templateName == "cfunc") {
            if (expr->types.empty()) {
                Report("cfunc requires a return type", "E_CFUNC_TYPE_ARITY");
                Save(expr, {InvalidSymbolId, "error", false});
                return;
            }
            std::vector<std::string> types;
            types.reserve(expr->types.size());
            for (const auto& type : expr->types) types.push_back(ResolveType(type.get()));
            if (types.front() == "auto" || types.front() == "dynamic")
                Report("cfunc return type must be concrete", "E_CFUNC_TYPE_RETURN");
            for (size_t index = 1; index < types.size(); ++index)
                if (types[index] == "void" || types[index] == "auto" || types[index] == "dynamic")
                    Report("cfunc parameter types must be concrete non-void types",
                        "E_CFUNC_TYPE_PARAMETER");
            // Each component must itself be C-ABI safe (no Absolute aggregates/func/managed).
            ValidateCAbiType(types.front(), "cfunc return", true);
            for (size_t index = 1; index < types.size(); ++index)
                ValidateCAbiType(types[index],
                    "cfunc parameter " + std::to_string(index), false);
            Save(expr, {InvalidSymbolId,
                CFunctionTypeName(types.front(),
                    std::vector<std::string>(types.begin() + 1, types.end())),
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
            specialized = ResolveTypeReference(specialized);
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
        std::string expectedReturn;
        std::vector<std::string> expectedParameters;
        const bool hasExpectedSignature = ParseFunctionType(
            expectedType, expectedReturn, expectedParameters);
        const std::string savedExpectedType = expectedType;
        expectedType.clear();

        std::vector<std::string> parameterTypes;
        parameterTypes.reserve(expr->parameters.size());
        table.EnterScope();
        lambdaContexts.push_back({expr, table.ScopeDepth()});
        ++functionDepth;
        const int savedLoopDepth = loopDepth;
        const int savedCatchDepth = catchDepth;
        const int savedFinallyDepth = finallyDepth;
        const int savedDeferDepth = deferDepth;
        const bool savedAsync = currentFunctionAsync;
        const bool savedFlowTerminated = flowTerminated;
        const std::string savedReturnType = currentReturnType;
        loopDepth = catchDepth = finallyDepth = deferDepth = 0;
        currentFunctionAsync = false;
        flowTerminated = false;
        for (const auto& parameter : expr->parameters) {
            if (!parameter) continue;
            const std::string name = ExtractIdentifier(parameter->name.get());
            const std::string type = ResolveDeclaredType(*parameter);
            if (parameter->isReference)
                Report("lambda parameters cannot be value references",
                    "E_VALUE_REF_LAMBDA");
            parameterTypes.push_back(type);
            if (name.empty()) Report("lambda parameter requires a name", "E_LAMBDA_PARAMETER");
            else if (const auto declared = table.Declare(SymbolKind::Parameter, name, type))
                expressionInfo[parameter.get()] = {*declared, type, true};
            else Report("duplicate lambda parameter '" + name + "'", "E_LAMBDA_PARAMETER");
            if (parameter->value)
                Report("lambda parameters cannot have default values", "E_LAMBDA_DEFAULT_PARAMETER");
        }

        std::string lambdaReturnType;
        Result body;
        bool returnsEverywhere = true;
        if (expr->expressionBody) {
            body = hasExpectedSignature
                ? EvaluateExpected(expr->expressionBody.get(), expectedReturn)
                : Evaluate(expr->expressionBody.get());
            lambdaReturnType = hasExpectedSignature ? expectedReturn : body.type;
            if (body.type == "void")
                Report("an expression lambda cannot return void", "E_LAMBDA_VOID_EXPRESSION");
        }
        else {
            const std::string explicitReturn = expr->returnType
                ? ResolveType(expr->returnType.get()) : std::string{};
            if (!explicitReturn.empty() && hasExpectedSignature &&
                explicitReturn != expectedReturn)
                Report("lambda declares return type '" + explicitReturn +
                    "', expected '" + expectedReturn + "'", "E_LAMBDA_RETURN");
            lambdaReturnType = !explicitReturn.empty() ? explicitReturn : expectedReturn;
            if (lambdaReturnType.empty()) {
                Report("a statement lambda needs '-> ReturnType' when its func type is not known",
                    "E_LAMBDA_RETURN_TYPE");
                lambdaReturnType = "error";
            }
            currentReturnType = lambdaReturnType;
            lambdaFunctionBoundaries.push_back(
                {keepScopes.size(), valueFlowScopes.size()});
            AcceptIfPresent(expr->statementBody, *this);
            returnsEverywhere = flowTerminated;
            lambdaFunctionBoundaries.pop_back();
            if (lambdaReturnType != "void" && lambdaReturnType != "error" &&
                !returnsEverywhere)
                Report("lambda does not return a value on every control-flow path",
                    "E_LAMBDA_MISSING_RETURN");
        }

        lambdaCaptures[expr] = std::move(lambdaContexts.back().captures);
        lambdaContexts.pop_back();
        --functionDepth;
        table.ExitScope();
        loopDepth = savedLoopDepth;
        catchDepth = savedCatchDepth;
        finallyDepth = savedFinallyDepth;
        deferDepth = savedDeferDepth;
        currentFunctionAsync = savedAsync;
        flowTerminated = savedFlowTerminated;
        currentReturnType = savedReturnType;
        expectedType = savedExpectedType;

        const std::string type = FunctionTypeName(lambdaReturnType, parameterTypes);
        if (hasExpectedSignature) {
            if (expectedParameters != parameterTypes)
                Report("lambda parameter types do not match expected type '" + savedExpectedType + "'",
                    "E_LAMBDA_SIGNATURE");
            if (expr->expressionBody && !IsAssignable(expectedReturn, body.type))
                Report("lambda returns '" + body.type + "', expected '" + expectedReturn + "'",
                    "E_LAMBDA_RETURN");
        }
        Save(expr, {InvalidSymbolId, type, false});
    }

}
