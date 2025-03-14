#pragma once
#include "nodes.h"

int GetOperatorPrecedence(const std::string& op);

class Parser {
public:
    std::vector<Token> tokens;
    size_t pos = 0;

    Parser(std::vector<Token> tokens) : tokens(std::move(tokens)) {}

    ~Parser() = default;

    Token* CurrentToken() {
        return (pos < tokens.size()) ? &tokens[pos] : nullptr;
    }

	Token* PeekToken(size_t offset = 1) {
		return (pos + offset < tokens.size()) ? &tokens[pos + offset] : nullptr;
    }

    Token* Consume(TokenType tokenType);
    std::unique_ptr<Program> Parse();
    std::unique_ptr<Expression> ParseExpression();
    std::unique_ptr<AssignmentExpr> ParseAssignmentExpr();
    std::unique_ptr<IdentifierExpr> ParseIdentifierExpr();
	std::unique_ptr<Expression> ParseLiteralExpr();
    std::unique_ptr<NumberLiteralExpr> ParseNumberLiteralExpr();
    std::unique_ptr<StringLiteralExpr> ParseStringLiteralExpr();
    std::unique_ptr<CharLiteralExpr> ParseCharLiteralExpr();
    std::unique_ptr<Expression> ParseBinaryExpr(int minPrecedence);
    std::unique_ptr<Expression> ParsePrimaryExpr();
    std::unique_ptr<FunctionCallExpr> ParseFunctionCallExpr();
    std::unique_ptr<Statement> ParseStatement();
    std::unique_ptr<Statement> ParseIdentifier();
    std::unique_ptr<CompoundStmt> ParseCompoundStatement();
    std::unique_ptr<VarDeclStmt> ParseVarDeclaration();
    std::unique_ptr<FunctionDeclStmt> ParseFunctionDeclaration();
    std::unique_ptr<FunctionCallStmt> ParseFunctionCall();
    std::unique_ptr<AssignmentStmt> ParseAssignmentStmt();
};
