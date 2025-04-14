#include "parser_pch.h"
#include "parser.h"

namespace Absolute{
    std::vector<std::unique_ptr<VarDeclExpr>> Parser::ParseParameters()
    {
        if (CurrentToken()->value != "(") {
            ReportSyntaxError(CurrentToken(), "Expected '(' in parameters");
            std::exit(EXIT_FAILURE);
        }
        Consume(TokenType::BRACKET, "("); // "("

        std::vector<std::unique_ptr<VarDeclExpr>> parameters;
        while (CurrentToken()->type != TokenType::BRACKET && CurrentToken()->value != ")")
        {
            std::unique_ptr<PrimitiveTypeExpr> type = ParsePrimitiveType();
            std::unique_ptr<Expression> nameExpr = ParsePrimaryExpr();

            if (CurrentToken()->type == TokenType::OPERATOR && CurrentToken()->value == "=") {
                Consume(TokenType::OPERATOR, "=");
                std::unique_ptr<Expression> value = ParseExpression();
                parameters.push_back(std::make_unique<VarDeclExpr>(std::move(type), std::move(nameExpr), std::move(value)));
            }
            else {
                parameters.push_back(std::make_unique<VarDeclExpr>(std::move(type), std::move(nameExpr), nullptr));
            }

            if (CurrentToken()->type == TokenType::DELIMITER && CurrentToken()->value == ",") {
                Consume(TokenType::DELIMITER, ",");
            }
            else if (CurrentToken()->type == TokenType::BRACKET && CurrentToken()->value == ")") {
                break;
            }
        }
        Consume(TokenType::BRACKET, ")"); // ")"
        return parameters;
    }
}