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
    std::unique_ptr<Expression> ParseAssignmentExpr();
    std::unique_ptr<Identifier> ParseIdentifierExpr();
    std::unique_ptr<NumberLiteral> ParseNumberLiteralExpr();
    std::unique_ptr<StringLiteral> ParseStringLiteralExpr();
    std::unique_ptr<CharLiteral> ParseCharLiteralExpr();
    std::unique_ptr<Expression> ParseBinaryExpr(int minPrecedence);
    std::unique_ptr<Expression> ParsePrimaryExpr();
    std::unique_ptr<FunctionCallExpr> ParseFunctionCallExpr();
    std::unique_ptr<Statement> ParseStatement();
    std::unique_ptr<Statement> ParseIdentifier();
    std::unique_ptr<CompoundStmt> ParseCompoundStatement();
    std::unique_ptr<Statement> ParseVarDeclaration();
    std::unique_ptr<Statement> ParseFunctionDeclaration();
    std::unique_ptr<Statement> ParseFunctionCall();
    std::unique_ptr<Statement> ParseAssignment();
};
