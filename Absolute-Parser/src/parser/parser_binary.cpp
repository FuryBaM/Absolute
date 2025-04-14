#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    std::unique_ptr<Expression> Parser::ParseBinaryExpr(int minPrecedence, std::unique_ptr<Expression> left) {
        while (true) {
            Token* opToken = CurrentToken();
            if (!opToken || opToken->type != TokenType::OPERATOR)
                break;

            int precedence = GetOperatorPrecedence(opToken->value);
            if (precedence < minPrecedence)
                break; // Если оператор менее приоритетный, выходим

            if (opToken->value == "?") {
                return ParseTernaryExpr(std::move(left));
            }

            // Сохраняем значение оператора до его потребления
            std::string op = opToken->value;
            Consume(TokenType::OPERATOR);

            std::unique_ptr<Expression> right = ParsePrimaryExpr();
            if (!right) return nullptr;

            while (true) {
                Token* nextOp = CurrentToken();
                if (!nextOp || nextOp->type != TokenType::OPERATOR)
                    break;

                int nextPrecedence = GetOperatorPrecedence(nextOp->value);
                if (nextPrecedence <= precedence)
                    break;

                right = ParseBinaryExpr(precedence + 1, std::move(right));
            }

            left = std::make_unique<BinaryExpr>(op, std::move(left), std::move(right));
        }

        return left;
    }
}