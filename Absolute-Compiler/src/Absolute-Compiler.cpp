#include "pch.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

using namespace Absolute;

namespace {
    namespace fs = std::filesystem;

    enum class CommandMode {
        Compile,
        NewProject
    };

    struct CommandLine {
        CommandMode mode = CommandMode::Compile;
        fs::path input;
        std::string projectName;
        fs::path projectDirectory;
        bool emitLlvm = false;
        bool parseOnly = false;
        fs::path output;
    };

    struct ProjectConfig {
        std::string name;
        fs::path root;
        fs::path entry;
        std::vector<fs::path> sourceDirectories;
    };

    struct Compilation {
        std::unique_ptr<Program> program;
        std::string moduleName;
    };

    void PrintUsage() {
        std::cerr
            << "Usage:\n"
            << "  absolutec <source.abs> [--parse-only | --emit-llvm] [-o output.ll]\n"
            << "  absolutec build <project.absproj> [--parse-only | --emit-llvm] [-o output.ll]\n"
            << "  absolutec new <name> [--directory path]\n";
    }

    void ParseCompileOptions(CommandLine& result, int argc, char* argv[], int firstOption) {
        for (int index = firstOption; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--emit-llvm") result.emitLlvm = true;
            else if (argument == "--parse-only") result.parseOnly = true;
            else if (argument == "-o") {
                if (++index >= argc) throw std::invalid_argument("-o requires an output path");
                result.output = argv[index];
            }
            else throw std::invalid_argument("unknown argument: " + argument);
        }
        if (!result.output.empty() && !result.emitLlvm)
            throw std::invalid_argument("-o currently requires --emit-llvm");
        if (result.parseOnly && result.emitLlvm)
            throw std::invalid_argument("--parse-only cannot be combined with --emit-llvm");
    }

    CommandLine ParseCommandLine(int argc, char* argv[]) {
        if (argc < 2) {
            PrintUsage();
            throw std::invalid_argument("a command or source file is required");
        }

        CommandLine result;
        const std::string command = argv[1];
        if (command == "new") {
            if (argc < 3) throw std::invalid_argument("new requires a project name");
            result.mode = CommandMode::NewProject;
            result.projectName = argv[2];
            result.projectDirectory = result.projectName;
            for (int index = 3; index < argc; ++index) {
                const std::string argument = argv[index];
                if (argument == "--directory") {
                    if (++index >= argc) throw std::invalid_argument("--directory requires a path");
                    result.projectDirectory = argv[index];
                }
                else throw std::invalid_argument("unknown argument for new: " + argument);
            }
            return result;
        }

        if (command == "build") {
            if (argc < 3) throw std::invalid_argument("build requires a .absproj path");
            result.input = argv[2];
            ParseCompileOptions(result, argc, argv, 3);
            return result;
        }

        result.input = argv[1];
        ParseCompileOptions(result, argc, argv, 2);
        return result;
    }

    std::string ReadFile(const fs::path& filename) {
        const fs::path fullPath = fs::absolute(filename);
        std::ifstream file(fullPath, std::ios::binary);
        if (!file) throw std::runtime_error("Cannot open file: " + fullPath.string());
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    void WriteFile(const fs::path& filename, const std::string& content) {
        std::ofstream file(filename, std::ios::binary);
        if (!file) throw std::runtime_error("Cannot write file: " + filename.string());
        file << content;
    }

    std::string JsonString(const std::string& document, const std::string& key) {
        const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
        std::smatch match;
        if (!std::regex_search(document, match, pattern))
            throw std::runtime_error("Project file requires string property '" + key + "'");
        return match[1].str();
    }

    std::vector<std::string> JsonStringArray(const std::string& document, const std::string& key) {
        const std::regex arrayPattern("\\\"" + key + "\\\"\\s*:\\s*\\[([^\\]]*)\\]");
        std::smatch arrayMatch;
        if (!std::regex_search(document, arrayMatch, arrayPattern)) return {};
        std::vector<std::string> result;
        const std::string values = arrayMatch[1].str();
        const std::regex valuePattern("\\\"([^\\\"]+)\\\"");
        for (std::sregex_iterator it(values.begin(), values.end(), valuePattern), end; it != end; ++it)
            result.push_back((*it)[1].str());
        return result;
    }

    ProjectConfig LoadProjectConfig(const fs::path& projectFile) {
        if (projectFile.extension() != ".absproj")
            throw std::runtime_error("Project file must use the .absproj extension");
        ProjectConfig result;
        result.root = fs::absolute(projectFile).parent_path();
        const std::string document = ReadFile(projectFile);
        result.name = JsonString(document, "name");
        result.entry = result.root / JsonString(document, "entry");
        for (const std::string& directory : JsonStringArray(document, "sources"))
            result.sourceDirectories.push_back(result.root / directory);
        if (result.sourceDirectories.empty()) result.sourceDirectories.push_back(result.entry.parent_path());
        return result;
    }

    void CreateProject(const CommandLine& commandLine) {
        if (!std::regex_match(commandLine.projectName, std::regex("[A-Za-z_][A-Za-z0-9_-]*")))
            throw std::runtime_error("Project name must start with a letter or '_' and contain only letters, digits, '_' or '-'");

        const fs::path root = fs::absolute(commandLine.projectDirectory);
        if (fs::exists(root) && !fs::is_empty(root))
            throw std::runtime_error("Project directory is not empty: " + root.string());
        fs::create_directories(root / "src");

        const std::string project =
            "{\n"
            "  \"name\": \"" + commandLine.projectName + "\",\n"
            "  \"entry\": \"src/main.abs\",\n"
            "  \"sources\": [\"src\"]\n"
            "}\n";
        const std::string source =
            "int32 main() {\n"
            "    int32 value = 40;\n"
            "    value += 2;\n"
            "    println(format(\"" + commandLine.projectName + " value={}\", value));\n"
            "    return 0;\n"
            "}\n";
        WriteFile(root / (commandLine.projectName + ".absproj"), project);
        WriteFile(root / "src" / "main.abs", source);
        std::cout << "Created Absolute project at " << root.string() << '\n';
        std::cout << "Build: absolutec build \"" <<
            (root / (commandLine.projectName + ".absproj")).string() << "\"\n";
    }

    class ImportCollector final : public StatementVisitor {
    public:
        std::vector<std::string> files;

        void Visit(ImportStmt* stmt) override { if (stmt->isFile) files.push_back(stmt->target); }
        void Visit(NamespaceDeclStmt* stmt) override {
            if (!stmt->body) return;
            for (const auto& statement : stmt->body->statements) if (statement) statement->Accept(*this);
        }
        void Visit(SingleStatement*) override {}
        void Visit(CompoundStmt*) override {}
        void Visit(FunctionCallStmt*) override {}
        void Visit(FunctionDeclStmt*) override {}
        void Visit(ReturnStmt*) override {}
        void Visit(AssignmentStmt*) override {}
        void Visit(VarDeclStmt*) override {}
        void Visit(StructDeclStmt*) override {}
        void Visit(ClassDeclStmt*) override {}
        void Visit(ConstructorDeclStmt*) override {}
        void Visit(EnumDeclStmt*) override {}
        void Visit(GroupDeclStmt*) override {}
        void Visit(IfStmt*) override {}
        void Visit(ForStmt*) override {}
        void Visit(WhileStmt*) override {}
        void Visit(DoWhileStmt*) override {}
        void Visit(ForEachStmt*) override {}
        void Visit(ContinueStmt*) override {}
        void Visit(BreakStmt*) override {}
    };

    void LoadSource(const fs::path& sourcePath,
        std::vector<std::unique_ptr<Program>>& programs,
        std::unordered_set<std::string>& loaded) {
        if (!fs::exists(sourcePath)) throw std::runtime_error("Imported source does not exist: " + sourcePath.string());
        const fs::path canonical = fs::weakly_canonical(sourcePath);
        const std::string key = canonical.generic_string();
        if (!loaded.insert(key).second) return;
        if (canonical.extension() != ".abs")
            throw std::runtime_error("Absolute source must use the .abs extension: " + canonical.string());

        std::unique_ptr<Program> program = ParseCode(Tokenize(ReadFile(canonical)));
        if (!program) throw std::runtime_error("Parsing failed: " + canonical.string());
        ImportCollector imports;
        for (const auto& statement : program->statements) if (statement) statement->Accept(imports);
        programs.push_back(std::move(program));
        for (const std::string& imported : imports.files)
            LoadSource(canonical.parent_path() / fs::path(imported), programs, loaded);
    }

    Compilation LoadCompilation(const fs::path& input) {
        std::vector<std::unique_ptr<Program>> programs;
        std::unordered_set<std::string> loaded;
        std::string moduleName;

        if (input.extension() == ".absproj") {
            const ProjectConfig project = LoadProjectConfig(input);
            moduleName = project.name;
            LoadSource(project.entry, programs, loaded);
            std::vector<fs::path> sources;
            for (const fs::path& directory : project.sourceDirectories) {
                if (!fs::exists(directory))
                    throw std::runtime_error("Project source directory does not exist: " + directory.string());
                for (const fs::directory_entry& entry : fs::recursive_directory_iterator(directory)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".abs") sources.push_back(entry.path());
                }
            }
            std::sort(sources.begin(), sources.end());
            for (const fs::path& source : sources) LoadSource(source, programs, loaded);
        }
        else {
            moduleName = input.filename().string();
            LoadSource(input, programs, loaded);
        }

        std::vector<std::unique_ptr<Statement>> statements;
        for (auto& program : programs) {
            for (auto& statement : program->statements) statements.push_back(std::move(statement));
        }
        return {std::make_unique<Program>(std::move(statements)), std::move(moduleName)};
    }
}

int main(int argc, char* argv[]) {
    try {
        const CommandLine commandLine = ParseCommandLine(argc, argv);
        if (commandLine.mode == CommandMode::NewProject) {
            CreateProject(commandLine);
            return 0;
        }

        Compilation compilation = LoadCompilation(commandLine.input);
        Analyzer analyzer({compilation.program.get()});
        if (!commandLine.parseOnly && !analyzer.Analyze()) {
            analyzer.PrintDiagnostics();
            return 1;
        }

        if (commandLine.emitLlvm) {
#ifdef ABSOLUTE_HAS_LLVM
            CodeGenerator generator(&analyzer);
            const std::string ir = generator.Generate(*compilation.program, compilation.moduleName);
            if (commandLine.output.empty()) std::cout << ir;
            else WriteFile(commandLine.output, ir);
            return 0;
#else
            throw std::runtime_error(
                "LLVM backend is unavailable in this build; configure with ABSOLUTE_ENABLE_LLVM=ON");
#endif
        }

        compilation.program->print();
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
