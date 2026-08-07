#include "parser_pch.h"
#include "parser.h"

namespace Absolute{
    std::unique_ptr<AssignmentExpr> Parser::ParseAssignmentExpr(std::unique_ptr<Expression> leftValue)
    {
        if (leftValue) {
		    Token* op = CurrentToken();
            if (op && GetOperatorCategory(op->value) == OperatorCategory::Assignment) {
                Consume(TokenType::OPERATOR); // Assignment operator
                std::unique_ptr<Expression> rightValue = ParseExpression();
                return std::make_unique<AssignmentExpr>(std::move(leftValue), std::move(op->value), std::move(rightValue));
            }
            ReportSyntaxError(op, "Expected assignment operator");
            throw std::runtime_error("Expected assignment operator");
            return nullptr;
        }
        return nullptr;
    }
}