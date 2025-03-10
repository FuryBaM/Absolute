#include "lexer.h"
#include <regex>

std::unordered_map<TokenType, std::string> token_spec = {
    {TokenType::NUMBER, R"(\d+(\.\d+)?)"},
    {TokenType::KEYWORD, R"(\b(int|float|double|char|bool|string|void|if|else|switch|case|default|for|while|foreach|do|break|continue|new|delete|using|namespace|return|keep|true|false|nullptr|class|struct|enum|this|public|private|protected|sealed|internal|virtual|override|const|static|auto|async|await|catch|finally|try|throw|yield|get|set|operator)\b)"},
    {TokenType::IDENTIFIER, R"([_a-zA-Z][_a-zA-Z0-9]*)"},
    {TokenType::COMMENT, R"(\/\*[\s\S]*?\*\/|\/\/.*)"},
    {TokenType::OPERATOR, R"(\+\+|--|==|!=|<=|>=|&&|\|\||!|~|<<|>>|\+=|-=|\*=|/=|%=|&=|\|=|\^=|[+\-*/=<>&|^])"},
    {TokenType::DELIMETER, R"([;,.])"},
    {TokenType::STRING, R"("(\\.|[^"\\])*")"},
    {TokenType::CHAR, R"('((\\.)|[^'\\])')"},
    {TokenType::BRACKET, R"([\{\}\[\]\(\)])"},
    {TokenType::WHITESPACE, R"(\s+)"},
};

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
