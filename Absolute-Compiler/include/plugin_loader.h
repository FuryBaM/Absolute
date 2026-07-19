#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Absolute {
    class PluginManager {
    public:
        PluginManager() = default;
        ~PluginManager();

        PluginManager(const PluginManager&) = delete;
        PluginManager& operator=(const PluginManager&) = delete;

        void AddSearchPath(const std::filesystem::path& path);
        void Load(const std::filesystem::path& path);
        const std::vector<std::filesystem::path>& NativeLibraries() const { return nativeLibraries; }

    private:
        struct LoadedPlugin {
            std::string version;
            std::filesystem::path library;
            std::filesystem::path manifest;
            std::vector<std::string> capabilities;
        };

        enum class ManifestState { Visiting, Loaded };

        void LoadManifest(const std::filesystem::path& path,
            const std::string& requestedName = {}, const std::string& versionRange = {});
        void LoadDynamicLibrary(const std::filesystem::path& path,
            const std::string& expectedName = {}, const std::string& version = {},
            const std::filesystem::path& manifest = {}, const std::vector<std::string>& capabilities = {});

        std::vector<void*> handles;
        std::vector<std::filesystem::path> searchPaths;
        std::vector<std::filesystem::path> manifestStack;
        std::unordered_map<std::string, std::string> loadedLibraries;
        std::unordered_map<std::string, LoadedPlugin> loadedPlugins;
        std::unordered_map<std::string, ManifestState> manifestStates;
        std::unordered_map<std::string, std::string> capabilityProviders;
        std::vector<std::filesystem::path> nativeLibraries;
    };
}
