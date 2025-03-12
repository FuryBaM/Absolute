#include "pch.h"
#include "parser.h"

int main()
{
    std::string code = 
        "int x = 42;\n"
        "float y = 3.14;\n"
        "int main() { int z = 5; float c = 2.7; }\n"
        "main(3 + (x - 6) * 7 - x, 7);";
    std::vector<Token> tokens = lexer(code);

    // Выводим токены (исправлено)
    for (const Token& token : tokens) {
        std::cout << "[" << token.type << "]: " << token.value << "\n";
    }

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
        std::cout << typeid(*stmt).name() << std::endl;

        stmt->print();
    }

    return 0;
}
