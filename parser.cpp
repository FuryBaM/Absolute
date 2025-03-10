#include "pch.h"
#include "parser.h"

void Parser::Consume(TokenType tokenType)
{
    Token* token = CurrentToken();
    if (token && token->type == tokenType) {
        pos++;
    }
    else {
        throw std::runtime_error("Unexpected token: " + (token ? token->value : "EOF"));
    }
}

ASTNode* Parser::Parse()
{
    std::vector<std::unique_ptr<Statement>> statements;
    while (CurrentToken()) {
        statements.push_back(std::unique_ptr<Statement>(ParseStatement()));
    }
	return new Program(std::move(statements));
}

Statement* Parser::ParseStatement()
{
	Token* token = CurrentToken();
    if (token->type == TokenType::KEYWORD)
    {
        return nullptr; //parse statement
    }
    return nullptr;
}
