#pragma once
#include "lexer.h"

struct ASTNode {
    virtual ~ASTNode() = default;
    virtual void print(int indent = 0) const {
        std::cout << "ASTNode\n";
    };
};

struct Expression : ASTNode {};
struct Statement : ASTNode {};

struct Program : ASTNode {
    std::vector<std::unique_ptr<Statement>> statements;
    explicit Program(std::vector<std::unique_ptr<Statement>> statements)
        : statements(std::move(statements)) {
    }
};

// 🔹 Базовый класс для выражений, которые можно использовать в Statements
struct ExpressionStmt : Statement {
    std::unique_ptr<Expression> expr;
    explicit ExpressionStmt(std::unique_ptr<Expression> expr)
        : expr(std::move(expr)) {
    }
};

// 🔹 Бинарное выражение (арифметика, логические операции)
struct BinaryExpr : Expression {
    std::string op;
    std::unique_ptr<Expression> left, right;

    BinaryExpr(std::string op, std::unique_ptr<Expression> left, std::unique_ptr<Expression> right)
        : op(std::move(op)), left(std::move(left)), right(std::move(right)) {
    }
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Binary expression: " << op << "\n";
        if (left) left->print(indent + 2);
        if (right) right->print(indent + 2);
    }
};

// 🔹 Литералы
struct Identifier : Expression {
    std::string name;
    explicit Identifier(std::string name) : name(std::move(name)) {}
    Identifier(Identifier& other) : name(std::move(other.name)) {}
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Identifier : " << name << "\n";
    }
};

struct NumberLiteral : Expression {
    std::string value;
    explicit NumberLiteral(std::string value) : value(std::move(value)) {}
    void print(int indent = 0) const override {
		std::cout << std::string(indent, ' ') << "Number literal: " << value << "\n";
    }
};

struct StringLiteral : Expression {
    std::string value;
    explicit StringLiteral(std::string value) : value(std::move(value)) {}
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "String literal: " << value << "\n";
    }
};

struct CharLiteral : Expression {
    char value;
    explicit CharLiteral(char value) : value(std::move(value)) {}
	void print(int indent = 0) const override {
		std::cout << std::string(indent, ' ') << "Char literal: " << value << "\n";
	}
};

// 🔹 Присваивание (x = 5)
struct AssignmentExpr : Expression {
    std::unique_ptr<Identifier> target;
    std::unique_ptr<Expression> value;

    AssignmentExpr(std::unique_ptr<Identifier> target, std::unique_ptr<Expression> value)
        : target(std::move(target)), value(std::move(value)) {
    }
};

// 🔹 Вызов функции (foo())
struct FunctionCallExpr : Expression {
    std::unique_ptr<Identifier> callee;
    std::vector<std::unique_ptr<Expression>> arguments;

    FunctionCallExpr(std::unique_ptr<Identifier> callee, std::vector<std::unique_ptr<Expression>> args)
        : callee(std::move(callee)), arguments(std::move(args)) {
    }
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Function call: " << callee->name << "\n";
        if (arguments.size()) {
            std::cout << std::string(indent + 1, ' ') << "Arguments: " << "\n";
            for (const auto& arg : arguments) {
                arg.get()->print(indent + 2);
            }
        }
    }
};

// Обертка, чтобы сделать AssignmentExpr полноценным Statement
struct AssignmentStmt : Statement {
    std::unique_ptr<AssignmentExpr> expr;

    explicit AssignmentStmt(std::unique_ptr<AssignmentExpr> expr)
        : expr(std::move(expr)) {
    }
};

// 🔹 Вызов функции без использования результата (foo();)
struct FunctionCallStmt : Statement {
    std::string name;
    std::unique_ptr<FunctionCallExpr> call;
    explicit FunctionCallStmt(std::string name, std::unique_ptr<FunctionCallExpr> call)
        : name(std::move(name)), call(std::move(call)) {
    }
    void print(int indent = 0) const override {
        call.get()->print(indent);
    }
};

// 🔹 Объявление переменной (int x;)
struct VarDeclStmt : Statement {
    std::string type;
    std::string name;
    std::unique_ptr<Expression> value;
    explicit VarDeclStmt(std::string type, std::string name, std::unique_ptr<Expression> value)
        : type(std::move(type)), name(std::move(name)), value(std::move(value)) {
    }
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Variable declaration: " << type << " " << name << "\n";
		if (value) value->print(indent + 1);
    }
};

// 🔹 Блок кода { }
struct CompoundStmt : Statement {
    std::vector<std::unique_ptr<Statement>> statements;
    explicit CompoundStmt(std::vector<std::unique_ptr<Statement>> statements)
        : statements(std::move(statements)) {
    }
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Compound statement: " << "\n";
        for (const auto& stmt : statements) {
            if (stmt)
			stmt->print(indent + 1);
		}
    }
};

// 🔹 return x;
struct ReturnStmt : Statement {
    std::unique_ptr<Expression> expr;
    explicit ReturnStmt(std::unique_ptr<Expression> expr) : expr(std::move(expr)) {}
};

// 🔹 Объявление функции
struct FunctionDecl : Statement {
    std::unique_ptr<Token> returnType;
    std::unique_ptr<Token> name;
    std::vector<std::unique_ptr<VarDeclStmt>> parameters;
    std::unique_ptr<CompoundStmt> body;

    FunctionDecl(std::unique_ptr<Token> returnType, std::unique_ptr<Token> name,
        std::vector<std::unique_ptr<VarDeclStmt>> params,
        std::unique_ptr<CompoundStmt> body)
        : returnType(std::move(returnType)),
        name(std::move(name)),
        parameters(std::move(params)),
        body(std::move(body)) {
    }

    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Function declaration: " << returnType->value << " " << name->value << "\n";
        if (parameters.size()) {
            std::cout << std::string(indent + 1, ' ') << "Function parameters: " << "\n";
            for (const auto& param : parameters) {
                param.get()->print(indent + 2);
            }
        }
        if (body) body->print(indent + 1);
    }
};

// 🔹 Циклы
struct ForStmt : Statement {
    std::unique_ptr<Statement> init;
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Statement> update;
    std::unique_ptr<Statement> body;

    ForStmt(std::unique_ptr<Statement> init, std::unique_ptr<Expression> condition,
        std::unique_ptr<Statement> update, std::unique_ptr<Statement> body)
        : init(std::move(init)), condition(std::move(condition)),
        update(std::move(update)), body(std::move(body)) {
    }
};

struct WhileStmt : Statement {
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Statement> body;

    WhileStmt(std::unique_ptr<Expression> condition, std::unique_ptr<Statement> body)
        : condition(std::move(condition)), body(std::move(body)) {
    }
};

struct DoWhileStmt : Statement {
    std::unique_ptr<Statement> body;
    std::unique_ptr<Expression> condition;

    DoWhileStmt(std::unique_ptr<Statement> body, std::unique_ptr<Expression> condition)
        : body(std::move(body)), condition(std::move(condition)) {
    }
};

// 🔹 Классы, структуры, enum
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
