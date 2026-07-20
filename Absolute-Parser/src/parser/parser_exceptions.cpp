#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<ThrowStmt> Parser::ParseThrowStmt() {
        Consume(TokenType::KEYWORD, "throw");
        std::unique_ptr<Expression> value;
        if (!CurrentToken() || !IsEndOfStatement(*CurrentToken())) value = ParseExpression();
        Consume(TokenType::DELIMITER, ";");
        return std::make_unique<ThrowStmt>(std::move(value));
    }

    std::unique_ptr<TryStmt> Parser::ParseTryStmt() {
        Consume(TokenType::KEYWORD, "try");
        if (!CurrentToken() || CurrentToken()->value != "{") {
            ReportSyntaxError(CurrentToken(), "Expected a compound body after 'try'");
            throw std::runtime_error("Invalid try statement");
        }
        auto body = ParseCompoundStatement();
        std::vector<TryStmt::CatchClause> catches;
        std::unique_ptr<CompoundStmt> finallyBody;

        while (CurrentToken() && CurrentToken()->type == TokenType::KEYWORD &&
            CurrentToken()->value == "catch") {
            Consume(TokenType::KEYWORD, "catch");
            Consume(TokenType::BRACKET, "(");
            auto type = ParseType();
            Token* name = Consume(TokenType::IDENTIFIER);
            Consume(TokenType::BRACKET, ")");
            if (!CurrentToken() || CurrentToken()->value != "{") {
                ReportSyntaxError(CurrentToken(), "Expected a compound body after 'catch'");
                throw std::runtime_error("Invalid catch clause");
            }
            auto parameter = std::make_unique<VarDeclExpr>(std::move(type),
                std::make_unique<IdentifierExpr>(name->value), nullptr);
            catches.emplace_back(std::move(parameter), ParseCompoundStatement());
        }

        if (CurrentToken() && CurrentToken()->type == TokenType::KEYWORD &&
            CurrentToken()->value == "finally") {
            Consume(TokenType::KEYWORD, "finally");
            if (!CurrentToken() || CurrentToken()->value != "{") {
                ReportSyntaxError(CurrentToken(), "Expected a compound body after 'finally'");
                throw std::runtime_error("Invalid finally clause");
            }
            finallyBody = ParseCompoundStatement();
        }
        if (catches.empty() && !finallyBody) {
            ReportSyntaxError(CurrentToken(), "A try statement requires catch or finally");
            throw std::runtime_error("Incomplete try statement");
        }
        return std::make_unique<TryStmt>(
            std::move(body), std::move(catches), std::move(finallyBody));
    }
}
