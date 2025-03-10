#pragma once
#include "nodes.h"

class Parser {
public:
    std::vector<Token> tokens;
    size_t pos = 0;

    Parser(std::vector<Token> tokens) : tokens(std::move(tokens)) {}

    ~Parser() = default;

    Token* CurrentToken() {
        return (pos < tokens.size()) ? &tokens[pos] : nullptr;
    }

    void Consume(TokenType tokenType);
    ASTNode* Parse();
	Statement* ParseStatement();
	Expression* ParseExpression();
    
};
