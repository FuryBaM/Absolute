#include "pch.h"
#include "parser.h"

void printTokens(const std::vector<Token>& tokens) {
    // Выводим токены (исправлено)
    for (const Token& token : tokens) {
        std::cout << "[" << token.type << "]: " << token.value << "\n";
    }
}

int main()
{
    std::string code = 
        "int x = 42;\n"
        "float y = 3.14;\n"
        "string b = \"dfs\";"
		"char a = 'a';\n"
        "int test(int x = 6) { int z = 5; float c = 2.7; }\n"
        "{\n"
        "main(3 + (c - 6) * 7 - x, 7);\n"
        "foo(x+4);\n"
        "}\n";
    std::vector<Token> tokens = lexer(code);

    //printTokens(tokens);

    Parser parser(tokens);
    std::unique_ptr<Program> ast(parser.Parse()); // Используем `unique_ptr`

    if (!ast) {
        std::cerr << "Parsing failed!\n";
        return 1;
    }

    for (const auto& stmt : ast->statements) {
		if (stmt == nullptr) {
			continue;
		}
        stmt->print();
    }

    return 0;
}
