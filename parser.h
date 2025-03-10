#pragma once
#include <vector>
#include <string>
#include <memory>
#include "lexer.h"

struct ASTNode {
    virtual ~ASTNode() = default;
};

struct Expression : ASTNode {};

struct BinaryExpr : Expression {
    std::string op;
    std::unique_ptr<Expression> left, right;

    BinaryExpr(std::string op, std::unique_ptr<Expression> left, std::unique_ptr<Expression> right)
        : op(std::move(op)), left(std::move(left)), right(std::move(right)) {
    }
};

struct Identifier : Expression {
    std::string name;
    explicit Identifier(std::string name) : name(std::move(name)) {}
};

struct NumberLiteral : Expression {
    std::string value;
    explicit NumberLiteral(std::string value) : value(std::move(value)) {}
};

struct Statement : ASTNode {};

struct ReturnStmt : Statement {
    std::unique_ptr<Expression> expr;
    explicit ReturnStmt(std::unique_ptr<Expression> expr) : expr(std::move(expr)) {}
};

struct FunctionDecl : Statement {
    std::string returnType, name;
    std::vector<std::pair<std::string, std::string>> parameters;
    std::vector<std::unique_ptr<Statement>> body;

    FunctionDecl(std::string returnType, std::string name, std::vector<std::pair<std::string, std::string>> params)
        : returnType(std::move(returnType)), name(std::move(name)), parameters(std::move(params)) {
    }
};

struct ForStmt : Statement {
    std::unique_ptr<Statement> init;
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Statement> update;
    std::vector<std::unique_ptr<Statement>> body;

    ForStmt(std::unique_ptr<Statement> init, std::unique_ptr<Expression> condition,
        std::unique_ptr<Statement> update, std::vector<std::unique_ptr<Statement>> body)
        : init(std::move(init)), condition(std::move(condition)), update(std::move(update)), body(std::move(body)) {
    }
};

struct WhileStmt : Statement {
    std::unique_ptr<Expression> condition;
    std::vector<std::unique_ptr<Statement>> body;

    WhileStmt(std::unique_ptr<Expression> condition, std::vector<std::unique_ptr<Statement>> body)
        : condition(std::move(condition)), body(std::move(body)) {
    }
};

struct DoWhileStmt : Statement {
    std::vector<std::unique_ptr<Statement>> body;
    std::unique_ptr<Expression> condition;

    DoWhileStmt(std::vector<std::unique_ptr<Statement>> body, std::unique_ptr<Expression> condition)
        : body(std::move(body)), condition(std::move(condition)) {
    }
};

struct ClassDecl : Statement {
    std::string name;
    std::vector<std::unique_ptr<Statement>> members;

    ClassDecl(std::string name, std::vector<std::unique_ptr<Statement>> members)
        : name(std::move(name)), members(std::move(members)) {
    }
};

struct StructDecl : Statement {
    std::string name;
    std::vector<std::unique_ptr<Statement>> members;

    StructDecl(std::string name, std::vector<std::unique_ptr<Statement>> members)
        : name(std::move(name)), members(std::move(members)) {
    }
};

struct EnumDecl : Statement {
	std::string name;
	std::vector<std::string> members;
	EnumDecl(std::string name, std::vector<std::string> members)
		: name(std::move(name)), members(std::move(members)) {
	}
};

class Parser {

};