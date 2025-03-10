#pragma once

enum TokenType {
	NUMBER = 0,
	KEYWORD = 1,
	IDENTIFIER = 2,
	OPERATOR = 3,
	DELIMETER = 4,
	STRING = 5,
	CHAR = 6,
	COMMENT = 7,
	BRACKET = 8,
	WHITESPACE = 9,
};

struct Token {
	TokenType type;
	std::string value;
};

extern std::unordered_map<TokenType, std::string> token_spec;

std::vector<Token> lexer(const std::string& code);

