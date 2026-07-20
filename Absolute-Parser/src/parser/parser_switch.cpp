#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<SwitchStmt> Parser::ParseSwitchStmt(bool exhaustive) {
        Consume(TokenType::KEYWORD, exhaustive ? "match" : "switch");
        Consume(TokenType::BRACKET, "(");
        auto value = ParseExpression();
        Consume(TokenType::BRACKET, ")");
        Consume(TokenType::BRACKET, "{");

        std::vector<SwitchStmt::Case> cases;
        std::unique_ptr<CompoundStmt> defaultCase;
        bool sawDefault = false;

        auto parseCaseBody = [&]() {
            std::vector<std::unique_ptr<Statement>> statements;
            while (CurrentToken() && CurrentToken()->value != "}" &&
                !(CurrentToken()->type == TokenType::KEYWORD &&
                    (CurrentToken()->value == "case" || CurrentToken()->value == "default"))) {
                const size_t statementStart = pos;
                auto statement = ParseStatement();
                if (statement) statements.push_back(std::move(statement));
                else if (pos == statementStart) {
                    ReportSyntaxError(CurrentToken(), "Unexpected token in switch case");
                    throw std::runtime_error("Parser made no progress in switch case");
                }
            }
            return std::make_unique<CompoundStmt>(std::move(statements));
        };

        while (CurrentToken() && CurrentToken()->value != "}") {
            if (CurrentToken()->type != TokenType::KEYWORD) {
                ReportSyntaxError(CurrentToken(), "Expected 'case' or 'default'");
                throw std::runtime_error("Invalid switch branch");
            }
            if (CurrentToken()->value == "case") {
                if (sawDefault) {
                    ReportSyntaxError(CurrentToken(), "A case cannot appear after default");
                    throw std::runtime_error("Invalid switch branch order");
                }
                Consume(TokenType::KEYWORD, "case");
                auto label = ParseExpression();
                Consume(TokenType::OPERATOR, ":");
                cases.emplace_back(std::move(label), parseCaseBody());
                continue;
            }
            if (CurrentToken()->value == "default") {
                if (sawDefault) {
                    ReportSyntaxError(CurrentToken(), "A switch can only have one default branch");
                    throw std::runtime_error("Duplicate default branch");
                }
                sawDefault = true;
                Consume(TokenType::KEYWORD, "default");
                Consume(TokenType::OPERATOR, ":");
                defaultCase = parseCaseBody();
                continue;
            }
            ReportSyntaxError(CurrentToken(), "Expected 'case' or 'default'");
            throw std::runtime_error("Invalid switch branch");
        }

        if (!CurrentToken()) {
            ReportSyntaxError(nullptr, "Unexpected end of file; expected '}' after switch");
            throw std::runtime_error("Unterminated switch statement");
        }
        Consume(TokenType::BRACKET, "}");
        return std::make_unique<SwitchStmt>(
            std::move(value), std::move(cases), std::move(defaultCase), exhaustive);
    }
}
