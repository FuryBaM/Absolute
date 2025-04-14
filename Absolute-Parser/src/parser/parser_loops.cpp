#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<ForStmt> Parser::ParseForStmt() {
        if (!CurrentToken() || CurrentToken()->type != TokenType::KEYWORD || CurrentToken()->value != "for")
            return nullptr;
        Consume(TokenType::KEYWORD, "for"); // "for"
        Consume(TokenType::BRACKET, "("); // "("

        std::vector<std::unique_ptr<Expression>> init;
        if (CurrentToken()->type != TokenType::DELIMITER || CurrentToken()->value != ";") {
            do {
                if (CurrentToken()->type == TokenType::KEYWORD) {
                    init.push_back(ParseVarDeclExpr());
                }
                else {
                    init.push_back(ParseExpression());
                }
            } while (CurrentToken()->type == TokenType::DELIMITER && CurrentToken()->value == "," && Consume(TokenType::DELIMITER));
        }
	    Consume(TokenType::DELIMITER, ";"); // ";"
        std::unique_ptr<Expression> condition = nullptr;
        if (CurrentToken()->type != TokenType::DELIMITER || CurrentToken()->value != ";") {
            condition = ParseExpression();
        }
        Consume(TokenType::DELIMITER, ";"); // ";"

        std::vector<std::unique_ptr<Expression>> update;
        if (CurrentToken()->type != TokenType::BRACKET || CurrentToken()->value != ")") {
            do {
                update.push_back(ParseExpression());
            } while (CurrentToken()->type == TokenType::DELIMITER && CurrentToken()->value == "," && Consume(TokenType::DELIMITER));
        }
        Consume(TokenType::BRACKET, ")"); // ")"

        std::unique_ptr<Statement> body = ParseCompoundStatement();

        return std::make_unique<ForStmt>(std::move(init), std::move(condition), std::move(update), std::move(body));
    }

    std::unique_ptr<WhileStmt> Parser::ParseWhileStmt()
    {
        Consume(TokenType::KEYWORD, "while"); // "while"
        Consume(TokenType::BRACKET, "("); // "("
        std::unique_ptr<Expression> condition = ParseExpression();
        Consume(TokenType::BRACKET, ")"); // ")"

        if (!CurrentToken() || CurrentToken()->type != TokenType::BRACKET || CurrentToken()->value != "{") {
            ReportSyntaxError(CurrentToken(), "Expected '{' after while condition");
            std::exit(EXIT_FAILURE);
        }

        std::unique_ptr<Statement> body = ParseCompoundStatement();

        return std::make_unique<WhileStmt>(std::move(condition), std::move(body));
    }

    std::unique_ptr<DoWhileStmt> Parser::ParseDoWhileStmt()
    {
        Consume(TokenType::KEYWORD, "do");

        if (!CurrentToken() || CurrentToken()->type != TokenType::BRACKET || CurrentToken()->value != "{") {
            ReportSyntaxError(CurrentToken(), "Error: Expected '{' after 'do'");
            std::exit(EXIT_FAILURE);
        }

        std::unique_ptr<Statement> body = ParseStatement();

        if (!CurrentToken() || CurrentToken()->type != TokenType::KEYWORD || CurrentToken()->value != "while") {
            ReportSyntaxError(CurrentToken(), "Error: Expected 'while' after do-while body.");
            std::exit(EXIT_FAILURE);
        }

        Consume(TokenType::KEYWORD, "while");
        Consume(TokenType::BRACKET, "(");
        std::unique_ptr<Expression> condition = ParseExpression();
        Consume(TokenType::BRACKET, ")");
        Consume(TokenType::DELIMITER, ";");

        return std::make_unique<DoWhileStmt>(std::move(body), std::move(condition));
    }

    std::unique_ptr<ForEachStmt> Parser::ParseForEachStmt()
    {
        Consume(TokenType::KEYWORD, "foreach");
	    Consume(TokenType::BRACKET, "(");
	    std::unique_ptr<VarDeclExpr> variable = ParseVarDeclExpr();
	    Consume(TokenType::KEYWORD, "in");
	    std::unique_ptr<Expression> iterable = ParseExpression();
	    Consume(TokenType::BRACKET, ")");
	    std::unique_ptr<Statement> body = ParseStatement();
	    return std::make_unique<ForEachStmt>(std::move(variable), std::move(iterable), std::move(body));
    }
}