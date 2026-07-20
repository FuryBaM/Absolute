#include "analyzer_internal.h"

namespace Absolute {
    std::optional<Analyzer::MemberSignature> Analyzer::FindConcreteMethod(
        const std::string& owner, const std::string& name,
        const std::vector<std::string>& parameterTypes) const {
        const auto found = types.find(owner);
        if (found == types.end() || found->second.kind != TypeKind::Class) return std::nullopt;
        if (const auto members = found->second.members.find(name); members != found->second.members.end()) {
            for (const MemberSignature& member : members->second)
                if (member.kind == SymbolKind::Method && !member.isStatic &&
                    member.parameterTypes == parameterTypes)
                    return member;
        }
        for (const std::string& parent : found->second.parents) {
            const auto parentType = types.find(parent);
            if (parentType != types.end() && parentType->second.kind == TypeKind::Class)
                if (auto method = FindConcreteMethod(parent, name, parameterTypes)) return method;
        }
        return std::nullopt;
    }

    void Analyzer::ValidateInterfaceImplementation(const std::string& className) {
        const auto found = types.find(className);
        if (found == types.end()) return;

        struct InterfaceRequirement {
            std::string contract;
            std::string methodName;
            MemberSignature signature;
        };
        std::unordered_map<std::string, std::vector<InterfaceRequirement>> requirements;
        const auto requirementKey = [](const std::string& methodName,
            const std::vector<std::string>& parameterTypes) {
            std::string key = methodName;
            for (const std::string& parameterType : parameterTypes)
                key += "\x1f" + parameterType;
            return key;
        };
        for (const std::string& parent : found->second.parents) {
            const auto contract = types.find(parent);
            if (contract == types.end() || contract->second.kind != TypeKind::Interface) continue;
            for (const auto& [methodName, overloads] : VisibleMembers(parent)) {
                for (const MemberSignature& requirement : overloads) {
                    if (requirement.kind != SymbolKind::Method) continue;
                    requirements[requirementKey(methodName, requirement.parameterTypes)]
                        .push_back({parent, methodName, requirement});
                }
            }
        }

        for (const auto& [key, contracts] : requirements) {
            (void)key;
            const InterfaceRequirement& first = contracts.front();
            const auto implementation = FindConcreteMethod(
                className, first.methodName, first.signature.parameterTypes);
            if (!implementation) {
                std::unordered_set<SymbolId> defaults;
                for (const InterfaceRequirement& contract : contracts) {
                    FunctionDeclStmt* declaration = FunctionDeclaration(contract.signature.symbol);
                    if (declaration && declaration->body) defaults.insert(contract.signature.symbol);
                }
                if (defaults.empty()) {
                    Report("class '" + className + "' does not implement interface method '" +
                        first.contract + "." + first.methodName + "'");
                }
                else if (defaults.size() > 1) {
                    Report("class '" + className + "' inherits multiple default implementations of '" +
                        first.methodName + "'; declare an override to resolve the ambiguity",
                        "E_INTERFACE_DEFAULT_AMBIGUOUS");
                }
                else {
                    const Symbol* defaultSymbol = table.Get(*defaults.begin());
                    for (const InterfaceRequirement& contract : contracts) {
                        if (defaultSymbol && (defaultSymbol->type != contract.signature.type ||
                            defaultSymbol->isConst != contract.signature.isConst)) {
                            Report("default interface method '" + defaultSymbol->name +
                                "' does not match the contract of interface '" + contract.contract + "'",
                                "E_INTERFACE_DEFAULT_CONTRACT");
                        }
                    }
                }
                continue;
            }
            for (const InterfaceRequirement& contract : contracts) {
                const MemberSignature& requirement = contract.signature;
                if (implementation->type != requirement.type) {
                    Report("class method '" + className + "." + contract.methodName +
                        "' returns '" + implementation->type + "', but interface '" + contract.contract +
                        "' requires '" + requirement.type + "'");
                }
                else if (implementation->isConst != requirement.isConst) {
                    Report("class method '" + className + "." + contract.methodName +
                        "' does not match the const contract of interface '" + contract.contract + "'");
                }
                else if (implementation->access != AccessLevel::Public) {
                    Report("class method '" + className + "." + contract.methodName +
                        "' implements interface '" + contract.contract + "' and must be public",
                        "E_INTERFACE_IMPLEMENTATION_ACCESS", implementation->symbol);
                }
            }
        }
    }

    void Analyzer::Visit(StructDeclStmt* stmt) {
        const std::string typeName = Qualify(stmt->name);
        std::unordered_map<std::string, std::string> genericScope;
        for (const Token& parameter : stmt->templateParams)
            genericScope.emplace(parameter.value, parameter.value);
        if (phase == Phase::CollectTypeNames) {
            DeclareType(stmt->name, TypeKind::Struct);
            for (const Token& parameter : stmt->templateParams)
                types[typeName].genericParameters.push_back(parameter.value);
            return;
        }
        if (phase == Phase::CollectDeclarations) {
            if (!genericScope.empty()) genericTypeScopes.push_back(genericScope);
            const std::string old = currentType;
            currentType = typeName;
            for (const auto& member : stmt->members) if (member) member->Accept(*this);
            currentType = old;
            if (!genericScope.empty()) genericTypeScopes.pop_back();
            return;
        }
        ValidateAttributes(*stmt, "struct declaration", false);
        if (!genericScope.empty()) genericTypeScopes.push_back(genericScope);
        const std::string old = currentType;
        currentType = typeName;
        table.EnterScope();
        for (const auto& [name, overloads] : types[typeName].members)
            for (const MemberSignature& member : overloads) {
                const auto declared = table.Declare(
                    member.kind, name, member.type, member.parameterTypes);
                if (declared) {
                    Symbol* symbol = table.Get(*declared);
                    symbol->isConst = member.isConst;
                    symbol->isStatic = member.isStatic;
                    symbol->access = member.access;
                    if (const Symbol* original = table.Get(member.symbol))
                        symbol->memberOwner = original->memberOwner;
                }
            }
        for (const auto& member : stmt->members) if (member) member->Accept(*this);
        table.ExitScope();
        currentType = old;
        if (!genericScope.empty()) genericTypeScopes.pop_back();
    }

    void Analyzer::Visit(ClassDeclStmt* stmt) {
        const std::string typeName = Qualify(stmt->name);
        std::unordered_map<std::string, std::string> genericScope;
        for (const Token& parameter : stmt->templateParams)
            genericScope.emplace(parameter.value, parameter.value);
        if (phase == Phase::CollectTypeNames) {
            DeclareType(stmt->name, TypeKind::Class);
            auto& definition = types[typeName];
            for (const Token& parameter : stmt->templateParams)
                definition.genericParameters.push_back(parameter.value);
            return;
        }
        if (phase == Phase::CollectDeclarations) {
            auto& definition = types[typeName];
            if (!genericScope.empty()) genericTypeScopes.push_back(genericScope);
            definition.parents.clear();
            for (const std::string& parent : stmt->parents)
                definition.parents.push_back(ResolveTypeReference(parent));
            const std::string old = currentType;
            currentType = typeName;
            if (stmt->body) stmt->body->Accept(*this);
            currentType = old;
            if (!genericScope.empty()) genericTypeScopes.pop_back();
            return;
        }
        ValidateAttributes(*stmt, "class declaration", false);
        if (!genericScope.empty()) genericTypeScopes.push_back(genericScope);
        size_t classParentCount = 0;
        for (const std::string& parent : stmt->parents) {
            const std::string resolvedParent = ResolveTypeReference(parent);
            if (!types.contains(resolvedParent))
                Report("unknown parent type '" + parent + "' of class '" + typeName + "'");
            else if (types[resolvedParent].kind == TypeKind::Class) ++classParentCount;
            else if (types[resolvedParent].kind != TypeKind::Interface)
                Report("class '" + typeName + "' cannot inherit non-class type '" + resolvedParent + "'");
        }
        if (classParentCount > 1)
            Report("class '" + typeName + "' cannot inherit more than one class");
        const std::string old = currentType;
        currentType = typeName;
        table.EnterScope();
        for (const auto& [name, overloads] : VisibleMembers(typeName))
            for (const MemberSignature& member : overloads) {
                const auto declared = table.Declare(
                    member.kind, name, member.type, member.parameterTypes);
                if (declared) {
                    Symbol* symbol = table.Get(*declared);
                    symbol->isConst = member.isConst;
                    symbol->isStatic = member.isStatic;
                    symbol->access = member.access;
                    if (const Symbol* original = table.Get(member.symbol))
                        symbol->memberOwner = original->memberOwner;
                }
            }
        if (stmt->body) stmt->body->Accept(*this);
        if (!types[typeName].constructor) {
            const std::string baseClass = DirectBaseClass(typeName);
            const auto baseParameters = ConstructorParameterTypes(baseClass);
            if (!baseClass.empty() && baseParameters && !baseParameters->empty())
                Report("implicit constructor of '" + typeName + "' cannot call base constructor '" +
                    baseClass + "' without arguments; declare a constructor with base(...)",
                    "E_BASE_CONSTRUCTOR_REQUIRED");
        }
        ValidateInterfaceImplementation(typeName);
        table.ExitScope();
        currentType = old;
        if (!genericScope.empty()) genericTypeScopes.pop_back();
    }

    void Analyzer::Visit(InterfaceDeclStmt* stmt) {
        const std::string typeName = Qualify(stmt->name);
        if (phase == Phase::CollectTypeNames) {
            DeclareType(stmt->name, TypeKind::Interface);
            return;
        }
        if (phase == Phase::CollectDeclarations) {
            auto& definition = types[typeName];
            definition.parents.clear();
            for (const std::string& parent : stmt->parents)
                definition.parents.push_back(ResolveTypeReference(parent));
            const std::string old = currentType;
            currentType = typeName;
            for (const auto& method : stmt->methods) if (method) method->Accept(*this);
            currentType = old;
            return;
        }
        ValidateAttributes(*stmt, "interface declaration", false);
        for (const std::string& parent : stmt->parents) {
            const std::string resolvedParent = ResolveTypeReference(parent);
            const auto found = types.find(resolvedParent);
            if (found == types.end())
                Report("unknown parent interface '" + parent + "' of interface '" + typeName + "'");
            else if (found->second.kind != TypeKind::Interface)
                Report("interface '" + typeName + "' can only inherit another interface");
        }
        const std::string old = currentType;
        currentType = typeName;
        table.EnterScope();
        for (const auto& [name, overloads] : VisibleMembers(typeName))
            for (const MemberSignature& member : overloads) {
                const auto declared = table.Declare(
                    member.kind, name, member.type, member.parameterTypes);
                if (declared) {
                    Symbol* symbol = table.Get(*declared);
                    symbol->isConst = member.isConst;
                    symbol->isStatic = member.isStatic;
                    symbol->access = member.access;
                    if (const Symbol* original = table.Get(member.symbol))
                        symbol->memberOwner = original->memberOwner;
                }
            }
        for (const auto& method : stmt->methods) if (method) method->Accept(*this);
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
            else {
                MemberSignature constructor{SymbolKind::Constructor, currentType,
                    ResolveParameterTypes(stmt->parameters)};
                constructor.access = DeclaredAccess(*stmt);
                constructor.owner = currentType;
                types[currentType].constructor = std::move(constructor);
            }
            return;
        }
        ValidateAccessModifiers(*stmt, true, "constructor");
        if (types[currentType].kind == TypeKind::Struct &&
            DeclaredAccess(*stmt) == AccessLevel::Protected)
            Report("struct constructor cannot be protected", "E_PROTECTED_STRUCT_MEMBER");
        ValidateAttributes(*stmt, "constructor", true);
        if (HasModifier(*stmt, "const"))
            Report("constructors cannot be const", "E_CONST_CONSTRUCTOR");
        if (HasModifier(*stmt, "static"))
            Report("constructors cannot be static", "E_STATIC_CONSTRUCTOR");
        if (stmt->name && Qualify(stmt->name->value) != currentType)
            Report("constructor '" + stmt->name->value + "' must match type '" + currentType + "'");
        ++functionDepth;
        const bool oldConstructor = currentConstructor;
        currentConstructor = true;
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
            if (const auto declared = table.Declare(SymbolKind::Parameter, name, type)) {
                table.Get(*declared)->isConst = parameter && parameter->isConst;
                RegisterFlowSymbol(*declared, {InitializationState::Initialized,
                    IsPointerType(type) ? PointerValidity::Unknown : PointerValidity::NotPointer,
                    InvalidSymbolId,
                    IsTaskType(type) ? TaskState::Unknown : TaskState::NotTask});
            }
            else Report("parameter '" + name + "' is already declared");
        }
        const std::string baseClass = DirectBaseClass(currentType);
        if (stmt->hasExplicitBaseCall && baseClass.empty()) {
            Report("constructor of '" + currentType + "' calls base(...), but the type has no base class",
                "E_BASE_WITHOUT_CLASS");
            for (const auto& argument : stmt->baseArguments) Evaluate(argument.get());
        }
        else if (!baseClass.empty()) {
            const auto declaredParameters = ConstructorParameterTypes(baseClass);
            std::string baseDefinition = baseClass;
            std::string genericBase;
            std::vector<std::string> genericArguments;
            if (ParseGenericTypeName(baseClass, genericBase, genericArguments))
                baseDefinition = genericBase;
            if (const auto base = types.find(baseDefinition);
                base != types.end() && base->second.constructor)
                RequireAccess(base->second.constructor->access, baseDefinition,
                    baseDefinition, base->second.constructor->symbol);
            const std::vector<std::string> expected = declaredParameters.value_or(
                std::vector<std::string>{});
            if (!stmt->hasExplicitBaseCall && declaredParameters && !expected.empty()) {
                Report("constructor of '" + currentType + "' must call base(...) with " +
                    std::to_string(expected.size()) + " argument(s)", "E_BASE_CONSTRUCTOR_REQUIRED");
            }
            if (stmt->hasExplicitBaseCall) {
                if (stmt->baseArguments.size() != expected.size())
                    Report("base constructor of '" + baseClass + "' expects " +
                        std::to_string(expected.size()) + " argument(s), got " +
                        std::to_string(stmt->baseArguments.size()), "E_BASE_ARGUMENT_COUNT");
                for (size_t index = 0; index < stmt->baseArguments.size(); ++index) {
                    const std::string expectedType = index < expected.size() ? expected[index] : std::string{};
                    const Result argument = EvaluateExpected(
                        stmt->baseArguments[index].get(), expectedType);
                    if (!expectedType.empty() && !IsAssignable(expectedType, argument.type))
                        Report("base constructor argument " + std::to_string(index + 1) +
                            " has type '" + argument.type + "', expected '" + expectedType + "'",
                            "E_BASE_ARGUMENT_TYPE", argument.symbol);
                }
            }
        }
        if (stmt->body) stmt->body->Accept(*this);
        PopValueFlowScope();
        PopKeepScope();
        keepLifetimes.clear();
        valueFlow.clear();
        table.ExitScope();
        --functionDepth;
        currentConstructor = oldConstructor;
    }

    void Analyzer::Visit(EnumDeclStmt* stmt) {
        const std::string typeName = Qualify(stmt->name);
        if (phase == Phase::CollectTypeNames) {
            DeclareType(stmt->name, TypeKind::Enum);
            return;
        }
        if (phase == Phase::ResolveBodies) {
            ValidateAttributes(*stmt, "enum declaration", false);
            return;
        }
        if (phase != Phase::CollectDeclarations) return;
        TypeDefinition& definition = types[typeName];
        definition.enumMembers.clear();
        std::unordered_set<std::string> members;
        for (const std::string& member : stmt->members) {
            if (!members.insert(member).second) {
                Report("enum '" + typeName + "' contains duplicate member '" + member + "'",
                    "E_DUPLICATE_ENUM_MEMBER");
                continue;
            }
            definition.enumMembers.push_back(member);
            const std::string qualifiedMember = typeName + "." + member;
            if (!table.Declare(SymbolKind::Variable, qualifiedMember, typeName))
                Report("enum member '" + qualifiedMember + "' is already declared",
                    "E_DUPLICATE_ENUM_MEMBER");
        }
    }

    void Analyzer::Visit(GroupDeclStmt* stmt) {
        if (phase == Phase::CollectTypeNames) {
            DeclareType(stmt->name);
        }
        else if (phase == Phase::ResolveBodies)
            ValidateAttributes(*stmt, "group declaration", false);
        for (const auto& declaration : stmt->enums) if (declaration) declaration->Accept(*this);
        for (const auto& declaration : stmt->subgroups) if (declaration) declaration->Accept(*this);
    }
}
