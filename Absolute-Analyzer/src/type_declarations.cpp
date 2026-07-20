#include "analyzer_build_pch.h"

namespace Absolute {
    std::optional<Analyzer::MemberSignature> Analyzer::FindConcreteMethod(
        const std::string& owner, const std::string& name,
        const std::vector<std::string>& parameterTypes) const {
        const auto found = types.find(owner);
        if (found == types.end() || found->second.kind != TypeKind::Class) return std::nullopt;
        if (const auto members = found->second.members.find(name); members != found->second.members.end()) {
            for (const MemberSignature& member : members->second)
                if (member.kind == SymbolKind::Method && member.parameterTypes == parameterTypes)
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
        for (const std::string& parent : found->second.parents) {
            const auto contract = types.find(parent);
            if (contract == types.end() || contract->second.kind != TypeKind::Interface) continue;
            for (const auto& [methodName, overloads] : VisibleMembers(parent)) {
                for (const MemberSignature& requirement : overloads) {
                    if (requirement.kind != SymbolKind::Method) continue;
                    const auto implementation = FindConcreteMethod(
                        className, methodName, requirement.parameterTypes);
                    if (!implementation) {
                        Report("class '" + className + "' does not implement interface method '" +
                            parent + "." + methodName + "'");
                    }
                    else if (implementation->type != requirement.type) {
                        Report("class method '" + className + "." + methodName +
                            "' returns '" + implementation->type + "', but interface '" + parent +
                            "' requires '" + requirement.type + "'");
                    }
                }
            }
        }
    }

    void Analyzer::Visit(StructDeclStmt* stmt) {
        const std::string typeName = Qualify(stmt->name);
        if (phase == Phase::CollectDeclarations) {
            DeclareType(stmt->name, TypeKind::Struct);
            const std::string old = currentType;
            currentType = typeName;
            for (const auto& member : stmt->members) if (member) member->Accept(*this);
            currentType = old;
            return;
        }
        const std::string old = currentType;
        currentType = typeName;
        table.EnterScope();
        for (const auto& [name, overloads] : types[typeName].members)
            for (const MemberSignature& member : overloads)
                table.Declare(member.kind, name, member.type, member.parameterTypes);
        for (const auto& member : stmt->members) if (member) member->Accept(*this);
        table.ExitScope();
        currentType = old;
    }

    void Analyzer::Visit(ClassDeclStmt* stmt) {
        const std::string typeName = Qualify(stmt->name);
        if (phase == Phase::CollectDeclarations) {
            DeclareType(stmt->name, TypeKind::Class);
            auto& definition = types[typeName];
            definition.parents.clear();
            for (const std::string& parent : stmt->parents)
                definition.parents.push_back(ResolveTypeReference(parent));
            const std::string old = currentType;
            currentType = typeName;
            if (stmt->body) stmt->body->Accept(*this);
            currentType = old;
            return;
        }
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
            for (const MemberSignature& member : overloads)
                table.Declare(member.kind, name, member.type, member.parameterTypes);
        if (stmt->body) stmt->body->Accept(*this);
        ValidateInterfaceImplementation(typeName);
        table.ExitScope();
        currentType = old;
    }

    void Analyzer::Visit(InterfaceDeclStmt* stmt) {
        const std::string typeName = Qualify(stmt->name);
        if (phase == Phase::CollectDeclarations) {
            DeclareType(stmt->name, TypeKind::Interface);
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
            for (const MemberSignature& member : overloads)
                table.Declare(member.kind, name, member.type, member.parameterTypes);
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
            else types[currentType].constructor = MemberSignature{SymbolKind::Constructor, currentType,
                ResolveParameterTypes(stmt->parameters)};
            return;
        }
        if (stmt->name && Qualify(stmt->name->value) != currentType)
            Report("constructor '" + stmt->name->value + "' must match type '" + currentType + "'");
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
        for (const auto& parameter : stmt->parameters) {
            const std::string name = parameter ? ExtractIdentifier(parameter->name.get()) : std::string{};
            const std::string type = parameter ? ResolveDeclaredType(*parameter) : "error";
            if (const auto declared = table.Declare(SymbolKind::Parameter, name, type))
                RegisterFlowSymbol(*declared, {InitializationState::Initialized,
                    IsPointerType(type) ? PointerValidity::Unknown : PointerValidity::NotPointer,
                    InvalidSymbolId,
                    IsTaskType(type) ? TaskState::Unknown : TaskState::NotTask});
            else Report("parameter '" + name + "' is already declared");
        }
        if (stmt->body) stmt->body->Accept(*this);
        PopValueFlowScope();
        PopKeepScope();
        keepLifetimes.clear();
        valueFlow.clear();
        table.ExitScope();
        --functionDepth;
    }

    void Analyzer::Visit(EnumDeclStmt* stmt) {
        if (phase == Phase::CollectDeclarations) DeclareType(stmt->name);
    }

    void Analyzer::Visit(GroupDeclStmt* stmt) {
        if (phase == Phase::CollectDeclarations) DeclareType(stmt->name);
        for (const auto& declaration : stmt->enums) if (declaration) declaration->Accept(*this);
        for (const auto& declaration : stmt->subgroups) if (declaration) declaration->Accept(*this);
    }
}
