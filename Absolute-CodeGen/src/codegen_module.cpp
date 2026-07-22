#include "codegen_internal.h"

namespace Absolute {
    llvm::Constant* CodeGenerator::Impl::GlobalConstant(Expression* expression, llvm::Type* type) {
        if (!expression) return llvm::Constant::getNullValue(type);
        if (auto* number = dynamic_cast<NumberLiteralExpr*>(expression)) {
            if (type->isFloatingPointTy())
                return llvm::ConstantFP::get(type, std::stod(number->value));
            return llvm::ConstantInt::get(type, std::stoll(number->value), true);
        }
        if (auto* boolean = dynamic_cast<BooleanLiteralExpr*>(expression))
            return llvm::ConstantInt::get(type, boolean->value ? 1 : 0);
        if (auto* character = dynamic_cast<CharLiteralExpr*>(expression))
            return llvm::ConstantInt::get(type, static_cast<unsigned char>(character->value));
        if (auto* string = dynamic_cast<StringLiteralExpr*>(expression)) {
            if (!type->isPointerTy()) Fail("string constant requires pointer storage");
            return llvm::cast<llvm::Constant>(builder.CreateGlobalStringPtr(
                string->value, "static.string", 0, module.get()));
        }
        if (dynamic_cast<NullExpr*>(expression)) {
            if (!type->isPointerTy()) Fail("null constant requires pointer storage");
            return llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(type));
        }
        if (auto* unary = dynamic_cast<PrefixUnaryExpr*>(expression);
            unary && unary->op == "-") {
            if (auto* number = dynamic_cast<NumberLiteralExpr*>(unary->operand.get())) {
                if (type->isFloatingPointTy())
                    return llvm::ConstantFP::get(type, -std::stod(number->value));
                return llvm::ConstantInt::get(type, -std::stoll(number->value), true);
            }
        }
        Fail("global initializer requires a constant primitive value");
    }

    void CodeGenerator::Impl::DeclareGlobalArray(VarDeclExpr& expression) {
        const std::string name = IdentifierName(expression.name.get());
        const std::string typeName = DeclaredTypeName(expression);
        const size_t rank = ArrayRankName(typeName);
        if (name.empty() || rank == 0) Fail("only global arrays are implemented");
        const std::string elementTypeName = ArrayElementTypeName(typeName, rank);
        llvm::Type* elementType = TypeFromName(elementTypeName);
        auto* literal = dynamic_cast<ArrayExpr*>(expression.value.get());
        const auto inferredShape = literal ? InferArrayShape(*literal)
            : std::optional<std::vector<size_t>>{};

        std::vector<size_t> dimensions;
        if (auto* declarator = dynamic_cast<ArrayAccessExpr*>(expression.name.get())) {
            for (size_t index = 0; index < declarator->indexes.size(); ++index) {
                if (auto* size = dynamic_cast<NumberLiteralExpr*>(declarator->indexes[index].get()))
                    dimensions.push_back(static_cast<size_t>(std::stoull(size->value)));
                else if (!declarator->indexes[index] && inferredShape && index < inferredShape->size())
                    dimensions.push_back((*inferredShape)[index]);
                else Fail("global array dimensions must be constant integers");
            }
        }
        else if (inferredShape) dimensions = *inferredShape;
        else Fail("global array requires constant dimensions or an array literal");
        if (dimensions.size() != rank) Fail("global array rank does not match its initializer");

        size_t elementCount = 1;
        for (size_t dimension : dimensions) {
            if (dimension == 0 || elementCount > std::numeric_limits<size_t>::max() / dimension)
                Fail("global array size is invalid");
            elementCount *= dimension;
        }
        llvm::ArrayType* storageType = llvm::ArrayType::get(elementType, elementCount);
        llvm::Constant* initializer = llvm::ConstantAggregateZero::get(storageType);
        if (literal) {
            std::vector<Expression*> values;
            FlattenArrayValues(*literal, values);
            if (values.size() != elementCount)
                Fail("global array initializer size does not match its dimensions");
            std::vector<llvm::Constant*> constants;
            constants.reserve(values.size());
            for (Expression* value : values) constants.push_back(GlobalConstant(value, elementType));
            initializer = llvm::ConstantArray::get(storageType, constants);
        }

        const std::string globalName = Qualify(name);
        auto* storage = new llvm::GlobalVariable(*module, storageType, false,
            llvm::GlobalValue::ExternalLinkage, initializer, globalName);
        storage->setAlignment(llvm::Align(16));
        llvm::Constant* zero = builder.getInt64(0);
        llvm::Constant* indices[] = {zero, zero};
        llvm::Constant* address = llvm::ConstantExpr::getInBoundsGetElementPtr(
            storageType, storage, llvm::ArrayRef<llvm::Constant*>(indices));
        std::vector<llvm::Value*> dimensionValues;
        for (size_t dimension : dimensions) dimensionValues.push_back(builder.getInt64(dimension));
        globals.emplace(globalName, Variable{address, elementType, typeName, false,
            true, elementType, std::move(dimensionValues), nullptr, InvalidSymbolId});
    }

    llvm::Function* CodeGenerator::Impl::DeclareFunction(FunctionDeclStmt& statement) {
        return DeclareFunction(statement, nullptr);
    }

    llvm::Function* CodeGenerator::Impl::DeclareFunction(
        FunctionDeclStmt& statement, const Symbol* specialization) {
        if (!specialization && statement.IsGeneric()) return nullptr;
        if (!statement.name || !statement.returnType) Fail("invalid function declaration");

        const auto oldSubstitutions = currentGenericSubstitutions;
        if (specialization) {
            const Symbol* origin = analyzer ? analyzer->GetSymbol(specialization->genericOrigin) : nullptr;
            if (origin && origin->genericParameters.size() == specialization->genericArguments.size()) {
                currentGenericSubstitutions.clear();
                for (size_t index = 0; index < origin->genericParameters.size(); ++index)
                    currentGenericSubstitutions.emplace(
                        origin->genericParameters[index], specialization->genericArguments[index]);
            }
        }

        const std::string sourceName = specialization
            ? specialization->name : Qualify(statement.name->value);
        g_last_context = "DeclareFunction:" + sourceName;


        std::vector<llvm::Type*> parameterTypes;
        std::vector<std::string> parameterTypeNames;
        const bool external = statement.UsesCAbi();
        const std::string returnTypeName = specialization
            ? specialization->type : ResolveTypeName(statement.returnType.get());

        parameterTypes.reserve(statement.parameters.size() + AbiReturnOffset(returnTypeName, external));
        parameterTypeNames.reserve(statement.parameters.size());
        if (AbiReturnOffset(returnTypeName, external) != 0)
            parameterTypes.push_back(builder.getPtrTy());
        for (size_t index = 0; index < statement.parameters.size(); ++index) {
            const auto& parameter = statement.parameters[index];
            parameterTypeNames.push_back(specialization
                ? specialization->parameterTypes[index] : DeclaredTypeName(*parameter));
            parameterTypes.push_back(AbiParameterType(parameterTypeNames.back(), external));
        }

        const Symbol* symbol = specialization ? specialization : (analyzer
            ? analyzer->FindFunctionSymbol(sourceName, parameterTypeNames) : nullptr);
        const std::string functionName = statement.UsesCAbi() ? statement.name->value :
            (symbol ? FunctionLinkName(*symbol) : sourceName);
        if (!symbol || (analyzer && analyzer->FunctionOverloadCount(sourceName) <= 1))
            functionLinkNames[sourceName] = functionName;

        llvm::FunctionType* functionType = llvm::FunctionType::get(
            AbiReturnType(returnTypeName, external),
            parameterTypes, false);
        if (llvm::Function* existing = module->getFunction(functionName)) {
            if (existing->getFunctionType() != functionType)
                Fail("conflicting declarations for external symbol '" + functionName + "'");
            if (statement.IsExported()) {
                existing->setVisibility(llvm::GlobalValue::DefaultVisibility);
                if (llvm::Triple(module->getTargetTriple()).isOSWindows())
                    existing->setDLLStorageClass(llvm::GlobalValue::DLLExportStorageClass);
            }
            ApplyCallableAttributes(*existing, statement);
            currentGenericSubstitutions = oldSubstitutions;
            return existing;
        }
        llvm::Function* function = llvm::Function::Create(
            functionType, llvm::Function::ExternalLinkage, functionName, *module);
        function->setCallingConv(llvm::CallingConv::C);
        if (statement.IsExported()) {
            function->setVisibility(llvm::GlobalValue::DefaultVisibility);
            if (llvm::Triple(module->getTargetTriple()).isOSWindows())
                function->setDLLStorageClass(llvm::GlobalValue::DLLExportStorageClass);
        }
        ApplyCallableAttributes(*function, statement);

        unsigned argumentIndex = 0;
        if (AbiReturnOffset(returnTypeName, external) != 0)
            function->getArg(argumentIndex++)->setName("__result");
        for (const auto& parameter : statement.parameters)
            function->getArg(argumentIndex++)->setName(IdentifierName(parameter->name.get()));
        currentGenericSubstitutions = oldSubstitutions;
        return function;
    }


    void CodeGenerator::Impl::EmitFunction(FunctionDeclStmt& statement) {
        EmitFunction(statement, nullptr);
    }

    void CodeGenerator::Impl::EmitFunction(
        FunctionDeclStmt& statement, const Symbol* specialization) {
        if (!specialization && statement.IsGeneric()) return;
        const auto oldSubstitutions = currentGenericSubstitutions;


        const std::string oldNamespace = currentNamespace;
        if (specialization) {
            const Symbol* origin = analyzer ? analyzer->GetSymbol(specialization->genericOrigin) : nullptr;
            if (!origin || origin->genericParameters.size() != specialization->genericArguments.size())
                Fail("invalid generic function specialization");
            currentGenericSubstitutions.clear();
            for (size_t index = 0; index < origin->genericParameters.size(); ++index)
                currentGenericSubstitutions.emplace(
                    origin->genericParameters[index], specialization->genericArguments[index]);
            const size_t separator = specialization->name.rfind('.');
            currentNamespace = separator == std::string::npos
                ? std::string{} : specialization->name.substr(0, separator);
        }
        llvm::Function* function = DeclareFunction(statement, specialization);
        if (!function->empty()) Fail("duplicate function body for '" + function->getName().str() + "'");

        llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
        builder.SetInsertPoint(entry);
        PushScope();
        const std::string oldReturnTypeName = currentReturnTypeName;
        llvm::Value* oldReturnStorage = currentReturnStorage;
        currentReturnTypeName = specialization
            ? specialization->type : ResolveTypeName(statement.returnType.get());
        unsigned argumentIndex = 0;
        currentReturnStorage = AbiReturnOffset(
            currentReturnTypeName, statement.UsesCAbi()) != 0
            ? function->getArg(argumentIndex++) : nullptr;

        for (size_t index = 0; index < statement.parameters.size(); ++index) {
            const auto& parameter = statement.parameters[index];
            const std::string typeName = specialization
                ? specialization->parameterTypes[index] : DeclaredTypeName(*parameter);
            BindCallableParameter(*function->getArg(argumentIndex++), *parameter,
                typeName, statement.UsesCAbi());
        }

        if (statement.body) statement.body->Accept(visitor);
        if (!builder.GetInsertBlock()->getTerminator()) {
            if (currentReturnStorage)
                builder.CreateStore(llvm::Constant::getNullValue(
                    TypeFromName(currentReturnTypeName)), currentReturnStorage);
            if (function->getReturnType()->isVoidTy()) builder.CreateRetVoid();
            else builder.CreateRet(llvm::Constant::getNullValue(function->getReturnType()));
        }

        std::string verifierMessage;
        llvm::raw_string_ostream verifierStream(verifierMessage);
        if (llvm::verifyFunction(*function, &verifierStream)) {
            verifierStream.flush();
            Fail("invalid function '" + function->getName().str() + "': " + verifierMessage);
        }

        PopScope();
        currentReturnTypeName = oldReturnTypeName;
        currentReturnStorage = oldReturnStorage;
        builder.ClearInsertionPoint();
        currentGenericSubstitutions = oldSubstitutions;
        currentNamespace = oldNamespace;
    }

    void CodeGenerator::Impl::BindCallableParameter(
        llvm::Argument& argument, VarDeclExpr& parameter,
        const std::string& explicitTypeName, bool external) {
        const std::string name = IdentifierName(parameter.name.get());
        const std::string typeName = explicitTypeName.empty()
            ? SubstituteCodegenType(DeclaredTypeName(parameter), currentGenericSubstitutions)
            : explicitTypeName;
        if (ArrayRankName(typeName) > 0) {
            ArrayView view = ArrayViewFromValue(&argument, typeName);
            Variable variable;
            variable.address = view.address;
            variable.type = view.elementType;
            variable.typeName = typeName;
            variable.isArray = true;
            variable.arrayElementType = view.elementType;
            variable.arrayDimensions = view.dimensions;
            variable.symbol = SemanticSymbol(&parameter);
            variable.arrayOwner = view.owner;
            if (!scopes.back().emplace(name, std::move(variable)).second)
                Fail("duplicate parameter '" + name + "'");
            return;
        }
        if (!external && IsIndirectValueType(typeName)) {
            if (!scopes.back().emplace(name,
                Variable{&argument, TypeFromName(typeName), typeName, false, false, nullptr, {},
                    nullptr, SemanticSymbol(&parameter)}).second)
                Fail("duplicate parameter '" + name + "'");
            return;
        }
        llvm::AllocaInst* address = CreateEntryAlloca(*argument.getParent(), argument.getType(), name);
        builder.CreateStore(&argument, address);
        if (!scopes.back().emplace(name,
            Variable{address, argument.getType(), typeName, false, false, nullptr, {},
                nullptr, SemanticSymbol(&parameter)}).second)
            Fail("duplicate parameter '" + name + "'");
    }

    void CodeGenerator::Impl::FinishClassCallable(llvm::Function& function) {
        if (!builder.GetInsertBlock()->getTerminator()) {
            if (currentReturnStorage)
                builder.CreateStore(llvm::Constant::getNullValue(
                    TypeFromName(currentReturnTypeName)), currentReturnStorage);
            if (function.getReturnType()->isVoidTy()) builder.CreateRetVoid();
            else builder.CreateRet(llvm::Constant::getNullValue(function.getReturnType()));
        }
        std::string verifierMessage;
        llvm::raw_string_ostream verifierStream(verifierMessage);
        if (llvm::verifyFunction(function, &verifierStream)) {
            verifierStream.flush();
            Fail("invalid class function '" + function.getName().str() + "': " + verifierMessage);
        }
    }

    void CodeGenerator::Impl::EmitMethod(ClassInfo& info, const ClassMethod& method) {
        llvm::Function* function = DeclareMethodFunction(method);
        if (!function->empty()) return;
        llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
        builder.SetInsertPoint(entry);
        PushScope();
        const std::string oldClass = currentClassName;
        llvm::Value* oldThis = currentThis;
        const std::string oldReturn = currentReturnTypeName;
        llvm::Value* oldReturnStorage = currentReturnStorage;
        const auto oldSubstitutions = currentGenericSubstitutions;
        currentGenericSubstitutions = method.substitutions;
        currentClassName = info.name;
        currentReturnTypeName = ResolveTypeName(method.statement->returnType.get());
        const unsigned returnOffset = AbiReturnOffset(method.returnType);
        currentReturnStorage = returnOffset != 0 ? function->getArg(0) : nullptr;
        currentThis = method.isStatic ? nullptr : function->getArg(returnOffset);
        const unsigned offset = returnOffset + (method.isStatic ? 0U : 1U);
        for (size_t index = 0; index < method.statement->parameters.size(); ++index)
            BindCallableParameter(*function->getArg(static_cast<unsigned>(index) + offset),
                *method.statement->parameters[index], method.parameterTypes[index]);
        if (method.statement->autoPropertyAccessor) {
            llvm::Value* storage = ImplicitFieldAddress(
                PropertyBackingFieldName(method.statement->propertyName));
            if (!storage) Fail("missing auto-property storage for '" +
                method.statement->propertyName + "'");
            if (method.statement->propertyAccessor == PropertyAccessorKind::Getter) {
                llvm::Value* propertyValue = builder.CreateLoad(
                    TypeFromName(method.returnType), storage, "auto.property.value");
                if (currentReturnStorage) {
                    builder.CreateStore(propertyValue, currentReturnStorage);
                    builder.CreateRetVoid();
                }
                else builder.CreateRet(propertyValue);
            }
            else {
                llvm::Value* assigned = function->getArg(offset);
                if (IsIndirectValueType(method.parameterTypes.front()))
                    assigned = builder.CreateLoad(
                        TypeFromName(method.parameterTypes.front()), assigned, "auto.property.input");
                if (TypeNeedsCleanup(method.parameterTypes.front()))
                    EmitValueCleanup(storage, method.parameterTypes.front());
                builder.CreateStore(assigned, storage);
                builder.CreateRetVoid();
            }
        }
        else if (method.statement->body) method.statement->body->Accept(visitor);
        FinishClassCallable(*function);
        PopScope();
        currentClassName = oldClass;
        currentThis = oldThis;
        currentReturnTypeName = oldReturn;
        currentReturnStorage = oldReturnStorage;
        currentGenericSubstitutions = oldSubstitutions;
        builder.ClearInsertionPoint();
    }

    void CodeGenerator::Impl::EmitConstructor(ClassInfo& info) {
        if (!ClassNeedsConstructor(info)) return;
        llvm::Function* function = DeclareConstructorFunction(info);
        if (!function->empty()) return;
        llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
        builder.SetInsertPoint(entry);
        PushScope();
        const std::string oldClass = currentClassName;
        llvm::Value* oldThis = currentThis;
        const std::string oldReturn = currentReturnTypeName;
        const auto oldSubstitutions = currentGenericSubstitutions;
        currentGenericSubstitutions = info.substitutions;
        currentClassName = info.name;
        currentThis = function->getArg(0);
        currentReturnTypeName = "void";
        if (info.constructor)
            for (size_t index = 0; index < info.constructor->parameters.size(); ++index)
                BindCallableParameter(*function->getArg(static_cast<unsigned>(index + 1)),
                    *info.constructor->parameters[index]);
        if (!info.baseClass.empty()) {
            auto base = classes.find(info.baseClass);
            if (base == classes.end()) Fail("missing base class '" + info.baseClass + "'");
            if (ClassNeedsConstructor(base->second)) {
                llvm::Function* baseConstructor = module->getFunction(info.baseClass + ".__ctor");
                if (!baseConstructor) Fail("missing base constructor for '" + info.baseClass + "'");
                std::vector<llvm::Value*> arguments;
                std::vector<std::string> parameterTypes;
                if (info.constructor && info.constructor->hasExplicitBaseCall) {
                    for (const auto& argument : info.constructor->baseArguments)
                        arguments.push_back(Evaluate(argument.get()));
                }
                if (base->second.constructor) {
                    for (const auto& parameter : base->second.constructor->parameters)
                        parameterTypes.push_back(SubstituteCodegenType(
                            DeclaredTypeName(*parameter), base->second.substitutions));
                }
                EmitAbiCall(baseConstructor->getFunctionType(), baseConstructor, "void",
                    {currentThis}, parameterTypes, arguments, "base.constructor.result");
                EmitExceptionCheck();
            }
        }
        if (info.constructor && info.constructor->body) info.constructor->body->Accept(visitor);
        FinishClassCallable(*function);
        PopScope();
        currentClassName = oldClass;
        currentThis = oldThis;
        currentReturnTypeName = oldReturn;
        currentGenericSubstitutions = oldSubstitutions;
        builder.ClearInsertionPoint();
    }

    void CodeGenerator::Impl::EmitClassBodies(ClassInfo& info) {
        if (info.emitted) return;
        for (const auto& [methodName, method] : info.declaredMethods) {
            (void)methodName;
            EmitMethod(info, method);
        }
        EmitConstructor(info);
        EmitClassDestructor(info);
        info.emitted = true;
    }

    void CodeGenerator::Impl::EmitInterfaceMethod(
        InterfaceInfo& info, const ClassMethod& method) {
        llvm::Function* function = DeclareMethodFunction(method);
        if (!function->empty()) return;
        llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
        builder.SetInsertPoint(entry);
        PushScope();
        const std::string oldClass = currentClassName;
        llvm::Value* oldThis = currentThis;
        const std::string oldReturn = currentReturnTypeName;
        llvm::Value* oldReturnStorage = currentReturnStorage;
        currentClassName = info.name;
        currentReturnTypeName = ResolveTypeName(method.statement->returnType.get());
        const unsigned returnOffset = AbiReturnOffset(method.returnType);
        currentReturnStorage = returnOffset != 0 ? function->getArg(0) : nullptr;
        currentThis = method.isStatic ? nullptr : function->getArg(returnOffset);
        const unsigned offset = returnOffset + (method.isStatic ? 0U : 1U);
        for (size_t index = 0; index < method.statement->parameters.size(); ++index)
            BindCallableParameter(*function->getArg(static_cast<unsigned>(index) + offset),
                *method.statement->parameters[index], method.parameterTypes[index]);
        method.statement->body->Accept(visitor);
        FinishClassCallable(*function);
        PopScope();
        currentClassName = oldClass;
        currentThis = oldThis;
        currentReturnTypeName = oldReturn;
        currentReturnStorage = oldReturnStorage;
        builder.ClearInsertionPoint();
    }

    void CodeGenerator::Impl::EmitInterfaceBodies(InterfaceInfo& info) {
        if (info.emitted) return;
        for (const auto& [methodName, method] : info.declaredMethods) {
            (void)methodName;
            EmitInterfaceMethod(info, method);
        }
        info.emitted = true;
    }

    void CodeGenerator::Impl::EmitStructMethod(StructInfo& info, const ClassMethod& method) {
        llvm::Function* function = DeclareMethodFunction(method);
        if (!function->empty()) return;
        llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
        builder.SetInsertPoint(entry);
        PushScope();
        const std::string oldClass = currentClassName;
        llvm::Value* oldThis = currentThis;
        const std::string oldReturn = currentReturnTypeName;
        llvm::Value* oldReturnStorage = currentReturnStorage;
        const auto oldSubstitutions = currentGenericSubstitutions;
        currentGenericSubstitutions = method.substitutions;
        currentClassName = info.name;
        currentReturnTypeName = ResolveTypeName(method.statement->returnType.get());
        const unsigned returnOffset = AbiReturnOffset(method.returnType);
        currentReturnStorage = returnOffset != 0 ? function->getArg(0) : nullptr;
        currentThis = method.isStatic ? nullptr : function->getArg(returnOffset);
        const unsigned offset = returnOffset + (method.isStatic ? 0U : 1U);
        for (size_t index = 0; index < method.statement->parameters.size(); ++index)
            BindCallableParameter(*function->getArg(static_cast<unsigned>(index) + offset),
                *method.statement->parameters[index], method.parameterTypes[index]);
        if (method.statement->autoPropertyAccessor) {
            llvm::Value* storage = ImplicitFieldAddress(
                PropertyBackingFieldName(method.statement->propertyName));
            if (!storage) Fail("missing auto-property storage for '" +
                method.statement->propertyName + "'");
            if (method.statement->propertyAccessor == PropertyAccessorKind::Getter) {
                llvm::Value* propertyValue = builder.CreateLoad(
                    TypeFromName(method.returnType), storage, "auto.property.value");
                if (currentReturnStorage) {
                    builder.CreateStore(propertyValue, currentReturnStorage);
                    builder.CreateRetVoid();
                }
                else builder.CreateRet(propertyValue);
            }
            else {
                llvm::Value* assigned = function->getArg(offset);
                if (IsIndirectValueType(method.parameterTypes.front()))
                    assigned = builder.CreateLoad(
                        TypeFromName(method.parameterTypes.front()), assigned, "auto.property.input");
                if (TypeNeedsCleanup(method.parameterTypes.front()))
                    EmitValueCleanup(storage, method.parameterTypes.front());
                builder.CreateStore(assigned, storage);
                builder.CreateRetVoid();
            }
        }
        else if (method.statement->body) method.statement->body->Accept(visitor);
        FinishClassCallable(*function);
        PopScope();
        currentClassName = oldClass;
        currentThis = oldThis;
        currentReturnTypeName = oldReturn;
        currentReturnStorage = oldReturnStorage;
        currentGenericSubstitutions = oldSubstitutions;
        builder.ClearInsertionPoint();
    }

    void CodeGenerator::Impl::EmitStructConstructor(StructInfo& info) {
        if (!info.constructor) return;
        llvm::Function* function = DeclareConstructorFunction(info);
        if (!function->empty()) return;
        llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
        builder.SetInsertPoint(entry);
        PushScope();
        const std::string oldClass = currentClassName;
        llvm::Value* oldThis = currentThis;
        const std::string oldReturn = currentReturnTypeName;
        const auto oldSubstitutions = currentGenericSubstitutions;
        currentGenericSubstitutions = info.substitutions;
        currentClassName = info.name;
        currentThis = function->getArg(0);
        currentReturnTypeName = "void";
        for (size_t index = 0; index < info.constructor->parameters.size(); ++index)
            BindCallableParameter(*function->getArg(static_cast<unsigned>(index + 1)),
                *info.constructor->parameters[index]);
        if (info.constructor->body) info.constructor->body->Accept(visitor);
        FinishClassCallable(*function);
        PopScope();
        currentClassName = oldClass;
        currentThis = oldThis;
        currentReturnTypeName = oldReturn;
        currentGenericSubstitutions = oldSubstitutions;
        builder.ClearInsertionPoint();
    }

    void CodeGenerator::Impl::EmitStructBodies(StructInfo& info) {
        if (info.emitted) return;
        for (const auto& [methodName, method] : info.methods) {
            (void)methodName;
            EmitStructMethod(info, method);
        }
        EmitStructConstructor(info);
        EmitStructDestructor(info);
        info.emitted = true;
    }

    llvm::Module& CodeGenerator::Impl::BuildModule(Program& program, const std::string& moduleName,
        const std::string& targetTriple, const std::string& dataLayout) {
        module = std::make_unique<llvm::Module>(moduleName, context);
        if (!targetTriple.empty()) module->setTargetTriple(targetTriple);
        if (!dataLayout.empty()) module->setDataLayout(dataLayout);
        scopes.clear();
        globals.clear();
        arrayDescriptorTypes.clear();
        classes.clear();
        classOrder.clear();
        structs.clear();
        structOrder.clear();
        interfaces.clear();
        interfaceOrder.clear();
        enumTypes.clear();
        enumConstants.clear();
        interfaceSlotCount = 1;
        loops.clear();
        exceptionTargets.clear();
        finallyTargets.clear();
        caughtExceptions.clear();
        deferredScopes.clear();
        deferredCleanupCursors.clear();
        emittingFinally = false;
        exceptionsEnabled = analyzer ? analyzer->UsesExceptions() : true;
        currentNamespace.clear();
        functionLinkNames.clear();
        value = nullptr;
        currentValueType.clear();
        valueCreatesManagedOwner = false;
        valueManagedPointee = nullptr;
        valueCreatesArrayOwner = false;
        valueArrayOwner = nullptr;
        addressMode = false;
        addressValue = nullptr;
        taskThunkCounter = 0;
        currentClassName.clear();
        currentThis = nullptr;
        currentReturnStorage = nullptr;
        currentGenericSubstitutions.clear();

        CollectClassDeclarations(program.statements);
        FinalizeInterfaces();
        DeclareStructs();
        DeclareClasses();

        phase = Phase::DeclareFunctions;
        for (const auto& statement : program.statements) {
            if (statement) statement->Accept(visitor);
        }
        if (analyzer) {
            for (SymbolId id : analyzer->GenericFunctionSpecializations()) {
                const Symbol* specialization = analyzer->GetSymbol(id);
                FunctionDeclStmt* declaration = analyzer->FunctionDeclaration(id);
                if (specialization && declaration && specialization->kind == SymbolKind::Function)
                    DeclareFunction(*declaration, specialization);
            }
        }

        phase = Phase::EmitBodies;
        for (const auto& statement : program.statements) {
            if (statement) statement->Accept(visitor);
        }
        if (analyzer) {
            for (SymbolId id : analyzer->GenericFunctionSpecializations()) {
                const Symbol* specialization = analyzer->GetSymbol(id);
                FunctionDeclStmt* declaration = analyzer->FunctionDeclaration(id);
                if (specialization && declaration && specialization->kind == SymbolKind::Function)
                    EmitFunction(*declaration, specialization);
            }
        }

        std::string verifierMessage;
        llvm::raw_string_ostream verifierStream(verifierMessage);
        if (llvm::verifyModule(*module, &verifierStream)) {
            verifierStream.flush();
            Fail("module verification failed: " + verifierMessage);
        }

        return *module;
    }

    std::string CodeGenerator::Impl::Generate(Program& program, const std::string& moduleName) {
        BuildModule(program, moduleName, llvm::sys::getDefaultTargetTriple());

        std::string output;
        llvm::raw_string_ostream stream(output);
        module->print(stream, nullptr);
        stream.flush();
        return output;
    }

    void CodeGenerator::Impl::GenerateObject(Program& program, const std::string& moduleName, const std::string& outputPath, bool sanitizeAddress) {
        static std::once_flag initializeTarget;
        std::call_once(initializeTarget, [] {
            if (llvm::InitializeNativeTarget())
                throw std::runtime_error("LLVM codegen: failed to initialize the native target");
            if (llvm::InitializeNativeTargetAsmPrinter())
                throw std::runtime_error("LLVM codegen: failed to initialize the native assembly printer");
        });

        const std::string triple = llvm::sys::getDefaultTargetTriple();
        std::string targetError;
        const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, targetError);
        if (!target) Fail("cannot select target '" + triple + "': " + targetError);

        llvm::TargetOptions options;
        const std::string cpu = llvm::sys::getHostCPUName().str();
        std::unique_ptr<llvm::TargetMachine> targetMachine(target->createTargetMachine(
            triple, cpu.empty() ? "generic" : cpu, "", options, llvm::Reloc::PIC_,
            std::nullopt, llvm::CodeGenOptLevel::Aggressive));
        if (!targetMachine) Fail("cannot create target machine for '" + triple + "'");
        const std::string dataLayout = targetMachine->createDataLayout().getStringRepresentation();
        llvm::Module& generatedModule = BuildModule(program, moduleName, triple, dataLayout);

        llvm::LoopAnalysisManager loopAnalyses;
        llvm::FunctionAnalysisManager functionAnalyses;
        llvm::CGSCCAnalysisManager cgsccAnalyses;
        llvm::ModuleAnalysisManager moduleAnalyses;
        llvm::PassBuilder passBuilder(targetMachine.get());
        if (sanitizeAddress) {
            passBuilder.registerOptimizerLastEPCallback(
                [](llvm::ModulePassManager& mpm, llvm::OptimizationLevel) {
                    llvm::AddressSanitizerOptions options;
                    mpm.addPass(llvm::AddressSanitizerPass(options));
                });
        }
        passBuilder.registerModuleAnalyses(moduleAnalyses);
        passBuilder.registerCGSCCAnalyses(cgsccAnalyses);
        passBuilder.registerFunctionAnalyses(functionAnalyses);
        passBuilder.registerLoopAnalyses(loopAnalyses);
        passBuilder.crossRegisterProxies(
            loopAnalyses, functionAnalyses, cgsccAnalyses, moduleAnalyses);
        llvm::ModulePassManager optimizationPasses =
            passBuilder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O3);
        optimizationPasses.run(generatedModule, moduleAnalyses);

        std::error_code error;
        llvm::raw_fd_ostream output(outputPath, error, llvm::sys::fs::OF_None);
        if (error) Fail("cannot create object file '" + outputPath + "': " + error.message());
        llvm::legacy::PassManager passes;
#if LLVM_VERSION_MAJOR >= 18
        constexpr llvm::CodeGenFileType fileType = llvm::CodeGenFileType::ObjectFile;
#else
        constexpr llvm::CodeGenFileType fileType = llvm::CGFT_ObjectFile;
#endif
        if (targetMachine->addPassesToEmitFile(passes, output, nullptr, fileType))
            Fail("target cannot emit an object file");
        passes.run(generatedModule);
        output.flush();
    }
}
