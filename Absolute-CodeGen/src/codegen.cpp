#include "codegen.h"
#include "syntax_plugins.h"
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
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
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
#include <limits>
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

            void Visit(ArrayTypeExpr* expr) override {
                name.clear();
                if (expr->element) expr->element->Accept(*this);
                if (!name.empty()) name += "[]";
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

        bool HasModifier(const Statement& statement, const std::string& name) {
            return std::any_of(statement.modifiers.begin(), statement.modifiers.end(),
                [&](const Token& modifier) { return modifier.value == name; });
        }

        size_t ArrayRankName(std::string type) {
            size_t rank = 0;
            while (type.ends_with("[]")) {
                type.resize(type.size() - 2);
                ++rank;
            }
            return rank;
        }

        std::string ArrayElementTypeName(std::string type, size_t dimensions = 1) {
            while (dimensions-- > 0 && type.ends_with("[]")) type.resize(type.size() - 2);
            return type;
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
            llvm::Value* address = nullptr;
            llvm::Type* type = nullptr;
            std::string typeName;
            bool managedOwner = false;
            bool isArray = false;
            llvm::Type* arrayElementType = nullptr;
            std::vector<llvm::Value*> arrayDimensions;
            llvm::AllocaInst* managedPointee = nullptr;
            SymbolId symbol = InvalidSymbolId;
        };

        struct ArrayView {
            llvm::Value* address = nullptr;
            llvm::Type* elementType = nullptr;
            std::string typeName;
            std::vector<llvm::Value*> dimensions;
        };

        struct ClassField {
            std::string name;
            std::string typeName;
            unsigned index = 0;
        };

        struct ClassMethod {
            FunctionDeclStmt* statement = nullptr;
            std::string owner;
            std::string linkName;
            std::optional<unsigned> virtualSlot;
        };

        struct ClassInfo {
            std::string name;
            std::string nameSpace;
            ClassDeclStmt* statement = nullptr;
            std::vector<std::string> parents;
            std::vector<VarDeclExpr*> ownPrimitiveFields;
            std::vector<InstanceDeclExpr*> ownObjectFields;
            std::vector<FunctionDeclStmt*> ownMethods;
            ConstructorDeclStmt* constructor = nullptr;
            std::vector<ClassField> fields;
            std::unordered_map<std::string, ClassField> fieldByName;
            std::unordered_map<std::string, ClassMethod> methods;
            std::unordered_map<std::string, ClassMethod> declaredMethods;
            std::vector<std::string> virtualNames;
            llvm::StructType* llvmType = nullptr;
            llvm::GlobalVariable* vtable = nullptr;
            bool finalizing = false;
            bool finalized = false;
            bool emitted = false;
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
        std::unordered_map<std::string, Variable> globals;
        std::unordered_map<std::string, llvm::StructType*> arrayDescriptorTypes;
        std::unordered_map<std::string, ClassInfo> classes;
        std::vector<std::string> classOrder;
        std::vector<LoopTarget> loops;
        std::string currentNamespace;
        std::unordered_map<std::string, std::string> functionLinkNames;
        bool valueCreatesManagedOwner = false;
        llvm::Value* valueManagedPointee = nullptr;
        std::string currentValueType;
        bool addressMode = false;
        llvm::Value* addressValue = nullptr;
        std::string currentReturnTypeName;
        std::string currentClassName;
        llvm::Value* currentThis = nullptr;
        std::uint64_t taskThunkCounter = 0;

        explicit Impl(CodeGenerator& visitor, const Analyzer* analyzer)
            : visitor(visitor), analyzer(analyzer), builder(context) {
        }

        [[noreturn]] void Fail(const std::string& message) const {
            throw std::runtime_error("LLVM codegen: " + message);
        }

        llvm::Type* TypeFromName(const std::string& name) {
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
            if (classes.contains(name)) {
                FinalizeClass(name);
                return classes.at(name).llvmType;
            }
            Fail("unsupported type '" + name + "'");
        }

        llvm::StructType* ArrayDescriptorType(const std::string& name) {
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
            if (typeVisitor.name.empty()) Fail("cannot resolve type expression");
            return typeVisitor.name;
        }

        std::string DeclaredTypeName(VarDeclExpr& expression) {
            if (analyzer) {
                if (const ExpressionInfo* info = analyzer->GetExpressionInfo(expression);
                    info && !info->type.empty() && info->type != "error") return info->type;
            }
            std::string type = ResolveTypeName(expression.type.get());
            if (const auto* declarator = dynamic_cast<const ArrayAccessExpr*>(expression.name.get()))
                for (size_t index = 0; index < declarator->indexes.size(); ++index) type += "[]";
            return type;
        }

        std::string QualifiedClassName(const std::string& name, const std::string& nameSpace) const {
            if (name.empty() || name.find('.') != std::string::npos || nameSpace.empty()) return name;
            return nameSpace + "." + name;
        }

        void CollectClass(ClassDeclStmt& statement, const std::string& nameSpace) {
            const std::string name = QualifiedClassName(statement.name, nameSpace);
            if (classes.contains(name)) Fail("duplicate class '" + name + "'");
            ClassInfo info;
            info.name = name;
            info.nameSpace = nameSpace;
            info.statement = &statement;
            info.llvmType = llvm::StructType::create(context, "absolute.class." + name);
            for (const std::string& parent : statement.parents)
                info.parents.push_back(QualifiedClassName(parent, nameSpace));

            auto* body = dynamic_cast<CompoundStmt*>(statement.body.get());
            if (!body) Fail("class '" + name + "' requires a compound body");
            for (const auto& member : body->statements) {
                if (auto* declaration = dynamic_cast<VarDeclStmt*>(member.get())) {
                    if (declaration->expr) info.ownPrimitiveFields.push_back(declaration->expr.get());
                }
                else if (auto* single = dynamic_cast<SingleStatement*>(member.get())) {
                    if (auto* instance = dynamic_cast<InstanceDeclExpr*>(single->expr.get()))
                        info.ownObjectFields.push_back(instance);
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

        void CollectClassDeclarations(const std::vector<std::unique_ptr<Statement>>& statements,
            const std::string& nameSpace = {}) {
            for (const auto& statement : statements) {
                if (auto* classDeclaration = dynamic_cast<ClassDeclStmt*>(statement.get()))
                    CollectClass(*classDeclaration, nameSpace);
                else if (auto* nameSpaceDeclaration = dynamic_cast<NamespaceDeclStmt*>(statement.get())) {
                    const std::string nested = nameSpace.empty()
                        ? nameSpaceDeclaration->name : nameSpace + "." + nameSpaceDeclaration->name;
                    if (nameSpaceDeclaration->body)
                        CollectClassDeclarations(nameSpaceDeclaration->body->statements, nested);
                }
            }
        }

        bool SameMethodSignature(FunctionDeclStmt& left, FunctionDeclStmt& right) {
            if (ResolveTypeName(left.returnType.get()) != ResolveTypeName(right.returnType.get()) ||
                left.parameters.size() != right.parameters.size()) return false;
            for (size_t index = 0; index < left.parameters.size(); ++index)
                if (DeclaredTypeName(*left.parameters[index]) != DeclaredTypeName(*right.parameters[index]))
                    return false;
            return true;
        }

        void FinalizeClass(const std::string& name) {
            auto found = classes.find(name);
            if (found == classes.end()) Fail("unknown class '" + name + "'");
            ClassInfo& info = found->second;
            if (info.finalized) return;
            if (info.finalizing) Fail("cyclic class inheritance involving '" + name + "'");
            info.finalizing = true;
            if (info.parents.size() > 1)
                Fail("multiple inheritance codegen is not implemented for class '" + name + "'");

            if (!info.parents.empty()) {
                std::string parentName = info.parents.front();
                if (!classes.contains(parentName) && classes.contains(info.statement->parents.front()))
                    parentName = info.statement->parents.front();
                if (!classes.contains(parentName)) Fail("unknown parent class '" + parentName + "'");
                info.parents.front() = parentName;
                FinalizeClass(parentName);
                const ClassInfo& parent = classes.at(parentName);
                info.fields = parent.fields;
                info.fieldByName = parent.fieldByName;
                info.methods = parent.methods;
                info.virtualNames = parent.virtualNames;
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

            for (FunctionDeclStmt* statement : info.ownMethods) {
                if (!statement || !statement->name) continue;
                const std::string methodName = statement->name->value;
                const auto inherited = info.methods.find(methodName);
                std::optional<unsigned> slot;
                if (inherited != info.methods.end() && inherited->second.virtualSlot) {
                    if (!SameMethodSignature(*inherited->second.statement, *statement))
                        Fail("override signature mismatch for '" + name + "." + methodName + "'");
                    slot = inherited->second.virtualSlot;
                }
                else if (HasModifier(*statement, "override"))
                    Fail("method '" + name + "." + methodName + "' overrides no virtual method");
                else if (HasModifier(*statement, "virtual")) {
                    slot = static_cast<unsigned>(info.virtualNames.size());
                    info.virtualNames.push_back(methodName);
                }
                ClassMethod method{statement, name, name + "." + methodName, slot};
                info.methods[methodName] = method;
                info.declaredMethods[methodName] = std::move(method);
            }

            std::vector<llvm::Type*> layout{builder.getPtrTy()};
            for (const ClassField& field : info.fields) layout.push_back(TypeFromName(field.typeName));
            info.llvmType->setBody(layout, false);
            info.finalizing = false;
            info.finalized = true;
        }

        llvm::FunctionType* MethodFunctionType(FunctionDeclStmt& statement) {
            std::vector<llvm::Type*> parameters{builder.getPtrTy()};
            for (const auto& parameter : statement.parameters)
                parameters.push_back(TypeFromName(DeclaredTypeName(*parameter)));
            return llvm::FunctionType::get(ResolveType(statement.returnType.get()), parameters, false);
        }

        llvm::Function* DeclareMethodFunction(const ClassMethod& method) {
            llvm::FunctionType* type = MethodFunctionType(*method.statement);
            llvm::Function* function = module->getFunction(method.linkName);
            if (!function)
                function = llvm::Function::Create(type, llvm::Function::ExternalLinkage, method.linkName, *module);
            if (function->getFunctionType() != type)
                Fail("conflicting method declaration '" + method.linkName + "'");
            function->setCallingConv(llvm::CallingConv::C);
            function->getArg(0)->setName("this");
            for (size_t index = 0; index < method.statement->parameters.size(); ++index)
                function->getArg(static_cast<unsigned>(index + 1))->setName(
                    IdentifierName(method.statement->parameters[index]->name.get()));
            return function;
        }

        llvm::Function* DeclareConstructorFunction(ClassInfo& info) {
            if (!info.constructor) return nullptr;
            std::vector<llvm::Type*> parameters{builder.getPtrTy()};
            for (const auto& parameter : info.constructor->parameters)
                parameters.push_back(TypeFromName(DeclaredTypeName(*parameter)));
            llvm::FunctionType* type = llvm::FunctionType::get(builder.getVoidTy(), parameters, false);
            const std::string name = info.name + ".__ctor";
            llvm::Function* function = module->getFunction(name);
            if (!function)
                function = llvm::Function::Create(type, llvm::Function::ExternalLinkage, name, *module);
            function->setCallingConv(llvm::CallingConv::C);
            function->getArg(0)->setName("this");
            for (size_t index = 0; index < info.constructor->parameters.size(); ++index)
                function->getArg(static_cast<unsigned>(index + 1))->setName(
                    IdentifierName(info.constructor->parameters[index]->name.get()));
            return function;
        }

        void DeclareClasses() {
            for (const std::string& name : classOrder) FinalizeClass(name);
            for (const std::string& name : classOrder) {
                ClassInfo& info = classes.at(name);
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

        std::string ClassNameFromType(std::string typeName) const {
            if (IsPointerTypeName(typeName)) typeName = PointerPointeeName(typeName);
            return typeName;
        }

        ClassInfo& RequireClassForType(const std::string& typeName) {
            const std::string name = ClassNameFromType(typeName);
            auto found = classes.find(name);
            if (found == classes.end()) Fail("type '" + typeName + "' is not a class");
            return found->second;
        }

        llvm::Value* ObjectPointer(Expression* expression, const std::string& typeName) {
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

        llvm::Value* FieldAddress(llvm::Value* object, ClassInfo& info, const ClassField& field) {
            return builder.CreateStructGEP(info.llvmType, object, field.index, field.name + ".address");
        }

        llvm::Value* ImplicitFieldAddress(const std::string& fieldName) {
            if (currentClassName.empty() || !currentThis) return nullptr;
            ClassInfo& info = classes.at(currentClassName);
            const auto field = info.fieldByName.find(fieldName);
            return field == info.fieldByName.end() ? nullptr : FieldAddress(currentThis, info, field->second);
        }

        llvm::Value* ObjectSize(ClassInfo& info) {
            return llvm::ConstantExpr::getSizeOf(info.llvmType);
        }

        void InitializeObject(llvm::Value* object, ClassInfo& info) {
            builder.CreateMemSet(object, builder.getInt8(0), ObjectSize(info), llvm::MaybeAlign(8));
            llvm::Value* vtableAddress = builder.CreateStructGEP(info.llvmType, object, 0, "vtable.address");
            builder.CreateStore(info.vtable, vtableAddress);
        }

        llvm::Value* Evaluate(Expression* expression) {
            if (!expression) Fail("missing expression");
            value = nullptr;
            valueCreatesManagedOwner = false;
            valueManagedPointee = nullptr;
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

        void EmitScopeCleanup(size_t index, SymbolId transferredOwner = InvalidSymbolId) {
            if (index >= scopes.size() || !builder.GetInsertBlock() || builder.GetInsertBlock()->getTerminator()) return;
            for (auto& [name, variable] : scopes[index]) {
                (void)name;
                if (IsTaskTypeName(variable.typeName)) {
                    llvm::Value* handle = builder.CreateLoad(variable.type, variable.address, "cleanup.task");
                    builder.CreateCall(TaskDestroy(), {handle});
                    builder.CreateStore(llvm::ConstantPointerNull::get(builder.getPtrTy()), variable.address);
                    continue;
                }
                if (!variable.managedOwner || variable.symbol == transferredOwner) continue;
                llvm::Value* handle = builder.CreateLoad(variable.type, variable.address, "cleanup.handle");
                builder.CreateCall(ManagedDestroy(), {handle});
                builder.CreateStore(builder.getInt64(0), variable.address);
                if (variable.managedPointee)
                    builder.CreateStore(llvm::ConstantPointerNull::get(builder.getPtrTy()), variable.managedPointee);
            }
        }

        void EmitCleanupsFrom(size_t firstScope, SymbolId transferredOwner = InvalidSymbolId) {
            for (size_t index = scopes.size(); index > firstScope; --index)
                EmitScopeCleanup(index - 1, transferredOwner);
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
            if (const auto found = globals.find(Qualify(name)); found != globals.end())
                return &found->second;
            if (const auto found = globals.find(name); found != globals.end())
                return &found->second;
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

        SymbolId SemanticSymbol(Expression* expression) const {
            if (!analyzer || !expression) return InvalidSymbolId;
            const ExpressionInfo* info = analyzer->GetExpressionInfo(*expression);
            return info ? info->symbol : InvalidSymbolId;
        }

        bool StaticManagedOwner(SymbolId symbol) const {
            if (!analyzer || symbol == InvalidSymbolId) return false;
            const Symbol* resolved = analyzer->GetSymbol(symbol);
            return resolved && resolved->managedOwner;
        }

        llvm::Value* ManagedPointee(Expression* expression, llvm::Value* handle) {
            if (auto* identifier = dynamic_cast<IdentifierExpr*>(expression)) {
                Variable* variable = FindVariable(identifier->name);
                const ExpressionInfo* info = analyzer ? analyzer->GetExpressionInfo(*expression) : nullptr;
                if (variable && variable->managedPointee && info &&
                    info->pointerOwner == variable->symbol) {
                    return builder.CreateLoad(builder.getPtrTy(), variable->managedPointee,
                        identifier->name + ".cached.pointee");
                }
            }
            return builder.CreateCall(ManagedGet(true), {handle}, "managed.pointee");
        }

        llvm::Value* BuildArrayDescriptor(const ArrayView& view) {
            llvm::StructType* type = ArrayDescriptorType(view.typeName);
            llvm::Value* descriptor = llvm::UndefValue::get(type);
            descriptor = builder.CreateInsertValue(descriptor, view.address, {0}, "array.data");
            for (size_t index = 0; index < view.dimensions.size(); ++index)
                descriptor = builder.CreateInsertValue(
                    descriptor, view.dimensions[index], {static_cast<unsigned>(index + 1)}, "array.dimension");
            return descriptor;
        }

        ArrayView ArrayViewFromValue(llvm::Value* descriptor, const std::string& typeName) {
            const size_t rank = ArrayRankName(typeName);
            if (rank == 0 || !descriptor->getType()->isStructTy())
                Fail("array expression does not produce a descriptor");
            ArrayView view;
            view.address = builder.CreateExtractValue(descriptor, {0}, "array.data");
            view.elementType = TypeFromName(ArrayElementTypeName(typeName, rank));
            view.typeName = typeName;
            for (size_t index = 0; index < rank; ++index)
                view.dimensions.push_back(builder.CreateExtractValue(
                    descriptor, {static_cast<unsigned>(index + 1)}, "array.dimension"));
            return view;
        }

        ArrayView ViewOfArray(Expression* expression) {
            if (auto* identifier = dynamic_cast<IdentifierExpr*>(expression)) {
                Variable& variable = RequireVariable(identifier->name);
                if (!variable.isArray) Fail("object is not an array");
                return {variable.address, variable.arrayElementType,
                    variable.typeName, variable.arrayDimensions};
            }
            const std::string typeName = SemanticType(expression);
            return ArrayViewFromValue(Evaluate(expression), typeName);
        }

        void EmitOrExit(llvm::Value* condition, const std::string& name) {
            llvm::Function* function = CurrentFunction();
            if (!function) Fail("runtime check outside a function");
            if (const auto* constant = llvm::dyn_cast<llvm::ConstantInt>(condition);
                constant && !constant->isZero()) return;
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
            ArrayView view = ViewOfArray(expression.base.get());
            if (expression.indexes.size() != view.dimensions.size())
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
                    index, view.dimensions[dimension], "array.index.below.size");
                EmitOrExit(builder.CreateAnd(nonNegative, belowSize, "array.index.valid"), "array.bounds");
                if (dimension == 0) offset = index;
                else {
                    offset = builder.CreateMul(offset, view.dimensions[dimension], "array.row.offset");
                    offset = builder.CreateAdd(offset, index, "array.linear.offset");
                }
            }
            return builder.CreateInBoundsGEP(
                view.elementType, view.address, offset, "array.element.address");
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
            if (classes.contains(name)) {
                FinalizeClass(name);
                return module->getDataLayout().getTypeAllocSize(classes.at(name).llvmType).getFixedValue();
            }
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

        llvm::Constant* GlobalArrayConstant(Expression* expression, llvm::Type* type) {
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

        void DeclareGlobalArray(VarDeclExpr& expression) {
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

        llvm::Function* DeclareFunction(FunctionDeclStmt& statement) {
            if (!statement.name || !statement.returnType) Fail("invalid function declaration");
            const std::string sourceName = Qualify(statement.name->value);
            const std::string functionName = statement.IsExternal() ? statement.name->value : sourceName;
            functionLinkNames[sourceName] = functionName;

            std::vector<llvm::Type*> parameterTypes;
            parameterTypes.reserve(statement.parameters.size());
            for (const auto& parameter : statement.parameters) {
                parameterTypes.push_back(TypeFromName(DeclaredTypeName(*parameter)));
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

        void BindCallableParameter(llvm::Argument& argument, VarDeclExpr& parameter) {
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

        void FinishClassCallable(llvm::Function& function) {
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

        void EmitMethod(ClassInfo& info, const ClassMethod& method) {
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

        void EmitConstructor(ClassInfo& info) {
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

        void EmitClassBodies(ClassInfo& info) {
            if (info.emitted) return;
            for (const auto& [methodName, method] : info.declaredMethods) {
                (void)methodName;
                EmitMethod(info, method);
            }
            EmitConstructor(info);
            info.emitted = true;
        }

        llvm::Module& BuildModule(Program& program, const std::string& moduleName) {
            module = std::make_unique<llvm::Module>(moduleName, context);
            scopes.clear();
            globals.clear();
            arrayDescriptorTypes.clear();
            classes.clear();
            classOrder.clear();
            loops.clear();
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
            const std::string cpu = llvm::sys::getHostCPUName().str();
            std::unique_ptr<llvm::TargetMachine> targetMachine(target->createTargetMachine(
                triple, cpu.empty() ? "generic" : cpu, "", options, llvm::Reloc::PIC_,
                std::nullopt, llvm::CodeGenOptLevel::Aggressive));
            if (!targetMachine) Fail("cannot create target machine for '" + triple + "'");
            generatedModule.setTargetTriple(triple);
            generatedModule.setDataLayout(targetMachine->createDataLayout());

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

    void CodeGenerator::Visit(ArrayTypeExpr* expr) {
        (void)expr;
        impl->Fail("an array type cannot be emitted as a runtime expression");
    }

    void CodeGenerator::Visit(IdentifierExpr* expr) {
        Impl::Variable* variable = impl->FindVariable(expr->name);
        if (!variable) {
            llvm::Value* fieldAddress = impl->ImplicitFieldAddress(expr->name);
            if (!fieldAddress) impl->Fail("unknown variable or field '" + expr->name + "'");
            if (impl->addressMode) {
                impl->addressValue = fieldAddress;
                return;
            }
            llvm::Type* fieldType = impl->TypeFromName(impl->SemanticType(expr));
            impl->value = impl->builder.CreateLoad(fieldType, fieldAddress, expr->name + ".value");
            impl->valueCreatesManagedOwner = false;
            return;
        }
        if (impl->addressMode) {
            impl->addressValue = variable->address;
            return;
        }
        if (variable->isArray) {
            impl->value = impl->BuildArrayDescriptor({variable->address, variable->arrayElementType,
                variable->typeName, variable->arrayDimensions});
            impl->valueCreatesManagedOwner = false;
            return;
        }
        impl->value = impl->builder.CreateLoad(variable->type, variable->address, expr->name + ".value");
        impl->valueCreatesManagedOwner = false;
    }

    void CodeGenerator::Visit(FunctionCallExpr* expr) {
        if (auto* member = dynamic_cast<MemberAccessExpr*>(expr->base.get())) {
            const std::string baseType = impl->SemanticType(member->base.get());
            const std::string className = impl->ClassNameFromType(baseType);
            auto classIterator = impl->classes.find(className);
            if (classIterator != impl->classes.end()) {
                Impl::ClassInfo& info = classIterator->second;
                const auto method = info.methods.find(member->member);
                if (method == info.methods.end())
                    impl->Fail("class '" + info.name + "' has no method '" + member->member + "'");
                llvm::Value* object = impl->ObjectPointer(member->base.get(), baseType);
                llvm::FunctionType* methodType = impl->MethodFunctionType(*method->second.statement);
                std::vector<llvm::Value*> arguments{object};
                for (size_t index = 0; index < expr->arguments.size(); ++index) {
                    if (index + 1 >= methodType->getNumParams())
                        impl->Fail("too many arguments for method '" + member->member + "'");
                    arguments.push_back(impl->Coerce(impl->Evaluate(expr->arguments[index].get()),
                        methodType->getParamType(static_cast<unsigned>(index + 1))));
                }
                if (arguments.size() != methodType->getNumParams())
                    impl->Fail("invalid argument count for method '" + member->member + "'");

                llvm::Value* callee = nullptr;
                if (method->second.virtualSlot) {
                    llvm::Value* vtableAddress = impl->builder.CreateStructGEP(
                        info.llvmType, object, 0, "vtable.address");
                    llvm::Value* vtable = impl->builder.CreateLoad(
                        impl->builder.getPtrTy(), vtableAddress, "vtable");
                    llvm::Value* slot = impl->builder.CreateGEP(impl->builder.getPtrTy(), vtable,
                        impl->builder.getInt64(*method->second.virtualSlot), "virtual.slot");
                    callee = impl->builder.CreateLoad(impl->builder.getPtrTy(), slot, "virtual.method");
                }
                else {
                    callee = impl->module->getFunction(method->second.linkName);
                    if (!callee) impl->Fail("missing method function '" + method->second.linkName + "'");
                }
                llvm::CallInst* call = impl->builder.CreateCall(methodType, callee, arguments,
                    methodType->getReturnType()->isVoidTy() ? "" : member->member + ".result");
                impl->value = methodType->getReturnType()->isVoidTy() ? nullptr : call;
                impl->valueCreatesManagedOwner = IsManagedPointerTypeName(impl->SemanticType(expr));
                return;
            }
        }
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
        if (expr->indexes.size() == 1 && !expr->indexes.front()) {
            if (impl->addressMode) impl->Fail("a slice is not assignable");
            impl->value = impl->BuildArrayDescriptor(impl->ViewOfArray(expr->base.get()));
            impl->valueCreatesManagedOwner = false;
            return;
        }
        llvm::Value* address = impl->ArrayElementAddress(*expr);
        if (impl->addressMode) {
            impl->addressValue = address;
            return;
        }
        llvm::Type* elementType = impl->TypeFromName(impl->SemanticType(expr));
        impl->value = impl->builder.CreateLoad(elementType, address, "array.element");
        impl->valueCreatesManagedOwner = false;
    }

    void CodeGenerator::Visit(SliceExpr* expr) {
        if (impl->addressMode) impl->Fail("a slice is not assignable");
        Impl::ArrayView source = impl->ViewOfArray(expr->base.get());
        if (source.dimensions.size() != 1) impl->Fail("slices require a one-dimensional array");
        llvm::Value* begin = expr->begin
            ? impl->Coerce(impl->Evaluate(expr->begin.get()), impl->builder.getInt64Ty())
            : impl->builder.getInt64(0);
        llvm::Value* end = expr->end
            ? impl->Coerce(impl->Evaluate(expr->end.get()), impl->builder.getInt64Ty())
            : source.dimensions.front();
        llvm::Value* valid = impl->builder.CreateAnd(
            impl->builder.CreateICmpSGE(begin, impl->builder.getInt64(0), "slice.begin.nonnegative"),
            impl->builder.CreateICmpSGE(end, begin, "slice.order.valid"), "slice.lower.valid");
        valid = impl->builder.CreateAnd(valid,
            impl->builder.CreateICmpSLE(end, source.dimensions.front(), "slice.end.below.size"),
            "slice.valid");
        impl->EmitOrExit(valid, "array.bounds");
        Impl::ArrayView slice;
        slice.address = impl->builder.CreateInBoundsGEP(
            source.elementType, source.address, begin, "slice.data");
        slice.elementType = source.elementType;
        slice.typeName = source.typeName;
        slice.dimensions = {impl->builder.CreateSub(end, begin, "slice.length")};
        impl->value = impl->BuildArrayDescriptor(slice);
        impl->valueCreatesManagedOwner = false;
    }

    void CodeGenerator::Visit(BinaryExpr* expr) {
        const std::string leftType = impl->SemanticType(expr->left.get());
        const std::string rightType = impl->SemanticType(expr->right.get());
        llvm::Value* left = impl->Evaluate(expr->left.get());
        llvm::Value* right = impl->Evaluate(expr->right.get());

        if (const PluginBinaryOperator* pluginOperator =
                FindPluginBinaryOperator(leftType, expr->op, rightType)) {
            llvm::Function* function = impl->module->getFunction(pluginOperator->functionName);
            if (!function || function->arg_size() != 2)
                impl->Fail("missing plugin operator function '" + pluginOperator->functionName + "'");
            left = impl->Coerce(left, function->getFunctionType()->getParamType(0));
            right = impl->Coerce(right, function->getFunctionType()->getParamType(1));
            impl->value = impl->builder.CreateCall(function, {left, right}, "plugin.operator");
            impl->valueCreatesManagedOwner = IsManagedPointerTypeName(pluginOperator->resultType);
            impl->valueManagedPointee = nullptr;
            return;
        }

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
        const std::string targetTypeName = impl->SemanticType(expr->target.get());
        if (ArrayRankName(targetTypeName) > 0)
            impl->Fail("whole-array assignment is not implemented; initialize a new array view instead");
        llvm::Value* targetAddress = impl->EvaluateAddress(expr->target.get());
        llvm::Type* targetType = impl->TypeFromName(targetTypeName);
        llvm::Value* assigned = impl->Evaluate(expr->value.get());
        const bool createsOwner = impl->valueCreatesManagedOwner;
        llvm::Value* assignedPointee = impl->valueManagedPointee;

        if (expr->op != "=") {
            llvm::Value* current = impl->builder.CreateLoad(targetType, targetAddress, "assignment.current");
            assigned = impl->ApplyBinary(expr->op.substr(0, expr->op.size() - 1), current, assigned);
        }

        if (IsManagedPointerTypeName(targetTypeName)) {
            const std::string name = IdentifierName(expr->target.get());
            if (!name.empty()) {
                Impl::Variable& variable = impl->RequireVariable(name);
                if (variable.managedOwner) {
                    llvm::Value* oldHandle = impl->builder.CreateLoad(variable.type, variable.address, "assignment.old.handle");
                    impl->builder.CreateCall(impl->ManagedDestroy(), {oldHandle});
                    if (createsOwner && assignedPointee) {
                        if (!variable.managedPointee)
                            variable.managedPointee = impl->CreateEntryAlloca(
                                *impl->CurrentFunction(), impl->builder.getPtrTy(), name + ".cached.pointee");
                        impl->builder.CreateStore(assignedPointee, variable.managedPointee);
                    }
                    else if (variable.managedPointee)
                        impl->builder.CreateStore(
                            llvm::ConstantPointerNull::get(impl->builder.getPtrTy()), variable.managedPointee);
                }
            }
        }
        assigned = impl->Coerce(assigned, targetType);
        impl->builder.CreateStore(assigned, targetAddress);
        impl->value = assigned;
        impl->valueCreatesManagedOwner = false;
        impl->valueManagedPointee = nullptr;
    }

    void CodeGenerator::Visit(VarDeclExpr* expr) {
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("global variables are not implemented yet");
        if (impl->scopes.empty()) impl->Fail("variable declaration outside a scope");

        const std::string name = IdentifierName(expr->name.get());
        if (name.empty()) impl->Fail("variable declaration requires an identifier");
        const std::string baseTypeName = impl->ResolveTypeName(expr->type.get());
        const std::string declaredTypeName = impl->DeclaredTypeName(*expr);
        if (auto* arrayDeclarator = dynamic_cast<ArrayAccessExpr*>(expr->name.get())) {
            if (arrayDeclarator->indexes.empty()) impl->Fail("array declaration requires a dimension");
            llvm::Type* elementType = impl->TypeFromName(baseTypeName);
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
            address->setAlignment(llvm::Align(16));
            llvm::Value* byteCount = impl->builder.CreateMul(
                elementCount,
                impl->builder.getInt64(impl->SizeOfTypeName(baseTypeName)),
                "array.byte.count");
            impl->builder.CreateMemSet(
                address, impl->builder.getInt8(0), byteCount, llvm::MaybeAlign(16));

            Impl::Variable variable;
            variable.address = address;
            variable.type = elementType;
            variable.typeName = declaredTypeName;
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
                    llvm::Value* destination = impl->builder.CreateInBoundsGEP(
                        elementType, address, impl->builder.getInt64(index), "array.initializer.element");
                    impl->builder.CreateStore(initial, destination);
                }
            }
            else if (expr->value) impl->Fail("array variable requires an array literal initializer");

            impl->value = address;
            impl->valueCreatesManagedOwner = false;
            return;
        }
        if (ArrayRankName(declaredTypeName) > 0) {
            if (!expr->value) impl->Fail("array view declaration requires an initializer");
            if (dynamic_cast<ArrayExpr*>(expr->value.get()))
                impl->Fail("use a sized array declarator for an array literal");
            Impl::ArrayView view = impl->ArrayViewFromValue(
                impl->Evaluate(expr->value.get()), declaredTypeName);
            if (!impl->scopes.back().emplace(name,
                Impl::Variable{view.address, view.elementType, declaredTypeName, false,
                    true, view.elementType, view.dimensions, nullptr, InvalidSymbolId}).second) {
                impl->Fail("duplicate variable '" + name + "'");
            }
            impl->value = impl->BuildArrayDescriptor(view);
            impl->valueCreatesManagedOwner = false;
            return;
        }
        const std::string& typeName = declaredTypeName;
        llvm::Type* type = impl->TypeFromName(typeName);
        if (type->isVoidTy()) impl->Fail("variable '" + name + "' cannot have type void");

        llvm::AllocaInst* address = impl->CreateEntryAlloca(*function, type, name);
        const SymbolId symbol = impl->SemanticSymbol(expr);
        const bool staticOwner = IsManagedPointerTypeName(typeName) && impl->StaticManagedOwner(symbol);
        if (!impl->scopes.back().emplace(name,
            Impl::Variable{address, type, typeName, staticOwner, false, nullptr, {},
                nullptr, symbol}).second) {
            impl->Fail("duplicate variable '" + name + "'");
        }

        llvm::Value* initial = expr->value ? impl->Evaluate(expr->value.get()) : llvm::Constant::getNullValue(type);
        const bool createsOwner = impl->valueCreatesManagedOwner;
        llvm::Value* managedPointee = impl->valueManagedPointee;
        initial = impl->Coerce(initial, type);
        impl->builder.CreateStore(initial, address);
        if (staticOwner && createsOwner && managedPointee) {
            Impl::Variable& variable = impl->RequireVariable(name);
            variable.managedPointee = impl->CreateEntryAlloca(
                *function, impl->builder.getPtrTy(), name + ".cached.pointee");
            impl->builder.CreateStore(managedPointee, variable.managedPointee);
        }
        impl->value = initial;
        impl->valueCreatesManagedOwner = false;
        impl->valueManagedPointee = nullptr;
    }

    void CodeGenerator::Visit(MemberAccessExpr* expr) {
        const std::string baseType = impl->SemanticType(expr->base.get());
        Impl::ClassInfo& info = impl->RequireClassForType(baseType);
        const auto field = info.fieldByName.find(expr->member);
        if (field == info.fieldByName.end())
            impl->Fail("class '" + info.name + "' has no field '" + expr->member + "'");
        llvm::Value* object = impl->ObjectPointer(expr->base.get(), baseType);
        llvm::Value* fieldAddress = impl->FieldAddress(object, info, field->second);
        if (impl->addressMode) {
            impl->addressValue = fieldAddress;
            return;
        }
        impl->value = impl->builder.CreateLoad(
            impl->TypeFromName(field->second.typeName), fieldAddress, expr->member + ".value");
        impl->valueCreatesManagedOwner = false;
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
        const bool classAllocation = impl->classes.contains(pointeeType);
        llvm::Value* allocationSize = classAllocation
            ? impl->ObjectSize(impl->classes.at(pointeeType))
            : impl->builder.getInt64(impl->SizeOfTypeName(pointeeType));
        if (rawAllocation) {
            pointer = impl->builder.CreateCall(
                impl->Malloc(), {allocationSize}, "raw.allocation");
            result = pointer;
        }
        else {
            llvm::Value* handle = impl->builder.CreateCall(
                impl->ManagedCreate(), {allocationSize}, "managed.handle");
            pointer = impl->builder.CreateCall(impl->ManagedGet(true), {handle}, "managed.allocation");
            result = handle;
        }

        if (classAllocation) {
            Impl::ClassInfo& info = impl->classes.at(pointeeType);
            impl->InitializeObject(pointer, info);
            if (info.constructor) {
                llvm::Function* constructor = impl->module->getFunction(info.name + ".__ctor");
                if (!constructor) impl->Fail("missing constructor for '" + info.name + "'");
                std::vector<llvm::Value*> arguments{pointer};
                for (size_t index = 0; index < expr->arguments.size(); ++index) {
                    if (index + 1 >= constructor->arg_size())
                        impl->Fail("too many constructor arguments for '" + info.name + "'");
                    arguments.push_back(impl->Coerce(impl->Evaluate(expr->arguments[index].get()),
                        constructor->getFunctionType()->getParamType(static_cast<unsigned>(index + 1))));
                }
                if (arguments.size() != constructor->arg_size())
                    impl->Fail("invalid constructor argument count for '" + info.name + "'");
                impl->builder.CreateCall(constructor, arguments);
            }
            else if (!expr->arguments.empty())
                impl->Fail("class '" + info.name + "' has no constructor");
            impl->value = result;
            impl->valueCreatesManagedOwner = !rawAllocation;
            impl->valueManagedPointee = rawAllocation ? nullptr : pointer;
            return;
        }

        llvm::Value* initial = expr->arguments.empty()
            ? llvm::Constant::getNullValue(pointee)
            : impl->Evaluate(expr->arguments.front().get());
        initial = impl->Coerce(initial, pointee);
        impl->builder.CreateStore(initial, pointer);
        impl->value = result;
        impl->valueCreatesManagedOwner = !rawAllocation;
        impl->valueManagedPointee = rawAllocation ? nullptr : pointer;
    }

    void CodeGenerator::Visit(DestructorCallExpr* expr) {
        const std::string typeName = impl->SemanticType(expr->target.get());
        llvm::Value* targetAddress = impl->EvaluateAddress(expr->target.get());
        llvm::Type* type = impl->TypeFromName(typeName);
        llvm::Value* pointer = impl->builder.CreateLoad(type, targetAddress, "delete.target");
        if (IsManagedPointerTypeName(typeName)) {
            const std::string name = IdentifierName(expr->target.get());
            if (!name.empty()) {
                Impl::Variable& variable = impl->RequireVariable(name);
                if (variable.managedPointee)
                    impl->builder.CreateStore(
                        llvm::ConstantPointerNull::get(impl->builder.getPtrTy()), variable.managedPointee);
            }
            impl->builder.CreateCall(impl->ManagedDestroy(), {pointer});
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
        llvm::Function* function = impl->CurrentFunction();
        if (!function || impl->scopes.empty()) impl->Fail("object declaration outside a function");
        const std::string name = IdentifierName(expr->identifierName.get());
        const std::string typeName = impl->SemanticType(expr);
        Impl::ClassInfo& info = impl->RequireClassForType(typeName);
        llvm::AllocaInst* address = impl->CreateEntryAlloca(*function, info.llvmType, name);
        impl->InitializeObject(address, info);
        if (expr->value) {
            llvm::Value* initial = impl->Coerce(impl->Evaluate(expr->value.get()), info.llvmType);
            impl->builder.CreateStore(initial, address);
        }
        else if (info.constructor && info.constructor->parameters.empty()) {
            llvm::Function* constructor = impl->module->getFunction(info.name + ".__ctor");
            impl->builder.CreateCall(constructor, {address});
        }
        if (!impl->scopes.back().emplace(name,
            Impl::Variable{address, info.llvmType, typeName, false, false, nullptr, {},
                nullptr, impl->SemanticSymbol(expr)}).second)
            impl->Fail("duplicate object '" + name + "'");
        impl->value = impl->builder.CreateLoad(info.llvmType, address, name + ".value");
        impl->valueCreatesManagedOwner = false;
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
                ? impl->ManagedPointee(expr->operand.get(), pointer)
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
        llvm::Value* result = nullptr;
        if (ArrayRankName(impl->currentReturnTypeName) > 0) {
            Impl::ArrayView source = impl->ArrayViewFromValue(
                impl->Evaluate(stmt->expr.get()), impl->currentReturnTypeName);
            llvm::Value* elementCount = impl->builder.getInt64(1);
            for (llvm::Value* dimension : source.dimensions)
                elementCount = impl->builder.CreateMul(elementCount, dimension, "return.array.element.count");
            llvm::Value* byteCount = impl->builder.CreateMul(elementCount,
                impl->builder.getInt64(impl->SizeOfTypeName(
                    ArrayElementTypeName(impl->currentReturnTypeName,
                        ArrayRankName(impl->currentReturnTypeName)))),
                "return.array.byte.count");
            llvm::Value* copiedData = impl->builder.CreateCall(
                impl->Malloc(), {byteCount}, "return.array.data");
            impl->builder.CreateMemCpy(copiedData, llvm::MaybeAlign(16),
                source.address, llvm::MaybeAlign(1), byteCount);
            source.address = copiedData;
            result = impl->BuildArrayDescriptor(source);
        }
        else result = impl->Coerce(impl->Evaluate(stmt->expr.get()), function->getReturnType());
        SymbolId transferredOwner = InvalidSymbolId;
        if (IsManagedPointerTypeName(impl->currentReturnTypeName)) {
            const auto* returnedIdentifier = dynamic_cast<IdentifierExpr*>(stmt->expr.get());
            if (returnedIdentifier) {
                Impl::Variable& returned = impl->RequireVariable(returnedIdentifier->name);
                if (returned.managedOwner) transferredOwner = returned.symbol;
            }
        }
        impl->EmitCleanupsFrom(0, transferredOwner);
        impl->builder.CreateRet(result);
    }

    void CodeGenerator::Visit(AssignmentStmt* stmt) {
        if (impl->phase == Impl::Phase::EmitBodies && stmt->expr) stmt->expr->Accept(*this);
    }

    void CodeGenerator::Visit(VarDeclStmt* stmt) {
        if (!stmt->expr) return;
        if (impl->phase == Impl::Phase::DeclareFunctions) {
            if (ArrayRankName(impl->DeclaredTypeName(*stmt->expr)) > 0)
                impl->DeclareGlobalArray(*stmt->expr);
            return;
        }
        if (!impl->CurrentFunction()) return;
        stmt->expr->Accept(*this);
    }

    void CodeGenerator::Visit(StructDeclStmt* stmt) {
        (void)stmt;
        if (impl->phase == Impl::Phase::EmitBodies) impl->Fail("struct codegen is not implemented yet");
    }

    void CodeGenerator::Visit(ClassDeclStmt* stmt) {
        if (impl->phase != Impl::Phase::EmitBodies) return;
        const std::string name = impl->Qualify(stmt->name);
        auto found = impl->classes.find(name);
        if (found == impl->classes.end()) impl->Fail("unregistered class '" + name + "'");
        impl->EmitClassBodies(found->second);
    }

    void CodeGenerator::Visit(ConstructorDeclStmt* stmt) {
        (void)stmt;
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
        if (impl->phase != Impl::Phase::EmitBodies) return;
        llvm::Function* function = impl->CurrentFunction();
        if (!function) impl->Fail("foreach outside a function");
        Impl::ArrayView source = impl->ViewOfArray(stmt->iterable.get());
        if (source.dimensions.size() != 1)
            impl->Fail("foreach requires a one-dimensional array or slice");

        impl->PushScope();
        if (!stmt->var) impl->Fail("foreach requires an iteration variable");
        stmt->var->Accept(*this);
        const std::string variableName = IdentifierName(stmt->var->name.get());
        Impl::Variable& iterationVariable = impl->RequireVariable(variableName);
        llvm::AllocaInst* index = impl->CreateEntryAlloca(
            *function, impl->builder.getInt64Ty(), "foreach.index");
        impl->builder.CreateStore(impl->builder.getInt64(0), index);

        llvm::BasicBlock* conditionBlock = llvm::BasicBlock::Create(
            impl->context, "foreach.condition", function);
        llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(
            impl->context, "foreach.body", function);
        llvm::BasicBlock* updateBlock = llvm::BasicBlock::Create(
            impl->context, "foreach.update", function);
        llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(
            impl->context, "foreach.end", function);
        impl->builder.CreateBr(conditionBlock);

        impl->builder.SetInsertPoint(conditionBlock);
        llvm::Value* current = impl->builder.CreateLoad(
            impl->builder.getInt64Ty(), index, "foreach.index.value");
        impl->builder.CreateCondBr(impl->builder.CreateICmpULT(
            current, source.dimensions.front(), "foreach.has.next"), bodyBlock, endBlock);

        impl->loops.push_back({updateBlock, endBlock, impl->scopes.size()});
        impl->builder.SetInsertPoint(bodyBlock);
        llvm::Value* elementAddress = impl->builder.CreateInBoundsGEP(
            source.elementType, source.address, current, "foreach.element.address");
        llvm::Value* element = impl->builder.CreateLoad(
            source.elementType, elementAddress, "foreach.element");
        impl->builder.CreateStore(impl->Coerce(element, iterationVariable.type), iterationVariable.address);
        if (stmt->body) stmt->body->Accept(*this);
        impl->BranchIfNeeded(updateBlock);

        impl->builder.SetInsertPoint(updateBlock);
        llvm::Value* next = impl->builder.CreateAdd(
            impl->builder.CreateLoad(impl->builder.getInt64Ty(), index),
            impl->builder.getInt64(1), "foreach.next");
        impl->builder.CreateStore(next, index);
        impl->BranchIfNeeded(conditionBlock);
        impl->loops.pop_back();

        impl->builder.SetInsertPoint(endBlock);
        impl->PopScope(true);
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
