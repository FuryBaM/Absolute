#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<IfStmt> Parser::ParseIfStmt()
    {
        Token* token = CurrentToken();
        if (token && token->type == TokenType::KEYWORD && token->value == "if") {
            Consume(TokenType::KEYWORD, "if");
            Consume(TokenType::BRACKET, "("); // "("
            std::unique_ptr<Expression> condition = ParseExpression();
            Consume(TokenType::BRACKET, ")"); // ")"

            std::vector<IfStmt::Branch> branches;
            branches.emplace_back(std::move(condition), ParseCompoundStatement());

            std::unique_ptr<Statement> elseBranch = nullptr;

            while (CurrentToken() && CurrentToken()->type == TokenType::KEYWORD && CurrentToken()->value == "else") {
                Consume(TokenType::KEYWORD, "else");
                if (CurrentToken()->type == TokenType::KEYWORD && CurrentToken()->value == "if") {
                    // `else if`
                    Consume(TokenType::KEYWORD, "if");
                    Consume(TokenType::BRACKET, "("); // "("
                    std::unique_ptr<Expression> elseCondition = ParseExpression();
                    Consume(TokenType::BRACKET, ")"); // ")"
                    branches.emplace_back(std::move(elseCondition), ParseCompoundStatement());
                }
                else {
                    // `else`
                    elseBranch = ParseCompoundStatement();
                    break; // `else` — последняя ветка, выходим из цикла
                }
            }

            return std::make_unique<IfStmt>(std::move(branches), std::move(elseBranch));
        }
        return nullptr;
    }
}