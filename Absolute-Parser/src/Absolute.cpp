#include "pch.h"
#include "parser.h"
#include <filesystem>
#include <fstream>
#include <sstream>

void printTokens(const std::vector<Token>& tokens) {
    for (const Token& token : tokens) {
        std::cout << "[" << (int)token.type << "]: " << token.value << "\n";
    }
}

//int main()
//{
//    std::string code = readFile("code.abs"); // Читаем код из файла
//
//    std::vector<Token> tokens = lexer(code);
//
//    //printTokens(tokens);
//
//    Parser parser(tokens);
//    std::unique_ptr<Program> ast(parser.Parse());
//
//    if (!ast) {
//        std::cerr << "Parsing failed!\n";
//        return 1;
//    }
//
//    for (const auto& stmt : ast->statements) {
//        if (stmt == nullptr) {
//            continue;
//        }
//        stmt->print();
//    }
//
//    return 0;
//}