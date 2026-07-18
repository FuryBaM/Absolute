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
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#if LLVM_VERSION_MAJOR >= 18
#include <llvm/TargetParser/Host.h>
#else
#include <llvm/Support/Host.h>
#endif

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <optional>
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

            void Visit(PointerTypeExpr* expr) override {
                name.clear();
                if (expr->pointee) expr->pointee->Accept(*this);
                if (!name.empty()) name = (expr->raw ? "raw " : "") + name + "*";
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

        bool IsRawPointerTypeName(const std::string& name) {
            return name.starts_with("raw ") && name.ends_with("*");
        }

        bool IsManagedPointerTypeName(const std::string& name) {
            return !IsRawPointerTypeName(name) && name.ends_with("*");
        }

        bool IsPointerTypeName(const std::string& name) {
            return IsRawPointerTypeName(name) || IsManagedPointerTypeName(name);
        }

        std::string PointerPointeeName(std::string name) {
            if (IsRawPointerTypeName(name)) name.erase(0, 4);
            if (!name.empty() && name.back() == '*') name.pop_back();
            return name;
        }

        bool IsTaskTypeName(const std::string& name) {
            return name.size() > 6 && name.starts_with("task<") && name.ends_with(">");
        }

        std::string TaskValueTypeName(const std::string& name) {
            return IsTaskTypeName(name) ? name.substr(5, name.size() - 6) : std::string{};
        }

        std::optional<std::vector<size_t>> InferArrayShape(const ArrayExpr& array) {
            std::vector<size_t> childShape;
            bool hasChildShape = false;
            for (const auto& value : array.values) {
                std::vector<size_t> currentShape;
                if (const auto* nested = dynamic_cast<const ArrayExpr*>(value.get())) {
                    const auto nestedShape = InferArrayShape(*nested);
                    if (!nestedShape) return std::nullopt;
                    currentShape = *nestedShape;
                }
                if (!hasChildShape) {
                    childShape = std::move(currentShape);
                    hasChildShape = true;
                }
                else if (childShape != currentShape) return std::nullopt;
            }
            childShape.insert(childShape.begin(), array.values.size());
            return childShape;
        }

        void FlattenArrayValues(ArrayExpr& array, std::vector<Expression*>& output) {
            for (const auto& value : array.values) {
                if (auto* nested = dynamic_cast<ArrayExpr*>(value.get())) FlattenArrayValues(*nested, output);
                else output.push_back(value.get());
            }
        }

        class FunctionCallProbe final : public BaseIdentifierVisitor {
        public:
            FunctionCallExpr* call = nullptr;

            void Visit(FunctionCallExpr* expr) override { call = expr; }
        };
    }

    struct CodeGenerator::Impl {
        enum class Phase {
            DeclareFunctions,
            EmitBodies
        };

        struct Variable {
            llvm::AllocaInst* address = nullptr;
            llvm::Type* type = nullptr;
            std::string typeName;
            llvm::AllocaInst* managedOwner = nullptr;
            bool keep = false;
            bool isArray = false;
            llvm::Type* arrayElementType = nullptr;
            std::vector<llvm::Value*> arrayDimensions;
        };

        struct LoopTarget {
            llvm::BasicBlock* continueBlock = nullptr;
            llvm::BasicBlock* breakBlock = nullptr;
            size_t scopeCount = 0;
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
        std::vector<LoopTarget> loops;
        std::string currentNamespace;
        std::unordered_map<std::string, std::string> functionLinkNames;
        bool valueCreatesManagedOwner = false;
        std::string currentValueType;
        bool addressMode = false;
        llvm::Value* addressValue = nullptr;
        bool declarationKeep = false;
        std::string currentReturnTypeName;
        std::uint64_t taskThunkCounter = 0;

        explicit Impl(CodeGenerator& visitor, const Analyzer* analyzer)
            : visitor(visitor), analyzer(analyzer), builder(context) {
        }

        [[noreturn]] void Fail(const std::string& message) const {
            throw std::runtime_error("LLVM codegen: " + message);
        }

        llvm::Type* TypeFromName(const std::string& name) {
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
            Fail("unsupported type '" + name + "'");
        }

        llvm::Type* ResolveType(Expression* expression) {
            return TypeFromName(ResolveTypeName(expression));
        }

        std::string ResolveTypeName(Expression* expression) {
            if (!expression) Fail("missing type expression");
            if (analyzer) {
                if (const ExpressionInfo* info = analyzer->GetExpressionInfo(*expression);
                    info && !info->type.empty() && info->type != "error") return info->type;
            }
            PrimitiveTypeNameVisitor typeVisitor;
            expression->Accept(typeVisitor);
            if (typeVisitor.name.empty()) Fail("user-defined types are not implemented yet");
            return typeVisitor.name;
        }

        llvm::Value* Evaluate(Expression* expression) {
            if (!expression) Fail("missing expression");
            value = nullptr;
            valueCreatesManagedOwner = false;
            expression->Accept(visitor);
            if (!value) Fail("expression does not produce a value");
            currentValueType = SemanticType(expression);
            return value;
        }

        llvm::Value* EvaluateAddress(Expression* expression) {
            if (!expression) Fail("missing assignable expression");
            const bool oldMode = addressMode;
            llvm::Value* oldAddress = addressValue;
            addressMode = true;
            addressValue = nullptr;
            expression->Accept(visitor);
            llvm::Value* result = addressValue;
            addressMode = oldMode;
            addressValue = oldAddress;
            if (!result) Fail("expression is not assignable");
            return result;
        }

        void PushScope() {
            scopes.emplace_back();
        }

        llvm::FunctionCallee ManagedDestroyIf() {
            llvm::FunctionType* type = llvm::FunctionType::get(
                builder.getVoidTy(), {builder.getInt64Ty(), builder.getInt1Ty()}, false);
            return module->getOrInsertFunction("absolute_managed_destroy_if", type);
        }

        llvm::FunctionCallee TaskSpawn() {
            llvm::FunctionType* entryType = llvm::FunctionType::get(
                builder.getVoidTy(), {builder.getPtrTy()}, false);
            llvm::FunctionType* type = llvm::FunctionType::get(
                builder.getPtrTy(), {entryType->getPointerTo(), builder.getPtrTy()}, false);
            return module->getOrInsertFunction("absolute_task_spawn", type);
        }

        llvm::FunctionCallee TaskAwait() {
            llvm::FunctionType* type = llvm::FunctionType::get(
                builder.getPtrTy(), {builder.getPtrTy()}, false);
            return module->getOrInsertFunction("absolute_task_await", type);
        }

        llvm::FunctionCallee TaskDestroy() {
            llvm::FunctionType* type = llvm::FunctionType::get(
                builder.getVoidTy(), {builder.getPtrTy()}, false);
            return module->getOrInsertFunction("absolute_task_destroy", type);
        }

        void EmitScopeCleanup(size_t index) {
            if (index >= scopes.size() || !builder.GetInsertBlock() || builder.GetInsertBlock()->getTerminator()) return;
            for (auto& [name, variable] : scopes[index]) {
                (void)name;
                if (IsTaskTypeName(variable.typeName)) {
                    llvm::Value* handle = builder.CreateLoad(variable.type, variable.address, "cleanup.task");
                    builder.CreateCall(TaskDestroy(), {handle});
                    builder.CreateStore(llvm::ConstantPointerNull::get(builder.getPtrTy()), variable.address);
                    continue;
                }
                if (!variable.managedOwner || variable.keep) continue;
                llvm::Value* handle = builder.CreateLoad(variable.type, variable.address, "cleanup.handle");
                llvm::Value* owner = builder.CreateLoad(builder.getInt1Ty(), variable.managedOwner, "cleanup.owner");
                builder.CreateCall(ManagedDestroyIf(), {handle, owner});
                builder.CreateStore(builder.getInt64(0), variable.address);
                builder.CreateStore(builder.getFalse(), variable.managedOwner);
            }
        }

        void EmitCleanupsFrom(size_t firstScope) {
            for (size_t index = scopes.size(); index > firstScope; --index)
                EmitScopeCleanup(index - 1);
        }

        void PopScope(bool cleanup = false) {
            if (scopes.empty()) return;
            if (cleanup) EmitScopeCleanup(scopes.size() - 1);
            scopes.pop_back();
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

        void EmitOrExit(llvm::Value* condition, const std::string& name) {
            llvm::Function* function = CurrentFunction();
            if (!function) Fail("runtime check outside a function");
            llvm::BasicBlock* success = llvm::BasicBlock::Create(context, name + ".success", function);
            llvm::BasicBlock* failure = llvm::BasicBlock::Create(context, name + ".failure", function);
            builder.CreateCondBr(condition, success, failure);
            builder.SetInsertPoint(failure);
            const std::string message = name == "array.size"
                ? "Array size must be greater than zero"
                : "Array index out of bounds";
            builder.CreateCall(Puts(), {builder.CreateGlobalStringPtr(message, name + ".message")});
            builder.CreateCall(ExitFailure(), {builder.getInt32(1)});
            builder.CreateUnreachable();
            builder.SetInsertPoint(success);
        }

        llvm::Value* ArrayElementAddress(ArrayAccessExpr& expression) {
            Variable& variable = AddressOf(expression.base.get());
            if (!variable.isArray) Fail("object is not an array");
            if (expression.indexes.size() != variable.arrayDimensions.size())
                Fail("array access must provide all dimensions");

            llvm::Value* offset = builder.getInt64(0);
            for (size_t dimension = 0; dimension < expression.indexes.size(); ++dimension) {
                if (!expression.indexes[dimension]) Fail("array access requires an index");
                const bool outerAddressMode = addressMode;
                addressMode = false;
                llvm::Value* index = Evaluate(expression.indexes[dimension].get());
                addressMode = outerAddressMode;
                index = Coerce(index, builder.getInt64Ty());
                llvm::Value* nonNegative = builder.CreateICmpSGE(
                    index, builder.getInt64(0), "array.index.nonnegative");
                llvm::Value* belowSize = builder.CreateICmpSLT(
                    index, variable.arrayDimensions[dimension], "array.index.below.size");
                EmitOrExit(builder.CreateAnd(nonNegative, belowSize, "array.index.valid"), "array.bounds");
                if (dimension == 0) offset = index;
                else {
                    offset = builder.CreateMul(offset, variable.arrayDimensions[dimension], "array.row.offset");
                    offset = builder.CreateAdd(offset, index, "array.linear.offset");
                }
            }
            return builder.CreateGEP(variable.arrayElementType, variable.address, offset, "array.element.address");
        }

        llvm::AllocaInst* CreateEntryAlloca(llvm::Function& function, llvm::Type* type, const std::string& name) {
            llvm::IRBuilder<> entryBuilder(&function.getEntryBlock(), function.getEntryBlock().begin());
            return entryBuilder.CreateAlloca(type, nullptr, name);
        }

        llvm::Value* Coerce(llvm::Value* source, llvm::Type* target) {
            if (!source) Fail("cannot convert an empty value");
            llvm::Type* sourceType = source->getType();
            if (sourceType == target) return source;

            if (target->isIntegerTy(64) && llvm::isa<llvm::ConstantPointerNull>(source))
                return builder.getInt64(0);

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
            if (IsManagedPointerTypeName(currentValueType) && type->isIntegerTy(64))
                return builder.CreateCall(ManagedValid(), {condition}, "managed.valid");
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
            std::string name;
            if (analyzer && expression) {
                if (const ExpressionInfo* info = analyzer->GetExpressionInfo(*expression)) {
                    if (const Symbol* symbol = analyzer->GetSymbol(info->symbol)) name = symbol->name;
                }
            }
            if (name.empty()) name = IdentifierName(expression);
            const auto linked = functionLinkNames.find(name);
            return linked == functionLinkNames.end() ? name : linked->second;
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

        llvm::FunctionCallee Free() {
            llvm::FunctionType* type = llvm::FunctionType::get(
                builder.getVoidTy(), {builder.getPtrTy()}, false);
            return module->getOrInsertFunction("free", type);
        }

        llvm::Value* EncodeTaskSlot(llvm::IRBuilder<>& targetBuilder, llvm::Value* source) {
            llvm::Type* type = source->getType();
            if (type->isPointerTy())
                return targetBuilder.CreatePtrToInt(source, targetBuilder.getInt64Ty(), "task.slot.ptr");
            if (type->isIntegerTy())
                return targetBuilder.CreateIntCast(source, targetBuilder.getInt64Ty(), false, "task.slot.int");
            if (type->isFloatTy()) {
                llvm::Value* bits = targetBuilder.CreateBitCast(source, targetBuilder.getInt32Ty(), "task.slot.float");
                return targetBuilder.CreateZExt(bits, targetBuilder.getInt64Ty(), "task.slot.float64");
            }
            if (type->isDoubleTy())
                return targetBuilder.CreateBitCast(source, targetBuilder.getInt64Ty(), "task.slot.double");
            Fail("async tasks only support primitive and pointer values");
        }

        llvm::Value* DecodeTaskSlot(
            llvm::IRBuilder<>& targetBuilder, llvm::Value* slot, llvm::Type* target) {
            if (target->isPointerTy())
                return targetBuilder.CreateIntToPtr(slot, target, "task.value.ptr");
            if (target->isIntegerTy())
                return targetBuilder.CreateIntCast(slot, target, false, "task.value.int");
            if (target->isFloatTy()) {
                llvm::Value* bits = targetBuilder.CreateTrunc(slot, targetBuilder.getInt32Ty(), "task.value.float32");
                return targetBuilder.CreateBitCast(bits, target, "task.value.float");
            }
            if (target->isDoubleTy())
                return targetBuilder.CreateBitCast(slot, target, "task.value.double");
            Fail("async tasks only support primitive and pointer values");
        }

        llvm::Value* EmitSpawn(FunctionCallExpr& call) {
            const std::string name = ResolvedName(&call);
            llvm::Function* target = module->getFunction(name);
            if (!target) Fail("unknown async function '" + name + "'");
            if (target->arg_size() != call.arguments.size())
                Fail("invalid async argument count for '" + name + "'");

            const std::uint64_t slotCount = static_cast<std::uint64_t>(call.arguments.size()) + 1;
            llvm::Value* contextPointer = builder.CreateCall(
                Malloc(), {builder.getInt64(slotCount * 8)}, "task.context");

            size_t argumentIndex = 0;
            for (const auto& argument : call.arguments) {
                llvm::Value* argumentValue = Evaluate(argument.get());
                argumentValue = Coerce(argumentValue,
                    target->getFunctionType()->getParamType(static_cast<unsigned>(argumentIndex)));
                llvm::Value* slot = builder.CreateGEP(
                    builder.getInt64Ty(), contextPointer,
                    builder.getInt64(static_cast<std::uint64_t>(argumentIndex + 1)), "task.argument.slot");
                builder.CreateStore(EncodeTaskSlot(builder, argumentValue), slot);
                ++argumentIndex;
            }

            llvm::FunctionType* thunkType = llvm::FunctionType::get(
                builder.getVoidTy(), {builder.getPtrTy()}, false);
            llvm::Function* thunk = llvm::Function::Create(
                thunkType, llvm::Function::InternalLinkage,
                "absolute.task.thunk." + std::to_string(taskThunkCounter++), *module);
            llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", thunk);
            llvm::IRBuilder<> thunkBuilder(entry);
            llvm::Value* thunkContext = thunk->getArg(0);

            std::vector<llvm::Value*> thunkArguments;
            thunkArguments.reserve(call.arguments.size());
            for (size_t index = 0; index < call.arguments.size(); ++index) {
                llvm::Value* slot = thunkBuilder.CreateGEP(
                    thunkBuilder.getInt64Ty(), thunkContext,
                    thunkBuilder.getInt64(static_cast<std::uint64_t>(index + 1)), "task.argument.slot");
                llvm::Value* encoded = thunkBuilder.CreateLoad(
                    thunkBuilder.getInt64Ty(), slot, "task.argument");
                thunkArguments.push_back(DecodeTaskSlot(
                    thunkBuilder, encoded, target->getFunctionType()->getParamType(static_cast<unsigned>(index))));
            }
            llvm::CallInst* result = thunkBuilder.CreateCall(target, thunkArguments,
                target->getReturnType()->isVoidTy() ? "" : "task.result");
            if (!target->getReturnType()->isVoidTy()) {
                llvm::Value* resultSlot = thunkBuilder.CreateGEP(
                    thunkBuilder.getInt64Ty(), thunkContext, thunkBuilder.getInt64(0), "task.result.slot");
                thunkBuilder.CreateStore(EncodeTaskSlot(thunkBuilder, result), resultSlot);
            }
            thunkBuilder.CreateRetVoid();

            return builder.CreateCall(TaskSpawn(), {thunk, contextPointer}, "task.handle");
        }

        llvm::Value* EmitAwait(PrefixUnaryExpr& expression) {
            llvm::Value* handle = Evaluate(expression.operand.get());
            llvm::Value* contextPointer = builder.CreateCall(TaskAwait(), {handle}, "task.completed.context");
            const std::string taskName = IdentifierName(expression.operand.get());
            if (!taskName.empty()) {
                Variable& task = RequireVariable(taskName);
                builder.CreateStore(llvm::ConstantPointerNull::get(builder.getPtrTy()), task.address);
            }

            const std::string resultTypeName = SemanticType(&expression);
            if (resultTypeName == "void") {
                builder.CreateCall(Free(), {contextPointer});
                return nullptr;
            }
            llvm::Value* encoded = builder.CreateLoad(
                builder.getInt64Ty(), contextPointer, "task.result.encoded");
            llvm::Value* result = DecodeTaskSlot(builder, encoded, TypeFromName(resultTypeName));
            builder.CreateCall(Free(), {contextPointer});
            return result;
        }

        llvm::FunctionCallee ManagedCreate() {
            llvm::FunctionType* type = llvm::FunctionType::get(
                builder.getInt64Ty(), {builder.getInt64Ty()}, false);
            return module->getOrInsertFunction("absolute_managed_create", type);
        }

        llvm::FunctionCallee ManagedGet(bool requireValid = false) {
            llvm::FunctionType* type = llvm::FunctionType::get(
                builder.getPtrTy(), {builder.getInt64Ty()}, false);
            return module->getOrInsertFunction(
                requireValid ? "absolute_managed_require" : "absolute_managed_get", type);
        }

        llvm::FunctionCallee ManagedValid() {
            llvm::FunctionType* type = llvm::FunctionType::get(
                builder.getInt1Ty(), {builder.getInt64Ty()}, false);
            return module->getOrInsertFunction("absolute_managed_valid", type);
        }

        llvm::FunctionCallee ManagedDestroy() {
            llvm::FunctionType* type = llvm::FunctionType::get(
                builder.getVoidTy(), {builder.getInt64Ty()}, false);
            return module->getOrInsertFunction("absolute_managed_destroy", type);
        }

        std::uint64_t SizeOfTypeName(const std::string& name) {
            if (name == "int8" || name == "uint8" || name == "char" || name == "bool") return 1;
            if (name == "int16" || name == "uint16") return 2;
            if (name == "int32" || name == "uint32" || name == "float") return 4;
            if (name == "int64" || name == "uint64" || name == "double" || name == "string" ||
                IsPointerTypeName(name)) return 8;
            Fail("cannot determine allocation size of '" + name + "'");
        }

        llvm::FunctionCallee Abort() {
            llvm::FunctionType* type = llvm::FunctionType::get(builder.getVoidTy(), {}, false);
            return module->getOrInsertFunction("abort", type);
        }

        llvm::FunctionCallee Puts() {
            llvm::FunctionType* type = llvm::FunctionType::get(
                builder.getInt32Ty(), {builder.getPtrTy()}, false);
            return module->getOrInsertFunction("puts", type);
        }

        llvm::FunctionCallee ExitFailure() {
            llvm::FunctionType* type = llvm::FunctionType::get(
                builder.getVoidTy(), {builder.getInt32Ty()}, false);
            return module->getOrInsertFunction("exit", type);
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
            const std::string sourceName = Qualify(statement.name->value);
            const std::string functionName = statement.IsExternal() ? statement.name->value : sourceName;
            functionLinkNames[sourceName] = functionName;

            std::vector<llvm::Type*> parameterTypes;
            parameterTypes.reserve(statement.parameters.size());
            for (const auto& parameter : statement.parameters) {
                parameterTypes.push_back(ResolveType(parameter->type.get()));
            }

            llvm::FunctionType* functionType = llvm::FunctionType::get(
                ResolveType(statement.returnType.get()), parameterTypes, false);
            if (llvm::Function* existing = module->getFunction(functionName)) {
                if (existing->getFunctionType() != functionType)
                    Fail("conflicting declarations for external symbol '" + functionName + "'");
                return existing;
            }
            llvm::Function* function = llvm::Function::Create(
                functionType, llvm::Function::ExternalLinkage, functionName, *module);
            function->setCallingConv(llvm::CallingConv::C);

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
            const std::string oldReturnTypeName = currentReturnTypeName;
            currentReturnTypeName = ResolveTypeName(statement.returnType.get());

            size_t index = 0;
            for (llvm::Argument& argument : function->args()) {
                const auto& parameter = statement.parameters[index++];
                const std::string name = IdentifierName(parameter->name.get());
                const std::string typeName = ResolveTypeName(parameter->type.get());
                llvm::AllocaInst* address = CreateEntryAlloca(*function, argument.getType(), name);
                builder.CreateStore(&argument, address);
                if (!scopes.back().emplace(name,
                    Variable{address, argument.getType(), typeName, nullptr, false, false, nullptr, {}}).second) {
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

        llvm::Module& BuildModule(Program& program, const std::string& moduleName) {
            module = std::make_unique<llvm::Module>(moduleName, context);
            scopes.clear();
            loops.clear();
            currentNamespace.clear();
            functionLinkNames.clear();
            value = nullptr;
            currentValueType.clear();
            valueCreatesManagedOwner = false;
            addressMode = false;
            addressValue = nullptr;
            declarationKeep = false;
            taskThunkCounter = 0;

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

        std::string Generate(Program& program, const std::string& moduleName) {
            BuildModule(program, moduleName);

            std::string output;
            llvm::raw_string_ostream stream(output);
            module->print(stream, nullptr);
            stream.flush();
            return output;
        }

        void GenerateObject(Program& program, const std::string& moduleName, const std::string& outputPath) {
            static std::once_flag initializeTarget;
            std::call_once(initializeTarget, [] {
                if (llvm::InitializeNativeTarget())
                    throw std::runtime_error("LLVM codegen: failed to initialize the native target");
                if (llvm::InitializeNativeTargetAsmPrinter())
                    throw std::runtime_error("LLVM codegen: failed to initialize the native assembly printer");
            });

            llvm::Module& generatedModule = BuildModule(program, moduleName);
            const std::string triple = llvm::sys::getDefaultTargetTriple();
            std::string targetError;
            const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, targetError);
            if (!target) Fail("cannot select target '" + triple + "': " + targetError);

            llvm::TargetOptions options;
            std::unique_ptr<llvm::TargetMachine> targetMachine(
                target->createTargetMachine(triple, "generic", "", options, llvm::Reloc::PIC_));
            if (!targetMachine) Fail("cannot create target machine for '" + triple + "'");
            generatedModule.setTargetTriple(triple);
            generatedModule.setDataLayout(targetMachine->createDataLayout());

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
    };

    CodeGenerator::CodeGenerator(const Analyzer* analyzer)
        : impl(std::make_unique<Impl>(*this, analyzer)) {
    }

    CodeGenerator::~CodeGenerator() = default;

    std::string CodeGenerator::Generate(Program& program, const std::string& moduleName) {
        return impl->Generate(program, moduleName);
    }

    void CodeGenerator::GenerateObject(
        Program& program, const std::string& moduleName, const std::string& outputPath) {
        impl->GenerateObject(program, moduleName, outputPath);
    }

    void CodeGenerator::Visit(PrimitiveTypeExpr* expr) {
        (void)expr;
        impl->Fail("a type cannot be emitted as a runtime expression");
    }

    void CodeGenerator::Visit(UserTypeExpr* expr) {
        (void)expr;
        impl->Fail("user-defined values are not implemented yet");
    }

    void CodeGenerator::Visit(PointerTypeExpr* expr) {
        (void)expr;
        impl->Fail("a pointer type cannot be emitted as a runtime expression");
    }

    void CodeGenerator::Visit(IdentifierExpr* expr) {
        Impl::Variable& variable = impl->RequireVariable(expr->name);
        if (impl->addressMode) {
            impl->addressValue = variable.address;
            return;
        }
        if (variable.isArray) impl->Fail("array '" + expr->name + "' requires an index");
        impl->value = impl->builder.CreateLoad(variable.type, variable.address, expr->name + ".value");
        impl->valueCreatesManagedOwner = false;
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
        impl->valueCreatesManagedOwner = IsManagedPointerTypeName(impl->SemanticType(expr));
    }

    void CodeGenerator::Visit(ArrayAccessExpr* expr) {
        llvm::Value* address = impl->ArrayElementAddress(*expr);
        if (impl->addressMode) {
            impl->addressValue = address;
            return;
        }
        llvm::Type* elementType = impl->TypeFromName(impl->SemanticType(expr));
        impl->value = impl->builder.CreateLoad(elementType, address, "array.element");
        impl->valueCreatesManagedOwner = false;
    }

    void CodeGenerator::Visit(BinaryExpr* expr) {
        const std::string leftType = impl->SemanticType(expr->left.get());
        const std::string rightType = impl->SemanticType(expr->right.get());
        llvm::Value* left = impl->Evaluate(expr->left.get());
        llvm::Value* right = impl->Evaluate(expr->right.get());

        const bool leftRaw = IsRawPointerTypeName(leftType);
        const bool rightRaw = IsRawPointerTypeName(rightType);
        const bool leftManaged = IsManagedPointerTypeName(leftType);
        const bool rightManaged = IsManagedPointerTypeName(rightType);
        const bool equality = expr->op == "==" || expr->op == "!=";

        if ((leftRaw || rightRaw) && (expr->op == "+" || expr->op == "-")) {
            if (leftRaw && rightRaw && expr->op == "-") {
                llvm::Value* leftAddress = impl->builder.CreatePtrToInt(left, impl->builder.getInt64Ty());
                llvm::Value* rightAddress = impl->builder.CreatePtrToInt(right, impl->builder.getInt64Ty());
                llvm::Value* bytes = impl->builder.CreateSub(leftAddress, rightAddress, "pointer.byte.diff");
                impl->value = impl->builder.CreateSDiv(bytes,
                    impl->builder.getInt64(impl->SizeOfTypeName(PointerPointeeName(leftType))),
                    "pointer.diff");
                impl->valueCreatesManagedOwner = false;
                return;
            }
            const bool pointerOnLeft = leftRaw;
            llvm::Value* pointer = pointerOnLeft ? left : right;
            llvm::Value* index = pointerOnLeft ? right : left;
            index = impl->Coerce(index, impl->builder.getInt64Ty());
            if (expr->op == "-" && pointerOnLeft)
                index = impl->builder.CreateNeg(index, "pointer.negative.offset");
            const std::string pointerType = pointerOnLeft ? leftType : rightType;
            impl->value = impl->builder.CreateGEP(
                impl->TypeFromName(PointerPointeeName(pointerType)), pointer, index, "pointer.offset");
            impl->valueCreatesManagedOwner = false;
            return;
        }

        if (equality && ((leftManaged && rightType == "null") || (rightManaged && leftType == "null"))) {
            llvm::Value* handle = leftManaged ? left : right;
            llvm::Value* valid = impl->builder.CreateCall(impl->ManagedValid(), {handle}, "managed.valid");
            impl->value = expr->op == "==" ? impl->builder.CreateNot(valid, "managed.is.null") : valid;
            impl->valueCreatesManagedOwner = false;
            return;
        }

        if ((leftRaw || rightRaw) &&
            (equality || expr->op == "<" || expr->op == "<=" || expr->op == ">" || expr->op == ">=")) {
            if (expr->op == "==") impl->value = impl->builder.CreateICmpEQ(left, right, "pointer.equal");
            else if (expr->op == "!=") impl->value = impl->builder.CreateICmpNE(left, right, "pointer.not.equal");
            else if (expr->op == "<") impl->value = impl->builder.CreateICmpULT(left, right, "pointer.less");
            else if (expr->op == "<=") impl->value = impl->builder.CreateICmpULE(left, right, "pointer.less.equal");
            else if (expr->op == ">") impl->value = impl->builder.CreateICmpUGT(left, right, "pointer.greater");
            else impl->value = impl->builder.CreateICmpUGE(left, right, "pointer.greater.equal");
            impl->valueCreatesManagedOwner = false;
            return;
        }

        if (leftManaged && rightManaged && equality) {
            impl->value = expr->op == "=="
                ? impl->builder.CreateICmpEQ(left, right, "managed.equal")
                : impl->builder.CreateICmpNE(left, right, "managed.not.equal");
            impl->valueCreatesManagedOwner = false;
            return;
        }
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
        impl->Fail("array literal requires an array variable initializer");
    }

    void CodeGenerator::Visit(AssignmentExpr* expr) {
        llvm::Value* targetAddress = impl->EvaluateAddress(expr->target.get());
        const std::string targetTypeName = impl->SemanticType(expr->target.get());
        llvm::Type* targetType = impl->TypeFromName(targetTypeName);
        llvm::Value* assigned = impl->Evaluate(expr->value.get());
        const bool createsOwner = impl->valueCreatesManagedOwner;

        if (expr->op != "=") {
            llvm::Value* current = impl->builder.CreateLoad(targetType, targetAddress, "assignment.current");
            assigned = impl->ApplyBinary(expr->op.substr(0, expr->op.size() - 1), current, assigned);
        }

        if (IsManagedPointerTypeName(targetTypeName)) {
            const std::string name = IdentifierName(expr->target.get());
            if (!name.empty()) {
                Impl::Variable& variable = impl->RequireVariable(name);
                if (variable.managedOwner && !variable.keep) {
                    llvm::Value* oldHandle = impl->builder.CreateLoad(variable.type, variable.address, "assignment.old.handle");
                    llvm::Value* oldOwner = impl->builder.CreateLoad(
                        impl->builder.getInt1Ty(), variable.managedOwner, "assignment.old.owner");
                    impl->builder.CreateCall(impl->ManagedDestroyIf(), {oldHandle, oldOwner});
                    impl->builder.CreateStore(impl->builder.getInt1(createsOwner), variable.managedOwner);
                }
            }
        }
        assigned = impl->Coerce(assigned, targetType);
        impl->builder.CreateStore(assigned, targetAddress);
        impl->value = assigned;
        impl->valueCreatesManagedOwner = false;
    }

    void CodeGenerator::Visit(VarDeclExpr* expr) {
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("global variables are not implemented yet");
        if (impl->scopes.empty()) impl->Fail("variable declaration outside a scope");

        const std::string name = IdentifierName(expr->name.get());
        if (name.empty()) impl->Fail("variable declaration requires an identifier");
        const std::string typeName = impl->ResolveTypeName(expr->type.get());
        if (auto* arrayDeclarator = dynamic_cast<ArrayAccessExpr*>(expr->name.get())) {
            if (arrayDeclarator->indexes.empty()) impl->Fail("array declaration requires a dimension");
            llvm::Type* elementType = impl->TypeFromName(typeName);
            std::vector<llvm::Value*> dimensions;
            dimensions.reserve(arrayDeclarator->indexes.size());

            auto* literal = dynamic_cast<ArrayExpr*>(expr->value.get());
            const auto inferredShape = literal ? InferArrayShape(*literal)
                : std::optional<std::vector<size_t>>{};
            for (size_t dimension = 0; dimension < arrayDeclarator->indexes.size(); ++dimension) {
                llvm::Value* size = nullptr;
                if (arrayDeclarator->indexes[dimension]) {
                    size = impl->Coerce(
                        impl->Evaluate(arrayDeclarator->indexes[dimension].get()),
                        impl->builder.getInt64Ty());
                }
                else if (inferredShape && dimension < inferredShape->size()) {
                    size = impl->builder.getInt64((*inferredShape)[dimension]);
                }
                else impl->Fail("array dimension requires a size or an initializer");
                impl->EmitOrExit(
                    impl->builder.CreateICmpSGT(size, impl->builder.getInt64(0), "array.size.positive"),
                    "array.size");
                dimensions.push_back(size);
            }

            llvm::Value* elementCount = impl->builder.getInt64(1);
            for (llvm::Value* dimension : dimensions)
                elementCount = impl->builder.CreateMul(elementCount, dimension, "array.element.count");
            llvm::AllocaInst* address = impl->builder.CreateAlloca(elementType, elementCount, name);
            llvm::Value* byteCount = impl->builder.CreateMul(
                elementCount,
                impl->builder.getInt64(impl->SizeOfTypeName(typeName)),
                "array.byte.count");
            impl->builder.CreateMemSet(
                address, impl->builder.getInt8(0), byteCount, llvm::MaybeAlign(1));

            Impl::Variable variable;
            variable.address = address;
            variable.type = elementType;
            variable.typeName = typeName;
            for (size_t dimension = 0; dimension < arrayDeclarator->indexes.size(); ++dimension)
                variable.typeName += "[]";
            variable.keep = impl->declarationKeep;
            variable.isArray = true;
            variable.arrayElementType = elementType;
            variable.arrayDimensions = dimensions;
            if (!impl->scopes.back().emplace(name, std::move(variable)).second)
                impl->Fail("duplicate variable '" + name + "'");

            if (literal) {
                std::vector<Expression*> values;
                FlattenArrayValues(*literal, values);
                for (size_t index = 0; index < values.size(); ++index) {
                    llvm::Value* initial = impl->Coerce(impl->Evaluate(values[index]), elementType);
                    llvm::Value* destination = impl->builder.CreateGEP(
                        elementType, address, impl->builder.getInt64(index), "array.initializer.element");
                    impl->builder.CreateStore(initial, destination);
                }
            }
            else if (expr->value) impl->Fail("array variable requires an array literal initializer");

            impl->value = address;
            impl->valueCreatesManagedOwner = false;
            return;
        }
        llvm::Type* type = impl->TypeFromName(typeName);
        if (type->isVoidTy()) impl->Fail("variable '" + name + "' cannot have type void");

        llvm::AllocaInst* address = impl->CreateEntryAlloca(*function, type, name);
        llvm::AllocaInst* ownerAddress = nullptr;
        if (IsManagedPointerTypeName(typeName))
            ownerAddress = impl->CreateEntryAlloca(*function, impl->builder.getInt1Ty(), name + ".owner");
        if (!impl->scopes.back().emplace(name,
            Impl::Variable{address, type, typeName, ownerAddress, impl->declarationKeep,
                false, nullptr, {}}).second) {
            impl->Fail("duplicate variable '" + name + "'");
        }

        llvm::Value* initial = expr->value ? impl->Evaluate(expr->value.get()) : llvm::Constant::getNullValue(type);
        const bool createsOwner = impl->valueCreatesManagedOwner;
        initial = impl->Coerce(initial, type);
        impl->builder.CreateStore(initial, address);
        if (ownerAddress) impl->builder.CreateStore(impl->builder.getInt1(createsOwner), ownerAddress);
        impl->value = initial;
        impl->valueCreatesManagedOwner = false;
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
        const std::string allocationType = impl->SemanticType(expr);
        const std::string pointeeType = allocationType.empty()
            ? impl->ResolveTypeName(expr->constructName.get())
            : PointerPointeeName(allocationType);
        const bool rawAllocation = IsRawPointerTypeName(allocationType) ||
            (allocationType.empty() && expr->raw);
        llvm::Type* pointee = impl->TypeFromName(pointeeType);
        llvm::Value* pointer = nullptr;
        llvm::Value* result = nullptr;
        if (rawAllocation) {
            pointer = impl->builder.CreateCall(
                impl->Malloc(), {impl->builder.getInt64(impl->SizeOfTypeName(pointeeType))}, "raw.allocation");
            result = pointer;
        }
        else {
            llvm::Value* handle = impl->builder.CreateCall(
                impl->ManagedCreate(), {impl->builder.getInt64(impl->SizeOfTypeName(pointeeType))}, "managed.handle");
            pointer = impl->builder.CreateCall(impl->ManagedGet(true), {handle}, "managed.allocation");
            result = handle;
        }

        llvm::Value* initial = expr->arguments.empty()
            ? llvm::Constant::getNullValue(pointee)
            : impl->Evaluate(expr->arguments.front().get());
        initial = impl->Coerce(initial, pointee);
        impl->builder.CreateStore(initial, pointer);
        impl->value = result;
        impl->valueCreatesManagedOwner = !rawAllocation;
    }

    void CodeGenerator::Visit(DestructorCallExpr* expr) {
        const std::string typeName = impl->SemanticType(expr->target.get());
        llvm::Value* targetAddress = impl->EvaluateAddress(expr->target.get());
        llvm::Type* type = impl->TypeFromName(typeName);
        llvm::Value* pointer = impl->builder.CreateLoad(type, targetAddress, "delete.target");
        if (IsManagedPointerTypeName(typeName)) {
            const std::string name = IdentifierName(expr->target.get());
            llvm::Value* owner = impl->builder.getFalse();
            if (!name.empty()) {
                Impl::Variable& variable = impl->RequireVariable(name);
                if (variable.managedOwner) {
                    owner = impl->builder.CreateLoad(
                        impl->builder.getInt1Ty(), variable.managedOwner, "delete.owner");
                    impl->builder.CreateStore(impl->builder.getFalse(), variable.managedOwner);
                }
            }
            impl->builder.CreateCall(impl->ManagedDestroyIf(), {pointer, owner});
            impl->builder.CreateStore(impl->builder.getInt64(0), targetAddress);
        }
        else {
            impl->builder.CreateCall(impl->Free(), {pointer});
            impl->builder.CreateStore(llvm::ConstantPointerNull::get(impl->builder.getPtrTy()), targetAddress);
        }
        impl->value = nullptr;
        impl->valueCreatesManagedOwner = false;
    }

    void CodeGenerator::Visit(InstanceDeclExpr* expr) {
        (void)expr;
        impl->Fail("user-defined instances are not implemented yet");
    }

    void CodeGenerator::Visit(PrefixUnaryExpr* expr) {
        if (expr->op == "spawn") {
            FunctionCallProbe probe;
            if (expr->operand) expr->operand->Accept(probe);
            if (!probe.call) impl->Fail("spawn requires a direct async function call");
            impl->value = impl->EmitSpawn(*probe.call);
            impl->valueCreatesManagedOwner = false;
            return;
        }
        if (expr->op == "await") {
            impl->value = impl->EmitAwait(*expr);
            impl->valueCreatesManagedOwner = false;
            return;
        }
        if (expr->op == "&") {
            impl->value = impl->EvaluateAddress(expr->operand.get());
            impl->valueCreatesManagedOwner = false;
            return;
        }
        if (expr->op == "*") {
            const std::string pointerType = impl->SemanticType(expr->operand.get());
            const std::string pointeeName = PointerPointeeName(pointerType);
            const bool outerAddressMode = impl->addressMode;
            impl->addressMode = false;
            llvm::Value* pointer = impl->Evaluate(expr->operand.get());
            impl->addressMode = outerAddressMode;
            llvm::Value* pointeeAddress = IsManagedPointerTypeName(pointerType)
                ? impl->builder.CreateCall(impl->ManagedGet(true), {pointer}, "managed.pointee")
                : pointer;
            if (outerAddressMode) {
                impl->addressValue = pointeeAddress;
                return;
            }
            impl->value = impl->builder.CreateLoad(
                impl->TypeFromName(pointeeName), pointeeAddress, "pointer.value");
            impl->valueCreatesManagedOwner = false;
            return;
        }
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
        impl->PopScope(true);
    }

    void CodeGenerator::Visit(FunctionCallStmt* stmt) {
        if (impl->phase == Impl::Phase::EmitBodies && stmt->value) stmt->value->Accept(*this);
    }

    void CodeGenerator::Visit(FunctionDeclStmt* stmt) {
        if (impl->phase == Impl::Phase::DeclareFunctions) impl->DeclareFunction(*stmt);
        else if (!stmt->IsExternal()) impl->EmitFunction(*stmt);
    }

    void CodeGenerator::Visit(ReturnStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("return outside a function");
        if (function->getReturnType()->isVoidTy()) {
            impl->EmitCleanupsFrom(0);
            impl->builder.CreateRetVoid();
            return;
        }
        llvm::Value* result = impl->Coerce(impl->Evaluate(stmt->expr.get()), function->getReturnType());
        if (IsManagedPointerTypeName(impl->currentReturnTypeName)) {
            const std::string returnedName = IdentifierName(stmt->expr.get());
            if (!returnedName.empty()) {
                Impl::Variable& returned = impl->RequireVariable(returnedName);
                if (returned.managedOwner)
                    impl->builder.CreateStore(impl->builder.getFalse(), returned.managedOwner);
            }
        }
        impl->EmitCleanupsFrom(0);
        impl->builder.CreateRet(result);
    }

    void CodeGenerator::Visit(AssignmentStmt* stmt) {
        if (impl->phase == Impl::Phase::EmitBodies && stmt->expr) stmt->expr->Accept(*this);
    }

    void CodeGenerator::Visit(VarDeclStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies || !stmt->expr) return;
        const bool oldKeep = impl->declarationKeep;
        impl->declarationKeep = std::any_of(stmt->modifiers.begin(), stmt->modifiers.end(),
            [](const Token& modifier) { return modifier.value == "keep"; });
        stmt->expr->Accept(*this);
        impl->declarationKeep = oldKeep;
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

        impl->loops.push_back({updateBlock, endBlock, impl->scopes.size()});
        impl->builder.SetInsertPoint(bodyBlock);
        if (stmt->body) stmt->body->Accept(*this);
        impl->BranchIfNeeded(updateBlock);

        impl->builder.SetInsertPoint(updateBlock);
        for (const auto& expression : stmt->update) if (expression) expression->Accept(*this);
        impl->BranchIfNeeded(conditionBlock);
        impl->loops.pop_back();

        impl->builder.SetInsertPoint(endBlock);
        impl->PopScope(true);
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

        impl->loops.push_back({conditionBlock, endBlock, impl->scopes.size()});
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

        impl->loops.push_back({conditionBlock, endBlock, impl->scopes.size()});
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
        impl->EmitCleanupsFrom(impl->loops.back().scopeCount);
        impl->builder.CreateBr(impl->loops.back().continueBlock);
    }

    void CodeGenerator::Visit(BreakStmt* stmt) {
        (void)stmt;
        if (impl->phase != Impl::Phase::EmitBodies) return;
        if (impl->loops.empty()) impl->Fail("break outside a loop");
        impl->EmitCleanupsFrom(impl->loops.back().scopeCount);
        impl->builder.CreateBr(impl->loops.back().breakBlock);
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
