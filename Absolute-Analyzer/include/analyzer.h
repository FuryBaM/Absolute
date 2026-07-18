#pragma once
#include "variable.h"

namespace Absolute {
    class ANALYZER_API SymbolTable {
    public:
        std::unordered_map<std::string, std::unique_ptr<Type>> variables;
    };

    class ANALYZER_API Analyzer {
        std::vector<std::unique_ptr<Program>> programs;
        SymbolTable table;

    public:
        explicit Analyzer(std::vector<std::unique_ptr<Program>> programs)
            : programs(std::move(programs)) {
        }

        void Analyze() {
            for (const auto& program : programs) {
                if (program) AnalyzeProgram(*program);
            }
        }
        void PrintVariables() {
            for (const auto& [name, varType] : table.variables) {
                if (auto primitive = dynamic_cast<const PrimitiveType*>(varType.get())){
                    std::cout << name << " " << PrimitiveEnumToString(primitive->type) << "\n";
                }
                else if (auto user = dynamic_cast<const UserType*>(varType.get())) {
                    std::cout << name << " " << user->name << "\n";
                }
                else {
                    std::cout << name << " unknown type\n";
                }
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
