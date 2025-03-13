#include "pch.h"
#include "parser.h"

int GetOperatorPrecedence(const std::string& op) {
    static const std::unordered_map<std::string, int> precedence = {
        {"=", 1}, {"+=", 1}, {"-=", 1}, {"*=", 1}, {"/=", 1}, {"%=", 1}, {"&=", 1}, {"|=", 1}, {"^=", 1}, // Присваивание
        {"||", 2}, // Логическое ИЛИ
        {"&&", 3}, // Логическое И
        {"|", 4},  // Побитовое ИЛИ
        {"^", 5},  // Побитовое исключающее ИЛИ
        {"&", 6},  // Побитовое И
        {"==", 7}, {"!=", 7}, // Равенство
        {"<", 8}, {"<=", 8}, {">", 8}, {">=", 8}, // Сравнение
        {"<<", 9}, {">>", 9}, // Сдвиги
        {"+", 10}, {"-", 10},  // Сложение, вычитание
        {"*", 11}, {"/", 11}, {"%", 11},  // Умножение, деление, остаток
        {"!", 12}, {"~", 12}, // Логическое НЕ, побитовое НЕ (унарные)
        {"++", 13}, {"--", 13}  // Инкремент и декремент (постфикс)
    };

    auto it = precedence.find(op);
    return (it != precedence.end()) ? it->second : -1; // -1 если оператор не найден
}

constexpr unsigned int Hash(const char* str, int h = 0) {
	return !str[h] ? 5381 : (Hash(str, h + 1) * 33) ^ str[h];
}

Token* Parser::Consume(TokenType tokenType)
{
    Token* token = CurrentToken();
    if (token && token->type == tokenType) {
        pos++;
    }
    else {
        throw std::runtime_error("Unexpected token: " + (token ? token->value : "EOF"));
    }
    return token;
}

std::unique_ptr<Program> Parser::Parse()
{
    std::vector<std::unique_ptr<Statement>> statements;
    while (CurrentToken()) {
        statements.push_back(std::unique_ptr<Statement>(ParseStatement()));
    }
	return std::make_unique<Program>(std::move(statements));
}

std::unique_ptr<Statement> Parser::ParseIdentifier()
{
	Token* token = CurrentToken();

    if (token->type == TokenType::IDENTIFIER) {
		Token* nextToken = PeekToken();
		if (nextToken && nextToken->value == "(") {
			return ParseFunctionCall();
		}
        if (nextToken && nextToken->value == "=") {
			return ParseAssignment();
        }
    }
    return nullptr;
}

std::unique_ptr<Expression> Parser::ParseExpression() {
    Token* token = CurrentToken();
    if (!token) return nullptr;

    if (PeekToken(1) && PeekToken(1)->type == TokenType::OPERATOR) {
        return ParseBinaryExpr(0);
    }

    // Присваивание (a = b + c)
    if (token->type == TokenType::IDENTIFIER && PeekToken(1) && PeekToken(1)->type == TokenType::OPERATOR && PeekToken(1)->value == "=") {
        return ParseAssignmentExpr();
    }

    // Вызов функции (foo(...))
    if (token->type == TokenType::IDENTIFIER) {
        Token* next = PeekToken(1);
        if (next && next->type == TokenType::BRACKET && next->value == "(") {
            return ParseFunctionCallExpr();
        }
    }

    // Число (42, 3.14)
    if (token->type == TokenType::NUMBER) {
        return ParseNumberLiteralExpr();
    }

    // Переменная (x, y)
    if (token->type == TokenType::IDENTIFIER) {
        return ParseIdentifierExpr();
    }

    // **Строка** ("Hello world")
    if (token->type == TokenType::STRING) {
        return ParseStringLiteralExpr();
    }

    // **Символ** ('A')
    if (token->type == TokenType::CHAR) {
        return ParseCharLiteralExpr();
    }

    // Бинарное выражение (a + b, 3 * 5)
    return ParseBinaryExpr(0);
}


std::unique_ptr<Expression> Parser::ParseAssignmentExpr()
{
    return std::unique_ptr<Expression>();
}

std::unique_ptr<Identifier> Parser::ParseIdentifierExpr()
{
    Token* identifier = CurrentToken();
	if (identifier && identifier->type == TokenType::IDENTIFIER) {
		Consume(TokenType::IDENTIFIER);
		return std::make_unique<Identifier>(identifier->value);
	}
    return nullptr;
}

std::unique_ptr<NumberLiteral> Parser::ParseNumberLiteralExpr()
{
    Token* numberToken = CurrentToken();
	if (numberToken && numberToken->type == TokenType::NUMBER) {
		Consume(TokenType::NUMBER);
		return std::make_unique<NumberLiteral>(numberToken->value);
	}
    return nullptr;
}

std::unique_ptr<StringLiteral> Parser::ParseStringLiteralExpr() {
    Token* token = CurrentToken();
    if (token && token->type == TokenType::STRING) {
        std::string value = token->value.substr(1, token->value.size() - 2); // Убираем кавычки
        Consume(TokenType::STRING);
        return std::make_unique<StringLiteral>(value);
    }
    return nullptr;
}

std::unique_ptr<CharLiteral> Parser::ParseCharLiteralExpr() {
    Token* token = CurrentToken();
    if (token && token->type == TokenType::CHAR) {
        Consume(TokenType::CHAR);
        return std::make_unique<CharLiteral>(token->value[1]); // Символ внутри кавычек
    }
    return nullptr;
}


std::unique_ptr<Expression> Parser::ParseBinaryExpr(int minPrecedence) {
    std::unique_ptr<Expression> left = ParsePrimaryExpr(); // Читаем первый операнд
    if (!left) return nullptr;

    while (true) {
        Token* opToken = CurrentToken();
        if (!opToken || opToken->type != TokenType::OPERATOR)
            break;

        int precedence = GetOperatorPrecedence(opToken->value);
        if (precedence < minPrecedence)
            break; // Если оператор менее приоритетный, выходим

        // Сохраняем значение оператора до его потребления
        std::string op = opToken->value;
        Consume(TokenType::OPERATOR);
        std::unique_ptr<Expression> right = ParseBinaryExpr(precedence + 1); // Парсим правый операнд

        if (!right)
            return nullptr;

        left = std::make_unique<BinaryExpr>(op, std::move(left), std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::ParsePrimaryExpr() {
    Token* token = CurrentToken();
    if (!token)
        return nullptr;

    if (token->type == TokenType::NUMBER) {
        std::string value = token->value;
        Consume(TokenType::NUMBER);
        return std::make_unique<NumberLiteral>(value);
    }

    if (token->type == TokenType::IDENTIFIER) {
        std::string value = token->value;
        Consume(TokenType::IDENTIFIER);
        return std::make_unique<Identifier>(value);
    }

    if (token->type == TokenType::STRING) {
        std::string value = token->value;
        Consume(TokenType::STRING);
        return std::make_unique<StringLiteral>(value);
    }

    if (token->type == TokenType::CHAR) {
        return ParseExpression();
    }

    if (token->type == TokenType::BRACKET && token->value == "(") {
        Consume(TokenType::BRACKET); // Пропускаем "("
        auto expr = ParseExpression(); // Парсим выражение внутри скобок
        if (!expr)
            return nullptr;

        // Проверка на наличие закрывающей скобки
        Token* closing = CurrentToken();
        if (closing && closing->type == TokenType::BRACKET && closing->value == ")") {
            Consume(TokenType::BRACKET); // Пропускаем ")"
        }
        else {
            std::cerr << "Ошибка: Ожидалась закрывающая скобка" << std::endl;
            return nullptr;
        }

        return expr;
    }

    return nullptr;
}


std::unique_ptr<FunctionCallExpr> Parser::ParseFunctionCallExpr()
{
    Token* identifier = CurrentToken();
    if (identifier && identifier->type == IDENTIFIER)
    {
        Consume(TokenType::IDENTIFIER);
        Consume(TokenType::BRACKET); // "("
        std::vector<std::unique_ptr<Expression>> arguments;
        while (CurrentToken()->type != TokenType::BRACKET && CurrentToken()->value != ")")
        {
            std::unique_ptr<Expression> argument = ParseExpression();
            arguments.push_back(std::move(argument));
            if (CurrentToken()->type == TokenType::DELIMITER && CurrentToken()->value == ",") {
                Consume(TokenType::DELIMITER);
            }
            else if (CurrentToken()->type == TokenType::BRACKET && CurrentToken()->value == ")") {
                break;
            }
        }
        Consume(TokenType::BRACKET); // ")"
        std::unique_ptr<Identifier> callee = std::make_unique<Identifier>(identifier->value);
        return std::make_unique<FunctionCallExpr>(std::move(callee), std::move(arguments));
    }

	return nullptr;
}

std::unique_ptr<Statement> Parser::ParseStatement()
{
    Token* token = CurrentToken();

    switch (token->type)
    {
    case TokenType::KEYWORD:
        switch (Hash(token->value.c_str()))
        {
        case Hash("int"):
        case Hash("float"):
        case Hash("double"):
        case Hash("string"):
        case Hash("char"):
        {
            printf("Token: %s\n", token->value.c_str());
            Token* nextToken = PeekToken();
            if (nextToken && nextToken->type == TokenType::IDENTIFIER) {
                Token* afterIdentifier = PeekToken(2);
                if (afterIdentifier && afterIdentifier->value == "(")
                {
                    printf("Function Declaration\n");
                    return ParseFunctionDeclaration();
                }
            }
            return ParseVarDeclaration();
        }
        case Hash("void"):
            return ParseFunctionDeclaration();
        default:
            break;
        }
        break;

    case TokenType::IDENTIFIER:
        return ParseIdentifier(); // Обрабатываем идентификатор

	case TokenType::BRACKET:
		if (token->value == "{") {
			return ParseCompoundStatement();
		}
		break;

	case TokenType::DELIMITER:
		if (token->value == ";") {
			Consume(TokenType::DELIMITER);
			return nullptr;
		}
		break;

    default:
        break;
    }
    return nullptr;
}

std::unique_ptr<CompoundStmt> Parser::ParseCompoundStatement()
{
    std::vector<std::unique_ptr<Statement>> statements;
    Consume(TokenType::BRACKET); // "{"

    while (!(CurrentToken()->type == TokenType::BRACKET && CurrentToken()->value == "}"))
    {
        statements.push_back(std::unique_ptr<Statement>(ParseStatement()));
    }

    Consume(TokenType::BRACKET); // "}"
    return std::make_unique<CompoundStmt>(std::move(statements));
}

std::unique_ptr<Statement> Parser::ParseVarDeclaration()
{
    Token* token = CurrentToken();
    if (token && token->type == TokenType::KEYWORD) {
        std::string type = token->value; // Сохраняем тип переменной
        Consume(TokenType::KEYWORD);

        Token* identifier = CurrentToken();
        if (identifier->type == TokenType::IDENTIFIER) {
            std::string varName = identifier->value; // Сохраняем имя переменной
            Consume(TokenType::IDENTIFIER);
            Consume(TokenType::OPERATOR);

            Token* valueToken = CurrentToken();
            if (valueToken->type == TokenType::NUMBER || valueToken->type == TokenType::STRING || valueToken->type == TokenType::CHAR) {
                std::unique_ptr<Expression> value = ParseExpression();
                Consume(TokenType::DELIMITER);
                return std::make_unique<VarDeclStmt>(token->value, identifier->value, std::move(value));
            }
        }
    }
    return nullptr;
}


std::unique_ptr<Statement> Parser::ParseFunctionDeclaration()
{
    Token* ret = Consume(TokenType::KEYWORD);
    Token* identifier = Consume(TokenType::IDENTIFIER);

    Consume(TokenType::BRACKET); // "("

    std::vector<std::unique_ptr<VarDeclStmt>> parameters;
    while (CurrentToken()->type != TokenType::BRACKET && CurrentToken()->value != ")")
    {
        Token* type = Consume(TokenType::KEYWORD);
        Token* name = Consume(TokenType::IDENTIFIER);

        if (CurrentToken()->type == TokenType::OPERATOR && CurrentToken()->value == "=") {
            Consume(TokenType::OPERATOR);
            std::unique_ptr<Expression> value = ParseExpression();
            parameters.push_back(std::make_unique<VarDeclStmt>(type->value, name->value, std::move(value)));
        }
        else {
            parameters.push_back(std::make_unique<VarDeclStmt>(type->value, name->value, nullptr));
        }

        if (CurrentToken()->type == TokenType::DELIMITER && CurrentToken()->value == ",") {
            Consume(TokenType::DELIMITER);
        }
        else if (CurrentToken()->type == TokenType::BRACKET && CurrentToken()->value == ")") {
            break;
        }
    }

    Consume(TokenType::BRACKET); // ")"

    // Парсим тело функции
    std::unique_ptr<CompoundStmt> body = ParseCompoundStatement();

    return std::make_unique<FunctionDecl>(
        std::make_unique<Token>(*ret),   // Создаём `unique_ptr` на копию токена
        std::make_unique<Token>(*identifier),
        std::move(parameters),
        std::move(body)
    );
}

std::unique_ptr<Statement> Parser::ParseFunctionCall()
{
    Token* identifier = CurrentToken();
    if (!identifier) return nullptr;
	std::unique_ptr<FunctionCallExpr> call = ParseFunctionCallExpr();
    return std::make_unique<FunctionCallStmt>(identifier->value, std::move(call));
}

std::unique_ptr<Statement> Parser::ParseAssignment()
{
    Token* token = CurrentToken();
    if (token->type == TokenType::IDENTIFIER) {
        auto target = std::make_unique<Identifier>(token->value);
        Consume(TokenType::IDENTIFIER);

        Token* op = CurrentToken();
        if (!op || op->value != "=") {
            return nullptr; // Это не присваивание
        }
        Consume(TokenType::OPERATOR);

        auto value = ParseExpression(); // `ParseExpression()` уже вернёт `std::unique_ptr<Expression>`

        return std::make_unique<AssignmentStmt>(
            std::make_unique<AssignmentExpr>(std::move(target), std::move(value))
        );
    }
    return nullptr;
}
