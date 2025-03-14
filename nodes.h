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
struct IdentifierExpr : Expression {
    std::string name;
    explicit IdentifierExpr(std::string name) : name(std::move(name)) {}
    IdentifierExpr(IdentifierExpr& other) : name(std::move(other.name)) {}
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Identifier : " << name << "\n";
    }
};

struct NumberLiteralExpr : Expression {
    std::string value;
    explicit NumberLiteralExpr(std::string value) : value(std::move(value)) {}
    void print(int indent = 0) const override {
		std::cout << std::string(indent, ' ') << "Number literal: " << value << "\n";
    }
};

struct StringLiteralExpr : Expression {
    std::string value;
    explicit StringLiteralExpr(std::string value) : value(std::move(value)) {}
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "String literal: " << value << "\n";
    }
};

struct CharLiteralExpr : Expression {
    char value;
    explicit CharLiteralExpr(char value) : value(std::move(value)) {}
	void print(int indent = 0) const override {
		std::cout << std::string(indent, ' ') << "Char literal: " << value << "\n";
	}
};

struct ArrayExpr : Expression {
    std::vector<std::unique_ptr<Expression>> sizes;
    std::vector<std::unique_ptr<Expression>> values;
    explicit ArrayExpr(std::vector<std::unique_ptr<Expression>> sizes, 
        std::vector<std::unique_ptr<Expression>> values) : 
		sizes(std::move(sizes)),
        values(std::move(values)) {}
    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Array: " << "\n";

        std::cout << std::string(indent + 1, ' ') << "Sizes:\n";
        for (const auto& size : sizes) {
            size->print(indent + 2);
        }

        std::cout << std::string(indent + 1, ' ') << "Values:\n";
        for (const auto& value : values) {
            value->print(indent + 2);
        }
    }
};

// 🔹 Присваивание (x = 5)
struct AssignmentExpr : Expression {
    std::unique_ptr<IdentifierExpr> target;
	std::string op;
    std::unique_ptr<Expression> value;

    AssignmentExpr(std::unique_ptr<IdentifierExpr> target, std::string op, std::unique_ptr<Expression> value)
        : target(std::move(target)), op(std::move(op)), value(std::move(value)) {
    }
	void print(int indent = 0) const override {
		std::cout << std::string(indent, ' ') << "Assignment: " << target->name << " " << op << "\n";
		if (value) value->print(indent + 1);
	}
};

// 🔹 Вызов функции (foo())
struct FunctionCallExpr : Expression {
    std::unique_ptr<IdentifierExpr> callee;
    std::vector<std::unique_ptr<Expression>> arguments;

    FunctionCallExpr(std::unique_ptr<IdentifierExpr> callee, std::vector<std::unique_ptr<Expression>> args)
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
	void print(int indent = 0) const override {
        expr.get()->print(indent);
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
    std::unique_ptr<Expression> value;  // Для присваивания
    bool isArray = false;
    bool isInstance = false;

    explicit VarDeclStmt(std::string type, std::string name, std::unique_ptr<Expression> value,
        bool isArray = false,
        bool isInstance = false)
        : type(std::move(type)), name(std::move(name)), value(std::move(value)),
        isArray(isArray), isInstance(isInstance) {
    }

    void print(int indent = 0) const override {
        std::cout << std::string(indent, ' ') << "Variable declaration: " << type << " " << name << 
            " array: " << isArray << 
            " instance: " << isInstance << "\n";
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
struct FunctionDeclStmt : Statement {
    std::unique_ptr<Token> returnType;
    std::unique_ptr<Token> name;
    std::vector<std::unique_ptr<VarDeclStmt>> parameters;
    std::unique_ptr<CompoundStmt> body;

    FunctionDeclStmt(std::unique_ptr<Token> returnType, std::unique_ptr<Token> name,
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
