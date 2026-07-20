#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<Statement> Parser::ParseIdentifier()
    {
	    Token* token = CurrentToken();

        if (token->type == TokenType::IDENTIFIER) {
            Token* nextToken = PeekToken();
            ScopeType currentScope = GetCurrentScopeType();
            bool inStructure = currentScope == ScopeType::Class || currentScope == ScopeType::Struct;
            if (inStructure && nextToken && nextToken->value == "(" && token->value == GetCurrentScopeName()) {
                return ParseConstructor();
            }
            else {
                std::unique_ptr<Expression> expr = ParsePrimaryExpr();
                Token* current = CurrentToken();
                if (current && GetOperatorCategory(current->value) == OperatorCategory::Assignment) {
                    expr = ParseAssignmentExpr(std::move(expr));
                }
                Consume(TokenType::DELIMITER, ";");
                auto stmt = std::make_unique<SingleStatement>(std::move(expr));
                stmt->modifiers = modifiers;
                stmt->attributes = attributes;
                return stmt;
            }
        }
        ReportSyntaxError(token, "Expected identifier token");
        std::exit(EXIT_FAILURE);
        return nullptr;
    }

    std::unique_ptr<Expression> Parser::ParseIdentifierExpr(bool allowTemplate) {
        Token* identifier = CurrentToken();
        if (!identifier || identifier->type != TokenType::IDENTIFIER) {
            ReportSyntaxError(identifier, "Expected identifier");
            std::exit(EXIT_FAILURE);
            return nullptr;
        }

        std::unique_ptr<Expression> expr = std::make_unique<IdentifierExpr>(identifier->value);
        Consume(TokenType::IDENTIFIER);

        while (true) {
            Token* next = CurrentToken();
            if (!next) break;

            if (next->value == "(") {
                expr = ParseFunctionCallExpr(std::move(expr));
            }
            else if (next->value == "[") {
                expr = ParseArrayAccess(std::move(expr));
            }
            else if (next->value == ".") {
                expr = ParseMemberAccess(std::move(expr));
            }
            else if (next->value == "<") {
                size_t close = 0;
                if (!IsTemplateArgumentList(pos, &close)) break;

                Token* afterTemplate = close + 1 < tokens.size() ? &tokens[close + 1] : nullptr;
                const bool followedByPostfix = afterTemplate &&
                    (afterTemplate->value == "(" || afterTemplate->value == "." || afterTemplate->value == "[");
                if (!allowTemplate && !followedByPostfix) break;

                expr = std::make_unique<UserTypeExpr>(std::move(expr));
                expr = ParseTemplateExpr(std::move(expr));
            }
            else {
                break;
            }
        }

        return expr;
    }
}
