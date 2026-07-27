#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    if old not in text:
        raise SystemExit(f"expected block not found in {path}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


codegen = Path("Absolute-CodeGen/src/codegen.cpp")
text = codegen.read_text(encoding="utf-8")

text = text.replace(
    '#include <llvm/Transforms/Scalar/SimplifyCFG.h>\n',
    '#include <llvm/Transforms/Scalar/SimplifyCFG.h>\n'
    '#include <llvm/Transforms/Utils/BasicBlockUtils.h>\n',
    1,
)

replace_once(
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
)

# Re-read after the first replacement because replace_once writes the file.
text = codegen.read_text(encoding="utf-8")
marker = '''        void AddAbsoluteOptimizationPasses(llvm::ModulePassManager& passes) {
            llvm::FunctionPassManager functions;
            functions.addPass(ProvenArrayBoundsPass());
            functions.addPass(llvm::SimplifyCFGPass());
            functions.addPass(llvm::DCEPass());
            passes.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(functions)));
        }
    }
'''
replacement = marker + '''
    void AddAbsoluteOptimizationPassesToPipeline(llvm::ModulePassManager& passes) {
        AddAbsoluteOptimizationPasses(passes);
    }
'''
if marker not in text:
    raise SystemExit("optimization pass marker not found")
text = text.replace(marker, replacement, 1)

orphan_start = text.find('''
        const std::string dataLayout =
            targetMachine->createDataLayout().getStringRepresentation();
''')
if orphan_start == -1:
    raise SystemExit("orphaned GenerateObject tail not found")
namespace_close = text.rfind('\n}')
if namespace_close == -1 or namespace_close <= orphan_start:
    raise SystemExit("namespace close not found")
text = text[:orphan_start] + text[namespace_close:]
codegen.write_text(text, encoding="utf-8")

module = Path("Absolute-CodeGen/src/codegen_module.cpp")
module_text = module.read_text(encoding="utf-8")n