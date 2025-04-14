#include "analyzer_pch.h"
#include "analyzer.h"

namespace Absolute {
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

    void Analyzer::AnalyzeAssignmentExpr(const AssignmentExpr& expr) {
        std::cout << "It is assignment\n";
    }

    void Analyzer::AnalyzeVarDeclExpr(const VarDeclExpr& expr) {
        Expression* name = expr.name.get();
        Expression* type = expr.type.get();

        if (auto* identifier = dynamic_cast<const IdentifierExpr*>(name)) {
            if (auto* varType = dynamic_cast<const PrimitiveTypeExpr*>(type)) {
                if (variables.find(identifier->name) == variables.end()) { // Проверяем, что переменной **нет**
                    auto primType = PrimitiveStringToEnum(varType->type);
                    if (primType) {
                        variables.emplace(identifier->name, *primType); // Добавляем **Enum**, а не строку
                    }
                    else {
                        std::cerr << "Error: unknown type '" << varType->type << "'\n";
                    }
                }
                else {
                    std::cerr << "Error: variable '" << identifier->name << "' already exists!\n";
                }
            }
            else {
                std::cerr << "Error: expected primitive type, but received another.\n";
            }
        }
        else {
            std::cerr << "Error: expected identifier of primitive type.\n";
        }
    }
}
