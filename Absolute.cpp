#include <iostream>
#include "lexer.h"

int main()
{
    std::string code = ("int x = 42; // This is comment\n"
        "void main() { int x; }\n"
        "int fun(int x, int y) { return x + y; }\n");
    std::vector<Token> tokens = lexer(code);

    for (const auto& [type, value] : tokens) {
        std::cout << "[" << type << "]: " << value << "\n";
    }
}
