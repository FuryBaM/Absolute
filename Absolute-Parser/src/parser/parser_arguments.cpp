#include "parser_pch.h"
#include "parser.h"

namespace Absolute{
    std::vector<std::unique_ptr<Expression>> Parser::ParseArguments()
    {
        Consume(TokenType::BRACKET, "("); // "("
        std::vector<std::unique_ptr<Expression>> arguments;
        while (!(RequireCurrent("an argument or ')'")->type == TokenType::BRACKET && CurrentToken()->value == ")"))
        {
            std::unique_ptr<Expression> argument = ParseExpression();
            arguments.push_back(std::move(argument));
            Token* current = RequireCurrent("',' or ')'");
            if (current->type == TokenType::DELIMITER && current->value == ",") {
                Consume(TokenType::DELIMITER, ",");
            }
            else if (CurrentToken()->type == TokenType::BRACKET && CurrentToken()->value == ")") {
                break;
            }
            else {
                ReportSyntaxError(CurrentToken(), "Expected ',' or ')' after argument");
                throw std::runtime_error("Invalid argument list");
            }
        }
        Consume(TokenType::BRACKET, ")"); // ")"
        return arguments;
    }
}
