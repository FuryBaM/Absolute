#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<Expression> Parser::ParsePrefixUnaryExpr() {
        std::string op;
        std::unique_ptr<Expression> operand;
    
        Token* current = RequireCurrent("an expression after unary operator");
        if  (current->type == TokenType::OPERATOR && IsPrefixUnary(*current)){
            op = current->value;
            // Поддерживаем `*`, `&`, `!`, `~`, `+`, `-`
            Consume(TokenType::OPERATOR, op);
            operand = ParsePrefixUnaryExpr();
            return std::make_unique<PrefixUnaryExpr>(op, std::move(operand));
        }
    
        // Теперь парсим операнд
        operand = ParsePrimaryExpr();
    
        // Если не было унарных операторов, просто возвращаем операнд
        if (op.empty()) return operand;
    
        return std::make_unique<PrefixUnaryExpr>(std::move(op), std::move(operand));
    }
    
    std::unique_ptr<Expression> Parser::ParsePostfixUnaryExpr(std::unique_ptr<Expression> base)
    {
        std::string op;
        Token* current = CurrentToken();
        if (current && current->type == TokenType::OPERATOR && IsPostfixUnary(*current)) {
            op = current->value;
            Consume(TokenType::OPERATOR, op);
            return std::make_unique<PostfixUnaryExpr>(op, std::move(base));
        }
        return base;
    }
}
