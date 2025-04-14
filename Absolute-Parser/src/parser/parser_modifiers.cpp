#include "parser_pch.h"
#include "parser.h"

namespace Absolute {
    void Parser::ParseModifiers() {
        modifiers.clear(); // Очищаем перед новым выражением

        while (CurrentToken()->type == TokenType::KEYWORD && IsModifier(CurrentToken()->value)) {
            modifiers.push_back(*Consume(TokenType::KEYWORD));
        }
    }
}