#include "parser_pch.h"
#include "parser.h"

namespace Absolute{
    std::vector<Token> Parser::ParseTemplateParameters() {
        std::vector<Token> parameters;
        if (!CurrentToken() || CurrentToken()->type != TokenType::OPERATOR ||
            CurrentToken()->value != "<") return parameters;
        Consume(TokenType::OPERATOR, "<");
        std::unordered_set<std::string> names;
        while (RequireCurrent("a generic parameter or '>'")->value != ">") {
            Token* parameter = Consume(TokenType::IDENTIFIER);
            if (!names.insert(parameter->value).second) {
                ReportSyntaxError(parameter, "Duplicate generic parameter '" + parameter->value + "'");
                throw std::runtime_error("Duplicate generic parameter");
            }
            parameters.push_back(*parameter);
            if (RequireCurrent("',' or '>'")->value == ",")
                Consume(TokenType::DELIMITER, ",");
            else if (CurrentToken()->value != ">") {
                ReportSyntaxError(CurrentToken(), "Expected ',' or '>' after generic parameter");
                throw std::runtime_error("Invalid generic parameter list");
            }
        }
        ConsumeTemplateClose();
        if (parameters.empty()) {
            ReportSyntaxError(CurrentToken(), "Generic parameter list cannot be empty");
            throw std::runtime_error("Empty generic parameter list");
        }
        return parameters;
    }

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
