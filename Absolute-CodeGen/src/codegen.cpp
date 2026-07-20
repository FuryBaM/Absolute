#include "codegen_internal.h"

#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/LazyValueInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Transforms/Scalar/DCE.h>
#include <llvm/Transforms/Scalar/SimplifyCFG.h>

namespace Absolute {
    namespace {
        bool IsArrayBoundsFailure(const llvm::BasicBlock* block) {
            return block && block->getName().find("array.bounds.failure") == 0 &&
                llvm::isa<llvm::UnreachableInst>(block->getTerminator());
        }

        bool IsKnownIntegerComparison(
            const llvm::ICmpInst& comparison,
            bool expected,
            llvm::Instruction* context,
            llvm::LazyValueInfo& lazyValues,
            llvm::ScalarEvolution& scalarEvolution) {
            const llvm::ICmpInst::Predicate predicate = expected
                ? comparison.getPredicate()
                : llvm::CmpInst::getInversePredicate(comparison.getPredicate());
            llvm::Value* left = comparison.getOperand(0);
            llvm::Value* right = comparison.getOperand(1);

            if (llvm::isa<llvm::Constant>(left) || llvm::isa<llvm::Constant>(right)) {
                llvm::Constant* result = lazyValues.getPredicateAt(
                    predicate, left, right, context, true);
                if (const auto* known = llvm::dyn_cast_or_null<llvm::ConstantInt>(result))
                    if (known->isOne()) return true;
            }

            if (!left->getType()->isIntegerTy() || left->getType() != right->getType())
                return false;
            return scalarEvolution.isKnownPredicateAt(
                predicate,
                scalarEvolution.getSCEV(left),
                scalarEvolution.getSCEV(right),
                context);
        }

        bool IsKnownBoolean(
            llvm::Value* value,
            bool expected,
            llvm::Instruction* context,
            llvm::LazyValueInfo& lazyValues,
            llvm::ScalarEvolution& scalarEvolution,
            unsigned depth = 0) {
            if (!value || depth > 12 || !value->getType()->isIntegerTy(1)) return false;
            if (const auto* constant = llvm::dyn_cast<llvm::ConstantInt>(value))
                return constant->isOne() == expected;

            llvm::Constant* known = lazyValues.getPredicateAt(
                llvm::CmpInst::ICMP_EQ,
                value,
                llvm::ConstantInt::get(value->getType(), expected),
                context,
                true);
            if (const auto* result = llvm::dyn_cast_or_null<llvm::ConstantInt>(known))
                if (result->isOne()) return true;

            if (const auto* comparison = llvm::dyn_cast<llvm::ICmpInst>(value)) {
                if (IsKnownIntegerComparison(
                    *comparison, expected, context, lazyValues, scalarEvolution)) return true;
            }

            const auto* binary = llvm::dyn_cast<llvm::BinaryOperator>(value);
            if (!binary) return false;
            llvm::Value* left = binary->getOperand(0);
            llvm::Value* right = binary->getOperand(1);
            switch (binary->getOpcode()) {
            case llvm::Instruction::And:
                if (expected)
                    return IsKnownBoolean(
                               left, true, context, lazyValues, scalarEvolution, depth + 1) &&
                        IsKnownBoolean(
                               right, true, context, lazyValues, scalarEvolution, depth + 1);
                return IsKnownBoolean(
                           left, false, context, lazyValues, scalarEvolution, depth + 1) ||
                    IsKnownBoolean(
                           right, false, context, lazyValues, scalarEvolution, depth + 1);
            case llvm::Instruction::Or:
                if (expected)
                    return IsKnownBoolean(
                               left, true, context, lazyValues, scalarEvolution, depth + 1) ||
                        IsKnownBoolean(
                               right, true, context, lazyValues, scalarEvolution, depth + 1);
                return IsKnownBoolean(
                           left, false, context, lazyValues, scalarEvolution, depth + 1) &&
                    IsKnownBoolean(
                           right, false, context, lazyValues, scalarEvolution, depth + 1);
            case llvm::Instruction::Xor:
                if (const auto* constant = llvm::dyn_cast<llvm::ConstantInt>(right)) {
                    if (constant->isOne())
                        return IsKnownBoolean(
                            left, !expected, context, lazyValues, scalarEvolution, depth + 1);
                    if (constant->isZero())
                        return IsKnownBoolean(
                            left, expected, context, lazyValues, scalarEvolution, depth + 1);
                }
                return false;
            default:
                return false;
            }
        }

        class ProvenArrayBoundsPass final
            : public llvm::PassInfoMixin<ProvenArrayBoundsPass> {
        public:
            llvm::PreservedAnalyses run(
                llvm::Function& function,
                llvm::FunctionAnalysisManager& analyses) {
                llvm::LazyValueInfo& lazyValues =
                    analyses.getResult<llvm::LazyValueAnalysis>(function);
                llvm::ScalarEvolution& scalarEvolution =
                    analyses.getResult<llvm::ScalarEvolutionAnalysis>(function);
                llvm::SmallVector<std::pair<llvm::BranchInst*, llvm::BasicBlock*>, 8> rewrites;

                for (llvm::BasicBlock& block : function) {
                    auto* branch = llvm::dyn_cast<llvm::BranchInst>(block.getTerminator());
                    if (!branch || !branch->isConditional()) continue;
                    llvm::BasicBlock* first = branch->getSuccessor(0);
                    llvm::BasicBlock* second = branch->getSuccessor(1);
                    const bool firstFailure = IsArrayBoundsFailure(first);
                    const bool secondFailure = IsArrayBoundsFailure(second);
                    if (firstFailure == secondFailure) continue;

                    const bool requiredCondition = secondFailure;
                    if (!IsKnownBoolean(branch->getCondition(), requiredCondition, branch,
                            lazyValues, scalarEvolution)) continue;
                    rewrites.emplace_back(branch, firstFailure ? second : first);
                }

                for (auto& [branch, success] : rewrites) {
                    llvm::BranchInst::Create(success, branch);
                    branch->eraseFromParent();
                }
                return rewrites.empty()
                    ? llvm::PreservedAnalyses::all()
                    : llvm::PreservedAnalyses::none();
            }
        };

        void AddAbsoluteOptimizationPasses(llvm::ModulePassManager& passes) {
            llvm::FunctionPassManager functions;
            functions.addPass(ProvenArrayBoundsPass());
            functions.addPass(llvm::SimplifyCFGPass());
            functions.addPass(llvm::DCEPass());
            passes.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(functions)));
        }
    }

    CodeGenerator::CodeGenerator(const Analyzer* analyzer)
        : impl(std::make_unique<Impl>(*this, analyzer)) {
    }

    CodeGenerator::~CodeGenerator() = default;

    std::string CodeGenerator::Generate(Program& program, const std::string& moduleName) {
        return impl->Generate(program, moduleName);
    }

    void CodeGenerator::GenerateObject(
        Program& program, const std::string& moduleName, const std::string& outputPath) {
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
        if (!target)
            throw std::runtime_error("cannot select target '" + triple + "': " + targetError);

        llvm::TargetOptions options;
        const std::string cpu = llvm::sys::getHostCPUName().str();
        std::unique_ptr<llvm::TargetMachine> targetMachine(target->createTargetMachine(
            triple, cpu.empty() ? "generic" : cpu, "", options, llvm::Reloc::PIC_,
            std::nullopt, llvm::CodeGenOptLevel::Aggressive));
        if (!targetMachine)
            throw std::runtime_error("cannot create target machine for '" + triple + "'");

        const std::string dataLayout =
            targetMachine->createDataLayout().getStringRepresentation();
        llvm::Module& generatedModule =
            impl->BuildModule(program, moduleName, triple, dataLayout);

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
        AddAbsoluteOptimizationPasses(optimizationPasses);
        optimizationPasses.run(generatedModule, moduleAnalyses);

        std::string verifierMessage;
        llvm::raw_string_ostream verifierStream(verifierMessage);
        if (llvm::verifyModule(generatedModule, &verifierStream)) {
            verifierStream.flush();
            throw std::runtime_error(
                "optimized module verification failed: " + verifierMessage);
        }

        std::error_code error;
        llvm::raw_fd_ostream output(outputPath, error, llvm::sys::fs::OF_None);
        if (error)
            throw std::runtime_error(
                "cannot create object file '" + outputPath + "': " + error.message());

        llvm::legacy::PassManager emissionPasses;
#if LLVM_VERSION_MAJOR >= 18
        constexpr llvm::CodeGenFileType fileType = llvm::CodeGenFileType::ObjectFile;
#else
        constexpr llvm::CodeGenFileType fileType = llvm::CGFT_ObjectFile;
#endif
        if (targetMachine->addPassesToEmitFile(
            emissionPasses, output, nullptr, fileType))
            throw std::runtime_error("target cannot emit an object file");
        emissionPasses.run(generatedModule);
        output.flush();
    }
}
