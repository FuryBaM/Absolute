#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    bool Parser::LooksLikeFunctionDeclaration() const
    {
        size_t index = pos;
        if (index < tokens.size() && tokens[index].type == TokenType::KEYWORD && tokens[index].value == "raw")
            ++index;
        if (index >= tokens.size()) return false;
        if (tokens[index].type == TokenType::KEYWORD && IsPrimitiveType(tokens[index].value)) {
            ++index;
        }
        else if (tokens[index].type == TokenType::IDENTIFIER) {
            ++index;
            while (index + 1 < tokens.size() && tokens[index].value == "." &&
                tokens[index + 1].type == TokenType::IDENTIFIER) index += 2;
        }
        else return false;
        while (index < tokens.size() && tokens[index].type == TokenType::OPERATOR && tokens[index].value == "*")
            ++index;
        while (index + 1 < tokens.size() && tokens[index].type == TokenType::BRACKET &&
            tokens[index].value == "[" && tokens[index + 1].type == TokenType::BRACKET &&
            tokens[index + 1].value == "]") index += 2;
        if (index >= tokens.size() || tokens[index].type != TokenType::IDENTIFIER) return false;
        ++index;
        if (index < tokens.size() && tokens[index].value == "<") {
            size_t close = 0;
            if (!IsTemplateArgumentList(index, &close)) return false;
            index = close + 1;
        }
        return index < tokens.size() && tokens[index].type == TokenType::BRACKET &&
            tokens[index].value == "(";
    }

    std::unique_ptr<FunctionCallExpr> Parser::ParseFunctionCallExpr(std::unique_ptr<Expression> base)
    {
        Token* bracket = CurrentToken();
        if (bracket && bracket->type == TokenType::BRACKET)
        {
            std::vector<std::unique_ptr<Expression>> arguments = ParseArguments();
            return std::make_unique<FunctionCallExpr>(std::move(base), std::move(arguments));
        }
        ReportSyntaxError(bracket, "Expected bracket '('");
        std::exit(EXIT_FAILURE);
        return nullptr;
    }

    std::unique_ptr<FunctionDeclStmt> Parser::ParseFunctionDeclaration()
    {
        std::vector<Token> modifiers = this->modifiers;
        std::vector<Attribute> attributes = this->attributes;
        std::unique_ptr<TypeExpr> returnType = ParseType();
        Token* identifier = Consume(TokenType::IDENTIFIER);
        std::vector<Token> templateParams = ParseTemplateParameters();

        std::vector<std::unique_ptr<VarDeclExpr>> parameters = ParseParameters();

        if (CurrentToken() && CurrentToken()->type == TokenType::KEYWORD &&
            CurrentToken()->value == "const") {
            modifiers.push_back(*Consume(TokenType::KEYWORD, "const"));
        }

        if (CurrentToken() && IsEndOfStatement(*CurrentToken())) {
            ReportSyntaxError(CurrentToken(), "A function body is required; use extern \"C\" for a native declaration");
            throw std::runtime_error("Function declaration without a body");
        }

        // Парсим тело функции
        std::unique_ptr<Statement> body = ParseStatement();

        auto stmt = std::make_unique<FunctionDeclStmt>(
            std::move(returnType),
            std::make_unique<Token>(*identifier),
            std::move(parameters),
            std::move(body)
        );
        stmt->modifiers = modifiers;
        stmt->attributes = std::move(attributes);
        stmt->templateParams = std::move(templateParams);
        return stmt;
    }

    std::unique_ptr<FunctionDeclStmt> Parser::ParseExternalFunctionDeclaration()
    {
        std::vector<Token> modifiers = this->modifiers;
        std::vector<Attribute> attributes = this->attributes;
        Consume(TokenType::KEYWORD, "extern");
        Token* abiToken = Consume(TokenType::STRING);
        if (!abiToken || abiToken->value != "\"C\"") {
            ReportSyntaxError(abiToken, "Only extern \"C\" is supported");
            throw std::runtime_error("Unsupported external ABI");
        }

        std::unique_ptr<TypeExpr> returnType = ParseType();
        Token* identifier = Consume(TokenType::IDENTIFIER);
        if (CurrentToken() && CurrentToken()->value == "<") {
            ReportSyntaxError(CurrentToken(), "Extern functions cannot declare generic parameters");
            throw std::runtime_error("Generic extern function");
        }
        auto parameters = ParseParameters();
        for (const auto& parameter : parameters) {
            if (parameter && parameter->value) {
                ReportSyntaxError(identifier, "Extern function parameters cannot have default values");
                throw std::runtime_error("Invalid extern parameter");
            }
        }
        Consume(TokenType::DELIMITER, ";");

        auto statement = std::make_unique<FunctionDeclStmt>(
            std::move(returnType), std::make_unique<Token>(*identifier),
            std::move(parameters), nullptr);
        statement->externalAbi = "C";
        statement->modifiers = std::move(modifiers);
        statement->attributes = std::move(attributes);
        return statement;
    }

    std::unique_ptr<FunctionDeclStmt> Parser::ParseExportFunctionDeclaration()
    {
        std::vector<Token> modifiers = this->modifiers;
        std::vector<Attribute> attributes = this->attributes;
        Consume(TokenType::KEYWORD, "export");
        Token* abiToken = Consume(TokenType::STRING);
        if (!abiToken || abiToken->value != "\"C\"") {
            ReportSyntaxError(abiToken, "Only export \"C\" is supported");
            throw std::runtime_error("Unsupported export ABI");
        }

        std::unique_ptr<TypeExpr> returnType = ParseType();
        Token* identifier = Consume(TokenType::IDENTIFIER);
        if (CurrentToken() && CurrentToken()->value == "<") {
            ReportSyntaxError(CurrentToken(), "Export functions cannot declare generic parameters");
            throw std::runtime_error("Generic export function");
        }
        auto parameters = ParseParameters();
        for (const auto& parameter : parameters) {
            if (parameter && parameter->value) {
                ReportSyntaxError(identifier, "Export function parameters cannot have default values");
                throw std::runtime_error("Invalid export parameter");
            }
        }
        if (CurrentToken() && IsEndOfStatement(*CurrentToken())) {
            ReportSyntaxError(CurrentToken(), "An export function requires an Absolute body");
            throw std::runtime_error("Export function without a body");
        }
        std::unique_ptr<Statement> body = ParseStatement();

        auto statement = std::make_unique<FunctionDeclStmt>(
            std::move(returnType), std::make_unique<Token>(*identifier),
            std::move(parameters), std::move(body));
        statement->exportAbi = "C";
        statement->modifiers = std::move(modifiers);
        statement->attributes = std::move(attributes);
        return statement;
    }

    std::unique_ptr<ReturnStmt> Parser::ParseReturnStmt()
    {
	    Token* token = CurrentToken();
	    if (token && token->type == TokenType::KEYWORD && token->value == "return") {
		    Consume(TokenType::KEYWORD);
		    std::unique_ptr<Expression> expr = ParseExpression();
		    Consume(TokenType::DELIMITER);
		    return std::make_unique<ReturnStmt>(std::move(expr));
        }
        return nullptr;
    }
}
