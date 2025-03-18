#pragma once
#include "lexer.h"

class ExpressionVisitor;
class BaseIdentifierVisitor;

struct ASTNode {
    virtual ~ASTNode() = default;
    virtual void print(int indent = 0) {
        std::cout << "ASTNode\n";
    };
};

struct Expression : ASTNode {
    virtual void Accept(ExpressionVisitor& visitor) = 0;
};

struct Statement : ASTNode {};

struct Program : ASTNode {
    std::vector<std::unique_ptr<Statement>> statements;
    explicit Program(std::vector<std::unique_ptr<Statement>> statements)
        : statements(std::move(statements)) {
    }
};

// 🔹 Базовый класс для выражений, которые можно использовать в Statements
struct SingleStatement : Statement {
    std::unique_ptr<Expression> expr;
    explicit SingleStatement(std::unique_ptr<Expression> expr)
        : expr(std::move(expr)) {
    }
    void print(int indent = 0) override {
        if (expr) {
            expr->print(indent);
        }
    }
};

// 🔹 Бинарное выражение (арифметика, логические операции)
struct BinaryExpr : Expression {
    std::string op;
    std::unique_ptr<Expression> left, right;

    BinaryExpr(std::string op, std::unique_ptr<Expression> left, std::unique_ptr<Expression> right)
        : op(std::move(op)), left(std::move(left)), right(std::move(right)) {
    }
    void print(int indent = 0) override {
        std::cout << std::string(indent, ' ') << "Binary expression: " << op << "\n";
        if (left) left->print(indent + 2);
        if (right) right->print(indent + 2);
    }
    void Accept(ExpressionVisitor& visitor) override;
};

// 🔹 Литералы
struct IdentifierExpr : Expression {
    std::string name;
    explicit IdentifierExpr(std::string name) : name(std::move(name)) {}
    IdentifierExpr(IdentifierExpr& other) : name(std::move(other.name)) {}

    void print(int indent = 0) override {
        std::cout << std::string(indent, ' ') << "Identifier : " << name << "\n";
    }

    void Accept(ExpressionVisitor& visitor) override;
};

struct NumberLiteralExpr : Expression {
    std::string value;
    explicit NumberLiteralExpr(std::string value) : value(std::move(value)) {}
    void print(int indent = 0) override {
		std::cout << std::string(indent, ' ') << "Number literal: " << value << "\n";
    }
    void Accept(ExpressionVisitor& visitor) override;
};

struct StringLiteralExpr : Expression {
    std::string value;
    explicit StringLiteralExpr(std::string value) : value(std::move(value)) {}
    void print(int indent = 0) override {
        std::cout << std::string(indent, ' ') << "String literal: " << value << "\n";
    }
    void Accept(ExpressionVisitor& visitor) override;
};

struct CharLiteralExpr : Expression {
    char value;
    explicit CharLiteralExpr(char value) : value(std::move(value)) {}
	void print(int indent = 0) override {
		std::cout << std::string(indent, ' ') << "Char literal: " << value << "\n";
	}
    void Accept(ExpressionVisitor& visitor) override;
};

struct ArrayExpr : Expression {
    std::vector<std::unique_ptr<Expression>> sizes;
    std::vector<std::unique_ptr<Expression>> values;
    explicit ArrayExpr(std::vector<std::unique_ptr<Expression>> sizes, 
        std::vector<std::unique_ptr<Expression>> values) : 
		sizes(std::move(sizes)),
        values(std::move(values)) {}
    void print(int indent = 0) override {
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

    void Accept(ExpressionVisitor& visitor) override;
};

struct ArrayAccessExpr : Expression {
    std::unique_ptr<Expression> base; // Может быть IdentifierExpr, вызов функции и т. д.
    std::vector<std::unique_ptr<Expression>> indexes; // Индексы []

    ArrayAccessExpr(std::unique_ptr<Expression> base, std::vector<std::unique_ptr<Expression>> indexes)
        : base(std::move(base)), indexes(std::move(indexes)) {
    }

    IdentifierExpr* GetIdentifier();

    void print(int indent = 0) override {
        IdentifierExpr* identifier = GetIdentifier();
        std::cout << std::string(indent, ' ') << "Array Access: " << identifier->name << "\n";
        base->print(indent + 1);
        std::cout << std::string(indent + 1, ' ') << "Indexes:" << "\n";
        for (const auto& index : indexes) {
            index->print(indent + 2);
        }
    }

    void Accept(ExpressionVisitor& visitor) override;
};

// 🔹 Присваивание (x = 5)
struct AssignmentExpr : Expression {
    std::unique_ptr<Expression> target;
	std::string op;
    std::unique_ptr<Expression> value;

    AssignmentExpr(std::unique_ptr<Expression> target, std::string op, std::unique_ptr<Expression> value)
        : target(std::move(target)), op(std::move(op)), value(std::move(value)) {
    }
	void print(int indent = 0) override {
		std::cout << std::string(indent, ' ') << "Assignment: " << op << "\n";
        target->print(indent + 1);
		if (value) value->print(indent + 1);
	}

    void Accept(ExpressionVisitor& visitor) override;
};

struct VarDeclExpr : Expression {
    std::string type;
    std::string name;
    std::unique_ptr<Expression> value;  // Для присваивания
    bool isArray = false;
    bool isInstance = false;

    explicit VarDeclExpr(std::string type, std::string name, std::unique_ptr<Expression> value,
        bool isArray = false,
        bool isInstance = false)
        : type(std::move(type)), name(std::move(name)), value(std::move(value)),
        isArray(isArray), isInstance(isInstance) {
    }

    void print(int indent = 0) override {
        std::cout << std::string(indent, ' ') << "Variable declaration: " << type << " " << name <<
            " identifier: " << isArray <<
            " instance: " << isInstance << "\n";
        if (value) value->print(indent + 1);
    }

    void Accept(ExpressionVisitor& visitor) override;
};

// 🔹 Вызов функции (foo())
struct FunctionCallExpr : Expression {
    std::unique_ptr<Expression> base;
    std::vector<std::unique_ptr<Expression>> arguments;

    FunctionCallExpr(std::unique_ptr<Expression> base, std::vector<std::unique_ptr<Expression>> args)
        : base(std::move(base)), arguments(std::move(args)) {
    }

    IdentifierExpr* GetIdentifier();

    void print(int indent = 0) override {
        IdentifierExpr* identifier = GetIdentifier();
        std::cout << std::string(indent, ' ') << "Function call: " << identifier->name << "\n";
        base->print(indent + 1);
        if (arguments.size()) {
            std::cout << std::string(indent + 1, ' ') << "Arguments: " << "\n";
            for (const auto& arg : arguments) {
                arg->print(indent + 2);
            }
        }
    }

    void Accept(ExpressionVisitor& visitor) override;
};

struct MemberAccessExpr : Expression {
    std::unique_ptr<Expression> base;  // К чему применяем `.`
    std::string member;                // Имя члена

    MemberAccessExpr(std::unique_ptr<Expression> base, std::string member)
        : base(std::move(base)), member(std::move(member)) {
    }

    IdentifierExpr* GetIdentifier();

    void print(int indent = 0) override {
        IdentifierExpr* identifier = GetIdentifier();
        std::cout << std::string(indent, ' ') << "Member Access: " << identifier->name <<"\n";
        base->print(indent + 1);
        std::cout << std::string(indent + 1, ' ') << "Member: " << member << "\n";
    }

    void Accept(ExpressionVisitor& visitor) override;
};

struct ConstructorCallExpr : Expression {
    std::string constructName;
    std::vector<std::unique_ptr<Expression>> arguments;

    ConstructorCallExpr(std::string constructName, std::vector<std::unique_ptr<Expression>> arguments)
        : constructName(std::move(constructName)), arguments(std::move(arguments)) {
    }

    void print(int indent = 0) override {
        std::cout << std::string(indent, ' ') << "Constructor call: " << constructName << "\n";
        for (const auto& argument : arguments) {
            if (argument) argument->print(indent + 1);
        }
    }
};

struct InstanceDeclExpr : Expression {
    std::string constructTypeName;
    std::string identifierName;
    std::unique_ptr<ConstructorCallExpr> call;

    InstanceDeclExpr(std::string constructTypeName,
        std::string identifierName,
        std::unique_ptr<ConstructorCallExpr> call)
        : constructTypeName(constructTypeName), identifierName(identifierName), call(std::move(call)) {
    }

    void print(int indent = 0) override {
        std::cout << std::string(indent, ' ') << "Instance " << constructTypeName << " declaration: " << identifierName << "\n";
        call->print(indent + 1);
    }
};

// Обертка, чтобы сделать AssignmentExpr полноценным Statement
struct AssignmentStmt : Statement {
    std::unique_ptr<AssignmentExpr> expr;

    explicit AssignmentStmt(std::unique_ptr<AssignmentExpr> expr)
        : expr(std::move(expr)) {
    }
	void print(int indent = 0) override {
        expr->print(indent);
	}
};

// 🔹 Вызов функции без использования результата (foo();)
struct FunctionCallStmt : Statement {
    std::string name;
    std::unique_ptr<FunctionCallExpr> call;
    explicit FunctionCallStmt(std::string name, std::unique_ptr<FunctionCallExpr> call)
        : name(std::move(name)), call(std::move(call)) {
    }
    void print(int indent = 0) override {
        call->print(indent);
    }
};

// 🔹 Объявление переменной (int x;)
struct VarDeclStmt : Statement {
	std::unique_ptr<VarDeclExpr> expr;
	explicit VarDeclStmt(std::unique_ptr<VarDeclExpr> expr) : expr(std::move(expr)) {}
	void print(int indent = 0) override {
		expr->print(indent);
	}
};

// 🔹 Блок кода { }
struct CompoundStmt : Statement {
    std::vector<std::unique_ptr<Statement>> statements;
    explicit CompoundStmt(std::vector<std::unique_ptr<Statement>> statements)
        : statements(std::move(statements)) {
    }
    void print(int indent = 0) override {
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
	void print(int indent = 0) override {
		std::cout << std::string(indent, ' ') << "Return statement: " << "\n";
		if (expr) expr->print(indent + 1);
	}
};

// 🔹 Объявление функции
struct FunctionDeclStmt : Statement {
    std::unique_ptr<Token> returnType;
    std::unique_ptr<Token> name;
    std::vector<std::unique_ptr<VarDeclExpr>> parameters;
    std::unique_ptr<Statement> body;

    FunctionDeclStmt(std::unique_ptr<Token> returnType, std::unique_ptr<Token> name,
        std::vector<std::unique_ptr<VarDeclExpr>> params,
        std::unique_ptr<Statement> body)
        : returnType(std::move(returnType)),
        name(std::move(name)),
        parameters(std::move(params)),
        body(std::move(body)) {
    }

    void print(int indent = 0) override {
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

struct ConstructorDeclStmt : Statement {
    std::unique_ptr<Token> name;
    std::vector<std::unique_ptr<VarDeclExpr>> parameters;
    std::unique_ptr<Statement> body;

    ConstructorDeclStmt(std::unique_ptr<Token> name,
        std::vector<std::unique_ptr<VarDeclExpr>> parameters, std::unique_ptr<Statement> body)
        : name(std::move(name)), parameters(std::move(parameters)), body(std::move(body)) {
    }

    void print(int indent = 0) override {
        std::cout << std::string(indent, ' ') << "Constructor declaration: " << name->value << "\n";
        if (parameters.size()) {
            std::cout << std::string(indent + 1, ' ') << "Constructor parameters: " << "\n";
            for (const auto& param : parameters) {
                param.get()->print(indent + 2);
            }
        }
        if (body) body->print(indent + 1);
    }
};

struct IfStmt : Statement {
    struct Branch {
        std::unique_ptr<Expression> condition;
        std::unique_ptr<Statement> body;

        Branch(std::unique_ptr<Expression> cond, std::unique_ptr<Statement> stmt)
            : condition(std::move(cond)), body(std::move(stmt)) {
        }
    };

    std::vector<Branch> branches; // if + else if
    std::unique_ptr<Statement> elseBranch; // else (если есть)

    IfStmt(std::vector<Branch> branches, std::unique_ptr<Statement> elseBranch)
        : branches(std::move(branches)), elseBranch(std::move(elseBranch)) {
    }
	void print(int indent = 0) override {
		std::cout << std::string(indent, ' ') << "If statement: " << "\n";
		for (const auto& branch : branches) {
			std::cout << std::string(indent + 1, ' ') << "Branch: " << "\n";
			branch.condition->print(indent + 2);
			branch.body->print(indent + 2);
		}
		if (elseBranch) {
			std::cout << std::string(indent + 1, ' ') << "Else branch: " << "\n";
			elseBranch->print(indent + 2);
		}
	}   
};

struct ForStmt : Statement {
    std::vector<std::unique_ptr<Expression>> init;  // Может быть несколько инициализаций
    std::unique_ptr<Expression> condition;
    std::vector<std::unique_ptr<Expression>> update; // Может быть несколько обновлений
    std::unique_ptr<Statement> body;

    ForStmt(std::vector<std::unique_ptr<Expression>> init,
        std::unique_ptr<Expression> condition,
        std::vector<std::unique_ptr<Expression>> update,
        std::unique_ptr<Statement> body)
        : init(std::move(init)), condition(std::move(condition)),
        update(std::move(update)), body(std::move(body)) {
    }

    void print(int indent = 0) override {
        std::cout << std::string(indent, ' ') << "For statement:\n";

        for (const auto& expr : init) {
            if (expr) expr->print(indent + 1);
        }

        if (condition) condition->print(indent + 1);

        for (const auto& expr : update) {
            if (expr) expr->print(indent + 1);
        }

        if (body) body->print(indent + 1);
    }
};

struct WhileStmt : Statement {
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Statement> body;

    WhileStmt(std::unique_ptr<Expression> condition, std::unique_ptr<Statement> body)
        : condition(std::move(condition)), body(std::move(body)) {
    }
    void print(int indent = 0) override {
        std::cout << std::string(indent, ' ') << "While statement:\n";
        if (condition) condition->print(indent + 1);
        if (body) body->print(indent + 1);
    }
};

struct DoWhileStmt : Statement {
    std::unique_ptr<Statement> body;
    std::unique_ptr<Expression> condition;

    DoWhileStmt(std::unique_ptr<Statement> body, std::unique_ptr<Expression> condition)
        : body(std::move(body)), condition(std::move(condition)) {
    }
	void print(int indent = 0) override {
		std::cout << std::string(indent, ' ') << "Do-while statement:\n";
		if (body) body->print(indent + 1);
		if (condition) condition->print(indent + 1);
	}
};

struct ForEachStmt : Statement {
    std::unique_ptr<VarDeclExpr> var;
    std::unique_ptr<Expression> iterable;
    std::unique_ptr<Statement> body;
    ForEachStmt(std::unique_ptr<VarDeclExpr> var, std::unique_ptr<Expression> iterable, std::unique_ptr<Statement> body)
        : var(std::move(var)), iterable(std::move(iterable)), body(std::move(body)) {
    };
    void print(int indent = 0) override {
		std::cout << std::string(indent, ' ') << "For-each statement:\n";
		if (var) var->print(indent + 1);
		if (iterable) iterable->print(indent + 1);
		if (body) body->print(indent + 1);
    }
};

// 🔹 Классы, структуры, enum
struct ClassDeclStmt : Statement {
    std::string name;
    std::vector<std::string> parents;
    std::unique_ptr<Statement> body;

    ClassDeclStmt(std::string name, std::vector<std::string> parents, std::unique_ptr<Statement> body)
        : name(std::move(name)), parents(std::move(parents)), body(std::move(body)) {
    }

    void print(int indent = 0) override {
        std::cout << std::string(indent, ' ') << "Class Declaration: " << name << " : ";
        for (const auto& parent : parents) {
            std::cout << parent << " ";
        }
        std::cout << "\n";
        body->print(indent + 1);
    }
};

struct StructDeclStmt : Statement {
    std::string name;
    std::vector<std::unique_ptr<Statement>> members;

    StructDeclStmt(std::string name, std::vector<std::unique_ptr<Statement>> members)
        : name(std::move(name)), members(std::move(members)) {
    }
};

struct EnumDeclStmt : Statement {
    std::string name;
    std::vector<std::string> members;

    EnumDeclStmt(std::string name, std::vector<std::string> members)
        : name(std::move(name)), members(std::move(members)) {
    }

    void print(int indent = 0) override {
        std::cout << std::string(indent, ' ') << "Enum: " << name << "\n";
        if (members.size()) {
            for (const auto& member : members) {
                std::cout << std::string(indent + 1, ' ') << "Member: " << member << "\n";;
            }
        }
    }
};

struct GroupDeclStmt : Statement {
    std::string name;
    std::vector<std::unique_ptr<EnumDeclStmt>> enums;
    std::vector<std::unique_ptr<GroupDeclStmt>> subgroups;

    GroupDeclStmt(std::string name, 
        std::vector<std::unique_ptr<EnumDeclStmt>> enums, 
        std::vector<std::unique_ptr<GroupDeclStmt>> subgroups)
        : name(std::move(name)), enums(std::move(enums)), subgroups(std::move(subgroups)) { }

    void print(int indent = 0) override {
        std::cout << std::string(indent, ' ') << "Group: " << name << "\n";
        if (subgroups.size()) {
            std::cout << std::string(indent + 1, ' ') << "Subgroups: \n";
            for (const auto& subgroup : subgroups) {
                subgroup->print(indent + 2);
            }
        }
        if (enums.size()) {
            std::cout << std::string(indent + 1, ' ') << "Enums: \n";
            for (const auto& en : enums) {
                en->print(indent + 2);
            }
        }
    }
};
