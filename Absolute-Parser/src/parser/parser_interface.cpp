#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<InterfaceDeclStmt> Parser::ParseInterfaceDecl()
    {
        std::vector<Token> declarationModifiers = modifiers;
        std::vector<Attribute> declarationAttributes = attributes;
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
                ParseAttributes();
                ParseModifiers();
                if (!LooksLikeFunctionDeclaration()) {
                    ReportSyntaxError(CurrentToken(), "Interfaces may only contain method signatures");
                    throw std::runtime_error("Invalid interface member");
                }
                std::vector<Token> methodModifiers = modifiers;
                std::vector<Attribute> methodAttributes = attributes;
                std::unique_ptr<TypeExpr> returnType = ParseType();
                Token* methodName = Consume(TokenType::IDENTIFIER);
                auto parameters = ParseParameters();
                if (CurrentToken() && CurrentToken()->type == TokenType::KEYWORD &&
                    CurrentToken()->value == "const") {
                    methodModifiers.push_back(*Consume(TokenType::KEYWORD, "const"));
                }
                std::unique_ptr<Statement> body;
                if (CurrentToken() && CurrentToken()->type == TokenType::DELIMITER &&
                    CurrentToken()->value == ";") {
                    Consume(TokenType::DELIMITER, ";");
                }
                else if (CurrentToken() && CurrentToken()->type == TokenType::BRACKET &&
                    CurrentToken()->value == "{") {
                    body = ParseCompoundStatement();
                }
                else {
                    ReportSyntaxError(CurrentToken(),
                        "Expected ';' or a default interface method body");
                    throw std::runtime_error("Invalid interface method declaration");
                }
                auto method = std::make_unique<FunctionDeclStmt>(
                    std::move(returnType), std::make_unique<Token>(*methodName),
                    std::move(parameters), std::move(body));
                method->modifiers = std::move(methodModifiers);
                method->attributes = std::move(methodAttributes);
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
        statement->attributes = std::move(declarationAttributes);
        return statement;
    }
}
