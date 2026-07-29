#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<VarDeclExpr> Parser::ParseVarDeclExpr()
    {
        const Token* start = CurrentToken();
        std::unique_ptr<TypeExpr> type = ParseType();
        if (CurrentToken() && CurrentToken()->type == TokenType::OPERATOR &&
            CurrentToken()->value == "&") {
            ReportSyntaxError(CurrentToken(), "'&' references are only supported for parameters");
            throw std::runtime_error("Value reference outside parameter list");
        }

        // Обрабатываем `*` и `&` перед именем переменной
        std::unique_ptr<Expression> nameExpr = ParsePrimaryExpr();

        // Объявление переменной без инициализации
        if (IsEndOfStatement(*CurrentToken()) || CurrentToken()->type == TokenType::KEYWORD) {
            auto declaration = std::make_unique<VarDeclExpr>(
                std::move(type), std::move(nameExpr), nullptr);
            if (start) {
                declaration->sourceFile = sourceFile;
                declaration->line = start->line;
                declaration->column = start->column;
            }
            return declaration;
        }
        // Объявление переменной с инициализацией
        else if (CurrentToken()->type == TokenType::OPERATOR) {
            Consume(TokenType::OPERATOR);
            std::unique_ptr<Expression> value = ParseExpression();
            auto declaration = std::make_unique<VarDeclExpr>(
                std::move(type), std::move(nameExpr), std::move(value));
            if (start) {
                declaration->sourceFile = sourceFile;
                declaration->line = start->line;
                declaration->column = start->column;
            }
            return declaration;
        }
        std::exit(EXIT_FAILURE);
        return nullptr;
    }

    std::unique_ptr<VarDeclStmt> Parser::ParseVarDeclaration() {
        std::unique_ptr<VarDeclExpr> variableDeclaration = ParseVarDeclExpr();
        variableDeclaration->isConst = std::any_of(modifiers.begin(), modifiers.end(),
            [](const Token& modifier) { return modifier.value == "const"; });
        variableDeclaration->isStatic = std::any_of(modifiers.begin(), modifiers.end(),
            [](const Token& modifier) { return modifier.value == "static"; });
        Consume(TokenType::DELIMITER, ";");  // Теперь `;` съедается здесь
        if (variableDeclaration) {
            auto stmt = std::make_unique<VarDeclStmt>(std::move(variableDeclaration));
            stmt->modifiers = modifiers;
            stmt->attributes = attributes;
            return stmt;
        }
        return nullptr;
    }
}
