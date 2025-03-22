#pragma once
#include "nodes.h"

class Parser {
public:
    std::vector<Token> tokens;
    std::vector<Token> modifiers;
    size_t pos = 0;

    Parser(std::vector<Token> tokens) : tokens(std::move(tokens)) {}

    ~Parser() = default;

    Token* CurrentToken() {
        return (pos < tokens.size()) ? &tokens[pos] : nullptr;
    }

	Token* PeekToken(size_t offset = 1) {
		return (pos + offset < tokens.size()) ? &tokens[pos + offset] : nullptr;
    }

    Token* PeekTokenAfterIdentifiers() {
        size_t currentPos = pos;

        // namespace1.namespace2.class
        while (currentPos + 1 < tokens.size() &&
            tokens[currentPos].type == TokenType::IDENTIFIER &&
            tokens[currentPos + 1].type == TokenType::DELIMITER &&
            tokens[currentPos + 1].value == ".")
        {
            currentPos += 2;
        }

        if (currentPos < tokens.size() && tokens[currentPos].type == TokenType::IDENTIFIER) {
            currentPos++;
        }

        return (currentPos < tokens.size()) ? &tokens[currentPos] : nullptr;
    }

    void ReportSyntaxError(const Token* token, const std::string& message);

    Token* Consume(TokenType tokenType);
    Token* Consume(TokenType tokenType, const std::string& expectedValue);

    void ParseModifiers();
    void ConsumeTemplateClose();

    std::unique_ptr<Expression> ParseExpression();
    std::vector<std::unique_ptr<VarDeclExpr>> ParseParameters();
    std::vector<std::unique_ptr<Expression>> ParseArguments();
    std::unique_ptr<AssignmentExpr> ParseAssignmentExpr(std::unique_ptr<Expression> leftValue);
    std::unique_ptr<Expression> ParseIdentifierExpr();
    std::unique_ptr<Expression> ParseTemplateExpr(std::unique_ptr<Expression> base);
	std::unique_ptr<Expression> ParseLiteralExpr();
    std::unique_ptr<NumberLiteralExpr> ParseNumberLiteralExpr();
    std::unique_ptr<StringLiteralExpr> ParseStringLiteralExpr();
    std::unique_ptr<CharLiteralExpr> ParseCharLiteralExpr();
    std::unique_ptr<Expression> ParseBinaryExpr(int minPrecedence, std::unique_ptr<Expression> left);
    std::unique_ptr<Expression> ParsePrimaryExpr();
    std::unique_ptr<FunctionCallExpr> ParseFunctionCallExpr(std::unique_ptr<Expression> base);
	std::unique_ptr<VarDeclExpr> ParseVarDeclExpr();
	std::unique_ptr<ArrayExpr> ParseArrayValues();
    std::unique_ptr<Expression> ParseArrayAccess(std::unique_ptr<Expression> base);
    std::unique_ptr<Expression> ParseMemberAccess(std::unique_ptr<Expression> base);
    std::unique_ptr<ConstructorCallExpr> ParseConstructorCall();
    std::unique_ptr<DestructorCallExpr> ParseDestructorCall();
    std::unique_ptr<InstanceDeclExpr> ParseInstanceDeclExpr();
    std::unique_ptr<Expression> ParseUnaryExpr();

    std::unique_ptr<Program> Parse();
    std::unique_ptr<Statement> ParseStatement();
    std::unique_ptr<Statement> ParseIdentifier();
    std::unique_ptr<CompoundStmt> ParseCompoundStatement();
    std::unique_ptr<VarDeclStmt> ParseVarDeclaration();
    std::unique_ptr<FunctionDeclStmt> ParseFunctionDeclaration();
	std::unique_ptr<ReturnStmt> ParseReturnStmt();
	std::unique_ptr<IfStmt> ParseIfStmt();
	std::unique_ptr<ForStmt> ParseForStmt();
	std::unique_ptr<WhileStmt> ParseWhileStmt();
	std::unique_ptr<DoWhileStmt> ParseDoWhileStmt();
	std::unique_ptr<ForEachStmt> ParseForEachStmt();
	std::unique_ptr<ClassDeclStmt> ParseClassDecl();
    std::unique_ptr<ConstructorDeclStmt> ParseConstructor();
    std::unique_ptr<SingleStatement> ParseInstanceDeclStmt();
	std::unique_ptr<StructDeclStmt> ParseStructDecl();
	std::unique_ptr<EnumDeclStmt> ParseEnumDecl();
    std::unique_ptr<GroupDeclStmt> ParseGroupDecl();
};
