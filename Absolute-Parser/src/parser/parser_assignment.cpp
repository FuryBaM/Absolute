#include "parser_pch.h"
#include "parser.h"

namespace Absolute{
    std::unique_ptr<AssignmentExpr> Parser::ParseAssignmentExpr(std::unique_ptr<Expression> leftValue)
    {
        if (leftValue) {
		    Token* op = CurrentToken();
            if (op && GetOperatorCategory(op->value) == OperatorCategory::Assignment) {
                const int line = op->line;
                const int column = op->column;
                Consume(TokenType::OPERATOR); // Assignment operator
                std::unique_ptr<Expression> rightValue = ParseExpression();
                auto assignment = std::make_unique<AssignmentExpr>(
                    std::move(leftValue), std::move(op->value), std::move(rightValue));
                // Without a location every assignment diagnostic -- a type
                // mismatch, an unassignable target, an ownership violation --
                // printed as a bare message with no file, line or column.
                assignment->sourceFile = sourceFile;
                assignment->line = line;
                assignment->column = column;
                return assignment;
            }
            ReportSyntaxError(op, "Expected assignment operator");
            throw std::runtime_error("Expected assignment operator");
            return nullptr;
        }
        return nullptr;
    }
}