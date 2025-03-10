#pragma once
#include <string>
#include <unordered_map>
#include <iostream>
#include <iterator>
#include <regex>

enum TokenType {
	NUMBER,
	KEYWORD,
	IDENTIFIER,
	OPERATOR,
	DELIMETER,
	STRING,
	CHAR,
	BRACKET,
	COMMENT,
	WHITESPACE
};

struct Token {
	TokenType type;
	std::string value;
};

extern std::unordered_map<TokenType, std::string> token_spec;

std::vector<Token> lexer(const std::string& code);

