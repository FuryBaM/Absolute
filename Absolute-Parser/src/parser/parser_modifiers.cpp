#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    void Parser::ParseAttributes() {
        attributes.clear();
        while (CurrentToken() && CurrentToken()->type == TokenType::OPERATOR &&
            CurrentToken()->value == "@") {
            const Token marker = *Consume(TokenType::OPERATOR, "@");
            const auto consumeName = [&]() -> Token* {
                Token* token = CurrentToken();
                if (!token || (token->type != TokenType::IDENTIFIER &&
                    token->type != TokenType::KEYWORD)) return nullptr;
                return Consume(token->type);
            };
            Token* first = consumeName();
            if (!first) throw std::runtime_error("Expected attribute name");

            Attribute attribute;
            attribute.name = first->value;
            attribute.line = marker.line;
            attribute.column = marker.column;
            while (CurrentToken() && CurrentToken()->type == TokenType::DELIMITER &&
                CurrentToken()->value == ".") {
                Consume(TokenType::DELIMITER, ".");
                Token* part = consumeName();
                if (!part) throw std::runtime_error("Expected qualified attribute name");
                attribute.name += "." + part->value;
            }

            if (CurrentToken() && CurrentToken()->type == TokenType::BRACKET &&
                CurrentToken()->value == "(") {
                Consume(TokenType::BRACKET, "(");
                while (RequireCurrent("an attribute argument or ')'")->value != ")") {
                    AttributeArgument argument;
                    if (CurrentToken()->type == TokenType::IDENTIFIER && PeekToken() &&
                        PeekToken()->type == TokenType::OPERATOR && PeekToken()->value == "=") {
                        argument.name = Consume(TokenType::IDENTIFIER)->value;
                        Consume(TokenType::OPERATOR, "=");
                    }

                    Token* value = RequireCurrent("a constant attribute argument");
                    if (value->type == TokenType::IDENTIFIER) {
                        argument.value.kind = AttributeValueKind::Identifier;
                        argument.value.text = Consume(TokenType::IDENTIFIER)->value;
                    }
                    else if (value->type == TokenType::STRING) {
                        argument.value.kind = AttributeValueKind::String;
                        argument.value.text = Consume(TokenType::STRING)->value;
                    }
                    else if (value->type == TokenType::NUMBER) {
                        argument.value.kind = AttributeValueKind::Number;
                        argument.value.text = Consume(TokenType::NUMBER)->value;
                    }
                    else if (value->type == TokenType::OPERATOR && value->value == "-" &&
                        PeekToken() && PeekToken()->type == TokenType::NUMBER) {
                        Consume(TokenType::OPERATOR, "-");
                        argument.value.kind = AttributeValueKind::Number;
                        argument.value.text = "-" + Consume(TokenType::NUMBER)->value;
                    }
                    else if (value->type == TokenType::CHAR) {
                        argument.value.kind = AttributeValueKind::Character;
                        argument.value.text = Consume(TokenType::CHAR)->value;
                    }
                    else if (value->type == TokenType::KEYWORD &&
                        (value->value == "true" || value->value == "false")) {
                        argument.value.kind = AttributeValueKind::Boolean;
                        argument.value.text = Consume(TokenType::KEYWORD)->value;
                    }
                    else {
                        ReportSyntaxError(value,
                            "Attribute arguments must be identifiers or literal constants");
                        throw std::runtime_error("Invalid attribute argument");
                    }
                    attribute.arguments.push_back(std::move(argument));

                    if (CurrentToken() && CurrentToken()->type == TokenType::DELIMITER &&
                        CurrentToken()->value == ",") {
                        Consume(TokenType::DELIMITER, ",");
                        continue;
                    }
                    if (!CurrentToken() || CurrentToken()->value != ")") {
                        ReportSyntaxError(CurrentToken(), "Expected ',' or ')' after attribute argument");
                        throw std::runtime_error("Unterminated attribute argument list");
                    }
                }
                Consume(TokenType::BRACKET, ")");
            }
            attributes.push_back(std::move(attribute));
        }
    }

    void Parser::ParseModifiers() {
        modifiers.clear(); // Очищаем перед новым выражением

        while (CurrentToken() && CurrentToken()->type == TokenType::KEYWORD && IsModifier(CurrentToken()->value)) {
            modifiers.push_back(*Consume(TokenType::KEYWORD));
        }
    }
}
