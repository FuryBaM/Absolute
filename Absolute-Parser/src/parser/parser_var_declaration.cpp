#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<VarDeclExpr> Parser::ParseVarDeclExpr()
    {
        std::unique_ptr<TypeExpr> type = ParseType();

        // Обрабатываем `*` и `&` перед именем переменной
        std::unique_ptr<Expression> nameExpr = ParsePrimaryExpr();

        // Объявление переменной без инициализации
        if (IsEndOfStatement(*CurrentToken()) || CurrentToken()->type == TokenType::KEYWORD) {
            return std::make_unique<VarDeclExpr>(std::move(type), std::move(nameExpr), nullptr);
        }
        // Объявление переменной с инициализацией
        else if (CurrentToken()->type == TokenType::OPERATOR) {
            Consume(TokenType::OPERATOR);
            std::unique_ptr<Expression> value = ParseExpression();
            return std::make_unique<VarDeclExpr>(std::move(type), std::move(nameExpr), std::move(value));
        }
        std::exit(EXIT_FAILURE);
        return nullptr;
    }

    std::unique_ptr<VarDeclStmt> Parser::ParseVarDeclaration() {
        std::unique_ptr<VarDeclExpr> variableDeclaration = ParseVarDeclExpr();
        Consume(TokenType::DELIMITER, ";");  // Теперь `;` съедается здесь
        if (variableDeclaration) {
            auto stmt = std::make_unique<VarDeclStmt>(std::move(variableDeclaration));
            stmt->modifiers = modifiers;
            return stmt;
        }
        return nullptr;
    }
}
