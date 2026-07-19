#include "parser_pch.h"
#include "lexer.h"

#include <regex>
#include <cctype>
#include <stdexcept>

namespace Absolute {
    std::unordered_map<TokenType, std::string> token_spec = {
        {TokenType::NUMBER, R"(\d+(\.\d+)?)"},
        {TokenType::KEYWORD, R"(\b(int8|int16|int32|int64|uint8|uint16|uint32|uint64|float|double|char|bool|string|void|dynamic|auto|if|else|switch|case|default|for|while|foreach|in|do|break|continue|new|delete|raw|using|import|namespace|extern|return|true|false|null|class|struct|enum|group|this|public|private|protected|sealed|internal|virtual|override|const|static|async|await|spawn|catch|finally|try|throw|yield|get|set|operator|as)\b)"},
        {TokenType::IDENTIFIER, R"([_a-zA-Z][_a-zA-Z0-9]*)"},
        {TokenType::COMMENT, R"(\/\*[\s\S]*?\*\/|\/\/.*)"},
        {TokenType::OPERATOR, R"(==|!=|<=|>=|&&|\|\||!|~|<<|>>|\+=|-=|\*=|/=|%=|&=|\|=|\^=|\+\+|--|\?|[+\-*/=<>&%|^:])"},
        {TokenType::DELIMITER, R"([;,.])"},
        {TokenType::STRING, R"("(\\.|[^"\\])*")"},
        {TokenType::CHAR, R"('((\\.)|[^'\\])')"},
        {TokenType::BRACKET, R"([\{\}\[\]\(\)])"},
        {TokenType::WHITESPACE, R"(\s+)"},
    };

    std::unordered_set<std::string> modifiers = {
        "public", "private", "protected", "sealed", "internal",
        "virtual", "override", "const", "static",
        "async"
    };

    std::unordered_map<std::string, int> precedence = {
        {"=", 1}, {"+=", 1}, {"-=", 1}, {"*=", 1}, {"/=", 1}, {"%=", 1}, {"&=", 1}, {"|=", 1}, {"^=", 1}, // Присваивание
        {"?", 2}, // Тернарный оператор (между `||` и `=`)
        {"||", 3}, // Логическое ИЛИ
        {"&&", 4}, // Логическое И
        {"|", 5},  // Побитовое ИЛИ
        {"^", 6},  // Побитовое исключающее ИЛИ
        {"&", 7},  // Побитовое И
        {"==", 8}, {"!=", 8}, // Равенство
        {"<", 9}, {"<=", 9}, {">", 9}, {">=", 9}, // Сравнение
        {"<<", 10}, {">>", 10}, // Сдвиги
        {"+", 11}, {"-", 11},  // Сложение, вычитание
        {"*", 12}, {"/", 12}, {"%", 12},  // Умножение, деление, остаток
        {"!", 13}, {"~", 13}, // Логическое НЕ, побитовое НЕ (унарные)
        {"++", 14}, {"--", 14}  // Инкремент и декремент (постфикс)
    };

    std::unordered_set<std::string> prefixUnaryOps = {
            "*", "&", "+", "-", "++", "--", "!", "~"
    };

    std::unordered_set<std::string> postfixUnaryOps = {
            "++", "--"
    };

    std::unordered_set<std::string> primitiveTypes = {
            "int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64", "float", "double", "bool", "string", "char", "dynamic", "void", "auto"
    };

    std::unordered_map<std::string, OperatorCategory> categories = {
            {"+", OperatorCategory::Arithmetic}, {"-", OperatorCategory::Arithmetic},
            {"*", OperatorCategory::Arithmetic}, {"/", OperatorCategory::Arithmetic},
            {"%", OperatorCategory::Arithmetic}, {"++", OperatorCategory::Arithmetic},
            {"--", OperatorCategory::Arithmetic},

            {"==", OperatorCategory::Comparison}, {"!=", OperatorCategory::Comparison},
            {"<", OperatorCategory::Comparison},  {">", OperatorCategory::Comparison},
            {"<=", OperatorCategory::Comparison}, {">=", OperatorCategory::Comparison},

            {"&&", OperatorCategory::Logical}, {"||", OperatorCategory::Logical},
            {"!", OperatorCategory::Logical},

            {"&", OperatorCategory::Bitwise}, {"|", OperatorCategory::Bitwise},
            {"^", OperatorCategory::Bitwise}, {"~", OperatorCategory::Bitwise},
            {"<<", OperatorCategory::Bitwise}, {">>", OperatorCategory::Bitwise},

            {"=", OperatorCategory::Assignment},  {"+=", OperatorCategory::Assignment},
            {"-=", OperatorCategory::Assignment}, {"*=", OperatorCategory::Assignment},
            {"/=", OperatorCategory::Assignment}, {"%=", OperatorCategory::Assignment},
            {"&=", OperatorCategory::Assignment}, {"|=", OperatorCategory::Assignment},
            {"^=", OperatorCategory::Assignment}
    };

    std::string TokenTypeToString(TokenType type) {
        switch (type) {
        case TokenType::NUMBER: return "NUMBER";
        case TokenType::KEYWORD: return "KEYWORD";
        case TokenType::IDENTIFIER: return "IDENTIFIER";
        case TokenType::OPERATOR: return "OPERATOR";
        case TokenType::DELIMITER: return "DELIMITER";
        case TokenType::STRING: return "STRING";
        case TokenType::CHAR: return "CHAR";
        case TokenType::COMMENT: return "COMMENT";
        case TokenType::BRACKET: return "BRACKET";
        case TokenType::WHITESPACE: return "WHITESPACE";
        default: return "UNKNOWN";
        }
    }

    int GetOperatorPrecedence(const std::string& op) {
        auto it = Absolute::precedence.find(op);
        return (it != Absolute::precedence.end()) ? it->second : -1; // -1 если оператор не найден
    }

    bool IsModifier(const std::string& value) {
        return modifiers.find(value) != modifiers.end();
    }

    bool IsPrimitiveType(const std::string& value) {
        return primitiveTypes.find(value) != primitiveTypes.end();
    }

    bool IsPrefixUnary(const Token& token) {
        return (token.type == TokenType::OPERATOR && prefixUnaryOps.count(token.value) > 0) ||
            (token.type == TokenType::KEYWORD &&
                (token.value == "await" || token.value == "spawn"));
    }

    bool IsPostfixUnary(const Token& token) {
        return token.type == TokenType::OPERATOR && postfixUnaryOps.count(token.value) > 0;
    }

    // Проверка, входит ли значение в допустимые токены по token_spec
    bool IsValidTokenValue(TokenType tokenType, const std::string& value) {
        auto it = Absolute::token_spec.find(tokenType);
        if (it != Absolute::token_spec.end()) {
            const std::regex& pattern = std::regex(it->second);
            return std::regex_match(value, pattern);
        }
        return false;
    }

    bool IsLiteral(const TokenType& type)
    {
        return type == TokenType::NUMBER || type == TokenType::STRING || type == TokenType::CHAR;
    }

    bool IsEndOfStatement(const Token& token)
    {
        return token.type == TokenType::DELIMITER && token.value == ";";
    }

    OperatorCategory GetOperatorCategory(const std::string& op) {
        auto it = categories.find(op);
        return (it != categories.end()) ? it->second : OperatorCategory::Unknown;
    }

    std::vector<Token> lexer(const std::string& code)
    {
        std::vector<Token> tokens;

        // Some token patterns overlap (for example, every keyword also matches
        // IDENTIFIER). Keep matching deterministic instead of depending on the
        // iteration order of unordered_map.
        static const std::vector<TokenType> tokenOrder = {
            TokenType::COMMENT,
            TokenType::STRING,
            TokenType::CHAR,
            TokenType::NUMBER,
            TokenType::KEYWORD,
            TokenType::OPERATOR,
            TokenType::DELIMITER,
            TokenType::BRACKET,
            TokenType::IDENTIFIER,
            TokenType::WHITESPACE,
        };

        std::string token_regex;
        for (TokenType type : tokenOrder) {
            if (!token_regex.empty()) token_regex += "|";
            token_regex += "(" + Absolute::token_spec.at(type) + ")";
        }

        std::regex re(token_regex);
        std::sregex_iterator begin(code.begin(), code.end(), re);
        std::sregex_iterator end;

        int line = 1;
        int column = 1;
        size_t lastPos = 0; // Отслеживаем текущую позицию

        for (std::sregex_iterator i = begin; i != end; ++i) {
            std::smatch match = *i;
            std::string value = match.str();
            size_t matchPos = match.position();

            // Any gap means that the lexer could not recognize source text.
            // Previously such characters were silently discarded.
            for (size_t j = lastPos; j < matchPos; j++) {
                if (!std::isspace(static_cast<unsigned char>(code[j]))) {
                    throw std::runtime_error(
                        "Lexical error at line " + std::to_string(line) +
                        ", column " + std::to_string(column) +
                        ": unexpected character '" + std::string(1, code[j]) + "'");
                }
                if (code[j] == '\n') {
                    line++;
                    column = 1;
                }
                else {
                    column++;
                }
            }

            for (TokenType name : tokenOrder) {
                std::regex regex_pattern(Absolute::token_spec.at(name));
                if (std::regex_match(value, regex_pattern)) {
                    if (name != TokenType::COMMENT && name != TokenType::WHITESPACE) {
                        tokens.emplace_back(name, value, line, column);
                    }

                    // Обновляем `line` и `column` после обработки токена
                    for (char c : value) {
                        if (c == '\n') {
                            line++;
                            column = 1;
                        }
                        else {
                            column++;
                        }
                    }

                    lastPos = matchPos + value.length();
                    break;
                }
            }
        }

        for (size_t j = lastPos; j < code.size(); ++j) {
            if (!std::isspace(static_cast<unsigned char>(code[j]))) {
                throw std::runtime_error(
                    "Lexical error at line " + std::to_string(line) +
                    ", column " + std::to_string(column) +
                    ": unexpected character '" + std::string(1, code[j]) + "'");
            }
            if (code[j] == '\n') {
                ++line;
                column = 1;
            }
            else {
                ++column;
            }
        }

        return tokens;
    }
}
