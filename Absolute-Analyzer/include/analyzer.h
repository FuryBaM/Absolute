#pragma once

class ANALYZER_API Analyzer {
    std::vector<std::unique_ptr<Program>> programs;

public:
    explicit Analyzer(std::vector<std::unique_ptr<Program>> programs)
        : programs(std::move(programs)) {
    }

    void Analyze() {
        for (const auto& program : programs) {
            AnalyzeProgram(*program.get());
        }
    }

private:
    void AnalyzeProgram(const Program& program);
    void AnalyzeStatement(const Statement& stmt);
    void AnalyzeExpression(const Expression& expr);
    void AnalyzeAssignmentExpr(const AssignmentExpr& expr);
    void AnalyzeVarDeclExpr(const VarDeclExpr& expr);
};