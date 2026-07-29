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
            const Token* parameterStart = CurrentToken();
            const bool isParams = CurrentToken()->type == TokenType::KEYWORD &&
                CurrentToken()->value == "params";
            if (isParams) Consume(TokenType::KEYWORD, "params");
            const bool isConst = CurrentToken()->type == TokenType::KEYWORD &&
                CurrentToken()->value == "const";
            if (isConst) Consume(TokenType::KEYWORD, "const");
            const bool refAlias = CurrentToken() &&
                CurrentToken()->type == TokenType::KEYWORD && CurrentToken()->value == "ref";
            if (refAlias) Consume(TokenType::KEYWORD, "ref");
            const bool outAlias = CurrentToken() &&
                ((CurrentToken()->type == TokenType::KEYWORD || CurrentToken()->type == TokenType::IDENTIFIER) &&
                 CurrentToken()->value == "out");
            if (outAlias) Consume(CurrentToken()->type);
            std::unique_ptr<TypeExpr> type = ParseType();
            const bool ampersandReference = CurrentToken() &&
                CurrentToken()->type == TokenType::OPERATOR && CurrentToken()->value == "&";
            if (ampersandReference) Consume(TokenType::OPERATOR, "&");
            if (refAlias && ampersandReference) {
                ReportSyntaxError(CurrentToken(),
                    "A parameter reference must use either '&' or the 'ref' alias, not both");
                throw std::runtime_error("Duplicate parameter reference marker");
            }
            const bool isReference = refAlias || ampersandReference || outAlias;
            std::unique_ptr<Expression> nameExpr = ParsePrimaryExpr();

            Token* current = RequireCurrent("'=', ',' or ')'");
            if (current->type == TokenType::OPERATOR && current->value == "=") {
                Consume(TokenType::OPERATOR, "=");
                std::unique_ptr<Expression> value = ParseExpression();
                auto parameter = std::make_unique<VarDeclExpr>(
                    std::move(type), std::move(nameExpr), std::move(value));
                parameter->isConst = isConst;
                parameter->isReference = isReference;
                parameter->isOut = outAlias;
                parameter->isParams = isParams;
                if (parameterStart) {
                    parameter->sourceFile = sourceFile;
                    parameter->line = parameterStart->line;
                    parameter->column = parameterStart->column;
                }
                parameters.push_back(std::move(parameter));
            }
            else {
                auto parameter = std::make_unique<VarDeclExpr>(
                    std::move(type), std::move(nameExpr), nullptr);
                parameter->isConst = isConst;
                parameter->isReference = isReference;
                parameter->isOut = outAlias;
                parameter->isParams = isParams;
                if (parameterStart) {
                    parameter->sourceFile = sourceFile;
                    parameter->line = parameterStart->line;
                    parameter->column = parameterStart->column;
                }
                parameters.push_back(std::move(parameter));
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
