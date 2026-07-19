#include "pch.h"
#include "plugin_loader.h"

#include <algorithm>
#include <cstdlib>
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
        bool emitObject = false;
        bool buildExecutable = false;
        bool parseOnly = false;
        fs::path output;
        std::vector<fs::path> plugins;
    };

    struct ProjectConfig {
        std::string name;
        fs::path root;
        fs::path entry;
        std::vector<fs::path> sourceDirectories;
        std::vector<fs::path> nativeLibraries;
        std::vector<fs::path> nativeSearchPaths;
        std::vector<fs::path> plugins;
    };

    struct Compilation {
        std::unique_ptr<Program> program;
        std::string moduleName;
        std::vector<fs::path> nativeLibraries;
        std::vector<fs::path> nativeSearchPaths;
    };

    void PrintUsage() {
        std::cerr
            << "Usage:\n"
            << "  absolutec <source.abs> [--plugin path] [--parse-only | --emit-llvm | --emit-object | --build-exe] [-o output]\n"
            << "  absolutec build <project.absproj> [--plugin path] [--parse-only | --emit-llvm | --emit-object | --build-exe] [-o output]\n"
            << "  absolutec new <name> [--directory path]\n";
    }

    void ParseCompileOptions(CommandLine& result, int argc, char* argv[], int firstOption) {
        for (int index = firstOption; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--emit-llvm") result.emitLlvm = true;
            else if (argument == "--emit-object") result.emitObject = true;
            else if (argument == "--build-exe") result.buildExecutable = true;
            else if (argument == "--parse-only") result.parseOnly = true;
            else if (argument == "--plugin") {
                if (++index >= argc) throw std::invalid_argument("--plugin requires a library path");
                result.plugins.emplace_back(argv[index]);
            }
            else if (argument == "-o") {
                if (++index >= argc) throw std::invalid_argument("-o requires an output path");
                result.output = argv[index];
            }
            else throw std::invalid_argument("unknown argument: " + argument);
        }
        const int outputModes = static_cast<int>(result.emitLlvm) + static_cast<int>(result.emitObject) +
            static_cast<int>(result.buildExecutable);
        if (outputModes > 1 || (result.parseOnly && outputModes != 0))
            throw std::invalid_argument("choose only one of --parse-only, --emit-llvm, --emit-object or --build-exe");
        if (!result.output.empty() && outputModes == 0)
            throw std::invalid_argument("-o requires --emit-llvm, --emit-object or --build-exe");
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
        for (const std::string& path : JsonStringArray(document, "nativeSearchPaths"))
            result.nativeSearchPaths.push_back(fs::path(path).is_absolute() ? fs::path(path) : result.root / path);
        for (const std::string& path : JsonStringArray(document, "nativeLibraries")) {
            fs::path library(path);
            const fs::path relativeToProject = result.root / library;
            if (!library.is_absolute() && (library.has_parent_path() || fs::exists(relativeToProject)))
                library = relativeToProject;
            result.nativeLibraries.push_back(std::move(library));
        }
        for (const std::string& path : JsonStringArray(document, "plugins")) {
            const fs::path plugin(path);
            result.plugins.push_back(plugin.is_absolute() ? plugin : result.root / plugin);
        }
        return result;
    }

    void CreateProject(const CommandLine& commandLine) {
        if (!std::regex_match(commandLine.projectName, std::regex("[A-Za-z_][A-Za-z0-9_-]*")))
            throw std::runtime_error("Project name must start with a letter or '_' and contain only letters, digits, '_' or '-'");

        const fs::path root = fs::absolute(commandLine.projectDirectory);
        if (fs::exists(root) && !fs::is_empty(root))
            throw std::runtime_error("Project directory is not empty: " + root.string());
        fs::create_directories(root / "src");
        fs::create_directories(root / "native");

        const std::string project =
            "{\n"
            "  \"name\": \"" + commandLine.projectName + "\",\n"
            "  \"entry\": \"src/main.abs\",\n"
            "  \"sources\": [\"src\"],\n"
            "  \"plugins\": [],\n"
            "  \"nativeLibraries\": [],\n"
            "  \"nativeSearchPaths\": [\"native\"]\n"
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

    Compilation LoadCompilation(const fs::path& input, PluginManager& plugins) {
        std::vector<std::unique_ptr<Program>> programs;
        std::unordered_set<std::string> loaded;
        std::string moduleName;
        std::vector<fs::path> nativeLibraries;
        std::vector<fs::path> nativeSearchPaths;

        if (input.extension() == ".absproj") {
            const ProjectConfig project = LoadProjectConfig(input);
            for (const fs::path& plugin : project.plugins) plugins.Load(plugin);
            moduleName = project.name;
            nativeLibraries = project.nativeLibraries;
            nativeSearchPaths = project.nativeSearchPaths;
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
        return {std::make_unique<Program>(std::move(statements)), std::move(moduleName),
            std::move(nativeLibraries), std::move(nativeSearchPaths)};
    }

#ifdef ABSOLUTE_HAS_LLVM
    std::string QuoteResponseArgument(const fs::path& path) {
        const std::string value = path.string();
        if (value.find_first_of("\"\r\n") != std::string::npos)
            throw std::runtime_error("Native path contains an unsupported character: " + value);
        return "\"" + value + "\"";
    }

    std::string QuoteShellArgument(const std::string& value) {
#ifdef _WIN32
        if (value.find('"') != std::string::npos)
            throw std::runtime_error("Command path contains an unsupported quote");
        return "\"" + value + "\"";
#else
        std::string quoted = "'";
        for (char character : value) quoted += character == '\'' ? "'\\''" : std::string(1, character);
        return quoted + "'";
#endif
    }

    void BuildExecutable(CodeGenerator& generator, Program& program, const Compilation& compilation,
        const fs::path& requestedOutput) {
        fs::path executable = fs::absolute(requestedOutput);
        if (!executable.parent_path().empty()) fs::create_directories(executable.parent_path());
        fs::path object = executable;
#ifdef _WIN32
        object.replace_extension(".obj");
#else
        object.replace_extension(".o");
#endif
        generator.GenerateObject(program, compilation.moduleName, object.string());

        fs::path response = executable;
        response += ".absolute-link.rsp";
        std::ostringstream arguments;
#ifdef _WIN32
        arguments << "/nologo\n" << QuoteResponseArgument(object) << '\n';
#ifdef ABSOLUTE_RUNTIME_LIBRARY
        arguments << QuoteResponseArgument(ABSOLUTE_RUNTIME_LIBRARY) << '\n';
#endif
        for (const fs::path& library : compilation.nativeLibraries)
            arguments << QuoteResponseArgument(library) << '\n';
        arguments << "/Fe:" << QuoteResponseArgument(executable) << "\n/link\n";
        for (const fs::path& path : compilation.nativeSearchPaths)
            arguments << "/LIBPATH:" << QuoteResponseArgument(path) << '\n';
#else
        arguments << QuoteResponseArgument(object) << '\n';
#ifdef ABSOLUTE_RUNTIME_LIBRARY
        arguments << QuoteResponseArgument(ABSOLUTE_RUNTIME_LIBRARY) << '\n';
#endif
        for (const fs::path& library : compilation.nativeLibraries)
            arguments << QuoteResponseArgument(library) << '\n';
        for (const fs::path& path : compilation.nativeSearchPaths)
            arguments << "-L" << QuoteResponseArgument(path) << '\n';
        arguments << "-o\n" << QuoteResponseArgument(executable) << '\n';
#endif
        WriteFile(response, arguments.str());

#ifndef ABSOLUTE_HOST_CXX_COMPILER
#define ABSOLUTE_HOST_CXX_COMPILER "c++"
#endif
        const std::string command = QuoteShellArgument(ABSOLUTE_HOST_CXX_COMPILER) +
            " @" + QuoteShellArgument(response.string());
        const int status = std::system(command.c_str());
        std::error_code ignored;
        fs::remove(response, ignored);
        if (status != 0) throw std::runtime_error("Native linker failed with exit code " + std::to_string(status));
        std::cout << "Built " << executable.string() << '\n';
        std::cout << "Object " << object.string() << '\n';
    }
#endif
}

int main(int argc, char* argv[]) {
    try {
        const CommandLine commandLine = ParseCommandLine(argc, argv);
        if (commandLine.mode == CommandMode::NewProject) {
            CreateProject(commandLine);
            return 0;
        }

        PluginManager plugins;
        for (const fs::path& plugin : commandLine.plugins) plugins.Load(plugin);
        Compilation compilation = LoadCompilation(commandLine.input, plugins);
        Analyzer analyzer({compilation.program.get()});
        if (!commandLine.parseOnly && !analyzer.Analyze()) {
            analyzer.PrintDiagnostics();
            return 1;
        }

        if (commandLine.emitLlvm || commandLine.emitObject || commandLine.buildExecutable) {
#ifdef ABSOLUTE_HAS_LLVM
            CodeGenerator generator(&analyzer);
            if (commandLine.emitLlvm) {
                const std::string ir = generator.Generate(*compilation.program, compilation.moduleName);
                if (commandLine.output.empty()) std::cout << ir;
                else WriteFile(commandLine.output, ir);
            }
            else if (commandLine.emitObject) {
                fs::path output = commandLine.output;
                if (output.empty()) {
                    output = compilation.moduleName;
#ifdef _WIN32
                    output.replace_extension(".obj");
#else
                    output.replace_extension(".o");
#endif
                }
                generator.GenerateObject(*compilation.program, compilation.moduleName, output.string());
            }
            else {
                fs::path output = commandLine.output;
                if (output.empty()) {
                    output = compilation.moduleName;
#ifdef _WIN32
                    output.replace_extension(".exe");
#endif
                }
                BuildExecutable(generator, *compilation.program, compilation, output);
            }
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
