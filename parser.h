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
    std::unique_ptr<AssignmentExpr> ParseAssignmentExpr(std::unique_ptr<Expression> leftValue);
    std::unique_ptr<Expression> ParseIdentifierExpr();
	std::unique_ptr<Expression> ParseLiteralExpr();
    std::unique_ptr<NumberLiteralExpr> ParseNumberLiteralExpr();
    std::unique_ptr<StringLiteralExpr> ParseStringLiteralExpr();
    std::unique_ptr<CharLiteralExpr> ParseCharLiteralExpr();
    std::unique_ptr<Expression> ParseBinaryExpr(int minPrecedence);
    std::unique_ptr<Expression> ParsePrimaryExpr();
    std::unique_ptr<FunctionCallExpr> ParseFunctionCallExpr(std::unique_ptr<Expression> base);
	std::unique_ptr<VarDeclExpr> ParseVarDeclExpr();
    std::unique_ptr<Statement> ParseStatement();
    std::unique_ptr<Statement> ParseIdentifier();
    std::unique_ptr<CompoundStmt> ParseCompoundStatement();
    std::unique_ptr<VarDeclStmt> ParseVarDeclaration();
	std::unique_ptr<VarDeclExpr> ParseVarDeclarationArray(const Token& type);
	std::unique_ptr<ArrayExpr> ParseArrayValues();
    std::unique_ptr<Expression> ParseArrayAccess(std::unique_ptr<Expression> base);
    std::unique_ptr<MemberAccessExpr> ParseMemberAccess(std::unique_ptr<Expression> base);
    std::unique_ptr<FunctionDeclStmt> ParseFunctionDeclaration();
	std::unique_ptr<ReturnStmt> ParseReturnStmt();
	std::unique_ptr<IfStmt> ParseIfStmt();
	std::unique_ptr<ForStmt> ParseForStmt();
	std::unique_ptr<WhileStmt> ParseWhileStmt();
	std::unique_ptr<DoWhileStmt> ParseDoWhileStmt();
	std::unique_ptr<ForEachStmt> ParseForEachStmt();
	std::unique_ptr<ClassDeclStmt> ParseClassDecl();
	std::unique_ptr<StructDeclStmt> ParseStructDecl();
	std::unique_ptr<EnumDeclStmt> ParseEnumDecl();
};
