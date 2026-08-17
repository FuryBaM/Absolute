#include "codegen_internal.h"

namespace Absolute {
    std::string g_last_context = "none";

    namespace {
        bool ContainsOpenGenericParameter(const std::string& typeName,
            const std::vector<Token>& parameters, const Analyzer* analyzer = nullptr) {
            for (const Token& parameter : parameters) {
                size_t position = typeName.find(parameter.value);
                while (position != std::string::npos) {
                    const auto identifier = [](char value) {
                        return std::isalnum(static_cast<unsigned char>(value)) || value == '_';
                    };
                    const bool leftBoundary = position == 0 || !identifier(typeName[position - 1]);
                    const size_t end = position + parameter.value.size();
                    const bool rightBoundary = end == typeName.size() || !identifier(typeName[end]);
                    if (leftBoundary && rightBoundary) return true;
                    position = typeName.find(parameter.value, position + 1);
                }
            }
            std::string base;
            std::vector<std::string> arguments;
            if (ParseCodegenGenericType(typeName, base, arguments)) {
                for (const std::string& arg : arguments) {
                    if (ContainsOpenGenericParameter(arg, parameters, analyzer)) return true;
                }
            } else {
                std::string baseName = ValueReferenceBaseTypeName(typeName);
                if (baseName.ends_with("[]")) baseName = baseName.substr(0, baseName.size() - 2);
                if (IsManagedPointerTypeName(baseName)) baseName = PointerPointeeName(baseName);
                if (IsRawPointerTypeName(baseName)) baseName = PointerPointeeName(baseName);
                if (baseName != "int8" && baseName != "uint8" && baseName != "int16" && baseName != "uint16" &&
                    baseName != "int32" && baseName != "uint32" && baseName != "int64" && baseName != "uint64" &&
                    baseName != "float" && baseName != "double" && baseName != "bool" && baseName != "char" &&
                    baseName != "string" && baseName != "void") {
                    if (analyzer) {
                        const SymbolId symId = analyzer->Symbols().Lookup(baseName);
                        const Symbol* sym = analyzer->Symbols().Get(symId);
                        if (!sym || sym->kind != SymbolKind::Type) return true;
                    }
                }
            }
            return false;
        }
    }

    llvm::Type* CodeGenerator::Impl::TypeFromName(const std::string& rawName) {
        const std::string name = ValueReferenceBaseTypeName(
            SubstituteCodegenType(rawName, currentGenericSubstitutions));
        if (ArrayRankName(name) > 0) return ArrayDescriptorType(name);

        if (IsTaskTypeName(name)) return builder.getPtrTy();
        std::string functionReturn;
        std::vector<std::string> functionParameters;
        if (ParseCodegenFunctionType(name, functionReturn, functionParameters))
            return builder.getPtrTy();
        if (ParseCodegenCFunctionType(name, functionReturn, functionParameters))
            return builder.getPtrTy();
        std::string genericBase;
        std::vector<std::string> genericArguments;
        if (ParseCodegenGenericType(name, genericBase, genericArguments) &&
            genericBase == "tuple") {
            if (genericArguments.size() < 2)
                Fail("tuple requires at least two element types");
            std::vector<llvm::Type*> elements;
            elements.reserve(genericArguments.size());
            for (const std::string& argument : genericArguments)
                elements.push_back(TypeFromName(argument));
            return llvm::StructType::get(context, elements, false);
        }
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
        if (!currentNamespace.empty()) {
            const std::string qualified = currentNamespace + "." + name;
            if (classes.contains(qualified)) {
                FinalizeClass(qualified);
                return classes.at(qualified).llvmType;
            }
            if (structs.contains(qualified)) {
                FinalizeStruct(qualified);
                return structs.at(qualified).llvmType;
            }
        }
        for (const auto& [clsName, info] : classes) {
            if (clsName == name ||
                (clsName.size() > name.size() && clsName.ends_with("." + name)) ||
                (name.size() > clsName.size() && name.ends_with("." + clsName))) {
                FinalizeClass(clsName);
                return classes.at(clsName).llvmType;
            }
        }
        for (const auto& [strName, info] : structs) {
            if (strName == name ||
                (strName.size() > name.size() && strName.ends_with("." + name)) ||
                (name.size() > strName.size() && name.ends_with("." + strName))) {
                FinalizeStruct(strName);
                return structs.at(strName).llvmType;
            }
        }
        llvm::Function* curFn = builder.GetInsertBlock() ? builder.GetInsertBlock()->getParent() : nullptr;
        std::string subsStr = "{";
        for (const auto& [k, v] : currentGenericSubstitutions) subsStr += k + ":" + v + " ";
        subsStr += "}";
        Fail("unsupported type '" + name + "' (ctx='" + g_last_context + "', rawName='" + rawName + "', func='" + (curFn ? curFn->getName().str() : "none") + "', subs=" + subsStr + ")");
    }







    llvm::StructType* CodeGenerator::Impl::ArrayDescriptorType(const std::string& name) {
        if (const auto found = arrayDescriptorTypes.find(name); found != arrayDescriptorTypes.end())
            return found->second;
        const size_t rank = ArrayRankName(name);
        if (rank == 0) Fail("array descriptor requires an array type");
        // The view address may point into an allocation after slicing. Keep the
        // allocation base separately so ownership can cross function returns.
        std::vector<llvm::Type*> fields{builder.getPtrTy(), builder.getPtrTy()};
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
        std::string type = SubstituteCodegenType(
            ResolveTypeName(expression.type.get()), currentGenericSubstitutions);
        const size_t rank = ArrayDeclaratorRank(expression.name.get());
        for (size_t index = 0; index < rank; ++index) type += "[]";
        return type;
    }

    std::string CodeGenerator::Impl::CallableParameterTypeName(VarDeclExpr& expression) {
        return ValueReferenceTypeName(
            DeclaredTypeName(expression), expression.isConst, expression.isReference);
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
            else if (auto* property = dynamic_cast<PropertyDeclStmt*>(member.get())) {
                info.ownProperties.push_back(property);
                if (property->getter) info.ownMethods.push_back(property->getter.get());
                if (property->setter) info.ownMethods.push_back(property->setter.get());
            }
            else if (auto* indexer = dynamic_cast<IndexerDeclStmt*>(member.get())) {
                if (indexer->getter) info.ownMethods.push_back(indexer->getter.get());
                if (indexer->setter) info.ownMethods.push_back(indexer->setter.get());
            }
            else if (auto* method = dynamic_cast<FunctionDeclStmt*>(member.get()))
                info.ownMethods.push_back(method);
            else if (auto* constructor = dynamic_cast<ConstructorDeclStmt*>(member.get())) {
                info.constructors.push_back(constructor);
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
            else if (auto* property = dynamic_cast<PropertyDeclStmt*>(member.get())) {
                info.ownProperties.push_back(property);
                if (property->getter) info.ownMethods.push_back(property->getter.get());
                if (property->setter) info.ownMethods.push_back(property->setter.get());
            }
            else if (auto* indexer = dynamic_cast<IndexerDeclStmt*>(member.get())) {
                if (indexer->getter) info.ownMethods.push_back(indexer->getter.get());
                if (indexer->setter) info.ownMethods.push_back(indexer->setter.get());
            }
            else if (auto* method = dynamic_cast<FunctionDeclStmt*>(member.get()))
                info.ownMethods.push_back(method);
            else if (auto* constructor = dynamic_cast<ConstructorDeclStmt*>(member.get())) {
                info.constructors.push_back(constructor);
            }
        }
        structs.emplace(name, std::move(info));
        structOrder.push_back(name);
    }

    void CodeGenerator::Impl::CollectInterface(InterfaceDeclStmt& statement,
        const std::string& nameSpace, const std::string& specializedName,
        std::unordered_map<std::string, std::string> substitutions) {
        const std::string name = specializedName.empty()
            ? QualifiedClassName(statement.name, nameSpace) : specializedName;
        if (interfaces.contains(name) || classes.contains(name) || structs.contains(name) || enumTypes.contains(name))
            Fail("duplicate type '" + name + "'");
        InterfaceInfo info;
        info.name = name;
        info.nameSpace = nameSpace;
        info.statement = &statement;
        info.substitutions = std::move(substitutions);
        for (const std::string& parent : statement.parents)
            info.parents.push_back(SubstituteCodegenType(
                QualifiedClassName(parent, nameSpace), info.substitutions));
        for (const auto& declaration : statement.staticFields) {
            if (!declaration || !declaration->expr) continue;
            VarDeclExpr* field = declaration->expr.get();
            info.staticFields.push_back({IdentifierName(field->name.get()),
                SubstituteCodegenType(DeclaredTypeName(*field), info.substitutions),
                field->value.get(), field->isConst});
        }
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
                            arguments.size() != classDeclaration->templateParams.size()) continue;
                        if (base != baseName && base != classDeclaration->name &&
                            baseName != QualifiedClassName(base, nameSpace)) continue;
                        if (std::any_of(arguments.begin(), arguments.end(), [&](const std::string& argument) {
                            return ContainsOpenGenericParameter(argument, classDeclaration->templateParams, analyzer);
                        })) continue;
                        std::unordered_map<std::string, std::string> substitutions;
                        for (size_t index = 0; index < arguments.size(); ++index)
                            substitutions.emplace(classDeclaration->templateParams[index].value, arguments[index]);
                        const std::string fullName = baseName + "<" + specialization.substr(specialization.find('<') + 1);
                        CollectClass(*classDeclaration, nameSpace, fullName, std::move(substitutions));
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
                            arguments.size() != structDeclaration->templateParams.size()) continue;
                        if (base != baseName && base != structDeclaration->name &&
                            baseName != QualifiedClassName(base, nameSpace)) continue;
                        if (std::any_of(arguments.begin(), arguments.end(), [&](const std::string& argument) {
                            return ContainsOpenGenericParameter(argument, structDeclaration->templateParams, analyzer);
                        })) continue;
                        std::unordered_map<std::string, std::string> substitutions;
                        for (size_t index = 0; index < arguments.size(); ++index)
                            substitutions.emplace(structDeclaration->templateParams[index].value, arguments[index]);
                        const std::string fullName = baseName + "<" + specialization.substr(specialization.find('<') + 1);
                        CollectStruct(*structDeclaration, nameSpace, fullName, std::move(substitutions));
                    }
                }
            }
            else if (auto* interfaceDeclaration = dynamic_cast<InterfaceDeclStmt*>(statement.get())) {
                if (interfaceDeclaration->templateParams.empty())
                    CollectInterface(*interfaceDeclaration, nameSpace);
                else if (analyzer) {
                    const std::string baseName = QualifiedClassName(
                        interfaceDeclaration->name, nameSpace);
                    std::vector<std::string> specializations(
                        analyzer->InstantiatedGenericTypes().begin(),
                        analyzer->InstantiatedGenericTypes().end());
                    std::sort(specializations.begin(), specializations.end());
                    for (const std::string& specialization : specializations) {
                        std::string base;
                        std::vector<std::string> arguments;
                        if (!ParseCodegenGenericType(specialization, base, arguments) ||
                            base != baseName ||
                            arguments.size() != interfaceDeclaration->templateParams.size()) continue;
                        if (std::any_of(arguments.begin(), arguments.end(), [&](const std::string& argument) {
                            return ContainsOpenGenericParameter(argument, interfaceDeclaration->templateParams, analyzer);
                        })) continue;
                        std::unordered_map<std::string, std::string> substitutions;
                        for (size_t index = 0; index < arguments.size(); ++index)
                            substitutions.emplace(
                                interfaceDeclaration->templateParams[index].value, arguments[index]);
                        CollectInterface(*interfaceDeclaration, nameSpace, specialization,
                            std::move(substitutions));
                    }
                }
            }
            else if (auto* enumDeclaration = dynamic_cast<EnumDeclStmt*>(statement.get())) {
                const std::string name = QualifiedClassName(enumDeclaration->name, nameSpace);
                if (classes.contains(name) || structs.contains(name) || interfaces.contains(name) ||
                    !enumTypes.insert(name).second)
                    Fail("duplicate type '" + name + "'");
                // A member stands for the number written after it, or for one
                // past its predecessor. The analyzer has already refused a
                // value that does not fit an i32 and two members sharing one.
                long long next = 0;
                for (const EnumMemberDecl& declared : enumDeclaration->members) {
                    const std::string member = name + "." + declared.name;
                    const long long value = declared.hasValue ? declared.value : next;
                    next = value + 1;
                    if (!enumConstants.emplace(member, static_cast<std::int32_t>(value)).second)
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
        if (!info.statement->templateParams.empty() && info.substitutions.empty()) return;
        if (info.finalizing) Fail("cyclic interface inheritance involving '" + name + "'");
        info.finalizing = true;
        const auto oldSubstitutions = currentGenericSubstitutions;
        currentGenericSubstitutions = info.substitutions;

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
            const InterfaceInfo& parentInfo = interfaces.at(parent);
            info.ambiguousDefaults.insert(
                parentInfo.ambiguousDefaults.begin(), parentInfo.ambiguousDefaults.end());
            for (const auto& [methodKey, contracts] : parentInfo.methodContracts) {
                auto& inheritedContracts = info.methodContracts[methodKey];
                inheritedContracts.insert(
                    inheritedContracts.end(), contracts.begin(), contracts.end());
            }
            for (const auto& [methodKey, method] : parentInfo.methods) {
                const auto inherited = info.methods.find(methodKey);
                if (inherited == info.methods.end()) {
                    info.methods.emplace(methodKey, method);
                    continue;
                }
                if (inherited->second.returnType != method.returnType ||
                    inherited->second.parameterTypes != method.parameterTypes)
                    Fail("inherited interface method signature mismatch for '" + name + "." +
                        method.statement->name->value + "'");
                const bool existingDefault = inherited->second.statement->body != nullptr;
                const bool candidateDefault = method.statement->body != nullptr;
                if (existingDefault && candidateDefault &&
                    inherited->second.statement != method.statement) {
                    info.ambiguousDefaults.insert(methodKey);
                }
                else if (!existingDefault && candidateDefault) {
                    inherited->second = method;
                }
            }
        }

        std::vector<FunctionDeclStmt*> interfaceMethods;
        for (const auto& method : info.statement->methods) interfaceMethods.push_back(method.get());
        for (const auto& property : info.statement->properties) {
            if (property->getter) interfaceMethods.push_back(property->getter.get());
            if (property->setter) interfaceMethods.push_back(property->setter.get());
        }
        for (const auto& indexer : info.statement->indexers) {
            if (indexer->getter) interfaceMethods.push_back(indexer->getter.get());
            if (indexer->setter) interfaceMethods.push_back(indexer->setter.get());
        }
        for (FunctionDeclStmt* statement : interfaceMethods) {
            if (!statement || !statement->name) continue;
            std::vector<std::string> parameterTypes;
            for (const auto& parameter : statement->parameters)
                parameterTypes.push_back(SubstituteCodegenType(
                    CallableParameterTypeName(*parameter), info.substitutions));
            const std::string methodKey = CallableKey(statement->name->value, parameterTypes);
            const bool staticMethod = HasModifier(*statement, "static");
            if (staticMethod) {
                ClassMethod method{
                    statement, name,
                    CallableKey(name + "." + statement->name->value, parameterTypes),
                    std::nullopt, parameterTypes,
                    SubstituteCodegenType(ResolveTypeName(statement->returnType.get()),
                        info.substitutions), info.substitutions};
                method.isStatic = true;
                info.staticMethods[methodKey] = method;
                if (statement->body) info.declaredMethods[methodKey] = std::move(method);
                continue;
            }
            std::optional<unsigned> slot;
            if (const auto inherited = info.methods.find(methodKey); inherited != info.methods.end()) {
                const std::string declaredReturn = SubstituteCodegenType(
                    ResolveTypeName(statement->returnType.get()), info.substitutions);
                if (inherited->second.returnType != declaredReturn ||
                    inherited->second.parameterTypes != parameterTypes)
                    Fail("interface method signature mismatch for '" + name + "." +
                        statement->name->value + "'");
                slot = inherited->second.virtualSlot;
            }
            else slot = interfaceSlotCount++;
            ClassMethod method{
                statement, name, CallableKey(name + "." + statement->name->value, parameterTypes),
                slot, parameterTypes,
                SubstituteCodegenType(ResolveTypeName(statement->returnType.get()),
                    info.substitutions), info.substitutions};
            auto& contracts = info.methodContracts[methodKey];
            if (contracts.empty()) {
                contracts.push_back(method);
            }
            else {
                for (ClassMethod& contract : contracts) {
                    const std::optional<unsigned> inheritedSlot = contract.virtualSlot;
                    contract = method;
                    contract.virtualSlot = inheritedSlot;
                }
                method.virtualSlot = contracts.front().virtualSlot;
            }
            info.methods[methodKey] = method;
            info.ambiguousDefaults.erase(methodKey);
            if (statement->body) info.declaredMethods[methodKey] = std::move(method);
        }
        info.finalizing = false;
        info.finalized = true;
        currentGenericSubstitutions = oldSubstitutions;
    }

    void CodeGenerator::Impl::FinalizeInterfaces() {
        for (const std::string& name : interfaceOrder) FinalizeInterface(name);
        for (const std::string& name : interfaceOrder) {
            InterfaceInfo& info = interfaces.at(name);
            DeclareStaticFields(info);
            for (const auto& [methodName, method] : info.staticMethods) {
                (void)methodName;
                DeclareMethodFunction(method);
            }
        }
    }

    void CodeGenerator::Impl::FinalizeStruct(const std::string& name) {
        g_last_context = "FinalizeStruct:" + name;
        auto found = structs.find(name);
        if (found == structs.end()) Fail("unknown struct '" + name + "'");

        StructInfo& info = found->second;
        if (info.finalized) return;
        if (!info.statement->templateParams.empty() && info.substitutions.empty()) return;
        if (info.finalizing)

            Fail("struct '" + name + "' contains itself by value; use a pointer field");
        info.finalizing = true;
        const auto oldSubstitutions = currentGenericSubstitutions;
        currentGenericSubstitutions = info.substitutions;

        const auto addField = [&](const std::string& fieldName, const std::string& typeName) {
            if (info.fieldByName.contains(fieldName))
                Fail("duplicate field '" + name + "." + fieldName + "'");
            const std::string substitutedType = SubstituteCodegenType(typeName, info.substitutions);
            ClassField field{fieldName, substitutedType, static_cast<unsigned>(info.fields.size())};
            info.fields.push_back(field);
            info.fieldByName.emplace(fieldName, std::move(field));
        };

        for (PropertyDeclStmt* property : info.ownProperties)
            if (property && property->HasAutoAccessor())
                addField(PropertyBackingFieldName(property->name),
                    ResolveTypeName(property->type.get()));
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
                parameterTypes.push_back(SubstituteCodegenType(
                    CallableParameterTypeName(*parameter), info.substitutions));
            const std::string methodName = statement->name->value;
            const std::string methodKey = CallableKey(methodName, parameterTypes);
            if (info.methods.contains(methodKey))
                Fail("duplicate method '" + name + "." + methodName + "'");
            info.methods.emplace(methodKey, ClassMethod{
                statement, name, CallableKey(name + "." + methodName, parameterTypes), std::nullopt,
                parameterTypes, SubstituteCodegenType(ResolveTypeName(statement->returnType.get()), info.substitutions), info.substitutions,
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

    void CodeGenerator::Impl::FinalizeClass(const std::string& name) {
        auto found = classes.find(name);
        if (found == classes.end()) Fail("unknown class '" + name + "'");
        ClassInfo& info = found->second;
        if (info.finalized) return;
        if (!info.statement->templateParams.empty() && info.substitutions.empty()) return;
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
        info.baseClass = baseClass;

        std::unordered_map<std::string, std::vector<ClassMethod>> interfaceRequirements;
        std::unordered_set<std::string> ambiguousInterfaceDefaults;
        for (const std::string& interfaceName : implementedInterfaces) {
            FinalizeInterface(interfaceName);
            const InterfaceInfo& interfaceInfo = interfaces.at(interfaceName);
            ambiguousInterfaceDefaults.insert(interfaceInfo.ambiguousDefaults.begin(),
                interfaceInfo.ambiguousDefaults.end());
            for (const auto& [methodKey, contracts] : interfaceInfo.methodContracts) {
                auto& requirements = interfaceRequirements[methodKey];
                requirements.insert(requirements.end(), contracts.begin(), contracts.end());
            }
        }

        const auto addField = [&](const std::string& fieldName, const std::string& typeName) {
            if (info.fieldByName.contains(fieldName))
                Fail("field '" + name + "." + fieldName + "' hides an inherited field");
            ClassField field{fieldName, typeName, static_cast<unsigned>(info.fields.size() + 1)};
            info.fields.push_back(field);
            info.fieldByName.emplace(fieldName, std::move(field));
        };
        for (PropertyDeclStmt* property : info.ownProperties)
            if (property && property->HasAutoAccessor())
                addField(PropertyBackingFieldName(property->name),
                    ResolveTypeName(property->type.get()));
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
                parameterTypes.push_back(CallableParameterTypeName(*parameter));
            const std::string methodKey = CallableKey(methodName, parameterTypes);
            const auto inherited = info.methods.find(methodKey);
            std::optional<unsigned> slot;
            const bool staticMethod = HasModifier(*statement, "static");
            if (!staticMethod && inherited != info.methods.end() && inherited->second.virtualSlot) {
                const std::string declaredReturn =
                    ResolveTypeName(statement->returnType.get());
                if (inherited->second.returnType != declaredReturn ||
                    inherited->second.parameterTypes != parameterTypes)
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
            if (implementation == info.methods.end()) {
                if (ambiguousInterfaceDefaults.contains(methodKey))
                    Fail("class '" + name + "' inherits ambiguous default interface method '" +
                        methodKey + "'");
                const ClassMethod* defaultMethod = nullptr;
                for (const ClassMethod& requirement : requirements) {
                    if (!requirement.statement->body) continue;
                    if (defaultMethod && defaultMethod->statement != requirement.statement)
                        Fail("class '" + name + "' inherits multiple default implementations of '" +
                            methodKey + "'");
                    defaultMethod = &requirement;
                }
                if (!defaultMethod)
                    Fail("class '" + name + "' does not implement interface method '" + methodKey + "'");
                info.methods.emplace(methodKey, *defaultMethod);
                implementation = info.methods.find(methodKey);
            }
            for (const ClassMethod& requirement : requirements) {
                if (requirement.returnType != implementation->second.returnType ||
                    requirement.parameterTypes != implementation->second.parameterTypes)
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
        const std::string returnType = SubstituteCodegenType(method.returnType, method.substitutions);
        if (AbiReturnOffset(returnType) != 0) parameters.push_back(builder.getPtrTy());
        if (!method.isStatic) parameters.push_back(builder.getPtrTy());
        for (const std::string& parameter : method.parameterTypes) {
            const std::string type =
                SubstituteCodegenType(parameter, method.substitutions);
            parameters.push_back(AbiParameterType(type));
            if (ParameterSupportsOwnershipName(type))
                parameters.push_back(builder.getInt1Ty());
        }
        return llvm::FunctionType::get(AbiReturnType(returnType), parameters, false);
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
        unsigned offset = AbiReturnOffset(method.returnType);
        if (offset != 0) function->getArg(0)->setName("__result");
        if (!method.isStatic) function->getArg(offset++)->setName("this");
        ApplyValueReferenceParameterAttributes(*function, offset, method.parameterTypes);
        unsigned argumentIndex = offset;
        for (size_t index = 0;
            index < method.statement->parameters.size(); ++index) {
            function->getArg(argumentIndex++)->setName(
                IdentifierName(method.statement->parameters[index]->name.get()));
            const std::string type = SubstituteCodegenType(
                method.parameterTypes[index], method.substitutions);
            if (ParameterSupportsOwnershipName(type))
                function->getArg(argumentIndex++)->setName(
                    IdentifierName(method.statement->parameters[index]->name.get()) +
                    ".is_owner");
        }
        return function;
    }

    void CodeGenerator::Impl::DeclareStaticFields(ClassInfo& info) {
        const auto oldSubstitutions = currentGenericSubstitutions;
        currentGenericSubstitutions = info.substitutions;
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
        currentGenericSubstitutions = oldSubstitutions;
    }

    void CodeGenerator::Impl::DeclareStaticFields(StructInfo& info) {
        const auto oldSubstitutions = currentGenericSubstitutions;
        currentGenericSubstitutions = info.substitutions;
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
        currentGenericSubstitutions = oldSubstitutions;
    }

    void CodeGenerator::Impl::DeclareStaticFields(InterfaceInfo& info) {
        const auto oldSubstitutions = currentGenericSubstitutions;
        currentGenericSubstitutions = info.substitutions;
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
        currentGenericSubstitutions = oldSubstitutions;
    }



    bool CodeGenerator::Impl::ClassNeedsConstructor(const ClassInfo& info) const {
        if (!info.constructors.empty()) return true;
        if (info.baseClass.empty()) return false;
        const auto base = classes.find(info.baseClass);
        return base != classes.end() && ClassNeedsConstructor(base->second);
    }

    std::vector<std::string> CodeGenerator::Impl::ConstructorParameterTypeNames(
        ConstructorDeclStmt* constructor,
        const std::unordered_map<std::string, std::string>& substitutions) {
        std::vector<std::string> parameterTypes;
        if (!constructor) return parameterTypes;
        parameterTypes.reserve(constructor->parameters.size());
        for (const auto& parameter : constructor->parameters)
            parameterTypes.push_back(SubstituteCodegenType(
                CallableParameterTypeName(*parameter), substitutions));
        return parameterTypes;
    }

    std::string CodeGenerator::Impl::ConstructorLinkName(
        const std::string& typeName,
        const std::vector<std::string>& parameterTypes) const {
        return CallableKey(typeName + ".__ctor", parameterTypes);
    }

    llvm::Function* CodeGenerator::Impl::LookupConstructorFunction(
        const std::string& typeName, Expression* call) {
        std::vector<std::string> parameterTypes;
        if (analyzer && call) {
            if (const ExpressionInfo* info = analyzer->GetExpressionInfo(*call))
                parameterTypes = info->parameterTypes;
        }
        if (llvm::Function* function =
            module->getFunction(ConstructorLinkName(typeName, parameterTypes)))
            return function;
        // Fallback: unique constructor on the type.
        if (const auto found = classes.find(typeName); found != classes.end()) {
            if (found->second.constructors.size() == 1) {
                const auto params = ConstructorParameterTypeNames(
                    found->second.constructors.front(), found->second.substitutions);
                return module->getFunction(ConstructorLinkName(typeName, params));
            }
            if (found->second.constructors.empty())
                return module->getFunction(ConstructorLinkName(typeName, {}));
        }
        if (const auto found = structs.find(typeName); found != structs.end()) {
            if (found->second.constructors.size() == 1) {
                const auto params = ConstructorParameterTypeNames(
                    found->second.constructors.front(), found->second.substitutions);
                return module->getFunction(ConstructorLinkName(typeName, params));
            }
            if (found->second.constructors.empty())
                return module->getFunction(ConstructorLinkName(typeName, {}));
        }
        return nullptr;
    }

    bool CodeGenerator::Impl::RuntimeTypeDerivesFrom(
        const std::string& type, const std::string& base) const {
        if (type == base) return true;
        if (const auto found = classes.find(type); found != classes.end()) {
            for (const std::string& parent : found->second.parents)
                if (RuntimeTypeDerivesFrom(parent, base)) return true;
        }
        if (const auto found = interfaces.find(type); found != interfaces.end()) {
            for (const std::string& parent : found->second.parents)
                if (RuntimeTypeDerivesFrom(parent, base)) return true;
        }
        return false;
    }

    llvm::Value* CodeGenerator::Impl::EmitRuntimeTypeTest(
        llvm::Value* reference, const std::string& sourceType, const std::string& targetType) {
        llvm::Function* function = CurrentFunction();
        if (!function) Fail("runtime type test outside a function");
        llvm::Value* object = IsManagedPointerTypeName(sourceType)
            ? EmitManagedGet(reference, false)
            : reference;
        llvm::BasicBlock* origin = builder.GetInsertBlock();
        llvm::BasicBlock* inspect = llvm::BasicBlock::Create(context, "type.test.inspect", function);
        llvm::BasicBlock* complete = llvm::BasicBlock::Create(context, "type.test.end", function);
        llvm::Value* present = builder.CreateICmpNE(object,
            llvm::ConstantPointerNull::get(builder.getPtrTy()), "type.test.present");
        builder.CreateCondBr(present, inspect, complete);

        builder.SetInsertPoint(inspect);
        llvm::Value* dynamicVtable = builder.CreateLoad(builder.getPtrTy(), object, "type.test.vtable");
        llvm::Value* matches = builder.getFalse();
        for (const std::string& className : classOrder) {
            if (!RuntimeTypeDerivesFrom(className, targetType)) continue;
            const auto found = classes.find(className);
            if (found == classes.end() || !found->second.vtable) continue;
            llvm::Value* same = builder.CreateICmpEQ(
                dynamicVtable, found->second.vtable, "type.test.class");
            matches = builder.CreateOr(matches, same, "type.test.match");
        }
        llvm::BasicBlock* inspected = builder.GetInsertBlock();
        builder.CreateBr(complete);

        builder.SetInsertPoint(complete);
        llvm::PHINode* result = builder.CreatePHI(builder.getInt1Ty(), 2, "type.test.result");
        result->addIncoming(builder.getFalse(), origin);
        result->addIncoming(matches, inspected);
        return result;
    }

    llvm::Function* CodeGenerator::Impl::DeclareConstructorFunction(
        ClassInfo& info, ConstructorDeclStmt* constructor) {
        if (!constructor && !ClassNeedsConstructor(info)) return nullptr;
        std::vector<std::string> parameterTypes =
            ConstructorParameterTypeNames(constructor, info.substitutions);
        std::vector<llvm::Type*> parameters{builder.getPtrTy()};
        for (const std::string& parameterType : parameterTypes)
        {
            parameters.push_back(AbiParameterType(parameterType));
            if (ParameterSupportsOwnershipName(parameterType))
                parameters.push_back(builder.getInt1Ty());
        }
        llvm::FunctionType* type = llvm::FunctionType::get(builder.getVoidTy(), parameters, false);
        const std::string name = ConstructorLinkName(info.name, parameterTypes);
        llvm::Function* function = module->getFunction(name);
        if (!function)
            function = llvm::Function::Create(type, llvm::Function::ExternalLinkage, name, *module);
        if (function->getFunctionType() != type)
            Fail("conflicting constructor declaration '" + name + "'");
        function->setCallingConv(llvm::CallingConv::C);
        if (constructor) ApplyCallableAttributes(*function, *constructor);
        ApplyValueReferenceParameterAttributes(*function, 1, parameterTypes);
        function->getArg(0)->setName("this");
        if (constructor)
            for (size_t index = 0, argumentIndex = 1;
                index < constructor->parameters.size(); ++index) {
                function->getArg(static_cast<unsigned>(argumentIndex++))->setName(
                    IdentifierName(constructor->parameters[index]->name.get()));
                if (ParameterSupportsOwnershipName(parameterTypes[index]))
                    function->getArg(static_cast<unsigned>(argumentIndex++))->setName(
                        IdentifierName(constructor->parameters[index]->name.get()) +
                        ".is_owner");
            }
        return function;
    }

    llvm::Function* CodeGenerator::Impl::DeclareConstructorFunction(ClassInfo& info) {
        if (!info.constructors.empty()) {
            llvm::Function* last = nullptr;
            for (ConstructorDeclStmt* constructor : info.constructors)
                last = DeclareConstructorFunction(info, constructor);
            return last;
        }
        return DeclareConstructorFunction(info, nullptr);
    }

    llvm::Function* CodeGenerator::Impl::DeclareConstructorFunction(StructInfo& info) {
        if (info.constructors.empty()) return nullptr;
        llvm::Function* last = nullptr;
        for (ConstructorDeclStmt* constructor : info.constructors) {
            std::vector<std::string> parameterTypes =
                ConstructorParameterTypeNames(constructor, info.substitutions);
            std::vector<llvm::Type*> parameters{builder.getPtrTy()};
            for (const std::string& parameterType : parameterTypes)
            {
                parameters.push_back(AbiParameterType(parameterType));
                if (ParameterSupportsOwnershipName(parameterType))
                    parameters.push_back(builder.getInt1Ty());
            }
            llvm::FunctionType* type = llvm::FunctionType::get(builder.getVoidTy(), parameters, false);
            const std::string name = ConstructorLinkName(info.name, parameterTypes);
            llvm::Function* function = module->getFunction(name);
            if (!function)
                function = llvm::Function::Create(type, llvm::Function::ExternalLinkage, name, *module);
            if (function->getFunctionType() != type)
                Fail("conflicting constructor declaration '" + name + "'");
            function->setCallingConv(llvm::CallingConv::C);
            ApplyCallableAttributes(*function, *constructor);
            ApplyValueReferenceParameterAttributes(*function, 1, parameterTypes);
            function->getArg(0)->setName("this");
            for (size_t index = 0; index < constructor->parameters.size(); ++index)
                function->getArg(static_cast<unsigned>(index + 1))->setName(
                    IdentifierName(constructor->parameters[index]->name.get()));
            last = function;
        }
        return last;
    }

    llvm::Function* CodeGenerator::Impl::DeclareClassDestructor(ClassInfo& info) {
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getVoidTy(), {builder.getPtrTy()}, false);
        const std::string name = info.name + ".__destroy";
        llvm::Function* function = module->getFunction(name);
        if (!function)
            function = llvm::Function::Create(
                type, llvm::Function::InternalLinkage, name, *module);
        function->setCallingConv(llvm::CallingConv::C);
        function->getArg(0)->setName("this");
        return function;
    }

    llvm::Function* CodeGenerator::Impl::DeclareStructDestructor(StructInfo& info) {
        llvm::FunctionType* type = llvm::FunctionType::get(
            builder.getVoidTy(), {builder.getPtrTy()}, false);
        const std::string name = info.name + ".__destroy";
        llvm::Function* function = module->getFunction(name);
        if (!function)
            function = llvm::Function::Create(
                type, llvm::Function::InternalLinkage, name, *module);
        function->setCallingConv(llvm::CallingConv::C);
        function->getArg(0)->setName("this");
        return function;
    }

    void CodeGenerator::Impl::DeclareClasses() {
        for (const std::string& name : classOrder) FinalizeClass(name);
        for (const std::string& name : classOrder) {
            ClassInfo& info = classes.at(name);
            if (!info.statement->templateParams.empty() && info.substitutions.empty()) continue;
            DeclareStaticFields(info);
            for (const auto& [methodName, method] : info.declaredMethods) {
                (void)methodName;
                DeclareMethodFunction(method);
            }
            DeclareConstructorFunction(info);
            DeclareClassDestructor(info);
        }
        for (const std::string& name : classOrder) {
            ClassInfo& info = classes.at(name);
            if (!info.statement->templateParams.empty() && info.substitutions.empty()) continue;
            std::vector<llvm::Constant*> entries;
            for (size_t slot = 0; slot < info.virtualNames.size(); ++slot) {
                if (slot == 0) {
                    entries.push_back(DeclareClassDestructor(info));
                    continue;
                }
                const std::string& methodName = info.virtualNames[slot];
                if (methodName.empty()) {
                    entries.push_back(llvm::ConstantPointerNull::get(builder.getPtrTy()));
                    continue;
                }
                const auto found = info.methods.find(methodName);
                if (found == info.methods.end()) Fail("missing virtual slot for '" + methodName + "'");
                entries.push_back(DeclareMethodFunction(found->second));
            }
            llvm::ArrayType* vtableType = llvm::ArrayType::get(
                builder.getPtrTy(), entries.size());
            llvm::Constant* initializer = llvm::ConstantArray::get(vtableType, entries);
            const std::string vtableName = info.name + ".__vtable";
            auto* vtable = new llvm::GlobalVariable(*module, vtableType, true,
                llvm::GlobalValue::InternalLinkage, initializer, vtableName);
            vtable->setAlignment(llvm::Align(8));
            info.vtable = vtable;
        }
    }

    void CodeGenerator::Impl::DeclareStructs() {
        for (const std::string& name : structOrder) FinalizeStruct(name);
        for (const std::string& name : structOrder) {
            StructInfo& info = structs.at(name);
            if (!info.statement->templateParams.empty() && info.substitutions.empty()) continue;
            DeclareStaticFields(info);
            for (const auto& [methodName, method] : info.methods) {
                (void)methodName;
                DeclareMethodFunction(method);
            }
            DeclareConstructorFunction(info);
            if (TypeNeedsCleanup(info.name)) DeclareStructDestructor(info);
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

    bool CodeGenerator::Impl::HasAddressableStorage(Expression* expression) {
        if (!expression || !analyzer) return true;
        const ExpressionInfo* info = analyzer->GetExpressionInfo(*expression);
        if (!info) return true;
        if (!info->placeInfo.addressable) return false;
        Expression* base = nullptr;
        if (auto* member = dynamic_cast<MemberAccessExpr*>(expression)) base = member->base.get();
        else if (auto* array = dynamic_cast<ArrayAccessExpr*>(expression)) base = array->base.get();
        if (!base) return true;
        // Through a pointer or an array descriptor the field has real storage
        // behind it, whatever the expression that produced the pointer was.
        const std::string baseType = SemanticType(base);
        if (IsPointerTypeName(baseType) || ArrayRankName(baseType) > 0) return true;
        return HasAddressableStorage(base);
    }

    llvm::Value* CodeGenerator::Impl::ObjectPointer(Expression* expression, const std::string& typeName) {
        if (IsPointerTypeName(typeName)) {
            const bool oldAddressMode = addressMode;
            addressMode = false;
            // Reading a member or calling a method borrows the object: it does
            // not take the owner, so `make().value` used to drop the handle and
            // leak it. Registered for the end of the statement instead, which
            // is late enough for a chain like `make().child.value` to finish.
            llvm::Value* pointer = EvaluateBorrowed(expression);
            addressMode = oldAddressMode;
            return IsManagedPointerTypeName(typeName)
                ? ManagedPointee(expression, pointer) : pointer;
        }
        // A struct value that is not a place -- what a property getter or a
        // function returns -- has no address to take, but reading a field of
        // it is ordinary notation: `pair.key`, `config().timeout`. Copy it into
        // a temporary and read from there. Only when reading: with addressMode
        // set the caller is writing, and a write has to keep failing, because
        // it would land in the copy and be lost.
        if (!addressMode && !HasAddressableStorage(expression)) {
            const bool oldAddressMode = addressMode;
            addressMode = false;
            llvm::Value* value = Evaluate(expression);
            addressMode = oldAddressMode;
            llvm::Type* valueType = TypeFromName(typeName);
            llvm::AllocaInst* temporary = CreateEntryAlloca(
                *CurrentFunction(), valueType, "value.temporary");
            builder.CreateStore(Coerce(value, valueType), temporary);
            return temporary;
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

    bool CodeGenerator::Impl::TypeNeedsCleanup(const std::string& typeName) {
        std::string closureReturn;
        std::vector<std::string> closureParameters;
        if (ParseCodegenFunctionType(typeName, closureReturn, closureParameters)) return true;
        std::unordered_set<std::string> visiting;
        const auto inspect = [&](const auto& self, const std::string& candidate) -> bool {
            if (IsStrongManagedPointerTypeName(candidate) || ArrayRankName(candidate) > 0) return true;
            if (IsRawPointerTypeName(candidate) || !visiting.insert(candidate).second) return false;
            
            if (const PluginResourceDescriptor* descriptor = GetPluginResourceDescriptor(candidate)) {
                if (descriptor->isResource) return true;
            }
            
            const auto release = [&] { visiting.erase(candidate); };
            if (const auto found = classes.find(candidate); found != classes.end()) {
                // `methods` is keyed by CallableKey, which appends "$"-joined
                // parameter types to the bare name. A literal "destroy()" is not
                // a key this map can hold, so matching it here silently skipped
                // cleanup for every type whose only resource is its own destroy().
                if (found->second.methods.contains(CallableKey("destroy", {}))) {
                    release();
                    return true;
                }
                for (const ClassField& field : found->second.fields) {
                    if (self(self, field.typeName)) {
                        release();
                        return true;
                    }
                }
            }
            else if (const auto found = structs.find(candidate); found != structs.end()) {
                if (found->second.methods.contains(CallableKey("destroy", {}))) {
                    release();
                    return true;
                }
                for (const ClassField& field : found->second.fields) {
                    if (self(self, field.typeName)) {
                        release();
                        return true;
                    }
                }
            }
            release();
            return false;
        };
        return inspect(inspect, typeName);
    }

    void CodeGenerator::Impl::EmitPointeeCleanup(
        llvm::Value* pointer, const std::string& pointerTypeName) {
        const std::string pointeeName = PointerPointeeName(pointerTypeName);
        const bool dynamicClass = classes.contains(pointeeName) || interfaces.contains(pointeeName);
        const bool directStruct = structs.contains(pointeeName) && TypeNeedsCleanup(pointeeName);
        if (!dynamicClass && !directStruct) return;

        llvm::Function* parent = CurrentFunction();
        llvm::BasicBlock* cleanup = llvm::BasicBlock::Create(
            context, "aggregate.cleanup", parent);
        llvm::BasicBlock* complete = llvm::BasicBlock::Create(
            context, "aggregate.cleanup.end", parent);
        builder.CreateCondBr(builder.CreateICmpNE(pointer,
            llvm::ConstantPointerNull::get(builder.getPtrTy()), "aggregate.present"),
            cleanup, complete);
        builder.SetInsertPoint(cleanup);
        if (dynamicClass) {
            llvm::Value* vtable = builder.CreateLoad(
                builder.getPtrTy(), pointer, "aggregate.vtable");
            llvm::Value* slot = builder.CreateGEP(
                builder.getPtrTy(), vtable, builder.getInt64(0), "aggregate.destroy.slot");
            llvm::Value* destructor = builder.CreateLoad(
                builder.getPtrTy(), slot, "aggregate.destroy");
            llvm::FunctionType* type = llvm::FunctionType::get(
                builder.getVoidTy(), {builder.getPtrTy()}, false);
            builder.CreateCall(type, destructor, {pointer});
        }
        else builder.CreateCall(DeclareStructDestructor(structs.at(pointeeName)), {pointer});
        builder.CreateBr(complete);
        builder.SetInsertPoint(complete);
    }

    void CodeGenerator::Impl::EmitValueCleanup(
        llvm::Value* address, const std::string& typeName) {
        std::string closureReturn;
        std::vector<std::string> closureParameters;
        if (ParseCodegenFunctionType(typeName, closureReturn, closureParameters)) {
            llvm::Value* closure = builder.CreateLoad(
                builder.getPtrTy(), address, "closure.cleanup.value");
            builder.CreateCall(ClosureRelease(), {closure});
            builder.CreateStore(
                llvm::ConstantPointerNull::get(builder.getPtrTy()), address);
            return;
        }
        if (IsStrongManagedPointerTypeName(typeName)) {
            llvm::Value* handle = builder.CreateLoad(
                builder.getInt64Ty(), address, "field.cleanup.handle");
            llvm::Value* pointee = EmitManagedGet(handle, false);
            EmitPointeeCleanup(pointee, typeName);
            builder.CreateCall(ManagedDestroy(), {handle});
            builder.CreateStore(builder.getInt64(0), address);
            return;
        }
        if (ArrayRankName(typeName) > 0) {
            llvm::Value* descriptor = builder.CreateLoad(
                ArrayDescriptorType(typeName), address, "field.cleanup.array");
            llvm::Value* owner = builder.CreateExtractValue(
                descriptor, {1}, "field.cleanup.array.owner");
            builder.CreateCall(Free(), {owner});
            builder.CreateStore(llvm::Constant::getNullValue(
                ArrayDescriptorType(typeName)), address);
            return;
        }
        if (const auto found = classes.find(typeName); found != classes.end()) {
            if (TypeNeedsCleanup(typeName))
                builder.CreateCall(DeclareClassDestructor(found->second), {address});
            return;
        }
        if (const auto found = structs.find(typeName); found != structs.end()) {
            if (TypeNeedsCleanup(typeName))
                builder.CreateCall(DeclareStructDestructor(found->second), {address});
            return;
        }
        if (const PluginResourceDescriptor* descriptor = GetPluginResourceDescriptor(typeName)) {
            if (!descriptor->destroyFunction.empty()) {
                llvm::FunctionType* type = llvm::FunctionType::get(builder.getVoidTy(), {builder.getPtrTy()}, false);
                llvm::FunctionCallee callee = module->getOrInsertFunction(descriptor->destroyFunction, type);
                builder.CreateCall(callee, {address});
            }
        }
    }

    void CodeGenerator::Impl::EmitClassDestructor(ClassInfo& info) {
        llvm::Function* function = DeclareClassDestructor(info);
        if (!function->empty()) return;
        llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
        builder.SetInsertPoint(entry);
        llvm::Value* object = function->getArg(0);

        // Every destroy() hook in the hierarchy, most-derived first. `methods`
        // merges inherited entries, so a derived declaration shadows its
        // base's under the same key: taking only that one meant a base that
        // closes a handle or frees native memory silently stopped doing it as
        // soon as any derived class declared a hook of its own. Its fields
        // were still released -- only the hand-written half went missing,
        // which is why nothing failed loudly. `declaredMethods` holds just
        // what a class declares itself, so walking the base chain finds the
        // shadowed bodies, and the link name keeps an inherited entry from
        // being called twice.
        std::vector<const ClassMethod*> hooks;
        std::unordered_set<std::string> emitted;
        const auto collectHook = [&](const std::unordered_map<std::string, ClassMethod>& methods) {
            const auto found = methods.find(CallableKey("destroy", {}));
            if (found == methods.end()) return;
            if (!emitted.insert(found->second.linkName).second) return;
            hooks.push_back(&found->second);
        };
        collectHook(info.methods);
        for (std::string base = info.baseClass; !base.empty();) {
            const auto found = classes.find(base);
            if (found == classes.end()) break;
            collectHook(found->second.declaredMethods);
            base = found->second.baseClass;
        }
        for (const ClassMethod* hook : hooks) {
            llvm::FunctionCallee callee = module->getOrInsertFunction(
                hook->linkName, MethodFunctionType(*hook));
            EmitAbiCall(MethodFunctionType(*hook), callee.getCallee(),
                hook->returnType, {object}, hook->parameterTypes, {}, "class.destroy.user");
        }

        for (auto field = info.fields.rbegin(); field != info.fields.rend(); ++field) {
            if (!TypeNeedsCleanup(field->typeName)) continue;
            EmitValueCleanup(FieldAddress(object, info, *field), field->typeName);
        }
        if (const PluginResourceDescriptor* descriptor = GetPluginResourceDescriptor(info.name)) {
            if (!descriptor->destroyFunction.empty()) {
                llvm::FunctionType* type = llvm::FunctionType::get(builder.getVoidTy(), {builder.getPtrTy()}, false);
                llvm::FunctionCallee callee = module->getOrInsertFunction(descriptor->destroyFunction, type);
                builder.CreateCall(callee, {object});
            }
        }
        builder.CreateRetVoid();
        builder.ClearInsertionPoint();
    }

    void CodeGenerator::Impl::EmitStructDestructor(StructInfo& info) {
        if (!TypeNeedsCleanup(info.name)) return;
        llvm::Function* function = DeclareStructDestructor(info);
        if (!function->empty()) return;
        llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
        builder.SetInsertPoint(entry);
        llvm::Value* object = function->getArg(0);

        auto destroyMethod = info.methods.find(CallableKey("destroy", {}));
        if (destroyMethod != info.methods.end()) {
            llvm::FunctionCallee callee = module->getOrInsertFunction(
                destroyMethod->second.linkName, MethodFunctionType(destroyMethod->second));
            EmitAbiCall(MethodFunctionType(destroyMethod->second), callee.getCallee(),
                destroyMethod->second.returnType, {object}, destroyMethod->second.parameterTypes, {}, "struct.destroy.user");
        }

        for (auto field = info.fields.rbegin(); field != info.fields.rend(); ++field) {
            if (!TypeNeedsCleanup(field->typeName)) continue;
            EmitValueCleanup(FieldAddress(object, info, *field), field->typeName);
        }
        if (const PluginResourceDescriptor* descriptor = GetPluginResourceDescriptor(info.name)) {
            if (!descriptor->destroyFunction.empty()) {
                llvm::FunctionType* type = llvm::FunctionType::get(builder.getVoidTy(), {builder.getPtrTy()}, false);
                llvm::FunctionCallee callee = module->getOrInsertFunction(descriptor->destroyFunction, type);
                builder.CreateCall(callee, {object});
            }
        }
        builder.CreateRetVoid();
        builder.ClearInsertionPoint();
    }


}
