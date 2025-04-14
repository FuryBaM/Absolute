#include "pch.h"

#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace Absolute;

std::string readFile(const std::string& filename) {
    std::filesystem::path fullPath = std::filesystem::absolute(filename);
    std::cout << "Opening file: " << fullPath << std::endl;
    std::ifstream file(filename);
    if (!file) {
        std::cerr << "Error: Cannot open file " << filename << "\n";
        exit(1);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

int main() {
    std::string code = readFile("analyzer.abs");
    std::vector<Token> tokens = Tokenize(code);
    std::unique_ptr<Program> ast = ParseCode(tokens);

    if (!ast) {
        std::cerr << "Parsing failed!\n";
        return 1;
    }

    //ast->print();

    std::vector<std::unique_ptr<Program>> programs;
    programs.push_back(std::move(ast));
    Analyzer analyzer(std::move(programs));
    analyzer.Analyze();
    analyzer.PrintVariables();
    return 0;
}
