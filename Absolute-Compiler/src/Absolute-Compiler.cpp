#include "pch.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

using namespace Absolute;

namespace {
    struct CommandLine {
        std::filesystem::path source;
        bool emitLlvm = false;
        bool parseOnly = false;
        std::filesystem::path output;
    };

    void PrintUsage() {
        std::cerr << "Usage: absolutec <source.abs> [--parse-only | --emit-llvm] [-o output.ll]\n";
    }

    CommandLine ParseCommandLine(int argc, char* argv[]) {
        if (argc < 2) {
            PrintUsage();
            throw std::invalid_argument("source file is required");
        }

        CommandLine result;
        result.source = argv[1];

        for (int index = 2; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--emit-llvm") {
                result.emitLlvm = true;
            }
            else if (argument == "--parse-only") {
                result.parseOnly = true;
            }
            else if (argument == "-o") {
                if (++index >= argc) {
                    throw std::invalid_argument("-o requires an output path");
                }
                result.output = argv[index];
            }
            else {
                throw std::invalid_argument("unknown argument: " + argument);
            }
        }

        if (!result.output.empty() && !result.emitLlvm) {
            throw std::invalid_argument("-o currently requires --emit-llvm");
        }
        if (result.parseOnly && result.emitLlvm) {
            throw std::invalid_argument("--parse-only cannot be combined with --emit-llvm");
        }
        return result;
    }

    std::string ReadFile(const std::filesystem::path& filename) {
        const std::filesystem::path fullPath = std::filesystem::absolute(filename);
        std::ifstream file(fullPath, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Cannot open source file: " + fullPath.string());
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    void WriteFile(const std::filesystem::path& filename, const std::string& content) {
        std::ofstream file(filename, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Cannot write output file: " + filename.string());
        }
        file << content;
    }
}

int main(int argc, char* argv[]) {
    try {
        const CommandLine commandLine = ParseCommandLine(argc, argv);
        const std::string source = ReadFile(commandLine.source);
        std::unique_ptr<Program> ast = ParseCode(Tokenize(source));

        if (!ast) {
            throw std::runtime_error("Parsing failed");
        }

        Analyzer analyzer({ast.get()});
        if (!commandLine.parseOnly && !analyzer.Analyze()) {
            analyzer.PrintDiagnostics();
            return 1;
        }

        if (commandLine.emitLlvm) {
#ifdef ABSOLUTE_HAS_LLVM
            CodeGenerator generator;
            const std::string ir = generator.Generate(*ast, commandLine.source.filename().string());
            if (commandLine.output.empty()) {
                std::cout << ir;
            }
            else {
                WriteFile(commandLine.output, ir);
            }
            return 0;
#else
            throw std::runtime_error(
                "LLVM backend is unavailable in this build; configure with ABSOLUTE_ENABLE_LLVM=ON");
#endif
        }

        ast->print();
        if (!commandLine.parseOnly) analyzer.PrintVariables();
        return 0;
    }
    catch (const std::invalid_argument& error) {
        PrintUsage();
        std::cerr << "Error: " << error.what() << '\n';
        return 2;
    }
    catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
