#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: Path, old: str, new: str, description: str) -> None:
    text = path.read_text(encoding="utf-8")
    if old not in text:
        raise SystemExit(f"{description}: expected block not found in {path}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


codegen = Path("Absolute-CodeGen/src/codegen.cpp")
text = codegen.read_text(encoding="utf-8")
text = text.replace(
    '#include <llvm/Transforms/Utils/BasicBlockUtils.h>\n',
    '',
    1,
)
old_rewrite = '''                for (auto& [branch, success] : rewrites)
                    llvm::ReplaceInstWithInst(branch, llvm::BranchInst::Create(success));
'''
new_rewrite = '''                for (auto& [branch, success] : rewrites) {
                    llvm::IRBuilder<> rewriteBuilder(branch);
                    rewriteBuilder.CreateBr(success);
                    branch->eraseFromParent();
                }
'''
if old_rewrite not in text:
    raise SystemExit("LLVM branch rewrite block not found")
codegen.write_text(text.replace(old_rewrite, new_rewrite, 1), encoding="utf-8")

module = Path("Absolute-CodeGen/src/codegen_module.cpp")
replace_once(
    module,
    '''namespace Absolute {
''',
    '''namespace Absolute {
    void AddAbsoluteOptimizationPassesToPipeline(llvm::ModulePassManager& passes);

''',
    "optimization pipeline declaration",
)
replace_once(
    module,
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
