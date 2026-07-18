#include "parser_pch.h"
#include "parser.h"

namespace Absolute{
    std::unique_ptr<Expression> Parser::ParseTemplateExpr(std::unique_ptr<Expression> base)
    {
        std::vector<std::unique_ptr<Expression>> types;
        Consume(TokenType::OPERATOR, "<");

        while (RequireCurrent("a template argument or '>'")->value != ">") {
            auto type = ParseType();
            if (!type) {
                ReportSyntaxError(CurrentToken(), "Expected a template type argument");
                throw std::runtime_error("Invalid template argument");
            }
            types.push_back(std::move(type));

            if (RequireCurrent("',' or '>'")->value == ",") {
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
