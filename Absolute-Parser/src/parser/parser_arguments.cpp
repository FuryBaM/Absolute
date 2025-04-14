#include "parser_pch.h"
#include "parser.h"

namespace Absolute{
    std::vector<std::unique_ptr<Expression>> Parser::ParseArguments()
    {
        Consume(TokenType::BRACKET, "("); // "("
        std::vector<std::unique_ptr<Expression>> arguments;
        while (CurrentToken()->type != TokenType::BRACKET && CurrentToken()->value != ")")
        {
            std::unique_ptr<Expression> argument = ParseExpression();
            arguments.push_back(std::move(argument));
            if (CurrentToken()->type == TokenType::DELIMITER && CurrentToken()->value == ",") {
                Consume(TokenType::DELIMITER, ",");
            }
            else if (CurrentToken()->type == TokenType::BRACKET && CurrentToken()->value == ")") {
                break;
            }
        }
        Consume(TokenType::BRACKET, ")"); // ")"
        return arguments;
    }
}