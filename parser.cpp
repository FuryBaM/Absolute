#include "pch.h"
#include "parser.h"

constexpr unsigned int Hash(const char* str, int h = 0) {
    return !str[h] ? 5381 : (Hash(str, h + 1) * 33) ^ str[h];
}

void Parser::ReportSyntaxError(const Token* token, const std::string& message)
{
    std::cerr << "Syntax Error: " << message;

    if (token)
    {
        std::cerr << " at line " << token->line
            << ", column " << token->column
            << " (token: '" << token->value << "')";
    }

    std::cerr << std::endl;
}

Token* Parser::Consume(TokenType tokenType)
{
    Token* token = CurrentToken();
    if (token && token->type == tokenType) {
        pos++;
    }
    else {
        ReportSyntaxError(token, "Unexpected token: " + (token ? token->value : "EOF"));
        std::exit(EXIT_FAILURE);
    }
    return token;
}

Token* Parser::Consume(TokenType tokenType, const std::string& expectedValue) {
    Token* token = CurrentToken();

    // Проверяем, соответствует ли expectedValue типу токена
    if (!IsValidTokenValue(tokenType, expectedValue)) {
        throw std::invalid_argument("Invalid expected value '" + expectedValue +
            "' for token type " + std::to_string(static_cast<int>(tokenType)));
    }

    if (token && token->type == tokenType && token->value == expectedValue) {
        pos++;
    }
    else {
        std::string errorMsg = "Unexpected token: " + (token ? token->value : "EOF");
        errorMsg += ", expected: '" + expectedValue + "'";
        ReportSyntaxError(token, errorMsg);
        std::exit(EXIT_FAILURE);
    }
    return token;
}

std::vector<std::unique_ptr<VarDeclExpr>> Parser::ParseParameters()
{
    if (CurrentToken()->value != "(") {
        ReportSyntaxError(CurrentToken(), "Expected '(' in parameters");
        std::exit(EXIT_FAILURE);
    }
    Consume(TokenType::BRACKET, "("); // "("

    std::vector<std::unique_ptr<VarDeclExpr>> parameters;
    while (CurrentToken()->type != TokenType::BRACKET && CurrentToken()->value != ")")
    {
        Token* type = Consume(TokenType::KEYWORD);
        Token* name = Consume(TokenType::IDENTIFIER);

        if (CurrentToken()->type == TokenType::OPERATOR && CurrentToken()->value == "=") {
            Consume(TokenType::OPERATOR, "=");
            std::unique_ptr<Expression> value = ParseExpression();
            parameters.push_back(std::make_unique<VarDeclExpr>(type->value, name->value, std::move(value)));
        }
        else {
            parameters.push_back(std::make_unique<VarDeclExpr>(type->value, name->value, nullptr));
        }

        if (CurrentToken()->type == TokenType::DELIMITER && CurrentToken()->value == ",") {
            Consume(TokenType::DELIMITER, ",");
        }
        else if (CurrentToken()->type == TokenType::BRACKET && CurrentToken()->value == ")") {
            break;
        }
    }
    Consume(TokenType::BRACKET, ")"); // ")"
    return parameters;
}

std::vector<std::unique_ptr<Expression>> Parser::ParseArguments()
{
    Consume(TokenType::BRACKET, "("); // "("
    std::vector<std::unique_ptr<Expression>> arguments;
    while (CurrentToken()->type != TokenType::BRACKET && CurrentToken()->value != ")")
    {
        std::unique_ptr<Expression> argument = ParseExpression();
        arguments.push_back(std::move(argument));
        if (CurrentToken()->type == TokenType::DELIMITER && CurrentToken()->value == ",") {
            Consume(TokenType::DELIMITER, ",");
        }
        else if (CurrentToken()->type == TokenType::BRACKET && CurrentToken()->value == ")") {
            break;
        }
    }
    Consume(TokenType::BRACKET, ")"); // ")"
    return arguments;
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
        ScopeType currentScope = GetCurrentScopeType();
        bool inStructure = currentScope == ScopeType::Class || currentScope == ScopeType::Struct;
        if (inStructure && nextToken && nextToken->value == "(" && token->value == GetCurrentScopeName()) {
            return ParseConstructor();
        }
        else {
            std::unique_ptr<Expression> identifier = ParseIdentifierExpr();
            Consume(TokenType::DELIMITER);
            return std::make_unique<SingleStatement>(std::move(identifier));
        }
    }
    ReportSyntaxError(token, "Expected identifier.");
    std::exit(EXIT_FAILURE);
    return nullptr;
}

std::unique_ptr<Expression> Parser::ParseExpression() {
    Token* token = CurrentToken();
    if (!token) {
        ReportSyntaxError(token, "Expected identifier");
        std::exit(EXIT_FAILURE);
        return nullptr;
    }

    // Бинарные операторы
    Token* next = PeekToken(1);
    if (next && next->type == TokenType::OPERATOR) {
        return ParseBinaryExpr(0);
    }

    // Просто парсим любое первичное выражение (числа, идентификаторы, вызовы функций, массивы)
    return ParsePrimaryExpr();
}

std::unique_ptr<AssignmentExpr> Parser::ParseAssignmentExpr(std::unique_ptr<Expression> leftValue)
{
    if (leftValue) {
		Token* op = CurrentToken();
        if (op && GetOperatorCategory(op->value) == OperatorCategory::Assignment) {
            Consume(TokenType::OPERATOR); // Assignment operator
            std::unique_ptr<Expression> rightValue = ParseExpression();
            return std::make_unique<AssignmentExpr>(std::move(leftValue), std::move(op->value), std::move(rightValue));
        }
        ReportSyntaxError(op, "Expected assignment operator");
        std::exit(EXIT_FAILURE);
        return nullptr;
    }
    return nullptr;
}

std::unique_ptr<Expression> Parser::ParseIdentifierExpr() {
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

            // После вызова функции останавливаем цепочку MemberAccess
            next = CurrentToken();
            if (!next || next->value != "." && next->value != "[") {
                break;
            }
        }
        else if (next->value == "[") {
            expr = ParseArrayAccess(std::move(expr));
        }
        else if (next->value == ".") {
            expr = ParseMemberAccess(std::move(expr));
        }
        else {
            break;
        }
    }

    Token* op = CurrentToken();
    if (op && GetOperatorCategory(op->value) == OperatorCategory::Assignment) {
        return ParseAssignmentExpr(std::move(expr));
    }
    return expr;
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
    ReportSyntaxError(token, "Expected literal");
    std::exit(EXIT_FAILURE);
    return nullptr;
}

std::unique_ptr<NumberLiteralExpr> Parser::ParseNumberLiteralExpr()
{
    Token* numberToken = CurrentToken();
	if (numberToken && numberToken->type == TokenType::NUMBER) {
		Consume(TokenType::NUMBER);
		return std::make_unique<NumberLiteralExpr>(numberToken->value);
	}
    ReportSyntaxError(numberToken, "Expected number literal");
    std::exit(EXIT_FAILURE);
    return nullptr;
}

std::unique_ptr<StringLiteralExpr> Parser::ParseStringLiteralExpr() {
    Token* token = CurrentToken();
    if (token && token->type == TokenType::STRING) {
        std::string value = token->value.substr(1, token->value.size() - 2); // Убираем кавычки
        Consume(TokenType::STRING);
        return std::make_unique<StringLiteralExpr>(value);
    }
    ReportSyntaxError(token, "Expected string literal");
    std::exit(EXIT_FAILURE);
    return nullptr;
}

std::unique_ptr<CharLiteralExpr> Parser::ParseCharLiteralExpr() {
    Token* token = CurrentToken();
    if (token && token->type == TokenType::CHAR) {
        Consume(TokenType::CHAR);
        return std::make_unique<CharLiteralExpr>(token->value[1]); // Символ внутри кавычек
    }
    ReportSyntaxError(token, "Expected char literal");
    std::exit(EXIT_FAILURE);
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
    if (!token) {
        ReportSyntaxError(token, "Null token");
        std::exit(EXIT_FAILURE);
        return nullptr;
    }

    // Литералы (числа, строки, символы)
    if (token->type == TokenType::NUMBER || token->type == TokenType::STRING || token->type == TokenType::CHAR) {
        return ParseLiteralExpr();
    }

    // Переменная или массив (x, arr[2])
    if (token->type == TokenType::IDENTIFIER) {
        return ParseIdentifierExpr();
    }

    if (CurrentToken()->value == "new") {
        return ParseConstructorCall();
    }

    // Вложенное выражение в скобках: (a + b)
    if (token->type == TokenType::BRACKET && token->value == "(") {
        Consume(TokenType::BRACKET); // Пропускаем "("
        auto expr = ParseExpression();
        if (!expr)
            return nullptr;

        // Проверяем закрывающую скобку
        Token* closing = CurrentToken();
        if (closing && closing->type == TokenType::BRACKET && closing->value == ")") {
            Consume(TokenType::BRACKET);
        }
        else {
            ReportSyntaxError(closing, "Expected ')'");
            std::exit(EXIT_FAILURE);
            return nullptr;
        }

        return expr;
    }
    ReportSyntaxError(CurrentToken(), "Expected primary expression");
    std::exit(EXIT_FAILURE);
    return nullptr;
}

std::unique_ptr<VarDeclExpr> Parser::ParseVarDeclExpr()
{
    Token* token = CurrentToken();
    if (token && token->type == TokenType::KEYWORD) {
        std::string type = token->value; // Сохраняем тип переменной
        Consume(TokenType::KEYWORD);

        Token* identifier = CurrentToken();
        if (identifier->type == TokenType::IDENTIFIER) {
            // Объявление переменной без инициализации
            if (IsEndOfStatement(*PeekToken(1)) || PeekToken(1)->type == TokenType::KEYWORD) {
                Consume(TokenType::IDENTIFIER);
                return std::make_unique<VarDeclExpr>(token->value, identifier->value, nullptr);
            }
            // Объявление переменной с инициализацией
            else if (PeekToken(1)->type == TokenType::OPERATOR) {
                std::string varName = identifier->value; // Сохраняем имя переменной
                Consume(TokenType::IDENTIFIER);
                Consume(TokenType::OPERATOR);

                Token* valueToken = CurrentToken();
                if (IsLiteral(valueToken->type) || valueToken->type == TokenType::IDENTIFIER) {
                    std::unique_ptr<Expression> value = ParseExpression();
                    return std::make_unique<VarDeclExpr>(token->value, identifier->value, std::move(value));
                }
            }
            // Объявление переменной массива
            else if (PeekToken(1)->type == TokenType::BRACKET) {
                return ParseVarDeclarationArray(*token);
            }
        }
    }
    ReportSyntaxError(token, "Expected variable type");
    std::exit(EXIT_FAILURE);
    return nullptr;
}

std::unique_ptr<ArrayExpr> Parser::ParseArrayValues() {
    std::vector<std::unique_ptr<Expression>> values;

    Consume(TokenType::BRACKET);  // `{`

    while (CurrentToken()->value != "}") {
        if (CurrentToken()->value == "{") {
            values.push_back(ParseArrayValues());  // Рекурсивный вызов для вложенного массива
        }
        else {
            values.push_back(ParseExpression());  // Парсим обычное значение
        }

        // Проверяем `,` между значениями
        if (CurrentToken()->value == ",") {
            Consume(TokenType::DELIMITER);  // `,`
        }
        else if (CurrentToken()->value == "}") {
            break;
        }
        else {
            ReportSyntaxError(CurrentToken(), "Expected '.' or '}'.");
            std::exit(EXIT_FAILURE);
            break;
        }
    }

    Consume(TokenType::BRACKET);  // `}`
    return std::make_unique<ArrayExpr>(std::vector<std::unique_ptr<Expression>>(), std::move(values));
}

std::unique_ptr<Expression> Parser::ParseArrayAccess(std::unique_ptr<Expression> base) {
    std::vector<std::unique_ptr<Expression>> indexes;

    while (CurrentToken()->value == "[") {
        Consume(TokenType::BRACKET);
        indexes.push_back(ParseExpression());
        Consume(TokenType::BRACKET);
    }

    if (indexes.empty()) {
        return base;
    }

    return std::make_unique<ArrayAccessExpr>(std::move(base), std::move(indexes));
}

std::unique_ptr<MemberAccessExpr> Parser::ParseMemberAccess(std::unique_ptr<Expression> base) {
    Consume(TokenType::DELIMITER); // Потребляем `.`

    Token* memberToken = CurrentToken();
    if (!memberToken || memberToken->type != TokenType::IDENTIFIER) {
        ReportSyntaxError(memberToken, "Expected identifier after '.'");
        std::exit(EXIT_FAILURE);
        return nullptr;
    }

    std::string memberName = memberToken->value;
    Consume(TokenType::IDENTIFIER); // Потребляем имя члена

    return std::make_unique<MemberAccessExpr>(std::move(base), memberName);
}

std::unique_ptr<ConstructorCallExpr> Parser::ParseConstructorCall()
{
    Consume(TokenType::KEYWORD, "new");
    Token* identifier = Consume(TokenType::IDENTIFIER);
    std::vector<std::unique_ptr<Expression>> arguments = ParseArguments();
    return std::make_unique<ConstructorCallExpr>(identifier->value, std::move(arguments));
}

std::unique_ptr<InstanceDeclExpr> Parser::ParseInstanceDeclExpr()
{
    Token* constructTypeName = Consume(TokenType::IDENTIFIER);
    Token* identifierName = Consume(TokenType::IDENTIFIER);
    std::unique_ptr<Expression> initializer = nullptr;
    if (CurrentToken()->value == "=") {
        Consume(TokenType::OPERATOR, "=");
        initializer = ParseExpression();
    }
    return std::make_unique<InstanceDeclExpr>(std::move(constructTypeName->value), std::move(identifierName->value), std::move(initializer));
}

std::unique_ptr<VarDeclExpr> Parser::ParseVarDeclarationArray(const Token& type)
{
    Token* identifier = Consume(TokenType::IDENTIFIER);
    std::vector<std::unique_ptr<Expression>> sizes;  // Размеры массива
    while (CurrentToken()->value == "[") {
        Consume(TokenType::BRACKET);  // `[`
        Token* size = CurrentToken();
        if (IsLiteral(size->type) || size->type == TokenType::IDENTIFIER) {
            sizes.push_back(ParseExpression()); // Парсим число внутри `[]`
        }
        else {
            ReportSyntaxError(CurrentToken(), "Expected array size");
            std::exit(EXIT_FAILURE);
        }
        Consume(TokenType::BRACKET);  // `]`
    }

    Token* next = CurrentToken();
    if (next && next->type == TokenType::DELIMITER) {
        return std::make_unique<VarDeclExpr>(
            type.value,
            identifier->value,
            std::make_unique<ArrayExpr>(std::move(sizes), std::vector<std::unique_ptr<Expression>>()),
            true
        );
    }
    else if (next && GetOperatorCategory(next->value) == OperatorCategory::Assignment) {
        Consume(TokenType::OPERATOR);  // `=`
        next = CurrentToken();
        if (next && next->value == "{") {
            auto values = ParseArrayValues();  // Парсим вложенный массив

            return std::make_unique<VarDeclExpr>(
                type.value,
                identifier->value,
                std::make_unique<ArrayExpr>(std::move(sizes), std::move(values->values)),
                true
            );
        }
    }

    return nullptr;
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
        case Hash("void"):
            return ParseFunctionDeclaration();
		case Hash("return"):
			return ParseReturnStmt();
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
        if (PeekToken(1)->type == TokenType::IDENTIFIER) {
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

std::unique_ptr<VarDeclStmt> Parser::ParseVarDeclaration() {
    std::unique_ptr<VarDeclExpr> variableDeclaration = ParseVarDeclExpr();
    Consume(TokenType::DELIMITER, ";");  // Теперь `;` съедается здесь
    if (variableDeclaration) {
        return std::make_unique<VarDeclStmt>(std::move(variableDeclaration));
    }
    return nullptr;
}

std::unique_ptr<FunctionDeclStmt> Parser::ParseFunctionDeclaration()
{
    Token* ret = Consume(TokenType::KEYWORD);
    Token* identifier = Consume(TokenType::IDENTIFIER);

    std::vector<std::unique_ptr<VarDeclExpr>> parameters = ParseParameters();

    // Парсим тело функции
    std::unique_ptr<Statement> body = ParseStatement();

    return std::make_unique<FunctionDeclStmt>(
        std::make_unique<Token>(*ret),   // Создаём `unique_ptr` на копию токена
        std::make_unique<Token>(*identifier),
        std::move(parameters),
        std::move(body)
    );
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

std::unique_ptr<IfStmt> Parser::ParseIfStmt()
{
    Token* token = CurrentToken();
    if (token && token->type == TokenType::KEYWORD && token->value == "if") {
        Consume(TokenType::KEYWORD);
        Consume(TokenType::BRACKET); // "("
        std::unique_ptr<Expression> condition = ParseExpression();
        Consume(TokenType::BRACKET); // ")"

        std::vector<IfStmt::Branch> branches;
        branches.emplace_back(std::move(condition), ParseCompoundStatement());

        std::unique_ptr<Statement> elseBranch = nullptr;

        while (CurrentToken()->type == TokenType::KEYWORD && CurrentToken()->value == "else") {
            Consume(TokenType::KEYWORD);
            if (CurrentToken()->type == TokenType::KEYWORD && CurrentToken()->value == "if") {
                // `else if`
                Consume(TokenType::KEYWORD);
                Consume(TokenType::BRACKET); // "("
                std::unique_ptr<Expression> elseCondition = ParseExpression();
                Consume(TokenType::BRACKET); // ")"
                branches.emplace_back(std::move(elseCondition), ParseCompoundStatement());
            }
            else {
                // `else`
                elseBranch = ParseCompoundStatement();
                break; // `else` — последняя ветка, выходим из цикла
            }
        }

        return std::make_unique<IfStmt>(std::move(branches), std::move(elseBranch));
    }
    return nullptr;
}

std::unique_ptr<ForStmt> Parser::ParseForStmt() {
    if (!CurrentToken() || CurrentToken()->type != TokenType::KEYWORD || CurrentToken()->value != "for")
        return nullptr;
    Consume(TokenType::KEYWORD, "for"); // "for"
    Consume(TokenType::BRACKET, "("); // "("

    std::vector<std::unique_ptr<Expression>> init;
    if (CurrentToken()->type != TokenType::DELIMITER || CurrentToken()->value != ";") {
        do {
            if (CurrentToken()->type == TokenType::KEYWORD) {
                init.push_back(ParseVarDeclExpr());
            }
            else {
                init.push_back(ParseExpression());
            }
        } while (CurrentToken()->type == TokenType::DELIMITER && CurrentToken()->value == "," && Consume(TokenType::DELIMITER));
    }
	Consume(TokenType::DELIMITER, ";"); // ";"
    std::unique_ptr<Expression> condition = nullptr;
    if (CurrentToken()->type != TokenType::DELIMITER || CurrentToken()->value != ";") {
        condition = ParseExpression();
    }
    Consume(TokenType::DELIMITER, ";"); // ";"

    std::vector<std::unique_ptr<Expression>> update;
    if (CurrentToken()->type != TokenType::BRACKET || CurrentToken()->value != ")") {
        do {
            update.push_back(ParseExpression());
        } while (CurrentToken()->type == TokenType::DELIMITER && CurrentToken()->value == "," && Consume(TokenType::DELIMITER));
    }
    Consume(TokenType::BRACKET, ")"); // ")"

    std::unique_ptr<Statement> body = ParseCompoundStatement();

    return std::make_unique<ForStmt>(std::move(init), std::move(condition), std::move(update), std::move(body));
}

std::unique_ptr<WhileStmt> Parser::ParseWhileStmt()
{
    Consume(TokenType::KEYWORD, "while"); // "while"
    Consume(TokenType::BRACKET, "("); // "("
    std::unique_ptr<Expression> condition = ParseExpression();
    Consume(TokenType::BRACKET, ")"); // ")"

    if (!CurrentToken() || CurrentToken()->type != TokenType::BRACKET || CurrentToken()->value != "{") {
        ReportSyntaxError(CurrentToken(), "Expected '{' after while condition");
        std::exit(EXIT_FAILURE);
    }

    std::unique_ptr<Statement> body = ParseCompoundStatement();

    return std::make_unique<WhileStmt>(std::move(condition), std::move(body));
}

std::unique_ptr<DoWhileStmt> Parser::ParseDoWhileStmt()
{
    Consume(TokenType::KEYWORD, "do");

    if (!CurrentToken() || CurrentToken()->type != TokenType::BRACKET || CurrentToken()->value != "{") {
        ReportSyntaxError(CurrentToken(), "Error: Expected '{' after 'do'");
        std::exit(EXIT_FAILURE);
    }

    std::unique_ptr<Statement> body = ParseStatement();

    if (!CurrentToken() || CurrentToken()->type != TokenType::KEYWORD || CurrentToken()->value != "while") {
        ReportSyntaxError(CurrentToken(), "Error: Expected 'while' after do-while body.");
        std::exit(EXIT_FAILURE);
    }

    Consume(TokenType::KEYWORD, "while");
    Consume(TokenType::BRACKET, "(");
    std::unique_ptr<Expression> condition = ParseExpression();
    Consume(TokenType::BRACKET, ")");
    Consume(TokenType::DELIMITER, ";");

    return std::make_unique<DoWhileStmt>(std::move(body), std::move(condition));
}

std::unique_ptr<ForEachStmt> Parser::ParseForEachStmt()
{
    Consume(TokenType::KEYWORD, "foreach");
	Consume(TokenType::BRACKET, "(");
	std::unique_ptr<VarDeclExpr> variable = ParseVarDeclExpr();
	Consume(TokenType::KEYWORD, "in");
	std::unique_ptr<Expression> iterable = ParseExpression();
	Consume(TokenType::BRACKET, ")");
	std::unique_ptr<Statement> body = ParseStatement();
	return std::make_unique<ForEachStmt>(std::move(variable), std::move(iterable), std::move(body));
}

std::unique_ptr<ClassDeclStmt> Parser::ParseClassDecl()
{
    std::vector<std::string> parents;

    Consume(TokenType::KEYWORD, "class"); // class
    Token* identifier = Consume(TokenType::IDENTIFIER); // имя класса

    // Проверяем, есть ли наследование или реализация интерфейсов
    if (CurrentToken()->value == ":") {
        Consume(TokenType::OPERATOR, ":"); // :

        while (true) {
            Token* parent = Consume(TokenType::IDENTIFIER);
            parents.push_back(parent->value);

            Token* nextToken = CurrentToken();
            if (nextToken->value == ",") {
                Consume(TokenType::DELIMITER, ",");
            }
            else if (nextToken->value == "{") {
                break; // Всё ок, начинаем тело класса
            }
            else {
                ReportSyntaxError(CurrentToken(), "Expected ',' or '{' after parent class, but found '" + nextToken->value + "'");
                std::exit(EXIT_FAILURE);
            }
        }
    }
    scopeStack.push_back(ScopeType::Class);
    EnterScope(ScopeType::Class, identifier->value);
    std::unique_ptr<Statement> body = ParseCompoundStatement();
    ExitScope();
    return std::make_unique<ClassDeclStmt>(identifier->value, std::move(parents), std::move(body));
}

std::unique_ptr<ConstructorDeclStmt> Parser::ParseConstructor()
{
    Token* name = Consume(TokenType::IDENTIFIER);

    std::vector<std::unique_ptr<VarDeclExpr>> parameters = ParseParameters();

    std::unique_ptr<Statement> body = ParseStatement();
    return std::make_unique<ConstructorDeclStmt>(std::make_unique<Token>(*name), std::move(parameters), std::move(body));
}

std::unique_ptr<SingleStatement> Parser::ParseInstanceDeclStmt()
{
    std::unique_ptr<InstanceDeclExpr> instanceDecl = ParseInstanceDeclExpr();
    return std::make_unique<SingleStatement>(std::move(instanceDecl));
}

std::unique_ptr<StructDeclStmt> Parser::ParseStructDecl()
{
    return std::unique_ptr<StructDeclStmt>();
}

std::unique_ptr<EnumDeclStmt> Parser::ParseEnumDecl() {
    std::vector<std::string> members;

    Consume(TokenType::KEYWORD, "enum");
    Token* name = Consume(TokenType::IDENTIFIER);
    Consume(TokenType::BRACKET, "{"); // Открывающая `{`

    bool expectComma = false; // Флаг, ожидаем ли запятую перед следующим элементом

    while (CurrentToken()->value != "}") {
        if (expectComma) {
            if (CurrentToken()->value != ",") {
                ReportSyntaxError(CurrentToken(), "Expected ',' between enum members, but found '" + CurrentToken()->value + "'");
                std::exit(EXIT_FAILURE);
            }
            Consume(TokenType::DELIMITER, ",");
        }

        Token* member = Consume(TokenType::IDENTIFIER);
        members.push_back(member->value);

        expectComma = true; // После идентификатора теперь ожидаем запятую
    }

    Consume(TokenType::BRACKET, "}"); // Закрывающая `}`

    return std::make_unique<EnumDeclStmt>(name->value, members);
}

std::unique_ptr<GroupDeclStmt> Parser::ParseGroupDecl() {
    std::vector<std::unique_ptr<EnumDeclStmt>> enums;
    std::vector<std::unique_ptr<GroupDeclStmt>> groups;

    Consume(TokenType::KEYWORD, "group");
    Token* name = Consume(TokenType::IDENTIFIER);
    Consume(TokenType::BRACKET, "{"); // Открывающая `{`

    bool expectComma = false; // Следующий элемент должен идти после запятой

    while (CurrentToken()->value != "}") {
        Token* next = CurrentToken();

        if (next->type == TokenType::KEYWORD) {
            if (next->value == "enum") {
                enums.push_back(ParseEnumDecl());
            }
            else if (next->value == "group") {
                groups.push_back(ParseGroupDecl());
            }
            else {
                ReportSyntaxError(next, "Unexpected keyword '" + next->value + "' in group declaration");
                std::exit(EXIT_FAILURE);
            }
        }
        else {
            ReportSyntaxError(next, "Expected 'enum' or 'group', but found '" + next->value + "'");
            std::exit(EXIT_FAILURE);
        }

    }

    Consume(TokenType::BRACKET, "}"); // Закрывающая `}`

    return std::make_unique<GroupDeclStmt>(name->value, std::move(enums), std::move(groups));
}


