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
        if (nextToken && GetOperatorCategory(nextToken->value) == OperatorCategory::Assignment) {
            std::cout << "Assignment " << token->value << " " << nextToken->value << "\n";
			return ParseAssignmentStmt();
        }
    }
    return nullptr;
}

std::unique_ptr<Expression> Parser::ParseExpression() {
    Token* token = CurrentToken();
    if (!token) return nullptr;

    Token* next = PeekToken(1); // Получаем следующий токен один раз

    // Бинарные операторы
    if (next && next->type == TokenType::OPERATOR) {
        return ParseBinaryExpr(0);
    }

    // Присваивание (a = b + c)
    if (token->type == TokenType::IDENTIFIER && next) {
        if (next->type == TokenType::OPERATOR && GetOperatorCategory(next->value) == OperatorCategory::Assignment) {
            return ParseAssignmentExpr();
        }

        // Вызов функции (foo(...))
        if (next->type == TokenType::BRACKET && next->value == "(") {
            return ParseFunctionCallExpr();
        }

        // Переменная (x, y)
        return ParseIdentifierExpr();
    }

    // Литералы (42, "Hello", 'A')
    return ParseLiteralExpr();
}



std::unique_ptr<AssignmentExpr> Parser::ParseAssignmentExpr()
{
	Token* identifier = CurrentToken();
    if (identifier && identifier->type == TokenType::IDENTIFIER) {
		Consume(TokenType::IDENTIFIER);
		Token* op = CurrentToken();
		Consume(TokenType::OPERATOR); // Assignment operator
		std::unique_ptr<Expression> value = ParseExpression();
		return std::make_unique<AssignmentExpr>(std::make_unique<IdentifierExpr>(identifier->value), std::move(op->value), std::move(value));
    }
    return nullptr;
}

std::unique_ptr<IdentifierExpr> Parser::ParseIdentifierExpr()
{
    Token* identifier = CurrentToken();
	if (identifier && identifier->type == TokenType::IDENTIFIER) {
		Consume(TokenType::IDENTIFIER);
		return std::make_unique<IdentifierExpr>(identifier->value);
	}
    return nullptr;
}

std::unique_ptr<Expression> Parser::ParseLiteralExpr()
{
    Token* token = CurrentToken();
    // Число (42, 3.14)
    if (token->type == TokenType::NUMBER) {
        return ParseNumberLiteralExpr();
    }
    // **Строка** ("Hello world")
    if (token->type == TokenType::STRING) {
        return ParseStringLiteralExpr();
    }
    // **Символ** ('A')
    if (token->type == TokenType::CHAR) {
        return ParseCharLiteralExpr();
    }
    return nullptr;
}

std::unique_ptr<NumberLiteralExpr> Parser::ParseNumberLiteralExpr()
{
    Token* numberToken = CurrentToken();
	if (numberToken && numberToken->type == TokenType::NUMBER) {
		Consume(TokenType::NUMBER);
		return std::make_unique<NumberLiteralExpr>(numberToken->value);
	}
    return nullptr;
}

std::unique_ptr<StringLiteralExpr> Parser::ParseStringLiteralExpr() {
    Token* token = CurrentToken();
    if (token && token->type == TokenType::STRING) {
        std::string value = token->value.substr(1, token->value.size() - 2); // Убираем кавычки
        Consume(TokenType::STRING);
        return std::make_unique<StringLiteralExpr>(value);
    }
    return nullptr;
}

std::unique_ptr<CharLiteralExpr> Parser::ParseCharLiteralExpr() {
    Token* token = CurrentToken();
    if (token && token->type == TokenType::CHAR) {
        Consume(TokenType::CHAR);
        return std::make_unique<CharLiteralExpr>(token->value[1]); // Символ внутри кавычек
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

    if (token->type == TokenType::NUMBER || token->type == TokenType::STRING || token->type == TokenType::CHAR) {
        return ParseLiteralExpr();
    }

    if (token->type == TokenType::IDENTIFIER && GetOperatorCategory(PeekToken(1)->value) == OperatorCategory::Assignment) {
        return ParseAssignmentExpr();
    }

    if (token->type == TokenType::IDENTIFIER) {
        return ParseIdentifierExpr();
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
    if (identifier && identifier->type == TokenType::IDENTIFIER)
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
        std::unique_ptr<IdentifierExpr> callee = std::make_unique<IdentifierExpr>(identifier->value);
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
        case Hash("long"):
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

std::unique_ptr<VarDeclStmt> Parser::ParseVarDeclaration()
{
    Token* token = CurrentToken();
    if (token && token->type == TokenType::KEYWORD) {
        std::string type = token->value; // Сохраняем тип переменной
        Consume(TokenType::KEYWORD);

        Token* identifier = CurrentToken();
        if (identifier->type == TokenType::IDENTIFIER) {
            // Объявление переменной без инициализации
            if (IsEndOfStatement(*PeekToken(1))) {
                Consume(TokenType::IDENTIFIER);
                Consume(TokenType::DELIMITER);
                return std::make_unique<VarDeclStmt>(token->value, identifier->value, nullptr);
            }
            // Объявление переменной с инициализацией
            else if (PeekToken(1)->type == TokenType::OPERATOR) {
                std::string varName = identifier->value; // Сохраняем имя переменной
                Consume(TokenType::IDENTIFIER);
                Consume(TokenType::OPERATOR);

                Token* valueToken = CurrentToken();
                if (IsLiteral(valueToken->type)) {
                    std::unique_ptr<Expression> value = ParseExpression();
                    Consume(TokenType::DELIMITER);
                    return std::make_unique<VarDeclStmt>(token->value, identifier->value, std::move(value));
                }
            }
            // Объявление переменной массива
            else if (PeekToken(1)->type == TokenType::BRACKET) {
                return ParseVarDeclarationArray(*token);
            }
        }
    }
    return nullptr;
}

std::unique_ptr<VarDeclStmt> Parser::ParseVarDeclarationArray(const Token& type)
{
    Token* identifier = Consume(TokenType::IDENTIFIER);
    std::vector<std::unique_ptr<Expression>> sizes;  // Размеры массива

    while (CurrentToken()->value == "[") {  // Проверяем именно значение токена
        Consume(TokenType::BRACKET);  // `[`
        Token* size = CurrentToken();
        if (IsLiteral(size->type) || size->type == TokenType::IDENTIFIER) {
            sizes.push_back(ParseExpression()); // Парсим число внутри `[]`
        }
        else {
            throw std::runtime_error("Ошибка: ожидается размер массива");
        }
        Consume(TokenType::BRACKET);  // `]`
    }

    Token* next = CurrentToken();
    if (next && next->type == TokenType::DELIMITER) {
        Consume(TokenType::DELIMITER); // `;`
        return std::make_unique<VarDeclStmt>(
            type.value,
            identifier->value,
            std::make_unique<ArrayExpr>(std::move(sizes), std::vector<std::unique_ptr<Expression>>()),
            true  // Это массив
        );
    }
    else if (next && GetOperatorCategory(next->value) == OperatorCategory::Assignment) {
        std::vector<std::unique_ptr<Expression>> values; // Значения массива
        Consume(TokenType::OPERATOR);

        next = CurrentToken();
        if (next && next->value == "{") {  // Начало инициализации массива
            Consume(TokenType::BRACKET);  // `{`

            while (CurrentToken()->value != "}") {  // Пока не закроем массив
                values.push_back(ParseExpression());  // Парсим элемент массива

                // Проверяем `,` между значениями
                if (CurrentToken()->value == ",") {
                    Consume(TokenType::DELIMITER);  // `,`
                }
                else if (CurrentToken()->value == "}") {
                    break;  // Закрыли массив
                }
                else {
                    throw std::runtime_error("Ошибка: ожидается ',' или '}'");
                }
            }

            Consume(TokenType::BRACKET);  // `}`
        }

        Consume(TokenType::DELIMITER);  // `;`
        return std::make_unique<VarDeclStmt>(
            type.value,
            identifier->value,
            std::make_unique<ArrayExpr>(std::move(sizes), std::move(values)),
            true  // Это массив
        );
    }

    return nullptr;
}

std::unique_ptr<FunctionDeclStmt> Parser::ParseFunctionDeclaration()
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

    return std::make_unique<FunctionDeclStmt>(
        std::make_unique<Token>(*ret),   // Создаём `unique_ptr` на копию токена
        std::make_unique<Token>(*identifier),
        std::move(parameters),
        std::move(body)
    );
}

std::unique_ptr<FunctionCallStmt> Parser::ParseFunctionCall()
{
    Token* identifier = CurrentToken();
    if (!identifier) return nullptr;
	std::unique_ptr<FunctionCallExpr> call = ParseFunctionCallExpr();
    return std::make_unique<FunctionCallStmt>(identifier->value, std::move(call));
}

std::unique_ptr<AssignmentStmt> Parser::ParseAssignmentStmt()
{
    Token* token = CurrentToken();
    if (token && token->type == TokenType::IDENTIFIER) {
		std::unique_ptr<AssignmentExpr> expr = ParseAssignmentExpr();
		std::unique_ptr<AssignmentStmt> stmt = std::make_unique<AssignmentStmt>(std::move(expr));
        return stmt;
    }
    return nullptr;
}
