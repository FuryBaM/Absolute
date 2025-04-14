#pragma once
#include "variable.h"

namespace Absolute {
    class ANALYZER_API Analyzer {
        std::vector<std::unique_ptr<Program>> programs;
        std::unordered_map<std::string, PrimitiveTypeEnum> variables;

    public:
        explicit Analyzer(std::vector<std::unique_ptr<Program>> programs)
            : programs(std::move(programs)) {
        }

        void Analyze() {
            for (const auto& program : programs) {
                AnalyzeProgram(*program.get());
            }
        }
        void PrintVariables() {
            for (const auto& var : variables) {
                std::cout << var.first << " " << PrimitiveEnumToString(var.second) << "\n";
            }
        }
    private:
        void AnalyzeProgram(const Program& program);
        void AnalyzeStatement(const Statement& stmt);
        void AnalyzeExpression(const Expression& expr);
        void AnalyzeAssignmentExpr(const AssignmentExpr& expr);
        void AnalyzeVarDeclExpr(const VarDeclExpr& expr);
    };
}