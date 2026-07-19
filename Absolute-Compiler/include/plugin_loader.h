#pragma once

#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace Absolute {
    class PluginManager {
    public:
        PluginManager() = default;
        ~PluginManager();

        PluginManager(const PluginManager&) = delete;
        PluginManager& operator=(const PluginManager&) = delete;

        void Load(const std::filesystem::path& path);

    private:
        std::vector<void*> handles;
        std::unordered_set<std::string> loadedPaths;
    };
}

