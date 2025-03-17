#include "pch.h"
#include "lexer.h"

std::unordered_map<TokenType, std::string> token_spec = {
    {TokenType::NUMBER, R"(\d+(\.\d+)?)"},
    {TokenType::KEYWORD, R"(\b(int|long|float|double|char|bool|string|void|dynamic|if|else|switch|case|default|for|while|foreach|in|do|break|continue|new|delete|using|namespace|return|keep|true|false|nullptr|class|struct|enum|this|public|private|protected|sealed|internal|virtual|override|const|static|auto|async|await|catch|finally|try|throw|yield|get|set|operator)\b)"},
    {TokenType::IDENTIFIER, R"([_a-zA-Z][_a-zA-Z0-9]*)"},
    {TokenType::COMMENT, R"(\/\*[\s\S]*?\*\/|\/\/.*)"},
    {TokenType::OPERATOR, R"(\+\+|--|==|!=|<=|>=|&&|\|\||!|~|<<|>>|\+=|-=|\*=|/=|%=|&=|\|=|\^=|[+\-*/=<>&|^:])"},
    {TokenType::DELIMITER, R"([;,.])"},
    {TokenType::STRING, R"("(\\.|[^"\\])*")"},
    {TokenType::CHAR, R"('((\\.)|[^'\\])')"},
    {TokenType::BRACKET, R"([\{\}\[\]\(\)])"},
    {TokenType::WHITESPACE, R"(\s+)"},
};

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

    for (std::sregex_iterator i = begin; i != end; ++i) {
        std::smatch match = *i;
        std::string value = match.str();

        for (const auto& [name, pattern] : token_spec) {
            std::regex regex_pattern(pattern);
            if (std::regex_match(value, regex_pattern)) {
                if (name == TokenType::COMMENT) {
                    break;
                }
                if (name != TokenType::WHITESPACE) {
                    tokens.emplace_back(name, value);
                }
                break;
            }
        }
    }
    return tokens;
}
