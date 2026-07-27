#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, description: str) -> str:
    if old not in text:
        raise SystemExit(f"expected block not found: {description}")
    return text.replace(old, new, 1)


codegen_path = Path("Absolute-CodeGen/src/codegen.cpp")
codegen = codegen_path.read_text(encoding="utf-8")

codegen = replace_once(
    codegen,
    '#include <llvm/Transforms/Scalar/SimplifyCFG.h>\n',
    '#include <llvm/Transforms/Scalar/SimplifyCFG.h>\n'
    '#include <llvm/Transforms/Utils/BasicBlockUtils.h>\n',
    "BasicBlockUtils include",
)

codegen = replace_once(
    codegen,
    '''                for (auto& [branch, success] : rewrites) {
                    llvm::BranchInst::Create(success, branch);
                    branch->eraseFromParent();
                }
''',
    '''                for (auto& [branch, success] : rewrites) {
                    llvm::ReplaceInstWithInst(branch, llvm::BranchInst::Create(success));
                }
''',
    "LLVM 21 branch replacement",
)

marker = '''        void AddAbsoluteOptimizationPasses(llvm::ModulePassManager& passes) {
            llvm::FunctionPassManager functions;
            functions.addPass(ProvenArrayBoundsPass());
            functions.addPass(llvm::SimplifyCFGPass());
            functions.addPass(llvm::DCEPass());
            passes.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(functions)));
        }
    }
'''
codegen = replace_once(
    codegen,
    marker,
    marker + '''
    void AddAbsoluteOptimizationPassesToPipeline(llvm::ModulePassManager& passes) {
        AddAbsoluteOptimizationPasses(passes);
    }
''',
    "optimization pass bridge",
)

orphan_start = codegen.find('''
        const std::string dataLayout =
            targetMachine->createDataLayout().getStringRepresentation();
''')
if orphan_start == -1:
    raise SystemExit("orphaned GenerateObject tail not found")
namespace_close = codegen.rfind('\n}')
if namespace_close == -1 or namespace_close <= orphan_start:
    raise SystemExit("namespace close not found")
codegen = codegen[:orphan_start] + codegen[namespace_close:]
codegen_path.write_text(codegen, encoding="utf-8")

module_path = Path("Absolute-CodeGen/src/codegen_module.cpp")
module = module_path.read_text(encoding="utf-8")
module = replace_once(
    module,
    '''namespace Absolute {
''',
    '''namespace Absolute {
    void AddAbsoluteOptimizationPassesToPipeline(llvm::ModulePassManager& passes);

''',
    "optimization pass declaration",
)
module = replace_once(
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
    "optimization pass pipeline hook",
)
module_path.write_text(module, encoding="utf-8")
