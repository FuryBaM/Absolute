#include "pch.h"

#ifndef ABSOLUTE_VERSION
#define ABSOLUTE_VERSION "0.0.0"
#endif
#include "plugin_loader.h"
#include "package_manager.h"
#include "syntax_plugins.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

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
        bool buildLibrary = false;
        bool parseOnly = false;
        bool sanitizeAddress = false;
        bool debugInfo = false;
        std::optional<OptimizationLevel> optimizationLevel;
        std::string targetTriple; // empty => host default
        fs::path output;
        std::vector<fs::path> plugins;
        std::vector<fs::path> pluginSearchPaths;
        std::string projectType = "app";
    };

    bool IsWebAssemblyTargetTriple(const std::string& triple) {
        if (triple.empty()) return false;
        const std::string lower = [&] {
            std::string value = triple;
            for (char& ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            return value;
        }();
        return lower.starts_with("wasm32") || lower.starts_with("wasm64") ||
            lower.find("wasm32") != std::string::npos ||
            lower.find("wasm64") != std::string::npos;
    }

    fs::path FindWasmLd() {
        if (const char* fromEnv = std::getenv("ABSOLUTE_WASM_LD")) {
            const fs::path candidate(fromEnv);
            if (fs::exists(candidate)) return candidate;
        }
#ifdef ABSOLUTE_WASM_LD
        {
            const fs::path candidate(ABSOLUTE_WASM_LD);
            if (fs::exists(candidate)) return candidate;
        }
#endif
#ifdef _WIN32
        const char* pathEnv = std::getenv("PATH");
        const char pathSep = ';';
        const char* wasmName = "wasm-ld.exe";
#else
        const char* pathEnv = std::getenv("PATH");
        const char pathSep = ':';
        const char* wasmName = "wasm-ld";
#endif
        if (pathEnv) {
            const std::string path = pathEnv;
            size_t start = 0;
            while (start <= path.size()) {
                const size_t end = path.find(pathSep, start);
                const std::string dir = path.substr(start,
                    end == std::string::npos ? std::string::npos : end - start);
                if (!dir.empty()) {
                    const fs::path candidate = fs::path(dir) / wasmName;
                    if (fs::exists(candidate)) return candidate;
                }
                if (end == std::string::npos) break;
                start = end + 1;
            }
        }
        const fs::path cwdCandidate = fs::path(".absolute") / "toolchains" /
            "llvm-18.1.8" / "bin" / wasmName;
        if (fs::exists(cwdCandidate)) return fs::absolute(cwdCandidate);
        return {};
    }

    struct ProjectConfig {
        std::string name;
        std::string type = "app";
        fs::path root;
        fs::path entry;
        std::vector<fs::path> sourceDirectories;
        std::vector<fs::path> nativeLibraries;
        std::vector<fs::path> nativeSearchPaths;
        std::vector<fs::path> plugins;
        std::vector<fs::path> pluginSearchPaths;
    };

    struct Compilation {
        std::unique_ptr<Program> program;
        std::string moduleName;
        std::vector<fs::path> nativeLibraries;
        std::vector<fs::path> nativeSearchPaths;
        bool sanitizeAddress = false;
        bool debugInfo = false;
        OptimizationLevel optimizationLevel = OptimizationLevel::O3;
    };

    void PrintUsage(std::ostream& output = std::cerr) {
        output
            << "Usage:\n"
            << "  absolutec <source.abs> [options]\n"
            << "  absolutec build <project.absproj> [options]\n"
            << "  absolutec new <name> [--type app|lib] [--directory path]\n"
            << "  absolutec --version\n"
            << "Options:\n"
            << "  --parse-only | --emit-llvm | --emit-object | --build-exe | --build-library\n"
            << "  --target <triple>     host default, or e.g. wasm32-unknown-unknown\n"
            << "  -O0 | -O1 | -O2 | -O3\n"
            << "  -g                    emit source and local-variable debug information\n"
            << "  --sanitize=address    (host targets only)\n"
            << "  --plugin path | --plugin-path directory | -o output\n";
    }

    void ParseCompileOptions(CommandLine& result, int argc, char* argv[], int firstOption) {
        for (int index = firstOption; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--emit-llvm") result.emitLlvm = true;
            else if (argument == "--emit-object") result.emitObject = true;
            else if (argument == "--build-exe") result.buildExecutable = true;
            else if (argument == "--build-library") result.buildLibrary = true;
            else if (argument == "--parse-only") result.parseOnly = true;
            else if (argument == "--sanitize=address") result.sanitizeAddress = true;
            else if (argument == "-g" || argument == "--debug-info") result.debugInfo = true;
            else if (argument == "-O0") {
                result.optimizationLevel = OptimizationLevel::O0;
            }
            else if (argument == "-O1") {
                result.optimizationLevel = OptimizationLevel::O1;
            }
            else if (argument == "-O2") {
                result.optimizationLevel = OptimizationLevel::O2;
            }
            else if (argument == "-O3") {
                result.optimizationLevel = OptimizationLevel::O3;
            }
            else if (argument == "--target") {
                if (++index >= argc) throw std::invalid_argument("--target requires a triple");
                result.targetTriple = argv[index];
            }
            else if (argument.starts_with("--target=")) {
                result.targetTriple = argument.substr(std::string("--target=").size());
                if (result.targetTriple.empty())
                    throw std::invalid_argument("--target requires a triple");
            }
            else if (argument == "--plugin") {
                if (++index >= argc) throw std::invalid_argument("--plugin requires a library path");
                result.plugins.emplace_back(argv[index]);
            }
            else if (argument == "--plugin-path") {
                if (++index >= argc) throw std::invalid_argument("--plugin-path requires a directory");
                result.pluginSearchPaths.emplace_back(argv[index]);
            }
            else if (argument == "-o") {
                if (++index >= argc) throw std::invalid_argument("-o requires an output path");
                result.output = argv[index];
            }
            else throw std::invalid_argument("unknown argument: " + argument);
        }
        const int outputModes = static_cast<int>(result.emitLlvm) + static_cast<int>(result.emitObject) +
            static_cast<int>(result.buildExecutable) + static_cast<int>(result.buildLibrary);
        if (outputModes > 1 || (result.parseOnly && outputModes != 0))
            throw std::invalid_argument(
                "choose only one of --parse-only, --emit-llvm, --emit-object, "
                "--build-exe or --build-library");
        if (!result.output.empty() && outputModes == 0)
            throw std::invalid_argument(
                "-o requires --emit-llvm, --emit-object, --build-exe or --build-library");
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
                else if (argument == "--type") {
                    if (++index >= argc) throw std::invalid_argument("--type requires app or lib");
                    result.projectType = argv[index];
                }
                else if (argument.starts_with("--type=")) {
                    result.projectType = argument.substr(std::string("--type=").size());
                }
                else throw std::invalid_argument("unknown argument for new: " + argument);
            }
            if (result.projectType == "application" || result.projectType == "exe")
                result.projectType = "app";
            if (result.projectType == "library")
                result.projectType = "lib";
            if (result.projectType != "app" && result.projectType != "lib")
                throw std::invalid_argument("--type must be app or lib");
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

    std::string JsonOptionalString(const std::string& document, const std::string& key,
        const std::string& fallback) {
        const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
        std::smatch match;
        return std::regex_search(document, match, pattern) ? match[1].str() : fallback;
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
        if (projectFile.extension() != ".absproj" && projectFile.filename() != "package.abs" && projectFile.filename() != "abspackage.json")
            throw std::runtime_error("Project file must use .absproj, package.abs or abspackage.json extension");
        ProjectConfig result;
        result.root = fs::absolute(projectFile).parent_path();
        const std::string document = ReadFile(projectFile);
        result.name = JsonString(document, "name");
        result.type = JsonOptionalString(document, "type", "app");
        if (result.type == "application" || result.type == "exe") result.type = "app";
        if (result.type == "library") result.type = "lib";
        if (result.type != "app" && result.type != "lib")
            throw std::runtime_error("Project property 'type' must be 'app' or 'lib'");
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
        for (const std::string& path : JsonStringArray(document, "pluginSearchPaths")) {
            const fs::path directory(path);
            result.pluginSearchPaths.push_back(directory.is_absolute() ? directory : result.root / directory);
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

        const bool library = commandLine.projectType == "lib";
        std::string symbolName = commandLine.projectName;
        std::transform(symbolName.begin(), symbolName.end(), symbolName.begin(),
            [](unsigned char ch) {
                if (ch == '-') return '_';
                return static_cast<char>(std::tolower(ch));
            });
        const std::string entry = library ? "src/lib.abs" : "src/main.abs";
        const std::string project =
            "{\n"
            "  \"name\": \"" + commandLine.projectName + "\",\n"
            "  \"type\": \"" + commandLine.projectType + "\",\n"
            "  \"entry\": \"" + entry + "\",\n"
            "  \"sources\": [\"src\"],\n"
            "  \"runArgs\": []\n"
            "}\n";
        const std::string source = library
            ? "export \"C\" int32 " + symbolName + "_add(int32 left, int32 right) {\n"
                "    return left + right;\n"
                "}\n"
            : "int32 main() {\n"
                "    println(\"Hello from " + commandLine.projectName + "!\");\n"
                "    return 0;\n"
                "}\n";
        WriteFile(root / (commandLine.projectName + ".absproj"), project);
        WriteFile(root / entry, source);
        std::cout << "Created Absolute " << (library ? "library" : "application")
            << " project at " << root.string() << '\n';
        std::cout << "Next:\n"
            << "  absolute build \"" << root.string() << "\"\n";
        if (!library) std::cout << "  absolute run \"" << root.string() << "\"\n";
    }

    struct CollectedImport {
        std::string target;
        bool isFile;
    };

    class ImportCollector final : public StatementVisitor {
    public:
        std::vector<CollectedImport> imports;

        void Visit(ImportStmt* stmt) override { imports.push_back({stmt->target, stmt->isFile}); }
        void Visit(NamespaceDeclStmt* stmt) override {
            if (!stmt->body) return;
            for (const auto& statement : stmt->body->statements) if (statement) statement->Accept(*this);
        }
        void Visit(SingleStatement*) override {}
        void Visit(CompoundStmt*) override {}
        void Visit(FunctionCallStmt*) override {}
        void Visit(FunctionDeclStmt*) override {}
        void Visit(PropertyDeclStmt*) override {}
        void Visit(IndexerDeclStmt*) override {}
        void Visit(ReturnStmt*) override {}
        void Visit(AssignmentStmt*) override {}
        void Visit(VarDeclStmt*) override {}
        void Visit(StructDeclStmt*) override {}
        void Visit(ClassDeclStmt*) override {}
        void Visit(InterfaceDeclStmt*) override {}
        void Visit(ConstructorDeclStmt*) override {}
        void Visit(EnumDeclStmt*) override {}
        void Visit(GroupDeclStmt*) override {}
        void Visit(IfStmt*) override {}
        void Visit(SwitchStmt*) override {}
        void Visit(ThrowStmt*) override {}
        void Visit(TryStmt*) override {}
        void Visit(DeferStmt*) override {}
        void Visit(ForStmt*) override {}
        void Visit(WhileStmt*) override {}
        void Visit(DoWhileStmt*) override {}
        void Visit(ForEachStmt*) override {}
        void Visit(ContinueStmt*) override {}
        void Visit(BreakStmt*) override {}
        void Visit(TypeAliasStmt*) override {}
        void Visit(OpaquePluginStmt*) override {}
    };

    std::vector<fs::path> ResolveImportPath(const fs::path& baseDirectory,
        const std::string& target,
        const ProjectConfig* project) {
        std::vector<std::string> candidateSubpaths;

        std::string cleanTarget = target;
        if (cleanTarget.starts_with("./")) cleanTarget = cleanTarget.substr(2);

        candidateSubpaths.push_back(cleanTarget);
        if (!cleanTarget.ends_with(".abs")) {
            candidateSubpaths.push_back(cleanTarget + ".abs");
        }

        if (cleanTarget.find('.') != std::string::npos &&
            cleanTarget.find('/') == std::string::npos &&
            cleanTarget.find('\\') == std::string::npos) {
            std::string dotAsPath = cleanTarget;
            std::replace(dotAsPath.begin(), dotAsPath.end(), '.', '/');
            candidateSubpaths.push_back(dotAsPath);
            candidateSubpaths.push_back(dotAsPath + ".abs");
        }

        std::vector<fs::path> basePaths;
        basePaths.push_back(baseDirectory);
        basePaths.push_back(baseDirectory / "std");
        if (baseDirectory.has_parent_path()) {
            basePaths.push_back(baseDirectory.parent_path());
            basePaths.push_back(baseDirectory.parent_path() / "std");
        }

        if (project) {
            basePaths.push_back(project->root);
            basePaths.push_back(project->root / "std");
            for (const fs::path& dir : project->sourceDirectories) {
                basePaths.push_back(dir);
                basePaths.push_back(dir / "std");
            }
            for (const fs::path& dir : project->nativeSearchPaths) {
                basePaths.push_back(dir);
                basePaths.push_back(dir / "std");
            }
        }

        fs::path currentDir = fs::current_path();
        basePaths.push_back(currentDir);
        basePaths.push_back(currentDir / "std");

        for (const fs::path& basePath : basePaths) {
            for (const std::string& subpath : candidateSubpaths) {
                fs::path candidate = basePath / subpath;
                if (fs::exists(candidate)) {
                    if (fs::is_regular_file(candidate)) {
                        return { fs::weakly_canonical(candidate) };
                    }
                    if (fs::is_directory(candidate)) {
                        std::vector<fs::path> dirFiles;
                        for (const auto& entry : fs::recursive_directory_iterator(candidate)) {
                            if (entry.is_regular_file() && entry.path().extension() == ".abs") {
                                dirFiles.push_back(fs::weakly_canonical(entry.path()));
                            }
                        }
                        std::sort(dirFiles.begin(), dirFiles.end());
                        if (!dirFiles.empty()) return dirFiles;
                    }
                }
            }
        }
        return {};
    }

    void LoadSource(const fs::path& sourcePath,
        std::vector<std::unique_ptr<Program>>& programs,
        std::unordered_set<std::string>& loaded,
        const ProjectConfig* project = nullptr) {
        if (!fs::exists(sourcePath)) throw std::runtime_error("Imported source does not exist: " + sourcePath.string());
        const fs::path canonical = fs::weakly_canonical(sourcePath);
        const std::string key = canonical.generic_string();
        if (!loaded.insert(key).second) return;
        if (canonical.extension() != ".abs")
            throw std::runtime_error("Absolute source must use the .abs extension: " + canonical.string());

        std::unique_ptr<Program> program = ParseCode(
            Tokenize(ReadFile(canonical)), canonical.string());
        if (!program) throw std::runtime_error("Parsing failed: " + canonical.string());
        ImportCollector imports;
        for (const auto& statement : program->statements) if (statement) statement->Accept(imports);
        programs.push_back(std::move(program));
        for (const CollectedImport& imported : imports.imports) {
            std::vector<fs::path> resolved = ResolveImportPath(canonical.parent_path(), imported.target, project);
            if (resolved.empty()) {
                if (const std::string* virtualSource = FindPluginVirtualModule(imported.target)) {
                    programs.push_back(ParseCode(Tokenize(*virtualSource),
                        "<plugin:" + imported.target + ">"));
                }
                else if (imported.isFile) {
                    throw std::runtime_error("Imported source does not exist: " + imported.target);
                }
            }
            else {
                for (const fs::path& path : resolved) {
                    LoadSource(path, programs, loaded, project);
                }
            }
        }
    }

    bool IsHardTopLevelDeclaration(const Statement& statement) {
        return dynamic_cast<const FunctionDeclStmt*>(&statement) ||
            dynamic_cast<const ClassDeclStmt*>(&statement) ||
            dynamic_cast<const StructDeclStmt*>(&statement) ||
            dynamic_cast<const InterfaceDeclStmt*>(&statement) ||
            dynamic_cast<const EnumDeclStmt*>(&statement) ||
            dynamic_cast<const GroupDeclStmt*>(&statement) ||
            dynamic_cast<const NamespaceDeclStmt*>(&statement) ||
            dynamic_cast<const TypeAliasStmt*>(&statement) ||
            dynamic_cast<const ImportStmt*>(&statement) ||
            dynamic_cast<const OpaquePluginStmt*>(&statement);
    }

    bool IsExplicitMain(const Statement& statement) {
        const auto* function = dynamic_cast<const FunctionDeclStmt*>(&statement);
        return function && function->name && function->name->value == "main";
    }

    void AddScriptEntryPoint(std::vector<std::unique_ptr<Statement>>& statements) {
        const bool explicitMain = std::any_of(statements.begin(), statements.end(),
            [](const std::unique_ptr<Statement>& statement) {
                return statement && IsExplicitMain(*statement);
            });
        const bool executableTopLevel = std::any_of(statements.begin(), statements.end(),
            [](const std::unique_ptr<Statement>& statement) {
                return statement && !IsHardTopLevelDeclaration(*statement) &&
                    dynamic_cast<const VarDeclStmt*>(statement.get()) == nullptr;
            });
        if (!executableTopLevel) return;
        if (explicitMain) {
            throw std::runtime_error(
                "Top-level executable statements cannot be combined with an explicit main function");
        }

        std::vector<std::unique_ptr<Statement>> declarations;
        std::vector<std::unique_ptr<Statement>> scriptBody;
        declarations.reserve(statements.size() + 1);
        scriptBody.reserve(statements.size() + 1);
        for (auto& statement : statements) {
            if (!statement) continue;
            if (IsHardTopLevelDeclaration(*statement))
                declarations.push_back(std::move(statement));
            else
                scriptBody.push_back(std::move(statement));
        }
        scriptBody.push_back(std::make_unique<ReturnStmt>(
            std::make_unique<NumberLiteralExpr>("0")));
        auto hiddenMain = std::make_unique<FunctionDeclStmt>(
            std::make_unique<PrimitiveTypeExpr>("int32"),
            std::make_unique<Token>(TokenType::IDENTIFIER, "main", 0, 0),
            std::vector<std::unique_ptr<VarDeclExpr>>{},
            std::make_unique<CompoundStmt>(std::move(scriptBody)));
        declarations.push_back(std::move(hiddenMain));
        statements = std::move(declarations);
    }

    Compilation LoadCompilation(const fs::path& input, PluginManager& plugins) {
        std::vector<std::unique_ptr<Program>> programs;
        std::unordered_set<std::string> loaded;
        std::string moduleName;
        std::vector<fs::path> nativeLibraries;
        std::vector<fs::path> nativeSearchPaths;
        std::unique_ptr<ProjectConfig> project;

        if (input.extension() == ".absproj" || input.filename() == "package.abs" || input.filename() == "abspackage.json") {
            project = std::make_unique<ProjectConfig>(LoadProjectConfig(input));
            for (const fs::path& path : project->pluginSearchPaths) plugins.AddSearchPath(path);
            for (const fs::path& plugin : project->plugins) plugins.Load(plugin);
            moduleName = project->name;
            nativeLibraries = project->nativeLibraries;
            nativeSearchPaths = project->nativeSearchPaths;

            PackageManifest pkgManifest = PackageManager::LoadManifest(input);
            PackageLockfile lockfile = PackageManager::LoadLockfile(input.parent_path() / "abspackage.lock");
            PackageLockfile resolvedLock = PackageManager::ResolveDependencies(pkgManifest, pkgManifest.registries, lockfile);
            PackageManager::SaveLockfile(resolvedLock, input.parent_path() / "abspackage.lock");
        }
        else {
            moduleName = input.filename().string();
        }

        std::vector<std::unique_ptr<Program>> preludePrograms;
        preludePrograms.push_back(ParseCode(Tokenize(
            "class Error {\n"
            "    public string message;\n"
            "    public Error(string value) { message = value; }\n"
            "}\n")));
        for (const std::string& prelude : SyntaxPluginPreludes())
            preludePrograms.push_back(ParseCode(Tokenize(prelude)));

        if (project) {
            LoadSource(project->entry, programs, loaded, project.get());
            std::vector<fs::path> sources;
            for (const fs::path& directory : project->sourceDirectories) {
                if (!fs::exists(directory))
                    throw std::runtime_error("Project source directory does not exist: " + directory.string());
                for (const fs::directory_entry& entry : fs::recursive_directory_iterator(directory)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".abs") sources.push_back(entry.path());
                }
            }
            std::sort(sources.begin(), sources.end());
            for (const fs::path& source : sources) LoadSource(source, programs, loaded, project.get());
        }
        else {
            LoadSource(input, programs, loaded, nullptr);
        }

        for (const fs::path& library : plugins.NativeLibraries()) {
            if (std::find(nativeLibraries.begin(), nativeLibraries.end(), library) == nativeLibraries.end())
                nativeLibraries.push_back(library);
        }

        std::vector<std::unique_ptr<Statement>> statements;
        for (auto& program : preludePrograms) {
            for (auto& statement : program->statements) statements.push_back(std::move(statement));
        }
        for (auto& program : programs) {
            for (auto& statement : program->statements) statements.push_back(std::move(statement));
        }
        AddScriptEntryPoint(statements);
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

    int RunProcess(const fs::path& executable, const std::vector<std::string>& arguments) {
#ifdef _WIN32
        std::wstring commandLine = L"\"" + executable.wstring() + L"\"";
        for (const std::string& argument : arguments) {
            commandLine.push_back(L' ');
            commandLine.push_back(L'"');
            for (char ch : argument) {
                if (ch == '"') throw std::runtime_error("Process argument contains a quote");
                commandLine.push_back(static_cast<wchar_t>(static_cast<unsigned char>(ch)));
            }
            commandLine.push_back(L'"');
        }
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
        mutableCommand.push_back(L'\0');
        if (!CreateProcessW(executable.wstring().c_str(), mutableCommand.data(),
                nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
            throw std::runtime_error("failed to launch process: " + executable.string());
        }
        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD exitCode = 1;
        GetExitCodeProcess(process.hProcess, &exitCode);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return static_cast<int>(exitCode);
#else
        std::vector<std::string> storage;
        storage.push_back(executable.string());
        storage.insert(storage.end(), arguments.begin(), arguments.end());
        std::vector<char*> argv;
        argv.reserve(storage.size() + 1);
        for (std::string& item : storage) argv.push_back(item.data());
        argv.push_back(nullptr);
        const pid_t pid = fork();
        if (pid < 0) throw std::runtime_error("fork failed for " + executable.string());
        if (pid == 0) {
            execv(executable.c_str(), argv.data());
            _exit(127);
        }
        int status = 0;
        if (waitpid(pid, &status, 0) < 0)
            throw std::runtime_error("waitpid failed for " + executable.string());
        if (WIFEXITED(status)) return WEXITSTATUS(status);
        return 1;
#endif
    }

    void BuildWasmModule(CodeGenerator& generator, Program& program,
        const Compilation& compilation, const fs::path& requestedOutput,
        const std::string& targetTriple) {
        const auto startTime = std::chrono::steady_clock::now();
        const fs::path wasmLd = FindWasmLd();
        if (wasmLd.empty()) {
            throw std::runtime_error(
                "wasm-ld was not found. Install LLVM with WebAssembly tools, set ABSOLUTE_WASM_LD, "
                "or place wasm-ld under .absolute/toolchains/llvm-*/bin (see docs/wasm-target.md)");
        }

        fs::path modulePath = fs::absolute(requestedOutput);
        if (modulePath.extension().empty() || modulePath.extension() == ".exe")
            modulePath.replace_extension(".wasm");
        if (!modulePath.parent_path().empty()) fs::create_directories(modulePath.parent_path());

        fs::path object = modulePath;
        object.replace_extension(".o");
        generator.GenerateObject(program, compilation.moduleName, object.string(),
            false, targetTriple, compilation.optimizationLevel,
            compilation.debugInfo);

        std::vector<std::string> linkArgs = {
            "--no-entry",
            "--export-all",
            object.string(),
        };
        // Prefer WASI / shared runtime via triple or ABSOLUTE_WASM_RUNTIME.
        const char* runtimePref = std::getenv("ABSOLUTE_WASM_RUNTIME");
        const bool wantWasi = targetTriple.find("wasi") != std::string::npos
            || (runtimePref && std::string(runtimePref) == "wasi");
        const bool wantShared = !wantWasi && (
            (runtimePref && std::string(runtimePref) == "shared")
            || std::getenv("ABSOLUTE_WASM_SHARED") != nullptr);
#ifdef ABSOLUTE_WASM_WASI_OBJECT
        if (wantWasi) {
            const fs::path wasi(ABSOLUTE_WASM_WASI_OBJECT);
            if (fs::exists(wasi))
                linkArgs.push_back(wasi.string());
            else
                throw std::runtime_error(
                    "WASI runtime object missing (ABSOLUTE_WASM_WASI_OBJECT). Rebuild Absolute-Runtime.");
        }
#endif
#ifdef ABSOLUTE_WASM_SHARED_OBJECT
        if (wantShared) {
            const fs::path sharedRt(ABSOLUTE_WASM_SHARED_OBJECT);
            if (fs::exists(sharedRt))
                linkArgs.push_back(sharedRt.string());
            else
                throw std::runtime_error(
                    "Shared wasm runtime object missing (ABSOLUTE_WASM_SHARED_OBJECT). Rebuild Absolute-Runtime.");
            // Shared linear memory: host imports env.memory as SharedArrayBuffer.
            linkArgs.insert(linkArgs.begin(), {
                "--shared-memory",
                "--import-memory",
                "--max-memory=16777216",
            });
        }
#endif
#ifdef ABSOLUTE_WASM_SHIM_OBJECT
        if (!wantWasi && !wantShared) {
            const fs::path shim(ABSOLUTE_WASM_SHIM_OBJECT);
            if (fs::exists(shim))
                linkArgs.push_back(shim.string());
        }
#endif
#if !defined(ABSOLUTE_WASM_WASI_OBJECT)
        if (wantWasi) {
            throw std::runtime_error(
                "This absolutec was built without ABSOLUTE_WASM_WASI_OBJECT; "
                "link absolute_wasm_runtime_wasi.o via ABSOLUTE_WASM_LIBS or rebuild.");
        }
#endif
#if !defined(ABSOLUTE_WASM_SHARED_OBJECT)
        if (wantShared) {
            throw std::runtime_error(
                "This absolutec was built without ABSOLUTE_WASM_SHARED_OBJECT; rebuild Absolute-Runtime.");
        }
#endif
        // Optional override / additional objects (space-separated paths).
        if (const char* extra = std::getenv("ABSOLUTE_WASM_LIBS")) {
            std::string text = extra;
            size_t start = 0;
            while (start < text.size()) {
                while (start < text.size() && (text[start] == ' ' || text[start] == ';'))
                    ++start;
                size_t end = start;
                while (end < text.size() && text[end] != ' ' && text[end] != ';')
                    ++end;
                if (end > start) {
                    const fs::path lib(text.substr(start, end - start));
                    if (fs::exists(lib))
                        linkArgs.push_back(lib.string());
                }
                start = end;
            }
        }
        linkArgs.push_back("-o");
        linkArgs.push_back(modulePath.string());

        // Console/assert lower to puts/printf/abort (shim). Host Absolute-Runtime
        // (managed heap, tasks, load, FS) is still not wasm-compatible.
        const int status = RunProcess(wasmLd, linkArgs);
        if (status != 0) {
            throw std::runtime_error(
                "wasm-ld failed with exit code " + std::to_string(status) +
                ". Provide a wasm runtime for missing symbols, use export \"C\"-only modules, "
                "or --emit-object (docs/wasm-target.md)");
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        std::cout << "Built " << modulePath.filename().string() << " [" << elapsed << " ms]\n";
    }

    void BuildExecutable(CodeGenerator& generator, Program& program, const Compilation& compilation,
        const fs::path& requestedOutput) {
        const auto startTime = std::chrono::steady_clock::now();
        fs::path executable = fs::absolute(requestedOutput);
        if (!executable.parent_path().empty()) fs::create_directories(executable.parent_path());
        fs::path object = executable;
#ifdef _WIN32
        object.replace_extension(".obj");
#else
        object.replace_extension(".o");
#endif
        generator.GenerateObject(program, compilation.moduleName, object.string(),
            compilation.sanitizeAddress, {},
            compilation.optimizationLevel,
            compilation.debugInfo);

        fs::path response = executable;
        response += ".absolute-link.rsp";
        std::ostringstream arguments;
#ifdef _WIN32
        arguments << "/nologo\n/out:" << QuoteResponseArgument(executable)
            << "\n/subsystem:console\n/stack:67108864\n" << QuoteResponseArgument(object) << '\n';
        if (compilation.debugInfo) {
            fs::path pdb = executable;
            pdb.replace_extension(".pdb");
            arguments << "/debug:full\n/pdb:" << QuoteResponseArgument(pdb) << '\n';
        }
#ifdef ABSOLUTE_RUNTIME_LIBRARY
        arguments << QuoteResponseArgument(ABSOLUTE_RUNTIME_LIBRARY) << '\n';
#endif
        for (const fs::path& library : compilation.nativeLibraries)
            arguments << QuoteResponseArgument(library) << '\n';
        // LLVM-emitted COFF objects do not carry the /DEFAULTLIB directives
        // that cl.exe normally writes while compiling C/C++. Supply the
        // Release dynamic CRT explicitly so mainCRTStartup and C builtins are
        // available even when the runtime archive contributes no object file.
        arguments << "msvcrt.lib\nvcruntime.lib\nucrt.lib\noldnames.lib\n"
            "legacy_stdio_definitions.lib\nws2_32.lib\nshell32.lib\n";
        if (compilation.sanitizeAddress) {
            arguments << "clang_rt.asan_dynamic-x86_64.lib\nclang_rt.asan_dynamic_runtime_thunk-x86_64.lib\n";
        }
        for (const fs::path& path : compilation.nativeSearchPaths)
            arguments << "/LIBPATH:" << QuoteResponseArgument(path) << '\n';
#else
        arguments << QuoteResponseArgument(object) << '\n';
#ifdef ABSOLUTE_RUNTIME_LIBRARY
        arguments << QuoteResponseArgument(ABSOLUTE_RUNTIME_LIBRARY) << '\n';
#endif
#ifdef ABSOLUTE_RUNTIME_DL_LIBRARY
        arguments << "-l" ABSOLUTE_RUNTIME_DL_LIBRARY "\n";
#endif
#ifdef ABSOLUTE_RUNTIME_UCONTEXT_LIBRARY
        arguments << "-l" ABSOLUTE_RUNTIME_UCONTEXT_LIBRARY "\n";
#endif
        for (const fs::path& library : compilation.nativeLibraries)
            arguments << QuoteResponseArgument(library) << '\n';
        for (const fs::path& path : compilation.nativeSearchPaths)
            arguments << "-L" << QuoteResponseArgument(path) << '\n';
        if (compilation.sanitizeAddress) arguments << "-fsanitize=address\n";
        if (compilation.debugInfo) arguments << "-g\n";
        arguments << "-o\n" << QuoteResponseArgument(executable) << '\n';
#endif
        WriteFile(response, arguments.str());

#ifndef ABSOLUTE_HOST_CXX_COMPILER
#define ABSOLUTE_HOST_CXX_COMPILER "c++"
#endif
#ifdef _WIN32
#ifndef ABSOLUTE_HOST_LINKER
#define ABSOLUTE_HOST_LINKER "link.exe"
#endif
        constexpr const char* nativeTool = ABSOLUTE_HOST_LINKER;
#else
        constexpr const char* nativeTool = ABSOLUTE_HOST_CXX_COMPILER;
#endif
        std::string command = QuoteShellArgument(nativeTool) +
            " @" + QuoteShellArgument(response.string());
#ifdef _WIN32
        // cmd.exe removes the first pair of quotes when /c starts with a
        // quoted executable. The outer pair preserves paths such as
        // "C:\\Program Files\\...\\cl.exe" when invoked through system().
        command = "\"" + command + "\"";
#endif
        const int status = std::system(command.c_str());
        std::error_code ignored;
        fs::remove(response, ignored);
        if (status != 0) throw std::runtime_error("Native linker failed with exit code " + std::to_string(status));
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        std::cout << "Built " << executable.filename().string() << " [" << elapsed << " ms]\n";
    }

    void BuildLibrary(CodeGenerator& generator, Program& program, const Compilation& compilation,
        const fs::path& requestedOutput) {
        const auto startTime = std::chrono::steady_clock::now();
        fs::path library = fs::absolute(requestedOutput);
#ifdef _WIN32
        if (library.extension() != ".dll") library.replace_extension(".dll");
#elif defined(__APPLE__)
        if (library.extension() != ".dylib") library.replace_extension(".dylib");
#else
        if (library.extension() != ".so") library.replace_extension(".so");
#endif
        if (!library.parent_path().empty()) fs::create_directories(library.parent_path());

        fs::path object = library;
#ifdef _WIN32
        object.replace_extension(".obj");
#else
        object.replace_extension(".o");
#endif
        generator.GenerateObject(program, compilation.moduleName, object.string(),
            compilation.sanitizeAddress, {},
            compilation.optimizationLevel,
            compilation.debugInfo);

        fs::path response = library;
        response += ".absolute-link.rsp";
        std::ostringstream arguments;
#ifdef _WIN32
        fs::path importLibrary = library;
        importLibrary.replace_extension(".lib");
        arguments << "/nologo\n/dll\n/out:" << QuoteResponseArgument(library)
            << "\n/implib:" << QuoteResponseArgument(importLibrary)
            << '\n' << QuoteResponseArgument(object) << '\n';
        if (compilation.debugInfo) {
            fs::path pdb = library;
            pdb.replace_extension(".pdb");
            arguments << "/debug:full\n/pdb:" << QuoteResponseArgument(pdb) << '\n';
        }
#ifdef ABSOLUTE_RUNTIME_LIBRARY
        arguments << QuoteResponseArgument(ABSOLUTE_RUNTIME_LIBRARY) << '\n';
#endif
        for (const fs::path& nativeLibrary : compilation.nativeLibraries)
            arguments << QuoteResponseArgument(nativeLibrary) << '\n';
        arguments << "msvcrt.lib\nvcruntime.lib\nucrt.lib\noldnames.lib\n"
            "legacy_stdio_definitions.lib\nws2_32.lib\nshell32.lib\n";
        if (compilation.sanitizeAddress) {
            arguments << "clang_rt.asan_dynamic-x86_64.lib\n"
                "clang_rt.asan_dynamic_runtime_thunk-x86_64.lib\n";
        }
        for (const fs::path& path : compilation.nativeSearchPaths)
            arguments << "/LIBPATH:" << QuoteResponseArgument(path) << '\n';
#else
        arguments << "-shared\n" << QuoteResponseArgument(object) << '\n';
#ifdef ABSOLUTE_RUNTIME_LIBRARY
        arguments << QuoteResponseArgument(ABSOLUTE_RUNTIME_LIBRARY) << '\n';
#endif
#ifdef ABSOLUTE_RUNTIME_DL_LIBRARY
        arguments << "-l" ABSOLUTE_RUNTIME_DL_LIBRARY "\n";
#endif
#ifdef ABSOLUTE_RUNTIME_UCONTEXT_LIBRARY
        arguments << "-l" ABSOLUTE_RUNTIME_UCONTEXT_LIBRARY "\n";
#endif
        for (const fs::path& nativeLibrary : compilation.nativeLibraries)
            arguments << QuoteResponseArgument(nativeLibrary) << '\n';
        for (const fs::path& path : compilation.nativeSearchPaths)
            arguments << "-L" << QuoteResponseArgument(path) << '\n';
        if (compilation.sanitizeAddress) arguments << "-fsanitize=address\n";
        if (compilation.debugInfo) arguments << "-g\n";
        arguments << "-o\n" << QuoteResponseArgument(library) << '\n';
#endif
        WriteFile(response, arguments.str());

#ifndef ABSOLUTE_HOST_CXX_COMPILER
#define ABSOLUTE_HOST_CXX_COMPILER "c++"
#endif
#ifdef _WIN32
#ifndef ABSOLUTE_HOST_LINKER
#define ABSOLUTE_HOST_LINKER "link.exe"
#endif
        constexpr const char* nativeTool = ABSOLUTE_HOST_LINKER;
#else
        constexpr const char* nativeTool = ABSOLUTE_HOST_CXX_COMPILER;
#endif
        std::string command = QuoteShellArgument(nativeTool) +
            " @" + QuoteShellArgument(response.string());
#ifdef _WIN32
        command = "\"" + command + "\"";
#endif
        const int status = std::system(command.c_str());
        std::error_code ignored;
        fs::remove(response, ignored);
        if (status != 0)
            throw std::runtime_error("Native library linker failed with exit code " +
                std::to_string(status));
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        std::cout << "Built " << library.filename().string() << " [" << elapsed << " ms]\n";
    }
#endif
}

int main(int argc, char* argv[]) {
    try {
        if (argc == 2) {
            const std::string argument = argv[1];
            if (argument == "--version" || argument == "-V") {
                std::cout << "absolutec " << ABSOLUTE_VERSION << '\n';
                return 0;
            }
            if (argument == "-h" || argument == "--help" || argument == "help") {
                PrintUsage(std::cout);
                return 0;
            }
        }
        const CommandLine commandLine = ParseCommandLine(argc, argv);
        if (commandLine.mode == CommandMode::NewProject) {
            CreateProject(commandLine);
            return 0;
        }

        PluginManager plugins;
        for (const fs::path& path : commandLine.pluginSearchPaths) plugins.AddSearchPath(path);
        for (const fs::path& plugin : commandLine.plugins) plugins.Load(plugin);
        Compilation compilation = LoadCompilation(commandLine.input, plugins);
        compilation.sanitizeAddress = commandLine.sanitizeAddress;
        compilation.debugInfo = commandLine.debugInfo;
        compilation.optimizationLevel =
            commandLine.optimizationLevel.value_or(
                OptimizationLevel::O3);
        Analyzer analyzer({compilation.program.get()});
        if (!commandLine.parseOnly && !analyzer.Analyze()) {
            analyzer.PrintDiagnostics(std::cout);
            return 1;
        }

        if (commandLine.emitLlvm || commandLine.emitObject ||
            commandLine.buildExecutable || commandLine.buildLibrary) {
#ifdef ABSOLUTE_HAS_LLVM
            const bool wasmTarget = IsWebAssemblyTargetTriple(commandLine.targetTriple);
            if (wasmTarget && commandLine.sanitizeAddress) {
                throw std::runtime_error(
                    "AddressSanitizer is not supported for WebAssembly targets");
            }

            CodeGenerator generator(&analyzer);
            if (commandLine.emitLlvm) {
                const std::string ir = generator.Generate(*compilation.program, compilation.moduleName,
                    commandLine.targetTriple,
                    commandLine.optimizationLevel,
                    compilation.debugInfo);
                if (commandLine.output.empty()) std::cout << ir;
                else WriteFile(commandLine.output, ir);
            }
            else if (commandLine.emitObject) {
                fs::path output = commandLine.output;
                if (output.empty()) {
                    output = compilation.moduleName;
                    if (wasmTarget) output.replace_extension(".o");
                    else {
#ifdef _WIN32
                        output.replace_extension(".obj");
#else
                        output.replace_extension(".o");
#endif
                    }
                }
                generator.GenerateObject(*compilation.program, compilation.moduleName,
                    output.string(), compilation.sanitizeAddress,
                    commandLine.targetTriple,
                    compilation.optimizationLevel,
                    compilation.debugInfo);
            }
            else if (commandLine.buildLibrary) {
                if (wasmTarget || !commandLine.targetTriple.empty()) {
                    throw std::runtime_error(
                        "--build-library currently supports the host target only; "
                        "use --emit-object for another target");
                }
                fs::path output = commandLine.output;
                if (output.empty()) {
#ifdef _WIN32
                    output = compilation.moduleName + ".dll";
#elif defined(__APPLE__)
                    output = "lib" + compilation.moduleName + ".dylib";
#else
                    output = "lib" + compilation.moduleName + ".so";
#endif
                }
                BuildLibrary(generator, *compilation.program, compilation, output);
            }
            else if (wasmTarget) {
                fs::path output = commandLine.output;
                if (output.empty()) {
                    output = compilation.moduleName;
                    output.replace_extension(".wasm");
                }
                BuildWasmModule(generator, *compilation.program, compilation, output,
                    commandLine.targetTriple);
            }
            else {
                if (!commandLine.targetTriple.empty()) {
                    throw std::runtime_error(
                        "--build-exe with a non-host --target is only supported for WebAssembly; "
                        "omit --target for host executables, or use --emit-object");
                }
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
        std::cerr << "Error: " << error.what() << std::endl;
        return 2;
    }
    catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << std::endl;
        return 1;
    }
}
