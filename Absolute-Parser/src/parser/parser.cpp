#include "parser_pch.h"
#include "parser.h"
#include "syntax_plugins.h"

namespace Absolute{
    constexpr unsigned int Hash(const char* str, int h = 0) {
        return !str[h] ? 5381 : (Hash(str, h + 1) * 33) ^ str[h];
    }
    std::vector<Token> Tokenize(const std::string& code) {
        return ExpandSyntaxPlugins(lexer(code));
    }

    std::unique_ptr<Program> ParseCode(const std::vector<Token>& tokens) {
        Parser parser(tokens);
        return parser.Parse();
    }

    std::unique_ptr<Program> Parser::Parse()
    {
        std::vector<std::unique_ptr<Statement>> statements;
        while (CurrentToken()) {
            const size_t statementStart = pos;
            auto statement = ParseStatement();
            if (statement) {
                statements.push_back(std::move(statement));
            }
            else if (pos == statementStart) {
                ReportSyntaxError(CurrentToken(), "Unexpected token at the start of a statement");
                throw std::runtime_error("Parser made no progress");
            }
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
        if (!left) return nullptr;

        Token* current = CurrentToken();
        if (current && GetOperatorCategory(current->value) == OperatorCategory::Assignment) {
            left = ParseAssignmentExpr(std::move(left));
        }

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

        if (token->value == "new") {
            return ParseConstructorCall();
        }

        if (token->value == "delete") {
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
        ParseAttributes();
        ParseModifiers();

        Token* token = CurrentToken();
        if (!token) return nullptr;

        if (auto opaque = TryParseOpaquePluginStatement(tokens, pos)) {
            opaque->modifiers = modifiers;
            opaque->attributes = attributes;
            return opaque;
        }

        if (!attributes.empty() && token->type != TokenType::IDENTIFIER &&
            !(token->type == TokenType::KEYWORD &&
                (IsPrimitiveType(token->value) || token->value == "raw" ||
                    token->value == "class" || token->value == "struct" ||
                    token->value == "interface" || token->value == "enum" ||
                    token->value == "group" || token->value == "extern" ||
                    token->value == "namespace" || token->value == "import"))) {
            ReportSyntaxError(token, "Attributes may only precede declarations or opaque plugin blocks");
            throw std::runtime_error("Invalid attribute target");
        }

        if (LooksLikeFunctionDeclaration()) return ParseFunctionDeclaration();

        if (token->type == TokenType::KEYWORD && IsPrimitiveType(token->value)) {
            return ParseVarDeclaration();
        }

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
            case Hash("await"):
            case Hash("spawn"):
            {
                auto expression = ParseExpression();
                Consume(TokenType::DELIMITER, ";");
                return std::make_unique<SingleStatement>(std::move(expression));
            }
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
            case Hash("switch"):
                return ParseSwitchStmt(false);
            case Hash("match"):
                return ParseSwitchStmt(true);
            case Hash("throw"):
                return ParseThrowStmt();
            case Hash("try"):
                return ParseTryStmt();
            case Hash("defer"):
                return ParseDeferStmt();
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
		    case Hash("interface"):
			    return ParseInterfaceDecl();
		    case Hash("enum"):
			    return ParseEnumDecl();
            case Hash("group"):
                return ParseGroupDecl();
            case Hash("import"):
                return ParseImport();
            case Hash("namespace"):
                return ParseNamespace();
            case Hash("extern"):
                return ParseExternalFunctionDeclaration();
            case Hash("raw"):
                return ParseVarDeclaration();
            default:
                break;
            }
            break;

        case TokenType::IDENTIFIER:
        {
            if (token->value == "task" && PeekToken() && PeekToken()->value == "<")
                return ParseVarDeclaration();
            Token* next = PeekToken(1);
            Token* afterIdentifiers = PeekTokenAfterIdentifiers();
            const auto isDeclaratorPrefix = [](const Token* candidate) {
                return candidate && candidate->type == TokenType::OPERATOR &&
                    (candidate->value == "*" || candidate->value == "&");
            };
            bool templateType = false;
            bool templatePointer = false;
            if (afterIdentifiers && afterIdentifiers->value == "<") {
                const size_t templateStart = static_cast<size_t>(afterIdentifiers - tokens.data());
                size_t templateClose = 0;
                if (IsTemplateArgumentList(templateStart, &templateClose)) {
                    Token* afterTemplate = templateClose + 1 < tokens.size() ? &tokens[templateClose + 1] : nullptr;
                    templateType = afterTemplate &&
                        (afterTemplate->type == TokenType::IDENTIFIER || isDeclaratorPrefix(afterTemplate));
                    templatePointer = isDeclaratorPrefix(afterTemplate);
                }
            }

            if ((next && next->type == TokenType::OPERATOR && next->value == "*") ||
                (afterIdentifiers && afterIdentifiers->type == TokenType::OPERATOR &&
                    afterIdentifiers->value == "*") || templatePointer) {
                return ParseVarDeclaration();
            }

            if ((next && (next->type == TokenType::IDENTIFIER || isDeclaratorPrefix(next))) ||
                (afterIdentifiers && (afterIdentifiers->type == TokenType::IDENTIFIER || isDeclaratorPrefix(afterIdentifiers))) ||
                templateType) {
                return ParseInstanceDeclStmt();
            }
            return ParseIdentifier(); // Обрабатываем идентификатор
        }

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
        {
            auto expression = ParseExpression();
            Consume(TokenType::DELIMITER, ";");
            return std::make_unique<SingleStatement>(std::move(expression));
        }

        default:
            break;
        }
        return nullptr;
    }

    std::unique_ptr<CompoundStmt> Parser::ParseCompoundStatement()
    {
        std::vector<std::unique_ptr<Statement>> statements;
        Consume(TokenType::BRACKET, "{"); // "{"

        while (CurrentToken() && !(CurrentToken()->type == TokenType::BRACKET && CurrentToken()->value == "}"))
        {
            const size_t statementStart = pos;
            auto statement = ParseStatement();
            if (statement) statements.push_back(std::move(statement));
            else if (pos == statementStart) {
                ReportSyntaxError(CurrentToken(), "Unexpected token in compound statement");
                throw std::runtime_error("Parser made no progress");
            }
        }

        if (!CurrentToken()) {
            ReportSyntaxError(nullptr, "Unexpected end of file; expected '}'");
            throw std::runtime_error("Unterminated compound statement");
        }

        Consume(TokenType::BRACKET, "}"); // "}"
        return std::make_unique<CompoundStmt>(std::move(statements));
    }
}
