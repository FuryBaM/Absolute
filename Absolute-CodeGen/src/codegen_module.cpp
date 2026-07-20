#include "codegen_internal.h"

namespace Absolute {
    llvm::Constant* CodeGenerator::Impl::GlobalArrayConstant(Expression* expression, llvm::Type* type) {
        if (auto* number = dynamic_cast<NumberLiteralExpr*>(expression)) {
            if (type->isFloatingPointTy())
                return llvm::ConstantFP::get(type, std::stod(number->value));
            return llvm::ConstantInt::get(type, std::stoll(number->value), true);
        }
        if (auto* boolean = dynamic_cast<BooleanLiteralExpr*>(expression))
            return llvm::ConstantInt::get(type, boolean->value ? 1 : 0);
        if (auto* character = dynamic_cast<CharLiteralExpr*>(expression))
            return llvm::ConstantInt::get(type, static_cast<unsigned char>(character->value));
        if (auto* unary = dynamic_cast<PrefixUnaryExpr*>(expression);
            unary && unary->op == "-") {
            if (auto* number = dynamic_cast<NumberLiteralExpr*>(unary->operand.get())) {
                if (type->isFloatingPointTy())
                    return llvm::ConstantFP::get(type, -std::stod(number->value));
                return llvm::ConstantInt::get(type, -std::stoll(number->value), true);
            }
        }
        Fail("global array initializer requires constant primitive values");
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
            for (Expression* value : values) constants.push_back(GlobalArrayConstant(value, elementType));
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
        if (!statement.name || !statement.returnType) Fail("invalid function declaration");
        const std::string sourceName = Qualify(statement.name->value);

        std::vector<llvm::Type*> parameterTypes;
        std::vector<std::string> parameterTypeNames;
        parameterTypes.reserve(statement.parameters.size());
        parameterTypeNames.reserve(statement.parameters.size());
        for (const auto& parameter : statement.parameters) {
            parameterTypeNames.push_back(DeclaredTypeName(*parameter));
            parameterTypes.push_back(TypeFromName(parameterTypeNames.back()));
        }

        const Symbol* symbol = analyzer
            ? analyzer->FindFunctionSymbol(sourceName, parameterTypeNames) : nullptr;
        const std::string functionName = statement.IsExternal() ? statement.name->value :
            (symbol ? FunctionLinkName(*symbol) : sourceName);
        if (!symbol || (analyzer && analyzer->FunctionOverloadCount(sourceName) <= 1))
            functionLinkNames[sourceName] = functionName;

        llvm::FunctionType* functionType = llvm::FunctionType::get(
            ResolveType(statement.returnType.get()), parameterTypes, false);
        if (llvm::Function* existing = module->getFunction(functionName)) {
            if (existing->getFunctionType() != functionType)
                Fail("conflicting declarations for external symbol '" + functionName + "'");
            ApplyCallableAttributes(*existing, statement);
            return existing;
        }
        llvm::Function* function = llvm::Function::Create(
            functionType, llvm::Function::ExternalLinkage, functionName, *module);
        function->setCallingConv(llvm::CallingConv::C);
        ApplyCallableAttributes(*function, statement);

        size_t index = 0;
        for (llvm::Argument& argument : function->args()) {
            const std::string name = IdentifierName(statement.parameters[index++]->name.get());
            argument.setName(name);
        }
        return function;
    }

    void CodeGenerator::Impl::EmitFunction(FunctionDeclStmt& statement) {
        llvm::Function* function = DeclareFunction(statement);
        if (!function->empty()) Fail("duplicate function body for '" + function->getName().str() + "'");

        llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
        builder.SetInsertPoint(entry);
        PushScope();
        const std::string oldReturnTypeName = currentReturnTypeName;
        currentReturnTypeName = ResolveTypeName(statement.returnType.get());

        size_t index = 0;
        for (llvm::Argument& argument : function->args()) {
            const auto& parameter = statement.parameters[index++];
            const std::string name = IdentifierName(parameter->name.get());
            const std::string typeName = DeclaredTypeName(*parameter);
            if (ArrayRankName(typeName) > 0) {
                ArrayView view = ArrayViewFromValue(&argument, typeName);
                if (!scopes.back().emplace(name,
                    Variable{view.address, view.elementType, typeName, false,
                        true, view.elementType, view.dimensions, nullptr, InvalidSymbolId}).second) {
                    Fail("duplicate parameter '" + name + "'");
                }
                continue;
            }
            llvm::AllocaInst* address = CreateEntryAlloca(*function, argument.getType(), name);
            builder.CreateStore(&argument, address);
            if (!scopes.back().emplace(name,
                Variable{address, argument.getType(), typeName, false, false, nullptr, {},
                    nullptr, SemanticSymbol(parameter.get())}).second) {
                Fail("duplicate parameter '" + name + "'");
            }
        }

        if (statement.body) statement.body->Accept(visitor);
        if (!builder.GetInsertBlock()->getTerminator()) {
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
        builder.ClearInsertionPoint();
    }

    void CodeGenerator::Impl::BindCallableParameter(llvm::Argument& argument, VarDeclExpr& parameter) {
        const std::string name = IdentifierName(parameter.name.get());
        const std::string typeName = DeclaredTypeName(parameter);
        if (ArrayRankName(typeName) > 0) {
            ArrayView view = ArrayViewFromValue(&argument, typeName);
            if (!scopes.back().emplace(name,
                Variable{view.address, view.elementType, typeName, false,
                    true, view.elementType, view.dimensions, nullptr, InvalidSymbolId}).second)
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
        currentClassName = info.name;
        currentThis = function->getArg(0);
        currentReturnTypeName = ResolveTypeName(method.statement->returnType.get());
        for (size_t index = 0; index < method.statement->parameters.size(); ++index)
            BindCallableParameter(*function->getArg(static_cast<unsigned>(index + 1)),
                *method.statement->parameters[index]);
        if (method.statement->body) method.statement->body->Accept(visitor);
        FinishClassCallable(*function);
        PopScope();
        currentClassName = oldClass;
        currentThis = oldThis;
        currentReturnTypeName = oldReturn;
        builder.ClearInsertionPoint();
    }

    void CodeGenerator::Impl::EmitConstructor(ClassInfo& info) {
        if (!info.constructor) return;
        llvm::Function* function = DeclareConstructorFunction(info);
        if (!function->empty()) return;
        llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
        builder.SetInsertPoint(entry);
        PushScope();
        const std::string oldClass = currentClassName;
        llvm::Value* oldThis = currentThis;
        const std::string oldReturn = currentReturnTypeName;
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
        builder.ClearInsertionPoint();
    }

    void CodeGenerator::Impl::EmitClassBodies(ClassInfo& info) {
        if (info.emitted) return;
        for (const auto& [methodName, method] : info.declaredMethods) {
            (void)methodName;
            EmitMethod(info, method);
        }
        EmitConstructor(info);
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
        currentClassName = info.name;
        currentThis = function->getArg(0);
        currentReturnTypeName = ResolveTypeName(method.statement->returnType.get());
        for (size_t index = 0; index < method.statement->parameters.size(); ++index)
            BindCallableParameter(*function->getArg(static_cast<unsigned>(index + 1)),
                *method.statement->parameters[index]);
        if (method.statement->body) method.statement->body->Accept(visitor);
        FinishClassCallable(*function);
        PopScope();
        currentClassName = oldClass;
        currentThis = oldThis;
        currentReturnTypeName = oldReturn;
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
        builder.ClearInsertionPoint();
    }

    void CodeGenerator::Impl::EmitStructBodies(StructInfo& info) {
        if (info.emitted) return;
        for (const auto& [methodName, method] : info.methods) {
            (void)methodName;
            EmitStructMethod(info, method);
        }
        EmitStructConstructor(info);
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
        interfaceSlotCount = 0;
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
        addressMode = false;
        addressValue = nullptr;
        taskThunkCounter = 0;
        currentClassName.clear();
        currentThis = nullptr;

        CollectClassDeclarations(program.statements);
        FinalizeInterfaces();
        DeclareStructs();
        DeclareClasses();

        phase = Phase::DeclareFunctions;
        for (const auto& statement : program.statements) {
            if (statement) statement->Accept(visitor);
        }

        phase = Phase::EmitBodies;
        for (const auto& statement : program.statements) {
            if (statement) statement->Accept(visitor);
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

    void CodeGenerator::Impl::GenerateObject(Program& program, const std::string& moduleName, const std::string& outputPath) {
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
