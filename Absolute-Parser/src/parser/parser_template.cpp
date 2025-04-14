#include "parser_pch.h"
#include "parser.h"

namespace Absolute{
    std::unique_ptr<Expression> Parser::ParseTemplateExpr(std::unique_ptr<Expression> base)
    {
        std::vector<std::unique_ptr<Expression>> types;
        Consume(TokenType::DOLLAR); // $
        Consume(TokenType::OPERATOR, "<");

        while (CurrentToken()->value != ">") {
            types.push_back(ParseType());

            if (CurrentToken()->value == ",") {
                Consume(TokenType::DELIMITER, ",");
            }
            else {
                ConsumeTemplateClose();
                break;
            }
        }
    
        return std::make_unique<TemplateExpr>(std::move(base), std::move(types));
    }
}