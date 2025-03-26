#include "analyzer_pch.h"
#include "analyzer.h"

void Analyzer::AnalyzeProgram(const Program& program) {
    for (const std::unique_ptr<Statement>& stmt : program.statements) {
        if (stmt) {
            AnalyzeStatement(*stmt);
        }
    }
}

void Analyzer::AnalyzeStatement(const Statement& stmt)
{
    if (auto* single = dynamic_cast<const SingleStatement*>(&stmt)) {
        AnalyzeExpression(*single->expr);
    }
    else if (auto* assignment = dynamic_cast<const AssignmentStmt*>(&stmt)) {
        AnalyzeAssignmentExpr(*assignment->expr);
    }
    else if (auto* vardecl = dynamic_cast<const VarDeclStmt*>(&stmt)) {
        AnalyzeVarDeclExpr(*vardecl->expr);
    }
}

void Analyzer::AnalyzeExpression(const Expression& expr) {
    if (auto* assignment = dynamic_cast<const AssignmentExpr*>(&expr)) {
        AnalyzeAssignmentExpr(*assignment);
    }
    else if (auto* assignment = dynamic_cast<const VarDeclExpr*>(&expr)) {
        AnalyzeVarDeclExpr(*assignment);
    }
}

void Analyzer::AnalyzeAssignmentExpr(const AssignmentExpr& expr){
    std::cout << "It is assignment\n";
}

void Analyzer::AnalyzeVarDeclExpr(const VarDeclExpr& expr){
    std::cout << "It is var declaration\n";
}
