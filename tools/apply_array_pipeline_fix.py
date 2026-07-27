#!/usr/bin/env python3
from pathlib import Path


def replace_once_in_text(text: str, old: str, new: str, description: str) -> str:
    if old not in text:
        raise SystemExit(f"{description}: expected block not found")
    return text.replace(old, new, 1)


# Keep the custom bounds pass buildable with both the LLVM 18 CI SDK and
# LLVM 21 shipped by Termux, then wire it into native object generation.
codegen = Path("Absolute-CodeGen/src/codegen.cpp")
text = codegen.read_text(encoding="utf-8")
text = text.replace('#include <llvm/Transforms/Utils/BasicBlockUtils.h>\n', '', 1)
text = replace_once_in_text(
    text,
    '''            if (llvm::isa<llvm::Constant>(left) || llvm::isa<llvm::Constant>(right)) {
                llvm::Constant* result = lazyValues.getPredicateAt(
                    predicate, left, right, context, true);
                if (const auto* known = llvm::dyn_cast_or_null<llvm::ConstantInt>(result))
                    if (known->isOne()) return true;
            }
''',
    '''            if (llvm::isa<llvm::Constant>(left) || llvm::isa<llvm::Constant>(right)) {
#if LLVM_VERSION_MAJOR >= 21
                llvm::Constant* result = lazyValues.getPredicateAt(
                    predicate, left, right, context, true);
                if (const auto* known = llvm::dyn_cast_or_null<llvm::ConstantInt>(result))
                    if (known->isOne()) return true;
#else
                if (lazyValues.getPredicateAt(
                        predicate, left, right, context, true) == llvm::LazyValueInfo::True)
                    return true;
#endif
            }
''',
    "LLVM 18/21 integer predicate compatibility",
)
text = replace_once_in_text(
    text,
    '''            llvm::Constant* known = lazyValues.getPredicateAt(
                llvm::CmpInst::ICMP_EQ,
                value,
                llvm::ConstantInt::get(value->getType(), expected),
                context,
                true);
            if (const auto* result = llvm::dyn_cast_or_null<llvm::ConstantInt>(known))
                if (result->isOne()) return true;
''',
    '''#if LLVM_VERSION_MAJOR >= 21
            llvm::Constant* known = lazyValues.getPredicateAt(
                llvm::CmpInst::ICMP_EQ,
                value,
                llvm::ConstantInt::get(value->getType(), expected),
                context,
                true);
            if (const auto* result = llvm::dyn_cast_or_null<llvm::ConstantInt>(known))
                if (result->isOne()) return true;
#else
            if (lazyValues.getPredicateAt(
                    llvm::CmpInst::ICMP_EQ,
                    value,
                    llvm::ConstantInt::get(value->getType(), expected),
                    context,
                    true) == llvm::LazyValueInfo::True)
                return true;
#endif
''',
    "LLVM 18/21 boolean predicate compatibility",
)
text = replace_once_in_text(
    text,
    '''                for (auto& [branch, success] : rewrites)
                    llvm::ReplaceInstWithInst(branch, llvm::BranchInst::Create(success));
''',
    '''                for (auto& [branch, success] : rewrites) {
                    llvm::IRBuilder<> rewriteBuilder(branch);
                    rewriteBuilder.CreateBr(success);
                    branch->eraseFromParent();
                }
''',
    "LLVM branch rewrite compatibility",
)
codegen.write_text(text, encoding="utf-8")

module = Path("Absolute-CodeGen/src/codegen_module.cpp")
module_text = module.read_text(encoding="utf-8")
module_text = replace_once_in_text(
    module_text,
    '''namespace Absolute {
''',
    '''namespace Absolute {
    void AddAbsoluteOptimizationPassesToPipeline(llvm::ModulePassManager& passes);

''',
    "optimization pipeline declaration",
)
module_text = replace_once_in_text(
    module_text,
    '''        llvm::ModulePassManager optimizationPasses =
            passBuilder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O3);
        optimizationPasses.run(generatedModule, moduleAnalyses);
''',
    '''        llvm::ModulePassManager optimizationPasses =
            passBuilder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O3);
        AddAbsoluteOptimizationPassesToPipeline(optimizationPasses);
        optimizationPasses.run(generatedModule, moduleAnalyses);
''',
    "Absolute optimization pipeline hook",
)

# A declaration such as matrix[2][3] is represented as nested ArrayAccessExpr
# nodes. Preserve every bracket group instead of silently treating it as [3].
internal = Path("Absolute-CodeGen/include/codegen_internal.h")
internal_text = internal.read_text(encoding="utf-8")
internal_text = replace_once_in_text(
    internal_text,
    '''        inline std::string IdentifierName(Expression* expression) {
            if (!expression) return {};
            BaseIdentifierVisitor visitor;
            expression->Accept(visitor);
            return visitor.identifierExpr ? visitor.identifierExpr->name : std::string{};
        }
''',
    '''        inline std::string IdentifierName(Expression* expression) {
            if (!expression) return {};
            BaseIdentifierVisitor visitor;
            expression->Accept(visitor);
            return visitor.identifierExpr ? visitor.identifierExpr->name : std::string{};
        }

        inline size_t ArrayDeclaratorRank(const Expression* expression) {
            size_t rank = 0;
            const Expression* current = expression;
            while (const auto* declarator = dynamic_cast<const ArrayAccessExpr*>(current)) {
                rank += declarator->indexes.size();
                current = declarator->base.get();
            }
            return rank;
        }

        inline void CollectArrayDeclaratorIndexes(
            Expression* expression, std::vector<Expression*>& indexes) {
            auto* declarator = dynamic_cast<ArrayAccessExpr*>(expression);
            if (!declarator) return;
            CollectArrayDeclaratorIndexes(declarator->base.get(), indexes);
            for (const auto& index : declarator->indexes) indexes.push_back(index.get());
        }
''',
    "array declarator helpers",
)
internal.write_text(internal_text, encoding="utf-8")

analyzer = Path("Absolute-Analyzer/src/analyzer.cpp")
analyzer_text = analyzer.read_text(encoding="utf-8")
analyzer_text = replace_once_in_text(
    analyzer_text,
    '''    std::string Analyzer::ResolveDeclaredType(VarDeclExpr& expression) {
        std::string type = ResolveType(expression.type.get());
        if (const auto* declarator = dynamic_cast<const ArrayAccessExpr*>(expression.name.get());
            declarator && !declarator->indexes.empty()) {
            type = ArrayType(std::move(type), declarator->indexes.size());
        }
        return type;
    }
''',
    '''    std::string Analyzer::ResolveDeclaredType(VarDeclExpr& expression) {
        std::string type = ResolveType(expression.type.get());
        size_t rank = 0;
        const Expression* current = expression.name.get();
        while (const auto* declarator = dynamic_cast<const ArrayAccessExpr*>(current)) {
            rank += declarator->indexes.size();
            current = declarator->base.get();
        }
        if (rank != 0) type = ArrayType(std::move(type), rank);
        return type;
    }
''',
    "analyzer multidimensional declaration rank",
)
analyzer.write_text(analyzer_text, encoding="utf-8")

types = Path("Absolute-CodeGen/src/codegen_types.cpp")
types_text = types.read_text(encoding="utf-8")
types_text = replace_once_in_text(
    types_text,
    '''        std::string type = SubstituteCodegenType(ResolveTypeName(expression.type.get()), currentGenericSubstitutions);
        if (const auto* declarator = dynamic_cast<const ArrayAccessExpr*>(expression.name.get()))
            for (size_t index = 0; index < declarator->indexes.size(); ++index) type += "[]";
        return type;
''',
    '''        std::string type = SubstituteCodegenType(
            ResolveTypeName(expression.type.get()), currentGenericSubstitutions);
        const size_t rank = ArrayDeclaratorRank(expression.name.get());
        for (size_t index = 0; index < rank; ++index) type += "[]";
        return type;
''',
    "codegen multidimensional declaration type",
)
types.write_text(types_text, encoding="utf-8")

values = Path("Absolute-CodeGen/src/codegen_values.cpp")
values_text = values.read_text(encoding="utf-8")
values_text = replace_once_in_text(
    values_text,
    '''        if (auto* arrayDeclarator = dynamic_cast<ArrayAccessExpr*>(expr->name.get())) {
            if (arrayDeclarator->indexes.empty()) impl->Fail("array declaration requires a dimension");
            llvm::Type* elementType = impl->TypeFromName(baseTypeName);
            std::vector<llvm::Value*> dimensions;
            dimensions.reserve(arrayDeclarator->indexes.size());

            auto* literal = dynamic_cast<ArrayExpr*>(expr->value.get());
            const auto inferredShape = literal ? InferArrayStorageShape(
                *literal, arrayDeclarator->indexes.size())
                : std::optional<std::vector<size_t>>{};
            for (size_t dimension = 0; dimension < arrayDeclarator->indexes.size(); ++dimension) {
                llvm::Value* size = nullptr;
                if (arrayDeclarator->indexes[dimension]) {
                    size = impl->Coerce(
                        impl->Evaluate(arrayDeclarator->indexes[dimension].get()),
                        impl->builder.getInt64Ty());
                }
''',
    '''        std::vector<Expression*> arrayDeclaratorIndexes;
        CollectArrayDeclaratorIndexes(expr->name.get(), arrayDeclaratorIndexes);
        if (!arrayDeclaratorIndexes.empty()) {
            llvm::Type* elementType = impl->TypeFromName(baseTypeName);
            std::vector<llvm::Value*> dimensions;
            dimensions.reserve(arrayDeclaratorIndexes.size());

            auto* literal = dynamic_cast<ArrayExpr*>(expr->value.get());
            const auto inferredShape = literal ? InferArrayStorageShape(
                *literal, arrayDeclaratorIndexes.size())
                : std::optional<std::vector<size_t>>{};
            for (size_t dimension = 0; dimension < arrayDeclaratorIndexes.size(); ++dimension) {
                llvm::Value* size = nullptr;
                if (arrayDeclaratorIndexes[dimension]) {
                    size = impl->Coerce(
                        impl->Evaluate(arrayDeclaratorIndexes[dimension]),
                        impl->builder.getInt64Ty());
                }
''',
    "local multidimensional array dimensions",
)
values.write_text(values_text, encoding="utf-8")

module_text = replace_once_in_text(
    module_text,
    '''        std::vector<size_t> dimensions;
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
''',
    '''        std::vector<size_t> dimensions;
        std::vector<Expression*> declaratorIndexes;
        CollectArrayDeclaratorIndexes(expression.name.get(), declaratorIndexes);
        if (!declaratorIndexes.empty()) {
            for (size_t index = 0; index < declaratorIndexes.size(); ++index) {
                if (auto* size = dynamic_cast<NumberLiteralExpr*>(declaratorIndexes[index]))
                    dimensions.push_back(static_cast<size_t>(std::stoull(size->value)));
                else if (!declaratorIndexes[index] && inferredShape && index < inferredShape->size())
                    dimensions.push_back((*inferredShape)[index]);
                else Fail("global array dimensions must be constant integers");
            }
        }
        else if (inferredShape) dimensions = *inferredShape;
''',
    "global multidimensional array dimensions",
)
module.write_text(module_text, encoding="utf-8")
