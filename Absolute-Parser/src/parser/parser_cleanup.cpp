#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<DeferStmt> Parser::ParseDeferStmt() {
        Consume(TokenType::KEYWORD, "defer");
        if (CurrentToken() && CurrentToken()->type == TokenType::BRACKET &&
            CurrentToken()->value == "{") {
            return std::make_unique<DeferStmt>(ParseCompoundStatement());
        }

        auto statement = ParseStatement();
        if (!statement) {
            ReportSyntaxError(CurrentToken(), "Expected a block or statement after 'defer'");
            throw std::runtime_error("Invalid defer statement");
        }
        std::vector<std::unique_ptr<Statement>> statements;
        statements.push_back(std::move(statement));
        return std::make_unique<DeferStmt>(
            std::make_unique<CompoundStmt>(std::move(statements)));
    }
}
