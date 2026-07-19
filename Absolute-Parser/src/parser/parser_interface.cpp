#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<InterfaceDeclStmt> Parser::ParseInterfaceDecl()
    {
        std::vector<Token> declarationModifiers = modifiers;
        std::vector<std::string> parents;
        Consume(TokenType::KEYWORD, "interface");
        Token* identifier = Consume(TokenType::IDENTIFIER);

        if (CurrentToken() && CurrentToken()->value == ":") {
            Consume(TokenType::OPERATOR, ":");
            while (true) {
                parents.push_back(Consume(TokenType::IDENTIFIER)->value);
                if (CurrentToken() && CurrentToken()->value == ",") {
                    Consume(TokenType::DELIMITER, ",");
                    continue;
                }
                break;
            }
        }

        Consume(TokenType::BRACKET, "{");
        EnterScope(ScopeType::Interface, identifier->value);
        std::vector<std::unique_ptr<FunctionDeclStmt>> methods;
        try {
            while (CurrentToken() && !(CurrentToken()->type == TokenType::BRACKET &&
                CurrentToken()->value == "}")) {
                ParseModifiers();
                if (!LooksLikeFunctionDeclaration()) {
                    ReportSyntaxError(CurrentToken(), "Interfaces may only contain method signatures");
                    throw std::runtime_error("Invalid interface member");
                }
                std::vector<Token> methodModifiers = modifiers;
                std::unique_ptr<TypeExpr> returnType = ParseType();
                Token* methodName = Consume(TokenType::IDENTIFIER);
                auto parameters = ParseParameters();
                Consume(TokenType::DELIMITER, ";");
                auto method = std::make_unique<FunctionDeclStmt>(
                    std::move(returnType), std::make_unique<Token>(*methodName),
                    std::move(parameters), nullptr);
                method->modifiers = std::move(methodModifiers);
                methods.push_back(std::move(method));
            }
            if (!CurrentToken()) {
                ReportSyntaxError(nullptr, "Unexpected end of file; expected '}'");
                throw std::runtime_error("Unterminated interface declaration");
            }
            Consume(TokenType::BRACKET, "}");
        }
        catch (...) {
            ExitScope();
            throw;
        }
        ExitScope();

        auto statement = std::make_unique<InterfaceDeclStmt>(
            identifier->value, std::move(parents), std::move(methods));
        statement->modifiers = std::move(declarationModifiers);
        return statement;
    }
}
