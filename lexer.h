#pragma once

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
	WHITESPACE = 9,
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

	Token(TokenType type, std::string value)
		: type(type), value(std::move(value)) {
	}

	Token(const Token& other) : type(other.type), value(other.value) {}
};

extern std::unordered_map<TokenType, std::string> token_spec;

bool IsLiteral(const TokenType& type);
bool IsEndOfStatement(const Token& token);
OperatorCategory GetOperatorCategory(const std::string& op);
std::vector<Token> lexer(const std::string& code);

