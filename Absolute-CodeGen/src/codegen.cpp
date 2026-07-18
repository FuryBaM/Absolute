#include "codegen.h"
#include "analyzer.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Absolute {
    namespace {
        class PrimitiveTypeNameVisitor final : public BaseIdentifierVisitor {
        public:
            std::string name;

            void Visit(PrimitiveTypeExpr* expr) override {
                name = expr->type;
            }

            void Visit(UserTypeExpr* expr) override {
                (void)expr;
            }
        };

        class StringLiteralProbe final : public BaseIdentifierVisitor {
        public:
            const StringLiteralExpr* literal = nullptr;

            void Visit(StringLiteralExpr* expr) override { literal = expr; }
        };

        std::string IdentifierName(Expression* expression) {
            if (!expression) return {};
            BaseIdentifierVisitor visitor;
            expression->Accept(visitor);
            return visitor.identifierExpr ? visitor.identifierExpr->name : std::string{};
        }
    }

    struct CodeGenerator::Impl {
        enum class Phase {
            DeclareFunctions,
            EmitBodies
        };

        struct Variable {
            llvm::AllocaInst* address = nullptr;
            llvm::Type* type = nullptr;
        };

        struct PrintableValue {
            std::string specifier;
            llvm::Value* value = nullptr;
        };

        CodeGenerator& visitor;
        const Analyzer* analyzer = nullptr;
        llvm::LLVMContext context;
        std::unique_ptr<llvm::Module> module;
        llvm::IRBuilder<> builder;
        llvm::Value* value = nullptr;
        Phase phase = Phase::DeclareFunctions;
        std::vector<std::unordered_map<std::string, Variable>> scopes;
        std::vector<std::pair<llvm::BasicBlock*, llvm::BasicBlock*>> loops;
        std::string currentNamespace;

        explicit Impl(CodeGenerator& visitor, const Analyzer* analyzer)
            : visitor(visitor), analyzer(analyzer), builder(context) {
        }

        [[noreturn]] void Fail(const std::string& message) const {
            throw std::runtime_error("LLVM codegen: " + message);
        }

        llvm::Type* TypeFromName(const std::string& name) {
            if (name == "int8" || name == "uint8" || name == "char") return builder.getInt8Ty();
            if (name == "int16" || name == "uint16") return builder.getInt16Ty();
            if (name == "int32" || name == "uint32") return builder.getInt32Ty();
            if (name == "int64" || name == "uint64") return builder.getInt64Ty();
            if (name == "bool") return builder.getInt1Ty();
            if (name == "float") return builder.getFloatTy();
            if (name == "double") return builder.getDoubleTy();
            if (name == "string") return builder.getPtrTy();
            if (name == "void") return builder.getVoidTy();
            Fail("unsupported type '" + name + "'");
        }

        llvm::Type* ResolveType(Expression* expression) {
            if (!expression) Fail("missing type expression");
            PrimitiveTypeNameVisitor typeVisitor;
            expression->Accept(typeVisitor);
            if (typeVisitor.name.empty()) Fail("user-defined types are not implemented yet");
            return TypeFromName(typeVisitor.name);
        }

        llvm::Value* Evaluate(Expression* expression) {
            if (!expression) Fail("missing expression");
            value = nullptr;
            expression->Accept(visitor);
            if (!value) Fail("expression does not produce a value");
            return value;
        }

        void PushScope() {
            scopes.emplace_back();
        }

        void PopScope() {
            if (!scopes.empty()) scopes.pop_back();
        }

        Variable* FindVariable(const std::string& name) {
            for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
                auto found = scope->find(name);
                if (found != scope->end()) return &found->second;
            }
            return nullptr;
        }

        Variable& RequireVariable(const std::string& name) {
            Variable* variable = FindVariable(name);
            if (!variable) Fail("unknown variable '" + name + "'");
            return *variable;
        }

        Variable& AddressOf(Expression* expression) {
            const std::string name = IdentifierName(expression);
            if (name.empty()) Fail("expected assignable identifier");
            return RequireVariable(name);
        }

        llvm::AllocaInst* CreateEntryAlloca(llvm::Function& function, llvm::Type* type, const std::string& name) {
            llvm::IRBuilder<> entryBuilder(&function.getEntryBlock(), function.getEntryBlock().begin());
            return entryBuilder.CreateAlloca(type, nullptr, name);
        }

        llvm::Value* Coerce(llvm::Value* source, llvm::Type* target) {
            if (!source) Fail("cannot convert an empty value");
            llvm::Type* sourceType = source->getType();
            if (sourceType == target) return source;

            if (sourceType->isIntegerTy() && target->isIntegerTy()) {
                return builder.CreateIntCast(source, target, true, "int.cast");
            }
            if (sourceType->isIntegerTy() && target->isFloatingPointTy()) {
                return builder.CreateSIToFP(source, target, "int.to.fp");
            }
            if (sourceType->isFloatingPointTy() && target->isIntegerTy()) {
                return builder.CreateFPToSI(source, target, "fp.to.int");
            }
            if (sourceType->isFloatingPointTy() && target->isFloatingPointTy()) {
                return builder.CreateFPCast(source, target, "fp.cast");
            }
            if (sourceType->isPointerTy() && target->isPointerTy()) {
                return builder.CreatePointerCast(source, target, "ptr.cast");
            }
            Fail("incompatible value conversion");
        }

        llvm::Type* CommonNumericType(llvm::Type* left, llvm::Type* right) {
            if (left->isDoubleTy() || right->isDoubleTy()) return builder.getDoubleTy();
            if (left->isFloatTy() || right->isFloatTy()) return builder.getFloatTy();
            if (left->isIntegerTy() && right->isIntegerTy()) {
                const unsigned bits = std::max(left->getIntegerBitWidth(), right->getIntegerBitWidth());
                return llvm::IntegerType::get(context, std::max(bits, 1U));
            }
            Fail("binary operator requires numeric operands");
        }

        llvm::Value* AsCondition(llvm::Value* condition) {
            llvm::Type* type = condition->getType();
            if (type->isIntegerTy(1)) return condition;
            if (type->isIntegerTy()) {
                return builder.CreateICmpNE(condition, llvm::ConstantInt::get(type, 0), "condition");
            }
            if (type->isFloatingPointTy()) {
                return builder.CreateFCmpONE(condition, llvm::ConstantFP::get(type, 0.0), "condition");
            }
            if (type->isPointerTy()) {
                return builder.CreateIsNotNull(condition, "condition");
            }
            Fail("value cannot be used as a condition");
        }

        llvm::Value* ApplyBinary(const std::string& op, llvm::Value* left, llvm::Value* right) {
            if (op == "&&" || op == "||") {
                left = AsCondition(left);
                right = AsCondition(right);
                return op == "&&" ? builder.CreateAnd(left, right, "logical.and")
                                    : builder.CreateOr(left, right, "logical.or");
            }

            llvm::Type* type = CommonNumericType(left->getType(), right->getType());
            left = Coerce(left, type);
            right = Coerce(right, type);
            const bool floating = type->isFloatingPointTy();

            if (op == "+") return floating ? builder.CreateFAdd(left, right, "add") : builder.CreateAdd(left, right, "add");
            if (op == "-") return floating ? builder.CreateFSub(left, right, "sub") : builder.CreateSub(left, right, "sub");
            if (op == "*") return floating ? builder.CreateFMul(left, right, "mul") : builder.CreateMul(left, right, "mul");
            if (op == "/") return floating ? builder.CreateFDiv(left, right, "div") : builder.CreateSDiv(left, right, "div");
            if (op == "%") return floating ? builder.CreateFRem(left, right, "rem") : builder.CreateSRem(left, right, "rem");

            if (op == "==") return floating ? builder.CreateFCmpOEQ(left, right, "equal") : builder.CreateICmpEQ(left, right, "equal");
            if (op == "!=") return floating ? builder.CreateFCmpONE(left, right, "not.equal") : builder.CreateICmpNE(left, right, "not.equal");
            if (op == "<") return floating ? builder.CreateFCmpOLT(left, right, "less") : builder.CreateICmpSLT(left, right, "less");
            if (op == "<=") return floating ? builder.CreateFCmpOLE(left, right, "less.equal") : builder.CreateICmpSLE(left, right, "less.equal");
            if (op == ">") return floating ? builder.CreateFCmpOGT(left, right, "greater") : builder.CreateICmpSGT(left, right, "greater");
            if (op == ">=") return floating ? builder.CreateFCmpOGE(left, right, "greater.equal") : builder.CreateICmpSGE(left, right, "greater.equal");

            if (!type->isIntegerTy()) Fail("bitwise operator requires integer operands");
            if (op == "&") return builder.CreateAnd(left, right, "bit.and");
            if (op == "|") return builder.CreateOr(left, right, "bit.or");
            if (op == "^") return builder.CreateXor(left, right, "bit.xor");
            if (op == "<<") return builder.CreateShl(left, right, "shift.left");
            if (op == ">>") return builder.CreateAShr(left, right, "shift.right");
            Fail("unsupported binary operator '" + op + "'");
        }

        llvm::Value* One(llvm::Type* type) {
            if (type->isFloatingPointTy()) return llvm::ConstantFP::get(type, 1.0);
            if (type->isIntegerTy()) return llvm::ConstantInt::get(type, 1);
            Fail("increment requires a numeric operand");
        }

        void BranchIfNeeded(llvm::BasicBlock* target) {
            llvm::BasicBlock* block = builder.GetInsertBlock();
            if (block && !block->getTerminator()) builder.CreateBr(target);
        }

        llvm::Function* CurrentFunction() {
            llvm::BasicBlock* block = builder.GetInsertBlock();
            return block ? block->getParent() : nullptr;
        }

        std::string Qualify(const std::string& name) const {
            if (name.empty() || currentNamespace.empty() || name.find('.') != std::string::npos) return name;
            return currentNamespace + "." + name;
        }

        std::string ResolvedName(Expression* expression) const {
            if (analyzer && expression) {
                if (const ExpressionInfo* info = analyzer->GetExpressionInfo(*expression)) {
                    if (const Symbol* symbol = analyzer->GetSymbol(info->symbol)) return symbol->name;
                }
            }
            return IdentifierName(expression);
        }

        bool IsBuiltinFunction(const std::string& name) const {
            return name == "print" || name == "println" || name == "format" ||
                name == "toString" || name == "assert";
        }

        llvm::FunctionCallee Printf() {
            llvm::FunctionType* type = llvm::FunctionType::get(
                builder.getInt32Ty(), {builder.getPtrTy()}, true);
            return module->getOrInsertFunction("printf", type);
        }

        llvm::FunctionCallee Snprintf() {
            llvm::FunctionType* type = llvm::FunctionType::get(
                builder.getInt32Ty(), {builder.getPtrTy(), builder.getInt64Ty(), builder.getPtrTy()}, true);
            return module->getOrInsertFunction("snprintf", type);
        }

        llvm::FunctionCallee Malloc() {
            llvm::FunctionType* type = llvm::FunctionType::get(
                builder.getPtrTy(), {builder.getInt64Ty()}, false);
            return module->getOrInsertFunction("malloc", type);
        }

        llvm::FunctionCallee Abort() {
            llvm::FunctionType* type = llvm::FunctionType::get(builder.getVoidTy(), {}, false);
            return module->getOrInsertFunction("abort", type);
        }

        std::string SemanticType(Expression* expression) const {
            if (!analyzer || !expression) return {};
            const ExpressionInfo* info = analyzer->GetExpressionInfo(*expression);
            return info ? info->type : std::string{};
        }

        PrintableValue PreparePrintable(llvm::Value* source, Expression* expression) {
            if (!source) Fail("cannot print an empty value");
            llvm::Type* type = source->getType();
            const std::string semanticType = SemanticType(expression);
            if (semanticType == "bool" || type->isIntegerTy(1)) {
                llvm::Value* trueText = builder.CreateGlobalStringPtr("true", "bool.true");
                llvm::Value* falseText = builder.CreateGlobalStringPtr("false", "bool.false");
                return {"%s", builder.CreateSelect(source, trueText, falseText, "bool.text")};
            }
            if (semanticType == "char") {
                return {"%c", builder.CreateZExt(source, builder.getInt32Ty(), "char.promoted")};
            }
            if (type->isIntegerTy()) {
                const bool unsignedInteger = semanticType.starts_with("uint");
                if (type->getIntegerBitWidth() < 32) {
                    source = unsignedInteger
                        ? builder.CreateZExt(source, builder.getInt32Ty(), "integer.promoted")
                        : builder.CreateSExt(source, builder.getInt32Ty(), "integer.promoted");
                }
                if (type->getIntegerBitWidth() > 32)
                    return {unsignedInteger ? "%llu" : "%lld", source};
                return {unsignedInteger ? "%u" : "%d", source};
            }
            if (type->isFloatTy()) {
                return {"%g", builder.CreateFPExt(source, builder.getDoubleTy(), "float.promoted")};
            }
            if (type->isDoubleTy()) return {"%g", source};
            if (type->isPointerTy()) {
                llvm::Value* nullText = builder.CreateGlobalStringPtr("<null>", "null.text");
                llvm::Value* safeText = builder.CreateSelect(
                    builder.CreateIsNull(source, "string.is.null"), nullText, source, "safe.string");
                return {"%s", safeText};
            }
            Fail("unsupported printable LLVM type");
        }

        llvm::CallInst* EmitPrintf(const std::string& format, const std::vector<PrintableValue>& values) {
            std::vector<llvm::Value*> arguments;
            arguments.reserve(values.size() + 1);
            arguments.push_back(builder.CreateGlobalStringPtr(format, "print.format"));
            for (const PrintableValue& printable : values) arguments.push_back(printable.value);
            return builder.CreateCall(Printf(), arguments, "print.result");
        }

        std::string BuildFormat(const std::string& source, const std::vector<PrintableValue>& values) {
            std::string result;
            size_t valueIndex = 0;
            for (size_t index = 0; index < source.size(); ++index) {
                const char current = source[index];
                if (current == '%') {
                    result += "%%";
                }
                else if (current == '{') {
                    if (index + 1 < source.size() && source[index + 1] == '{') {
                        result += '{';
                        ++index;
                    }
                    else if (index + 1 < source.size() && source[index + 1] == '}') {
                        if (valueIndex >= values.size()) Fail("format has too few values");
                        result += values[valueIndex++].specifier;
                        ++index;
                    }
                    else Fail("format contains an unmatched '{'");
                }
                else if (current == '}') {
                    if (index + 1 < source.size() && source[index + 1] == '}') {
                        result += '}';
                        ++index;
                    }
                    else Fail("format contains an unmatched '}'");
                }
                else result += current;
            }
            if (valueIndex != values.size()) Fail("format has too many values");
            return result;
        }

        llvm::Value* EmitFormat(const std::string& format, const std::vector<PrintableValue>& values) {
            llvm::Value* formatValue = builder.CreateGlobalStringPtr(format, "format.template");
            std::vector<llvm::Value*> sizeArguments = {
                llvm::ConstantPointerNull::get(builder.getPtrTy()),
                builder.getInt64(0),
                formatValue
            };
            for (const PrintableValue& printable : values) sizeArguments.push_back(printable.value);
            llvm::Value* length = builder.CreateCall(Snprintf(), sizeArguments, "format.length");
            llvm::Value* allocationSize = builder.CreateAdd(
                builder.CreateSExt(length, builder.getInt64Ty(), "format.length64"),
                builder.getInt64(1), "format.allocation.size");
            llvm::Value* buffer = builder.CreateCall(Malloc(), {allocationSize}, "format.buffer");

            std::vector<llvm::Value*> writeArguments = {buffer, allocationSize, formatValue};
            for (const PrintableValue& printable : values) writeArguments.push_back(printable.value);
            builder.CreateCall(Snprintf(), writeArguments, "format.write");
            return buffer;
        }

        void EmitBuiltin(FunctionCallExpr& expression, const std::string& name) {
            if (name == "print" || name == "println") {
                std::vector<PrintableValue> values;
                std::string format;
                for (const auto& argument : expression.arguments) {
                    PrintableValue printable = PreparePrintable(Evaluate(argument.get()), argument.get());
                    format += printable.specifier;
                    values.push_back(std::move(printable));
                }
                if (name == "println") format += '\n';
                EmitPrintf(format, values);
                value = nullptr;
                return;
            }

            if (name == "toString") {
                if (expression.arguments.size() != 1) Fail("toString expects exactly one argument");
                PrintableValue printable = PreparePrintable(
                    Evaluate(expression.arguments.front().get()), expression.arguments.front().get());
                value = EmitFormat(printable.specifier, {printable});
                return;
            }

            if (name == "format") {
                if (expression.arguments.empty()) Fail("format expects a string literal template");
                StringLiteralProbe probe;
                expression.arguments.front()->Accept(probe);
                if (!probe.literal) Fail("format template must be a string literal");
                std::vector<PrintableValue> values;
                for (size_t index = 1; index < expression.arguments.size(); ++index)
                    values.push_back(PreparePrintable(
                        Evaluate(expression.arguments[index].get()), expression.arguments[index].get()));
                value = EmitFormat(BuildFormat(probe.literal->value, values), values);
                return;
            }

            if (name == "assert") {
                if (expression.arguments.empty() || expression.arguments.size() > 2)
                    Fail("assert expects a condition and an optional message");
                llvm::Function* function = CurrentFunction();
                if (!function) Fail("assert outside a function");
                llvm::Value* condition = AsCondition(Evaluate(expression.arguments.front().get()));
                llvm::BasicBlock* success = llvm::BasicBlock::Create(context, "assert.success", function);
                llvm::BasicBlock* failure = llvm::BasicBlock::Create(context, "assert.failure", function);
                builder.CreateCondBr(condition, success, failure);
                builder.SetInsertPoint(failure);
                std::vector<PrintableValue> values;
                std::string format = "Assertion failed";
                if (expression.arguments.size() == 2) {
                    format += ": %s";
                    values.push_back(PreparePrintable(
                        Evaluate(expression.arguments[1].get()), expression.arguments[1].get()));
                }
                format += '\n';
                EmitPrintf(format, values);
                builder.CreateCall(Abort());
                builder.CreateUnreachable();
                builder.SetInsertPoint(success);
                value = nullptr;
                return;
            }

            Fail("unknown builtin function '" + name + "'");
        }

        llvm::Function* DeclareFunction(FunctionDeclStmt& statement) {
            if (!statement.name || !statement.returnType) Fail("invalid function declaration");
            const std::string functionName = Qualify(statement.name->value);
            if (llvm::Function* existing = module->getFunction(functionName)) return existing;

            std::vector<llvm::Type*> parameterTypes;
            parameterTypes.reserve(statement.parameters.size());
            for (const auto& parameter : statement.parameters) {
                parameterTypes.push_back(ResolveType(parameter->type.get()));
            }

            llvm::FunctionType* functionType = llvm::FunctionType::get(
                TypeFromName(statement.returnType->value), parameterTypes, false);
            llvm::Function* function = llvm::Function::Create(
                functionType, llvm::Function::ExternalLinkage, functionName, *module);

            size_t index = 0;
            for (llvm::Argument& argument : function->args()) {
                const std::string name = IdentifierName(statement.parameters[index++]->name.get());
                argument.setName(name);
            }
            return function;
        }

        void EmitFunction(FunctionDeclStmt& statement) {
            llvm::Function* function = DeclareFunction(statement);
            if (!function->empty()) Fail("duplicate function body for '" + function->getName().str() + "'");

            llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", function);
            builder.SetInsertPoint(entry);
            PushScope();

            size_t index = 0;
            for (llvm::Argument& argument : function->args()) {
                const auto& parameter = statement.parameters[index++];
                const std::string name = IdentifierName(parameter->name.get());
                llvm::AllocaInst* address = CreateEntryAlloca(*function, argument.getType(), name);
                builder.CreateStore(&argument, address);
                if (!scopes.back().emplace(name, Variable{address, argument.getType()}).second) {
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
            builder.ClearInsertionPoint();
        }

        std::string Generate(Program& program, const std::string& moduleName) {
            module = std::make_unique<llvm::Module>(moduleName, context);
            scopes.clear();
            loops.clear();
            currentNamespace.clear();
            value = nullptr;

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

            std::string output;
            llvm::raw_string_ostream stream(output);
            module->print(stream, nullptr);
            stream.flush();
            return output;
        }
    };

    CodeGenerator::CodeGenerator(const Analyzer* analyzer)
        : impl(std::make_unique<Impl>(*this, analyzer)) {
    }

    CodeGenerator::~CodeGenerator() = default;

    std::string CodeGenerator::Generate(Program& program, const std::string& moduleName) {
        return impl->Generate(program, moduleName);
    }

    void CodeGenerator::Visit(PrimitiveTypeExpr* expr) {
        (void)expr;
        impl->Fail("a type cannot be emitted as a runtime expression");
    }

    void CodeGenerator::Visit(UserTypeExpr* expr) {
        (void)expr;
        impl->Fail("user-defined values are not implemented yet");
    }

    void CodeGenerator::Visit(IdentifierExpr* expr) {
        Impl::Variable& variable = impl->RequireVariable(expr->name);
        impl->value = impl->builder.CreateLoad(variable.type, variable.address, expr->name + ".value");
    }

    void CodeGenerator::Visit(FunctionCallExpr* expr) {
        const std::string name = impl->ResolvedName(expr);
        if (impl->IsBuiltinFunction(name)) {
            impl->EmitBuiltin(*expr, name);
            return;
        }
        llvm::Function* function = impl->module->getFunction(name);
        if (!function) impl->Fail("unknown function '" + name + "'");
        if (function->arg_size() != expr->arguments.size()) {
            impl->Fail("invalid argument count for '" + name + "'");
        }

        std::vector<llvm::Value*> arguments;
        arguments.reserve(expr->arguments.size());
        size_t index = 0;
        for (const auto& expression : expr->arguments) {
            llvm::Value* argument = impl->Evaluate(expression.get());
            arguments.push_back(impl->Coerce(argument, function->getFunctionType()->getParamType(index++)));
        }

        llvm::CallInst* call = impl->builder.CreateCall(function, arguments,
            function->getReturnType()->isVoidTy() ? "" : name + ".result");
        impl->value = function->getReturnType()->isVoidTy() ? nullptr : call;
    }

    void CodeGenerator::Visit(ArrayAccessExpr* expr) {
        (void)expr;
        impl->Fail("array access is not implemented yet");
    }

    void CodeGenerator::Visit(BinaryExpr* expr) {
        llvm::Value* left = impl->Evaluate(expr->left.get());
        llvm::Value* right = impl->Evaluate(expr->right.get());
        impl->value = impl->ApplyBinary(expr->op, left, right);
    }

    void CodeGenerator::Visit(TernaryExpr* expr) {
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("ternary expression outside a function");

        llvm::Value* condition = impl->AsCondition(impl->Evaluate(expr->condition.get()));
        llvm::BasicBlock* trueBlock = llvm::BasicBlock::Create(impl->context, "ternary.true", function);
        llvm::BasicBlock* falseBlock = llvm::BasicBlock::Create(impl->context, "ternary.false", function);
        llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(impl->context, "ternary.end", function);
        impl->builder.CreateCondBr(condition, trueBlock, falseBlock);

        impl->builder.SetInsertPoint(trueBlock);
        llvm::Value* trueValue = impl->Evaluate(expr->trueExpr.get());
        trueBlock = impl->builder.GetInsertBlock();
        impl->BranchIfNeeded(mergeBlock);

        impl->builder.SetInsertPoint(falseBlock);
        llvm::Value* falseValue = impl->Evaluate(expr->falseExpr.get());
        falseBlock = impl->builder.GetInsertBlock();
        impl->BranchIfNeeded(mergeBlock);

        llvm::Type* resultType = impl->CommonNumericType(trueValue->getType(), falseValue->getType());
        impl->builder.SetInsertPoint(trueBlock->getTerminator());
        trueValue = impl->Coerce(trueValue, resultType);
        impl->builder.SetInsertPoint(falseBlock->getTerminator());
        falseValue = impl->Coerce(falseValue, resultType);
        impl->builder.SetInsertPoint(mergeBlock);
        llvm::PHINode* result = impl->builder.CreatePHI(resultType, 2, "ternary.result");
        result->addIncoming(trueValue, trueBlock);
        result->addIncoming(falseValue, falseBlock);
        impl->value = result;
    }

    void CodeGenerator::Visit(NullExpr* expr) {
        (void)expr;
        impl->value = llvm::ConstantPointerNull::get(impl->builder.getPtrTy());
    }

    void CodeGenerator::Visit(BooleanLiteralExpr* expr) {
        impl->value = impl->builder.getInt1(expr->value);
    }

    void CodeGenerator::Visit(NumberLiteralExpr* expr) {
        if (expr->value.find('.') != std::string::npos) {
            impl->value = llvm::ConstantFP::get(impl->builder.getDoubleTy(), std::stod(expr->value));
        }
        else {
            impl->value = llvm::ConstantInt::get(impl->builder.getInt32Ty(), std::stoll(expr->value), true);
        }
    }

    void CodeGenerator::Visit(StringLiteralExpr* expr) {
        impl->value = impl->builder.CreateGlobalStringPtr(expr->value, "string.literal");
    }

    void CodeGenerator::Visit(CharLiteralExpr* expr) {
        impl->value = impl->builder.getInt8(static_cast<unsigned char>(expr->value));
    }

    void CodeGenerator::Visit(ArrayExpr* expr) {
        (void)expr;
        impl->Fail("array literals are not implemented yet");
    }

    void CodeGenerator::Visit(AssignmentExpr* expr) {
        Impl::Variable& variable = impl->AddressOf(expr->target.get());
        llvm::Value* assigned = impl->Evaluate(expr->value.get());

        if (expr->op != "=") {
            llvm::Value* current = impl->builder.CreateLoad(variable.type, variable.address, "assignment.current");
            assigned = impl->ApplyBinary(expr->op.substr(0, expr->op.size() - 1), current, assigned);
        }

        assigned = impl->Coerce(assigned, variable.type);
        impl->builder.CreateStore(assigned, variable.address);
        impl->value = assigned;
    }

    void CodeGenerator::Visit(VarDeclExpr* expr) {
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("global variables are not implemented yet");
        if (impl->scopes.empty()) impl->Fail("variable declaration outside a scope");

        const std::string name = IdentifierName(expr->name.get());
        if (name.empty()) impl->Fail("variable declaration requires an identifier");
        llvm::Type* type = impl->ResolveType(expr->type.get());
        if (type->isVoidTy()) impl->Fail("variable '" + name + "' cannot have type void");

        llvm::AllocaInst* address = impl->CreateEntryAlloca(*function, type, name);
        if (!impl->scopes.back().emplace(name, Impl::Variable{address, type}).second) {
            impl->Fail("duplicate variable '" + name + "'");
        }

        llvm::Value* initial = expr->value ? impl->Evaluate(expr->value.get()) : llvm::Constant::getNullValue(type);
        initial = impl->Coerce(initial, type);
        impl->builder.CreateStore(initial, address);
        impl->value = initial;
    }

    void CodeGenerator::Visit(MemberAccessExpr* expr) {
        (void)expr;
        impl->Fail("member access is not implemented yet");
    }

    void CodeGenerator::Visit(CastExpr* expr) {
        llvm::Type* target = impl->ResolveType(expr->typeName.get());
        impl->value = impl->Coerce(impl->Evaluate(expr->base.get()), target);
    }

    void CodeGenerator::Visit(ConstructorCallExpr* expr) {
        (void)expr;
        impl->Fail("constructors are not implemented yet");
    }

    void CodeGenerator::Visit(DestructorCallExpr* expr) {
        (void)expr;
        impl->Fail("destructors are not implemented yet");
    }

    void CodeGenerator::Visit(InstanceDeclExpr* expr) {
        (void)expr;
        impl->Fail("user-defined instances are not implemented yet");
    }

    void CodeGenerator::Visit(PrefixUnaryExpr* expr) {
        if (expr->op == "++" || expr->op == "--") {
            Impl::Variable& variable = impl->AddressOf(expr->operand.get());
            llvm::Value* current = impl->builder.CreateLoad(variable.type, variable.address, "prefix.current");
            llvm::Value* updated = impl->ApplyBinary(expr->op == "++" ? "+" : "-", current, impl->One(variable.type));
            impl->builder.CreateStore(updated, variable.address);
            impl->value = updated;
            return;
        }

        llvm::Value* operand = impl->Evaluate(expr->operand.get());
        if (expr->op == "+") impl->value = operand;
        else if (expr->op == "-") {
            impl->value = operand->getType()->isFloatingPointTy()
                ? impl->builder.CreateFNeg(operand, "negate")
                : impl->builder.CreateNeg(operand, "negate");
        }
        else if (expr->op == "!") impl->value = impl->builder.CreateNot(impl->AsCondition(operand), "logical.not");
        else if (expr->op == "~") impl->value = impl->builder.CreateNot(operand, "bit.not");
        else impl->Fail("unsupported prefix operator '" + expr->op + "'");
    }

    void CodeGenerator::Visit(PostfixUnaryExpr* expr) {
        if (expr->op != "++" && expr->op != "--") {
            impl->Fail("unsupported postfix operator '" + expr->op + "'");
        }
        Impl::Variable& variable = impl->AddressOf(expr->operand.get());
        llvm::Value* current = impl->builder.CreateLoad(variable.type, variable.address, "postfix.current");
        llvm::Value* updated = impl->ApplyBinary(expr->op == "++" ? "+" : "-", current, impl->One(variable.type));
        impl->builder.CreateStore(updated, variable.address);
        impl->value = current;
    }

    void CodeGenerator::Visit(TemplateExpr* expr) {
        (void)expr;
        impl->Fail("generic specialization is not implemented yet");
    }

    void CodeGenerator::Visit(SingleStatement* stmt) {
        if (impl->phase == Impl::Phase::EmitBodies && stmt->expr) stmt->expr->Accept(*this);
    }

    void CodeGenerator::Visit(CompoundStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        impl->PushScope();
        for (const auto& statement : stmt->statements) {
            if (!statement || impl->builder.GetInsertBlock()->getTerminator()) break;
            statement->Accept(*this);
        }
        impl->PopScope();
    }

    void CodeGenerator::Visit(FunctionCallStmt* stmt) {
        if (impl->phase == Impl::Phase::EmitBodies && stmt->value) stmt->value->Accept(*this);
    }

    void CodeGenerator::Visit(FunctionDeclStmt* stmt) {
        if (impl->phase == Impl::Phase::DeclareFunctions) impl->DeclareFunction(*stmt);
        else impl->EmitFunction(*stmt);
    }

    void CodeGenerator::Visit(ReturnStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("return outside a function");
        if (function->getReturnType()->isVoidTy()) {
            impl->builder.CreateRetVoid();
            return;
        }
        impl->builder.CreateRet(impl->Coerce(impl->Evaluate(stmt->expr.get()), function->getReturnType()));
    }

    void CodeGenerator::Visit(AssignmentStmt* stmt) {
        if (impl->phase == Impl::Phase::EmitBodies && stmt->expr) stmt->expr->Accept(*this);
    }

    void CodeGenerator::Visit(VarDeclStmt* stmt) {
        if (impl->phase == Impl::Phase::EmitBodies && stmt->expr) stmt->expr->Accept(*this);
    }

    void CodeGenerator::Visit(StructDeclStmt* stmt) {
        (void)stmt;
        if (impl->phase == Impl::Phase::EmitBodies) impl->Fail("struct codegen is not implemented yet");
    }

    void CodeGenerator::Visit(ClassDeclStmt* stmt) {
        (void)stmt;
        if (impl->phase == Impl::Phase::EmitBodies) impl->Fail("class codegen is not implemented yet");
    }

    void CodeGenerator::Visit(ConstructorDeclStmt* stmt) {
        (void)stmt;
        if (impl->phase == Impl::Phase::EmitBodies) impl->Fail("constructor codegen is not implemented yet");
    }

    void CodeGenerator::Visit(EnumDeclStmt* stmt) {
        (void)stmt;
    }

    void CodeGenerator::Visit(GroupDeclStmt* stmt) {
        (void)stmt;
    }

    void CodeGenerator::Visit(IfStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("if outside a function");

        llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(impl->context, "if.end", function);
        for (size_t index = 0; index < stmt->branches.size(); ++index) {
            auto& branch = stmt->branches[index];
            llvm::Value* condition = impl->AsCondition(impl->Evaluate(branch.condition.get()));
            llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(impl->context, "if.body", function);
            const bool hasNext = index + 1 < stmt->branches.size() || stmt->elseBranch;
            llvm::BasicBlock* nextBlock = hasNext
                ? llvm::BasicBlock::Create(impl->context, "if.next", function)
                : mergeBlock;
            impl->builder.CreateCondBr(condition, bodyBlock, nextBlock);

            impl->builder.SetInsertPoint(bodyBlock);
            if (branch.body) branch.body->Accept(*this);
            impl->BranchIfNeeded(mergeBlock);
            impl->builder.SetInsertPoint(nextBlock);
        }

        if (stmt->elseBranch) {
            stmt->elseBranch->Accept(*this);
            impl->BranchIfNeeded(mergeBlock);
        }
        impl->builder.SetInsertPoint(mergeBlock);
    }

    void CodeGenerator::Visit(ForStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("for outside a function");

        impl->PushScope();
        for (const auto& expression : stmt->init) if (expression) expression->Accept(*this);

        llvm::BasicBlock* conditionBlock = llvm::BasicBlock::Create(impl->context, "for.condition", function);
        llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(impl->context, "for.body", function);
        llvm::BasicBlock* updateBlock = llvm::BasicBlock::Create(impl->context, "for.update", function);
        llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(impl->context, "for.end", function);
        impl->builder.CreateBr(conditionBlock);

        impl->builder.SetInsertPoint(conditionBlock);
        llvm::Value* condition = stmt->condition ? impl->AsCondition(impl->Evaluate(stmt->condition.get())) : impl->builder.getTrue();
        impl->builder.CreateCondBr(condition, bodyBlock, endBlock);

        impl->loops.emplace_back(updateBlock, endBlock);
        impl->builder.SetInsertPoint(bodyBlock);
        if (stmt->body) stmt->body->Accept(*this);
        impl->BranchIfNeeded(updateBlock);

        impl->builder.SetInsertPoint(updateBlock);
        for (const auto& expression : stmt->update) if (expression) expression->Accept(*this);
        impl->BranchIfNeeded(conditionBlock);
        impl->loops.pop_back();

        impl->builder.SetInsertPoint(endBlock);
        impl->PopScope();
    }

    void CodeGenerator::Visit(WhileStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("while outside a function");

        llvm::BasicBlock* conditionBlock = llvm::BasicBlock::Create(impl->context, "while.condition", function);
        llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(impl->context, "while.body", function);
        llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(impl->context, "while.end", function);
        impl->builder.CreateBr(conditionBlock);

        impl->builder.SetInsertPoint(conditionBlock);
        impl->builder.CreateCondBr(impl->AsCondition(impl->Evaluate(stmt->condition.get())), bodyBlock, endBlock);

        impl->loops.emplace_back(conditionBlock, endBlock);
        impl->builder.SetInsertPoint(bodyBlock);
        if (stmt->body) stmt->body->Accept(*this);
        impl->BranchIfNeeded(conditionBlock);
        impl->loops.pop_back();

        impl->builder.SetInsertPoint(endBlock);
    }

    void CodeGenerator::Visit(DoWhileStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("do-while outside a function");

        llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(impl->context, "do.body", function);
        llvm::BasicBlock* conditionBlock = llvm::BasicBlock::Create(impl->context, "do.condition", function);
        llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(impl->context, "do.end", function);
        impl->builder.CreateBr(bodyBlock);

        impl->loops.emplace_back(conditionBlock, endBlock);
        impl->builder.SetInsertPoint(bodyBlock);
        if (stmt->body) stmt->body->Accept(*this);
        impl->BranchIfNeeded(conditionBlock);

        impl->builder.SetInsertPoint(conditionBlock);
        impl->builder.CreateCondBr(impl->AsCondition(impl->Evaluate(stmt->condition.get())), bodyBlock, endBlock);
        impl->loops.pop_back();
        impl->builder.SetInsertPoint(endBlock);
    }

    void CodeGenerator::Visit(ForEachStmt* stmt) {
        (void)stmt;
        if (impl->phase == Impl::Phase::EmitBodies) impl->Fail("foreach codegen is not implemented yet");
    }

    void CodeGenerator::Visit(ContinueStmt* stmt) {
        (void)stmt;
        if (impl->phase != Impl::Phase::EmitBodies) return;
        if (impl->loops.empty()) impl->Fail("continue outside a loop");
        impl->builder.CreateBr(impl->loops.back().first);
    }

    void CodeGenerator::Visit(BreakStmt* stmt) {
        (void)stmt;
        if (impl->phase != Impl::Phase::EmitBodies) return;
        if (impl->loops.empty()) impl->Fail("break outside a loop");
        impl->builder.CreateBr(impl->loops.back().second);
    }

    void CodeGenerator::Visit(ImportStmt* stmt) {
        (void)stmt;
    }

    void CodeGenerator::Visit(NamespaceDeclStmt* stmt) {
        const std::string oldNamespace = impl->currentNamespace;
        impl->currentNamespace = impl->Qualify(stmt->name);
        if (stmt->body) {
            for (const auto& statement : stmt->body->statements) {
                if (statement) statement->Accept(*this);
            }
        }
        impl->currentNamespace = oldNamespace;
    }
}
