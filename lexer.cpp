#include "pch.h"
#include "lexer.h"

std::unordered_map<TokenType, std::string> token_spec = {
    {TokenType::NUMBER, R"(\d+(\.\d+)?)"},
    {TokenType::KEYWORD, R"(\b(int|long|float|double|char|bool|string|void|dynamic|if|else|switch|case|default|for|while|foreach|in|do|break|continue|new|delete|using|namespace|return|keep|true|false|nullptr|class|struct|enum|group|this|public|private|protected|sealed|internal|virtual|override|const|static|auto|async|await|catch|finally|try|throw|yield|get|set|operator)\b)"},
    {TokenType::IDENTIFIER, R"([_a-zA-Z][_a-zA-Z0-9]*)"},
    {TokenType::COMMENT, R"(\/\*[\s\S]*?\*\/|\/\/.*)"},
    {TokenType::OPERATOR, R"(\+\+|--|==|!=|<=|>=|&&|\|\||!|~|<<|>>|\+=|-=|\*=|/=|%=|&=|\|=|\^=|[+\-*/=<>&|^:])"},
    {TokenType::DELIMITER, R"([;,.])"},
    {TokenType::STRING, R"("(\\.|[^"\\])*")"},
    {TokenType::CHAR, R"('((\\.)|[^'\\])')"},
    {TokenType::BRACKET, R"([\{\}\[\]\(\)])"},
    {TokenType::WHITESPACE, R"(\s+)"},
};

std::unordered_map<std::string, int> precedence = {
    {"=", 1}, {"+=", 1}, {"-=", 1}, {"*=", 1}, {"/=", 1}, {"%=", 1}, {"&=", 1}, {"|=", 1}, {"^=", 1}, // Присваивание
    {"||", 2}, // Логическое ИЛИ
    {"&&", 3}, // Логическое И
    {"|", 4},  // Побитовое ИЛИ
    {"^", 5},  // Побитовое исключающее ИЛИ
    {"&", 6},  // Побитовое И
    {"==", 7}, {"!=", 7}, // Равенство
    {"<", 8}, {"<=", 8}, {">", 8}, {">=", 8}, // Сравнение
    {"<<", 9}, {">>", 9}, // Сдвиги
    {"+", 10}, {"-", 10},  // Сложение, вычитание
    {"*", 11}, {"/", 11}, {"%", 11},  // Умножение, деление, остаток
    {"!", 12}, {"~", 12}, // Логическое НЕ, побитовое НЕ (унарные)
    {"++", 13}, {"--", 13}  // Инкремент и декремент (постфикс)
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

// Проверка, входит ли значение в допустимые токены по token_spec
bool IsValidTokenValue(TokenType tokenType, const std::string& value) {
    auto it = token_spec.find(tokenType);
    if (it != token_spec.end()) {
        const std::regex& pattern = std::regex(it->second);
        return std::regex_match(value, pattern);
    }
    return false;
}

int GetOperatorPrecedence(const std::string& op) {
    auto it = precedence.find(op);
    return (it != precedence.end()) ? it->second : -1; // -1 если оператор не найден
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
    static const std::unordered_map<std::string, OperatorCategory> categories = {
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

    auto it = categories.find(op);
    return (it != categories.end()) ? it->second : OperatorCategory::Unknown;
}

std::vector<Token> lexer(const std::string& code)
{
    std::vector<Token> tokens;

    std::string token_regex;
    for (const auto& [_, pattern] : token_spec) {
        if (!token_regex.empty()) token_regex += "|";
        token_regex += "(" + pattern + ")";
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

        // Обновляем `line` и `column`, учитывая пропущенные символы
        for (size_t j = lastPos; j < matchPos; j++) {
            if (code[j] == '\n') {
                line++;
                column = 1;
            }
            else {
                column++;
            }
        }

        for (const auto& [name, pattern] : token_spec) {
            std::regex regex_pattern(pattern);
            if (std::regex_match(value, regex_pattern)) {
                if (name == TokenType::COMMENT) {
                    break; // Пропускаем комментарии
                }

                if (name != TokenType::WHITESPACE) {
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

    return tokens;
}
