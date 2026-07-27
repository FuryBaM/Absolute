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

#if LLVM_VERSION_MAJOR >= 21
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
                    llvm::IRBuilder<> rewriteBuilder(branch);
                    rewriteBuilder.CreateBr(success);
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

    void AddAbsoluteOptimizationPassesToPipeline(llvm::ModulePassManager& passes) {
        AddAbsoluteOptimizationPasses(passes);
    }

    CodeGenerator::CodeGenerator(const Analyzer* analyzer)
        : impl(std::make_unique<Impl>(*this, analyzer)) {
    }

    CodeGenerator::~CodeGenerator() = default;

    std::string CodeGenerator::Generate(Program& program, const std::string& moduleName,
        const std::string& targetTriple) {
        return impl->Generate(program, moduleName, targetTriple);
    }

    void CodeGenerator::GenerateObject(
        Program& program, const std::string& moduleName, const std::string& outputPath,
        bool sanitizeAddress, const std::string& targetTriple) {
        impl->GenerateObject(program, moduleName, outputPath, sanitizeAddress, targetTriple);
    }
}
