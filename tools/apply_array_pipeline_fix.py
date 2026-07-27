#!/usr/bin/env python3
from pathlib import Path


def replace_once_in_text(text: str, old: str, new: str, description: str) -> str:
    if old not in text:
        raise SystemExit(f"{description}: expected block not found")
    return text.replace(old, new, 1)


codegen = Path("Absolute-CodeGen/src/codegen.cpp")
text = codegen.read_text(encoding="utf-8")
text = text.replace(
    '#include <llvm/Transforms/Utils/BasicBlockUtils.h>\n',
    '',
    1,
)

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
module.write_text(module_text, encoding="utf-8")
