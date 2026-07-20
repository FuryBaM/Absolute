#include "codegen_internal.h"

namespace Absolute {
    llvm::Type* CodeGenerator::Impl::TypeFromName(const std::string& name) {
        if (ArrayRankName(name) > 0) return ArrayDescriptorType(name);
        if (IsTaskTypeName(name)) return builder.getPtrTy();
        if (IsManagedPointerTypeName(name)) return builder.getInt64Ty();
        if (IsRawPointerTypeName(name)) return builder.getPtrTy();
        if (name == "int8" || name == "uint8" || name == "char") return builder.getInt8Ty();
        if (name == "int16" || name == "uint16") return builder.getInt16Ty();
        if (name == "int32" || name == "uint32") return builder.getInt32Ty();
        if (name == "int64" || name == "uint64") return builder.getInt64Ty();
        if (name == "bool") return builder.getInt1Ty();
        if (name == "float") return builder.getFloatTy();
        if (name == "double") return builder.getDoubleTy();
        if (name == "string") return builder.getPtrTy();
        if (name == "void") return builder.getVoidTy();
        if (enumTypes.contains(name)) return builder.getInt32Ty();
        if (classes.contains(name)) {
            FinalizeClass(name);
            return classes.at(name).llvmType;
        }
        if (structs.contains(name)) {
            FinalizeStruct(name);
            return structs.at(name).llvmType;
        }
        Fail("unsupported type '" + name + "'");
    }

    llvm::StructType* CodeGenerator::Impl::ArrayDescriptorType(const std::string& name) {
        if (const auto found = arrayDescriptorTypes.find(name); found != arrayDescriptorTypes.end())
            return found->second;
        const size_t rank = ArrayRankName(name);
        if (rank == 0) Fail("array descriptor requires an array type");
        std::vector<llvm::Type*> fields{builder.getPtrTy()};
        fields.insert(fields.end(), rank, builder.getInt64Ty());
        llvm::StructType* descriptor = llvm::StructType::create(
            context, fields, "absolute.array." + ArrayElementTypeName(name, rank) + "." + std::to_string(rank));
        arrayDescriptorTypes.emplace(name, descriptor);
        return descriptor;
    }

    llvm::Type* CodeGenerator::Impl::ResolveType(Expression* expression) {
        return TypeFromName(ResolveTypeName(expression));
    }

    std::string CodeGenerator::Impl::ResolveTypeName(Expression* expression) {
        if (!expression) Fail("missing type expression");
        if (analyzer) {
            if (const ExpressionInfo* info = analyzer->GetExpressionInfo(*expression);
                info && !info->type.empty() && info->type != "error")
                return SubstituteCodegenType(info->type, currentGenericSubstitutions);
        }
        PrimitiveTypeNameVisitor typeVisitor;
        expression->Accept(typeVisitor);
        if (typeVisitor.name.empty()) Fail("cannot resolve type expression");
        return typeVisitor.name;
    }

    std::string CodeGenerator::Impl::DeclaredTypeName(VarDeclExpr& expression) {
        if (analyzer) {
            if (const ExpressionInfo* info = analyzer->GetExpressionInfo(expression);
                info && !info->type.empty() && info->type != "error")
                return SubstituteCodegenType(info->type, currentGenericSubstitutions);
        }
        std::string type = ResolveTypeName(expression.type.get());
        if (const auto* declarator = dynamic_cast<const ArrayAccessExpr*>(expression.name.get()))
            for (size_t index = 0; index < declarator->indexes.size(); ++index) type += "[]";
        return type;
    }

    std::string CodeGenerator::Impl::QualifiedClassName(const std::string& name, const std::string& nameSpace) const {
        if (name.empty() || name.find('.') != std::string::npos || nameSpace.empty()) return name;
        return nameSpace + "." + name;
    }

    void CodeGenerator::Impl::CollectClass(ClassDeclStmt& statement, const std::string& nameSpace,
        const std::string& specializedName,
        std::unordered_map<std::string, std::string> substitutions) {
        const std::string name = specializedName.empty()
            ? QualifiedClassName(statement.name, nameSpace) : specializedName;
        if (classes.contains(name) || structs.contains(name) || interfaces.contains(name) || enumTypes.contains(name))
            Fail("duplicate type '" + name + "'");
        ClassInfo info;
        info.name = name;
        info.nameSpace = nameSpace;
        info.statement = &statement;
        info.substitutions = std::move(substitutions);
        info.llvmType = llvm::StructType::create(context, "absolute.class." + name);
        for (const std::string& parent : statement.parents)
            info.parents.push_back(SubstituteCodegenType(
                QualifiedClassName(parent, nameSpace), info.substitutions));

        auto* body = dynamic_cast<CompoundStmt*>(statement.body.get());
        if (!body) Fail("class '" + name + "' requires a compound body");
        for (const auto& member : body->statements) {
            if (auto* declaration = dynamic_cast<VarDeclStmt*>(member.get())) {
                if (declaration->expr) {
                    if (declaration->expr->isStatic)
                        info.ownStaticPrimitiveFields.push_back(declaration->expr.get());
                    else info.ownPrimitiveFields.push_back(declaration->expr.get());
                }
            }
            else if (auto* single = dynamic_cast<SingleStatement*>(member.get())) {
                if (auto* instance = dynamic_cast<InstanceDeclExpr*>(single->expr.get()))
                    if (instance->isStatic) info.ownStaticObjectFields.push_back(instance);
                    else info.ownObjectFields.push_back(instance);
            }
            else if (auto* method = dynamic_cast<FunctionDeclStmt*>(member.get()))
                info.ownMethods.push_back(method);
            else if (auto* constructor = dynamic_cast<ConstructorDeclStmt*>(member.get())) {
                if (info.constructor) Fail("class '" + name + "' has multiple constructors");
                info.constructor = constructor;
            }
        }
        classes.emplace(name, std::move(info));
        classOrder.push_back(name);
    }

    void CodeGenerator::Impl::CollectStruct(StructDeclStmt& statement, const std::string& nameSpace,
        const std::string& specializedName,
        std::unordered_map<std::string, std::string> substitutions) {
        const std::string name = specializedName.empty()
            ? QualifiedClassName(statement.name, nameSpace) : specializedName;
        if (structs.contains(name) || classes.contains(name) || interfaces.contains(name) || enumTypes.contains(name))
            Fail("duplicate type '" + name + "'");
        StructInfo info;
        info.name = name;
        info.nameSpace = nameSpace;
        info.statement = &statement;
        info.substitutions = std::move(substitutions);
        info.llvmType = llvm::StructType::create(context, "absolute.struct." + name);
        for (const auto& member : statement.members) {
            if (auto* declaration = dynamic_cast<VarDeclStmt*>(member.get())) {
                if (declaration->expr) {
                    if (declaration->expr->isStatic)
                        info.ownStaticPrimitiveFields.push_back(declaration->expr.get());
                    else info.ownPrimitiveFields.push_back(declaration->expr.get());
                }
            }
            else if (auto* single = dynamic_cast<SingleStatement*>(member.get())) {
                if (auto* instance = dynamic_cast<InstanceDeclExpr*>(single->expr.get()))
                    if (instance->isStatic) info.ownStaticObjectFields.push_back(instance);
                    else info.ownObjectFields.push_back(instance);
            }
            else if (auto* method = dynamic_cast<FunctionDeclStmt*>(member.get()))
                info.ownMethods.push_back(method);
            else if (auto* constructor = dynamic_cast<ConstructorDeclStmt*>(member.get())) {
                if (info.constructor) Fail("struct '" + name + "' has multiple constructors");
                info.constructor = constructor;
            }
        }
        structs.emplace(name, std::move(info));
        structOrder.push_back(name);
    }

    void CodeGenerator::Impl::CollectInterface(InterfaceDeclStmt& statement, const std::string& nameSpace) {
        const std::string name = QualifiedClassName(statement.name, nameSpace);
        if (interfaces.contains(name) || classes.contains(name) || structs.contains(name) || enumTypes.contains(name))
            Fail("duplicate type '" + name + "'");
        InterfaceInfo info;
        info.name = name;
        info.nameSpace = nameSpace;
        info.statement = &statement;
        for (const std::string& parent : statement.parents)
            info.parents.push_back(QualifiedClassName(parent, nameSpace));
        interfaces.emplace(name, std::move(info));
        interfaceOrder.push_back(name);
    }

    void CodeGenerator::Impl::CollectClassDeclarations(const std::vector<std::unique_ptr<Statement>>& statements,
        const std::string& nameSpace) {
        for (const auto& statement : statements) {
            if (auto* classDeclaration = dynamic_cast<ClassDeclStmt*>(statement.get())) {
                if (classDeclaration->templateParams.empty()) CollectClass(*classDeclaration, nameSpace);
                else if (analyzer) {
                    const std::string baseName = QualifiedClassName(classDeclaration->name, nameSpace);
                    std::vector<std::string> specializations(
                        analyzer->InstantiatedGenericTypes().begin(),
                        analyzer->InstantiatedGenericTypes().end());
                    std::sort(specializations.begin(), specializations.end());
                    for (const std::string& specialization : specializations) {
                        std::string base;
                        std::vector<std::string> arguments;
                        if (!ParseCodegenGenericType(specialization, base, arguments) ||
                            base != baseName || arguments.size() != classDeclaration->templateParams.size()) continue;
                        std::unordered_map<std::string, std::string> substitutions;
                        for (size_t index = 0; index < arguments.size(); ++index)
                            substitutions.emplace(classDeclaration->templateParams[index].value, arguments[index]);
                        CollectClass(*classDeclaration, nameSpace, specialization, std::move(substitutions));
                    }
                }
            }
            else if (auto* structDeclaration = dynamic_cast<StructDeclStmt*>(statement.get())) {
                if (structDeclaration->templateParams.empty()) CollectStruct(*structDeclaration, nameSpace);
                else if (analyzer) {
                    const std::string baseName = QualifiedClassName(structDeclaration->name, nameSpace);
                    std::vector<std::string> specializations(
                        analyzer->InstantiatedGenericTypes().begin(),
                        analyzer->InstantiatedGenericTypes().end());
                    std::sort(specializations.begin(), specializations.end());
                    for (const std::string& specialization : specializations) {
                        std::string base;
                        std::vector<std::string> arguments;
                        if (!ParseCodegenGenericType(specialization, base, arguments) ||
                            base != baseName || arguments.size() != structDeclaration->templateParams.size()) continue;
                        std::unordered_map<std::string, std::string> substitutions;
                        for (size_t index = 0; index < arguments.size(); ++index)
                            substitutions.emplace(structDeclaration->templateParams[index].value, arguments[index]);
                        CollectStruct(*structDeclaration, nameSpace, specialization, std::move(substitutions));
                    }
                }
            }
            else if (auto* interfaceDeclaration = dynamic_cast<InterfaceDeclStmt*>(statement.get()))
                CollectInterface(*interfaceDeclaration, nameSpace);
            else if (auto* enumDeclaration = dynamic_cast<EnumDeclStmt*>(statement.get())) {
                const std::string name = QualifiedClassName(enumDeclaration->name, nameSpace);
                if (classes.contains(name) || structs.contains(name) || interfaces.contains(name) ||
                    !enumTypes.insert(name).second)
                    Fail("duplicate type '" + name + "'");
                for (size_t index = 0; index < enumDeclaration->members.size(); ++index) {
                    const std::string member = name + "." + enumDeclaration->members[index];
                    if (!enumConstants.emplace(member, static_cast<std::int32_t>(index)).second)
                        Fail("duplicate enum member '" + member + "'");
                }
            }
            else if (auto* nameSpaceDeclaration = dynamic_cast<NamespaceDeclStmt*>(statement.get())) {
                const std::string nested = nameSpace.empty()
                    ? nameSpaceDeclaration->name : nameSpace + "." + nameSpaceDeclaration->name;
                if (nameSpaceDeclaration->body)
                    CollectClassDeclarations(nameSpaceDeclaration->body->statements, nested);
            }
        }
    }

    void CodeGenerator::Impl::FinalizeInterface(const std::string& name) {
        auto found = interfaces.find(name);
        if (found == interfaces.end()) Fail("unknown interface '" + name + "'");
        InterfaceInfo& info = found->second;
        if (info.finalized) return;
        if (info.finalizing) Fail("cyclic interface inheritance involving '" + name + "'");
        info.finalizing = true;

        for (std::string& parent : info.parents) {
            if (!interfaces.contains(parent)) {
                const size_t separator = parent.rfind('.');
                const std::string shortName = separator == std::string::npos
                    ? parent : parent.substr(separator + 1);
                if (interfaces.contains(shortName)) parent = shortName;
            }
            if (!interfaces.contains(parent))
                Fail("unknown parent interface '" + parent + "' of '" + name + "'");
            FinalizeInterface(parent);
            for (const auto& [methodKey, method] : interfaces.at(parent).methods)
                info.methods.emplace(methodKey, method);
        }

        for (const auto& methodStatement : info.statement->methods) {
            FunctionDeclStmt* statement = methodStatement.get();
            if (!statement || !statement->name) continue;
            std::vector<std::string> parameterTypes;
            for (const auto& parameter : statement->parameters)
                parameterTypes.push_back(DeclaredTypeName(*parameter));
            const std::string methodKey = CallableKey(statement->name->value, parameterTypes);
            std::optional<unsigned> slot;
            if (const auto inherited = info.methods.find(methodKey); inherited != info.methods.end()) {
                if (!SameMethodSignature(*inherited->second.statement, *statement))
                    Fail("interface method signature mismatch for '" + name + "." +
                        statement->name->value + "'");
                slot = inherited->second.virtualSlot;
            }
            else slot = interfaceSlotCount++;
            info.methods[methodKey] = ClassMethod{
                statement, name, {}, slot, parameterTypes,
                ResolveTypeName(statement->returnType.get()), {}};
        }
        info.finalizing = false;
        info.finalized = true;
    }

    void CodeGenerator::Impl::FinalizeInterfaces() {
        for (const std::string& name : interfaceOrder) FinalizeInterface(name);
    }

    void CodeGenerator::Impl::FinalizeStruct(const std::string& name) {
        auto found = structs.find(name);
        if (found == structs.end()) Fail("unknown struct '" + name + "'");
        StructInfo& info = found->second;
        if (info.finalized) return;
        if (info.finalizing)
            Fail("struct '" + name + "' contains itself by value; use a pointer field");
        info.finalizing = true;
        const auto oldSubstitutions = currentGenericSubstitutions;
        currentGenericSubstitutions = info.substitutions;

        const auto addField = [&](const std::string& fieldName, const std::string& typeName) {
            if (info.fieldByName.contains(fieldName))
                Fail("duplicate field '" + name + "." + fieldName + "'");
            ClassField field{fieldName, typeName, static_cast<unsigned>(info.fields.size())};
            info.fields.push_back(field);
            info.fieldByName.emplace(fieldName, std::move(field));
        };
        for (VarDeclExpr* field : info.ownPrimitiveFields)
            addField(IdentifierName(field->name.get()), DeclaredTypeName(*field));
        for (InstanceDeclExpr* field : info.ownObjectFields)
            addField(IdentifierName(field->identifierName.get()), ResolveTypeName(field->constructType.get()));
        for (VarDeclExpr* field : info.ownStaticPrimitiveFields)
            info.staticFields.push_back({IdentifierName(field->name.get()), DeclaredTypeName(*field),
                field->value.get(), field->isConst});
        for (InstanceDeclExpr* field : info.ownStaticObjectFields)
            info.staticFields.push_back({IdentifierName(field->identifierName.get()),
                ResolveTypeName(field->constructType.get()), field->value.get(), field->isConst});

        for (FunctionDeclStmt* statement : info.ownMethods) {
            if (!statement || !statement->name) continue;
            std::vector<std::string> parameterTypes;
            parameterTypes.reserve(statement->parameters.size());
            for (const auto& parameter : statement->parameters)
                parameterTypes.push_back(DeclaredTypeName(*parameter));
            const std::string methodName = statement->name->value;
            const std::string methodKey = CallableKey(methodName, parameterTypes);
            if (info.methods.contains(methodKey))
                Fail("duplicate method '" + name + "." + methodName + "'");
            info.methods.emplace(methodKey, ClassMethod{
                statement, name, CallableKey(name + "." + methodName, parameterTypes), std::nullopt,
                parameterTypes, ResolveTypeName(statement->returnType.get()), info.substitutions,
                HasModifier(*statement, "static")});
        }

        std::vector<llvm::Type*> layout;
        layout.reserve(info.fields.size());
        for (const ClassField& field : info.fields) layout.push_back(TypeFromName(field.typeName));
        info.llvmType->setBody(layout, false);
        info.finalizing = false;
        info.finalized = true;
        currentGenericSubstitutions = oldSubstitutions;
    }

    bool CodeGenerator::Impl::SameMethodSignature(FunctionDeclStmt& left, FunctionDeclStmt& right) {
        if (ResolveTypeName(left.returnType.get()) != ResolveTypeName(right.returnType.get()) ||
            left.parameters.size() != right.parameters.size()) return false;
        for (size_t index = 0; index < left.parameters.size(); ++index)
            if (DeclaredTypeName(*left.parameters[index]) != DeclaredTypeName(*right.parameters[index]))
                return false;
        return true;
    }

    void CodeGenerator::Impl::FinalizeClass(const std::string& name) {
        auto found = classes.find(name);
        if (found == classes.end()) Fail("unknown class '" + name + "'");
        ClassInfo& info = found->second;
        if (info.finalized) return;
        if (info.finalizing) Fail("cyclic class inheritance involving '" + name + "'");
        info.finalizing = true;
        const auto oldSubstitutions = currentGenericSubstitutions;
        currentGenericSubstitutions = info.substitutions;

        std::string baseClass;
        std::vector<std::string> implementedInterfaces;
        for (size_t index = 0; index < info.parents.size(); ++index) {
            std::string parentName = info.parents[index];
            if (!classes.contains(parentName) && !interfaces.contains(parentName) &&
                index < info.statement->parents.size()) {
                const std::string& shortName = info.statement->parents[index];
                if (classes.contains(shortName) || interfaces.contains(shortName)) parentName = shortName;
            }
            if (classes.contains(parentName)) {
                if (!baseClass.empty())
                    Fail("class '" + name + "' cannot inherit more than one class");
                baseClass = parentName;
            }
            else if (interfaces.contains(parentName)) implementedInterfaces.push_back(parentName);
            else Fail("unknown parent type '" + parentName + "' of class '" + name + "'");
            info.parents[index] = parentName;
        }

        if (!baseClass.empty()) {
            FinalizeClass(baseClass);
            const ClassInfo& parent = classes.at(baseClass);
            info.fields = parent.fields;
            info.fieldByName = parent.fieldByName;
            info.methods = parent.methods;
            info.virtualNames = parent.virtualNames;
        }
        else info.virtualNames.resize(interfaceSlotCount);

        std::unordered_map<std::string, std::vector<ClassMethod>> interfaceRequirements;
        for (const std::string& interfaceName : implementedInterfaces) {
            FinalizeInterface(interfaceName);
            for (const auto& [methodKey, method] : interfaces.at(interfaceName).methods)
                interfaceRequirements[methodKey].push_back(method);
        }

        const auto addField = [&](const std::string& fieldName, const std::string& typeName) {
            if (info.fieldByName.contains(fieldName))
                Fail("field '" + name + "." + fieldName + "' hides an inherited field");
            ClassField field{fieldName, typeName, static_cast<unsigned>(info.fields.size() + 1)};
            info.fields.push_back(field);
            info.fieldByName.emplace(fieldName, std::move(field));
        };
        for (VarDeclExpr* field : info.ownPrimitiveFields)
            addField(IdentifierName(field->name.get()), DeclaredTypeName(*field));
        for (InstanceDeclExpr* field : info.ownObjectFields)
            addField(IdentifierName(field->identifierName.get()), ResolveTypeName(field->constructType.get()));
        for (VarDeclExpr* field : info.ownStaticPrimitiveFields)
            info.staticFields.push_back({IdentifierName(field->name.get()), DeclaredTypeName(*field),
                field->value.get(), field->isConst});
        for (InstanceDeclExpr* field : info.ownStaticObjectFields)
            info.staticFields.push_back({IdentifierName(field->identifierName.get()),
                ResolveTypeName(field->constructType.get()), field->value.get(), field->isConst});

        for (FunctionDeclStmt* statement : info.ownMethods) {
            if (!statement || !statement->name) continue;
            const std::string methodName = statement->name->value;
            std::vector<std::string> parameterTypes;
            parameterTypes.reserve(statement->parameters.size());
            for (const auto& parameter : statement->parameters)
                parameterTypes.push_back(DeclaredTypeName(*parameter));
            const std::string methodKey = CallableKey(methodName, parameterTypes);
            const auto inherited = info.methods.find(methodKey);
            std::optional<unsigned> slot;
            const bool staticMethod = HasModifier(*statement, "static");
            if (!staticMethod && inherited != info.methods.end() && inherited->second.virtualSlot) {
                if (!SameMethodSignature(*inherited->second.statement, *statement))
                    Fail("override signature mismatch for '" + name + "." + methodName + "'");
                slot = inherited->second.virtualSlot;
            }
            else if (HasModifier(*statement, "override") && !interfaceRequirements.contains(methodKey))
                Fail("method '" + name + "." + methodName + "' overrides no virtual method");
            else if (HasModifier(*statement, "virtual")) {
                slot = static_cast<unsigned>(info.virtualNames.size());
                info.virtualNames.push_back(methodKey);
            }
            ClassMethod method{statement, name, CallableKey(name + "." + methodName, parameterTypes), slot,
                parameterTypes, ResolveTypeName(statement->returnType.get()), info.substitutions,
                staticMethod};
            info.methods[methodKey] = method;
            info.declaredMethods[methodKey] = std::move(method);
        }

        for (const auto& [methodKey, requirements] : interfaceRequirements) {
            auto implementation = info.methods.find(methodKey);
            if (implementation == info.methods.end())
                Fail("class '" + name + "' does not implement interface method '" + methodKey + "'");
            for (const ClassMethod& requirement : requirements) {
                if (!SameMethodSignature(*requirement.statement, *implementation->second.statement))
                    Fail("class method '" + name + "." + methodKey +
                        "' does not match its interface contract");
                if (!requirement.virtualSlot) Fail("interface method is missing a dispatch slot");
                const unsigned slot = *requirement.virtualSlot;
                if (info.virtualNames.size() <= slot) info.virtualNames.resize(slot + 1);
                info.virtualNames[slot] = methodKey;
                if (!implementation->second.virtualSlot)
                    implementation->second.virtualSlot = slot;
            }
        }

        std::vector<llvm::Type*> layout{builder.getPtrTy()};
        for (const ClassField& field : info.fields) layout.push_back(TypeFromName(field.typeName));
        info.llvmType->setBody(layout, false);
        info.finalizing = false;
        info.finalized = true;
        currentGenericSubstitutions = oldSubstitutions;
    }

    llvm::FunctionType* CodeGenerator::Impl::MethodFunctionType(const ClassMethod& method) {
        std::vector<llvm::Type*> parameters;
        if (!method.isStatic) parameters.push_back(builder.getPtrTy());
        for (const std::string& parameter : method.parameterTypes)
            parameters.push_back(TypeFromName(parameter));
        return llvm::FunctionType::get(TypeFromName(method.returnType), parameters, false);
    }

    llvm::Function* CodeGenerator::Impl::DeclareMethodFunction(const ClassMethod& method) {
        llvm::FunctionType* type = MethodFunctionType(method);
        llvm::Function* function = module->getFunction(method.linkName);
        if (!function)
            function = llvm::Function::Create(type, llvm::Function::ExternalLinkage, method.linkName, *module);
        if (function->getFunctionType() != type)
            Fail("conflicting method declaration '" + method.linkName + "'");
        function->setCallingConv(llvm::CallingConv::C);
        ApplyCallableAttributes(*function, *method.statement);
        const unsigned offset = method.isStatic ? 0U : 1U;
        if (!method.isStatic) function->getArg(0)->setName("this");
        for (size_t index = 0; index < method.statement->parameters.size(); ++index)
            function->getArg(static_cast<unsigned>(index) + offset)->setName(
                IdentifierName(method.statement->parameters[index]->name.get()));
        return function;
    }

    void CodeGenerator::Impl::DeclareStaticFields(ClassInfo& info) {
        for (const StaticField& field : info.staticFields) {
            const std::string globalName = info.name + "." + field.name;
            if (globals.contains(globalName)) Fail("duplicate static field '" + globalName + "'");
            llvm::Type* type = TypeFromName(field.typeName);
            llvm::Constant* initializer = GlobalConstant(field.initializer, type);
            auto* storage = new llvm::GlobalVariable(*module, type, field.isConst,
                llvm::GlobalValue::ExternalLinkage, initializer, globalName);
            globals.emplace(globalName, Variable{storage, type, field.typeName, false,
                false, nullptr, {}, nullptr, InvalidSymbolId});
        }
    }

    void CodeGenerator::Impl::DeclareStaticFields(StructInfo& info) {
        for (const StaticField& field : info.staticFields) {
            const std::string globalName = info.name + "." + field.name;
            if (globals.contains(globalName)) Fail("duplicate static field '" + globalName + "'");
            llvm::Type* type = TypeFromName(field.typeName);
            llvm::Constant* initializer = GlobalConstant(field.initializer, type);
            auto* storage = new llvm::GlobalVariable(*module, type, field.isConst,
                llvm::GlobalValue::ExternalLinkage, initializer, globalName);
            globals.emplace(globalName, Variable{storage, type, field.typeName, false,
                false, nullptr, {}, nullptr, InvalidSymbolId});
        }
    }

    llvm::Function* CodeGenerator::Impl::DeclareConstructorFunction(ClassInfo& info) {
        if (!info.constructor) return nullptr;
        std::vector<llvm::Type*> parameters{builder.getPtrTy()};
        for (const auto& parameter : info.constructor->parameters)
            parameters.push_back(TypeFromName(SubstituteCodegenType(
                DeclaredTypeName(*parameter), info.substitutions)));
        llvm::FunctionType* type = llvm::FunctionType::get(builder.getVoidTy(), parameters, false);
        const std::string name = info.name + ".__ctor";
        llvm::Function* function = module->getFunction(name);
        if (!function)
            function = llvm::Function::Create(type, llvm::Function::ExternalLinkage, name, *module);
        function->setCallingConv(llvm::CallingConv::C);
        ApplyCallableAttributes(*function, *info.constructor);
        function->getArg(0)->setName("this");
        for (size_t index = 0; index < info.constructor->parameters.size(); ++index)
            function->getArg(static_cast<unsigned>(index + 1))->setName(
                IdentifierName(info.constructor->parameters[index]->name.get()));
        return function;
    }

    llvm::Function* CodeGenerator::Impl::DeclareConstructorFunction(StructInfo& info) {
        if (!info.constructor) return nullptr;
        std::vector<llvm::Type*> parameters{builder.getPtrTy()};
        for (const auto& parameter : info.constructor->parameters)
            parameters.push_back(TypeFromName(SubstituteCodegenType(
                DeclaredTypeName(*parameter), info.substitutions)));
        llvm::FunctionType* type = llvm::FunctionType::get(builder.getVoidTy(), parameters, false);
        const std::string name = info.name + ".__ctor";
        llvm::Function* function = module->getFunction(name);
        if (!function)
            function = llvm::Function::Create(type, llvm::Function::ExternalLinkage, name, *module);
        if (function->getFunctionType() != type)
            Fail("conflicting constructor declaration '" + name + "'");
        function->setCallingConv(llvm::CallingConv::C);
        ApplyCallableAttributes(*function, *info.constructor);
        function->getArg(0)->setName("this");
        for (size_t index = 0; index < info.constructor->parameters.size(); ++index)
            function->getArg(static_cast<unsigned>(index + 1))->setName(
                IdentifierName(info.constructor->parameters[index]->name.get()));
        return function;
    }

    void CodeGenerator::Impl::DeclareClasses() {
        for (const std::string& name : classOrder) FinalizeClass(name);
        for (const std::string& name : classOrder) {
            ClassInfo& info = classes.at(name);
            DeclareStaticFields(info);
            for (const auto& [methodName, method] : info.declaredMethods) {
                (void)methodName;
                DeclareMethodFunction(method);
            }
            DeclareConstructorFunction(info);
        }
        for (const std::string& name : classOrder) {
            ClassInfo& info = classes.at(name);
            std::vector<llvm::Constant*> entries;
            for (const std::string& methodName : info.virtualNames) {
                if (methodName.empty()) {
                    entries.push_back(llvm::ConstantPointerNull::get(builder.getPtrTy()));
                    continue;
                }
                const auto method = info.methods.find(methodName);
                if (method == info.methods.end()) Fail("missing virtual method '" + methodName + "'");
                entries.push_back(DeclareMethodFunction(method->second));
            }
            if (entries.empty()) entries.push_back(llvm::ConstantPointerNull::get(builder.getPtrTy()));
            llvm::ArrayType* tableType = llvm::ArrayType::get(builder.getPtrTy(), entries.size());
            info.vtable = new llvm::GlobalVariable(*module, tableType, true,
                llvm::GlobalValue::PrivateLinkage, llvm::ConstantArray::get(tableType, entries),
                "absolute.vtable." + name);
        }
    }

    void CodeGenerator::Impl::DeclareStructs() {
        for (const std::string& name : structOrder) FinalizeStruct(name);
        for (const std::string& name : structOrder) {
            StructInfo& info = structs.at(name);
            DeclareStaticFields(info);
            for (const auto& [methodName, method] : info.methods) {
                (void)methodName;
                DeclareMethodFunction(method);
            }
            DeclareConstructorFunction(info);
        }
    }

    std::string CodeGenerator::Impl::ClassNameFromType(std::string typeName) const {
        if (IsPointerTypeName(typeName)) typeName = PointerPointeeName(typeName);
        return typeName;
    }

    CodeGenerator::Impl::ClassInfo& CodeGenerator::Impl::RequireClassForType(const std::string& typeName) {
        const std::string name = ClassNameFromType(typeName);
        auto found = classes.find(name);
        if (found == classes.end()) Fail("type '" + typeName + "' is not a class");
        return found->second;
    }

    llvm::Value* CodeGenerator::Impl::ObjectPointer(Expression* expression, const std::string& typeName) {
        if (IsPointerTypeName(typeName)) {
            const bool oldAddressMode = addressMode;
            addressMode = false;
            llvm::Value* pointer = Evaluate(expression);
            addressMode = oldAddressMode;
            return IsManagedPointerTypeName(typeName)
                ? ManagedPointee(expression, pointer) : pointer;
        }
        return EvaluateAddress(expression);
    }

    llvm::Value* CodeGenerator::Impl::FieldAddress(llvm::Value* object, ClassInfo& info, const ClassField& field) {
        return builder.CreateStructGEP(info.llvmType, object, field.index, field.name + ".address");
    }

    llvm::Value* CodeGenerator::Impl::FieldAddress(llvm::Value* object, StructInfo& info, const ClassField& field) {
        return builder.CreateStructGEP(info.llvmType, object, field.index, field.name + ".address");
    }

    llvm::Value* CodeGenerator::Impl::ImplicitFieldAddress(const std::string& fieldName) {
        if (currentClassName.empty() || !currentThis) return nullptr;
        if (auto found = classes.find(currentClassName); found != classes.end()) {
            const auto field = found->second.fieldByName.find(fieldName);
            return field == found->second.fieldByName.end()
                ? nullptr : FieldAddress(currentThis, found->second, field->second);
        }
        if (auto found = structs.find(currentClassName); found != structs.end()) {
            const auto field = found->second.fieldByName.find(fieldName);
            return field == found->second.fieldByName.end()
                ? nullptr : FieldAddress(currentThis, found->second, field->second);
        }
        return nullptr;
    }

    llvm::Value* CodeGenerator::Impl::ObjectSize(ClassInfo& info) {
        return llvm::ConstantExpr::getSizeOf(info.llvmType);
    }

    llvm::Value* CodeGenerator::Impl::ObjectSize(StructInfo& info) {
        return llvm::ConstantExpr::getSizeOf(info.llvmType);
    }

    void CodeGenerator::Impl::InitializeObject(llvm::Value* object, ClassInfo& info) {
        builder.CreateMemSet(object, builder.getInt8(0), ObjectSize(info), llvm::MaybeAlign(8));
        llvm::Value* vtableAddress = builder.CreateStructGEP(info.llvmType, object, 0, "vtable.address");
        builder.CreateStore(info.vtable, vtableAddress);
    }

    void CodeGenerator::Impl::InitializeObject(llvm::Value* object, StructInfo& info) {
        builder.CreateMemSet(object, builder.getInt8(0), ObjectSize(info), llvm::MaybeAlign(8));
    }
}
