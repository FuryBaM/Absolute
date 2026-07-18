#include "pch.h"

#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

using namespace Absolute;

namespace {
std::string readFile(const std::filesystem::path& filename) {
    std::filesystem::path fullPath = std::filesystem::absolute(filename);
    std::cout << "Opening file: " << fullPath << std::endl;
    std::ifstream file(fullPath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open source file: " + fullPath.string());
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: absolutec <source.abs>\n";
        return 2;
    }

    try {
        std::string code = readFile(argv[1]);
        std::vector<Token> tokens = Tokenize(code);
        std::unique_ptr<Program> ast = ParseCode(tokens);

        if (!ast) {
            std::cerr << "Parsing failed!\n";
            return 1;
        }

        ast->print();

        std::vector<std::unique_ptr<Program>> programs;
        programs.push_back(std::move(ast));
        Analyzer analyzer(std::move(programs));
        analyzer.Analyze();
        analyzer.PrintVariables();
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
