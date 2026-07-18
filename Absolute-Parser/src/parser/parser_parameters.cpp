#include "parser_pch.h"
#include "parser.h"

namespace Absolute{
    std::vector<std::unique_ptr<VarDeclExpr>> Parser::ParseParameters()
    {
        if (!CurrentToken() || CurrentToken()->value != "(") {
            ReportSyntaxError(CurrentToken(), "Expected '(' in parameters");
            std::exit(EXIT_FAILURE);
        }
        Consume(TokenType::BRACKET, "("); // "("

        std::vector<std::unique_ptr<VarDeclExpr>> parameters;
        while (!(RequireCurrent("a parameter or ')'")->type == TokenType::BRACKET && CurrentToken()->value == ")"))
        {
            std::unique_ptr<PrimitiveTypeExpr> type = ParsePrimitiveType();
            std::unique_ptr<Expression> nameExpr = ParsePrimaryExpr();

            Token* current = RequireCurrent("'=', ',' or ')'");
            if (current->type == TokenType::OPERATOR && current->value == "=") {
                Consume(TokenType::OPERATOR, "=");
                std::unique_ptr<Expression> value = ParseExpression();
                parameters.push_back(std::make_unique<VarDeclExpr>(std::move(type), std::move(nameExpr), std::move(value)));
            }
            else {
                parameters.push_back(std::make_unique<VarDeclExpr>(std::move(type), std::move(nameExpr), nullptr));
            }

            current = RequireCurrent("',' or ')'");
            if (current->type == TokenType::DELIMITER && current->value == ",") {
                Consume(TokenType::DELIMITER, ",");
            }
            else if (CurrentToken()->type == TokenType::BRACKET && CurrentToken()->value == ")") {
                break;
            }
            else {
                ReportSyntaxError(CurrentToken(), "Expected ',' or ')' after parameter");
                throw std::runtime_error("Invalid parameter list");
            }
        }
        Consume(TokenType::BRACKET, ")"); // ")"
        return parameters;
    }
}
