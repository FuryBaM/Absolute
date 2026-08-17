#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Absolute {
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

	struct Token {
		TokenType type;
		std::string value;
		int line;   // Íîìåð ñòðîêè
		int column; // Íîìåð êîëîíêè

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
	bool IsPrimitiveType(const std::string& value);
	bool IsPrefixUnary(const Token& token);
	bool IsPostfixUnary(const Token& token);
	bool IsValidTokenValue(TokenType tokenType, const std::string& value);
	bool IsLiteral(const TokenType& type);
	// True when a NUMBER token denotes a floating-point value. The dot is not
	// the only spelling: `1e-9` has no dot and is not an integer.
	bool IsFloatingLiteral(const std::string& literal);
	bool IsEndOfStatement(const Token& token);
	OperatorCategory GetOperatorCategory(const std::string& op);
	std::vector<Token> lexer(const std::string& code);
}
