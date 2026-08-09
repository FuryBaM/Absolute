#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

int absolute_compiler_main(int argc, char* argv[]);

namespace {
    namespace fs = std::filesystem;

    bool HasTargetOption(int argc, char* argv[]) {
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index] ? argv[index] : "";
            if (argument == "--target" || argument.starts_with("--target=")) return true;
        }
        return false;
    }

    std::optional<std::string> ProjectTarget(const fs::path& projectFile) {
        if (!fs::exists(projectFile)) return std::nullopt;
        std::ifstream file(projectFile, std::ios::binary);
        if (!file) return std::nullopt;
        std::ostringstream buffer;
        buffer << file.rdbuf();
        const std::string document = buffer.str();
        const std::regex pattern("\\\"target\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
        std::smatch match;
        if (!std::regex_search(document, match, pattern)) return std::nullopt;
        std::string target = match[1].str();
        target.erase(target.begin(), std::find_if(target.begin(), target.end(),
            [](unsigned char character) { return !std::isspace(character); }));
        target.erase(std::find_if(target.rbegin(), target.rend(),
            [](unsigned char character) { return !std::isspace(character); }).base(), target.end());
        std::string lower = target;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        if (target.empty() || lower == "host" || lower == "native") return std::nullopt;
        return target;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3 || !argv[1] || std::string(argv[1]) != "build" || HasTargetOption(argc, argv))
        return absolute_compiler_main(argc, argv);

    const std::optional<std::string> target = ProjectTarget(argv[2]);
    if (!target) return absolute_compiler_main(argc, argv);

    std::vector<std::string> storage;
    storage.reserve(static_cast<size_t>(argc) + 2);
    for (int index = 0; index < argc; ++index) storage.emplace_back(argv[index] ? argv[index] : "");
    storage.emplace_back("--target");
    storage.push_back(*target);

    std::vector<char*> forwarded;
    forwarded.reserve(storage.size() + 1);
    for (std::string& argument : storage) forwarded.push_back(argument.data());
    forwarded.push_back(nullptr);
    return absolute_compiler_main(static_cast<int>(storage.size()), forwarded.data());
}
