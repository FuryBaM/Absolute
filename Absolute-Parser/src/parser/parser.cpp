#include "parser_pch.h"
#include "parser.h"

namespace Absolute{
    constexpr unsigned int Hash(const char* str, int h = 0) {
        return !str[h] ? 5381 : (Hash(str, h + 1) * 33) ^ str[h];
    }
    std::vector<Token> Tokenize(const std::string& code) {
        return lexer(code);  // Использует внутренний лексер
    }

    std::unique_ptr<Program> ParseCode(const std::vector<Token>& tokens) {
        Parser parser(tokens);
        return parser.Parse();
    }

    std::unique_ptr<Program> Parser::Parse()
    {
        std::vector<std::unique_ptr<Statement>> statements;
        while (CurrentToken()) {
            statements.push_back(std::unique_ptr<Statement>(ParseStatement()));
        }
	    return std::make_unique<Program>(std::move(statements));
    }

    std::unique_ptr<Expression> Parser::ParseExpression() {
        Token* token = CurrentToken();
        if (!token) {
            ReportSyntaxError(token, "Token is null");
            std::exit(EXIT_FAILURE);
            return nullptr;
        }
        std::unique_ptr<Expression> left = nullptr;

        // Парсим первичное выражение
        left = ParsePrimaryExpr();
        if (GetOperatorCategory(CurrentToken()->value) == OperatorCategory::Assignment) {
            left = ParseAssignmentExpr(std::move(left));
        }
        if (!left) return nullptr;

        // Проверяем, идет ли дальше бинарный оператор
        Token* next = CurrentToken();
        if (next && next->type == TokenType::OPERATOR) {
            return ParseBinaryExpr(0, std::move(left));
        }

        return left;
    }

    std::unique_ptr<Expression> Parser::ParseBaseExpr() {
        Token* token = CurrentToken();
        if (!token) {
            ReportSyntaxError(token, "Null token");
            std::exit(EXIT_FAILURE);
            return nullptr;
        }

        if (IsPrefixUnary(*token)) {
            return ParsePrefixUnaryExpr();
        }

        if (token->type == TokenType::KEYWORD) {
            if ((token->value == "true" || token->value == "false")) {
                return ParseBooleanLiteralExpr();
            }
            else if (token->value == "null") {
                Consume(TokenType::KEYWORD);
                return std::make_unique<NullExpr>();
            }
            else if (IsPrimitiveType(token->value)) {
                return ParsePrimitiveType();
            }
        }

        if (IsLiteral(token->type)) {
			return ParseLiteralExpr();
        }

        if (token->type == TokenType::IDENTIFIER) {
            return ParseIdentifierExpr();
        }

        if (CurrentToken()->value == "new") {
            return ParseConstructorCall();
        }

        if (CurrentToken()->value == "delete") {
            return ParseDestructorCall();
        }

        // Вложенное выражение в скобках: (a + b)
        if (token->type == TokenType::BRACKET && token->value == "(") {
            Consume(TokenType::BRACKET, "("); // Пропускаем "("
            auto expr = ParseExpression();
            if (!expr)
                return nullptr;

            // Проверяем закрывающую скобку
            Consume(TokenType::BRACKET, ")");

            return expr;
        }

        if (token->type == TokenType::BRACKET && token->value == "{") {
            return ParseArrayValues();
        }

        ReportSyntaxError(CurrentToken(), "Expected primary expression");
        std::exit(EXIT_FAILURE);
    }

    std::unique_ptr<Expression> Parser::ParseSuffixExpr(std::unique_ptr<Expression> base) {
        while (true) {
            Token* token = CurrentToken();

            if (!token) break;

            if (IsPostfixUnary(*token)) {
                base = ParsePostfixUnaryExpr(std::move(base));
            }
            else if (token->value == "as") {
                base = ParseCastExpr(std::move(base));
            }
            else {
                break;
            }
        }
        return base;
    }

    std::unique_ptr<Expression> Parser::ParsePrimaryExpr() {
        auto base = ParseBaseExpr();
        return ParseSuffixExpr(std::move(base));
    }

    std::unique_ptr<Statement> Parser::ParseStatement()
    {
        ParseModifiers();

        Token* token = CurrentToken();

        switch (token->type)
        {
        case TokenType::KEYWORD:
            switch (Hash(token->value.c_str()))
            {
            case Hash("int8"):
            case Hash("int16"):
            case Hash("int32"):
            case Hash("int64"):
            case Hash("uint8"):
            case Hash("uint16"):
            case Hash("uint32"):
            case Hash("uint64"):
            case Hash("float"):
            case Hash("double"):
            case Hash("string"):
            case Hash("char"):
            {
                Token* nextToken = PeekToken();
                if (nextToken && nextToken->type == TokenType::IDENTIFIER) {
                    Token* afterIdentifier = PeekToken(2);
                    if (afterIdentifier && afterIdentifier->value == "(")
                    {
                        return ParseFunctionDeclaration();
                    }
                }
                return ParseVarDeclaration();
            }
            case Hash("new"):
            case Hash("delete"):
                return std::make_unique<SingleStatement>(ParsePrimaryExpr());
            case Hash("void"):
                return ParseFunctionDeclaration();
		    case Hash("return"):
			    return ParseReturnStmt();
            case Hash("continue"):
                Consume(TokenType::KEYWORD, "continue");
                Consume(TokenType::DELIMITER, ";");
                return std::make_unique<ContinueStmt>();
            case Hash("break"):
                Consume(TokenType::KEYWORD, "break");
                Consume(TokenType::DELIMITER, ";");
                return std::make_unique<BreakStmt>();
            case Hash("if"):
			    return ParseIfStmt();
		    case Hash("for"):
			    return ParseForStmt();
		    case Hash("while"):
			    return ParseWhileStmt();
            case Hash("foreach"):
                return ParseForEachStmt();
		    case Hash("do"):
			    return ParseDoWhileStmt();
		    case Hash("class"):
			    return ParseClassDecl();
		    case Hash("struct"):
			    return ParseStructDecl();
		    case Hash("enum"):
			    return ParseEnumDecl();
            case Hash("group"):
                return ParseGroupDecl();
            default:
                break;
            }
            break;

        case TokenType::IDENTIFIER:
            if (PeekToken(1)->type == TokenType::DOLLAR || PeekToken(1)->type == TokenType::IDENTIFIER || 
                PeekTokenAfterIdentifiers()->type == TokenType::IDENTIFIER || 
                IsPrefixUnary(*PeekToken(1)) || IsPrefixUnary(*PeekTokenAfterIdentifiers())) {
                return ParseInstanceDeclStmt();
            }
            return ParseIdentifier(); // Обрабатываем идентификатор

	    case TokenType::BRACKET:
		    if (token->value == "{") {
			    return ParseCompoundStatement();
		    }
		    break;

	    case TokenType::DELIMITER:
		    if (IsEndOfStatement(*token)) {
			    Consume(TokenType::DELIMITER);
			    return nullptr;
		    }
		    break;

        case TokenType::OPERATOR:
            return std::make_unique<SingleStatement>(ParsePrimaryExpr());

        default:
            break;
        }
        return nullptr;
    }

    std::unique_ptr<CompoundStmt> Parser::ParseCompoundStatement()
    {
        std::vector<std::unique_ptr<Statement>> statements;
        Consume(TokenType::BRACKET, "{"); // "{"

        while (!(CurrentToken()->type == TokenType::BRACKET && CurrentToken()->value == "}"))
        {
            statements.push_back(std::unique_ptr<Statement>(ParseStatement()));
        }

        Consume(TokenType::BRACKET, "}"); // "}"
        return std::make_unique<CompoundStmt>(std::move(statements));
    }
}