#pragma once

#include <unordered_map>
#include <unordered_set>

enum class TokenType {
	NUMBER = 0,
	KEYWORD = 1,
	IDENTIFIER = 2,
	OPERATOR = 3,
	DELIMITER = 4,
	STRING = 5,
	CHAR = 6,
	COMMENT = 7,
	BRACKET = 8,
	DOLLAR = 9,
	WHITESPACE = 10,
};

enum class OperatorCategory {
	Arithmetic,
	Comparison,
	Logical,
	Bitwise,
	Assignment,
	Unknown
};

enum class Type {
	INTEGER,
	LONG,
	FLOAT,
	DOUBLE,
	STRING,
	CHAR,
	BOOL,
	VOID
};

struct Token {
	TokenType type;
	std::string value;
	int line;   // Номер строки
	int column; // Номер колонки

	Token(TokenType type, std::string value, int line, int column)
		: type(type), value(std::move(value)), line(line), column(column) {
	}

	Token(const Token& other)
		: type(other.type), value(other.value), line(other.line), column(other.column) {
	}
};

std::string TokenTypeToString(TokenType type);

extern std::unordered_map<TokenType, std::string> token_spec;
extern std::unordered_map<std::string, int> precedence;

int GetOperatorPrecedence(const std::string& op);

bool IsModifier(const std::string& value);
bool IsDataType(const std::string& value);
bool IsPrefixUnary(const Token& token);
bool IsPostfixUnary(const Token& token);
bool IsValidTokenValue(TokenType tokenType, const std::string& value);
bool IsLiteral(const TokenType& type);
bool IsEndOfStatement(const Token& token);
OperatorCategory GetOperatorCategory(const std::string& op);
std::vector<Token> lexer(const std::string& code);

