#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "plugin_api.h"

namespace Absolute {
    class PluginManager {
    public:
        PluginManager();
        ~PluginManager();

        PluginManager(const PluginManager&) = delete;
        PluginManager& operator=(const PluginManager&) = delete;

        void AddSearchPath(const std::filesystem::path& path);
        void Load(const std::filesystem::path& path);
        void Unload(const std::string& pluginName);
        void Reload(const std::filesystem::path& path);

        void SetPermissionPolicy(const ::AbsolutePermissionPolicyV1& policy);
        const ::AbsolutePermissionPolicyV1& GetPermissionPolicy() const { return permissionPolicy; }

        void SetIsolationMode(::AbsoluteIsolationModeV1 mode) { isolationMode = mode; }
        ::AbsoluteIsolationModeV1 GetIsolationMode() const { return isolationMode; }

        const ::AbsoluteEditorPluginV1* GetEditorPlugin(const std::string& pluginName) const;
        const ::AbsoluteCompilerPluginV1* GetCompilerPlugin(const std::string& pluginName) const;
        std::vector<std::string> LoadedPluginNames() const;

        const std::vector<std::filesystem::path>& NativeLibraries() const { return nativeLibraries; }

    private:
        struct LoadedPlugin {
            std::string name;
            std::string version;
            std::filesystem::path library;
            std::filesystem::path manifest;
            std::vector<std::string> capabilities;
            const ::AbsoluteCompilerPluginV1* compilerPlugin = nullptr;
            const ::AbsoluteEditorPluginV1* editorPlugin = nullptr;
            void* handle = nullptr;
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
        ::AbsolutePermissionPolicyV1 permissionPolicy{};
        ::AbsoluteIsolationModeV1 isolationMode = ABSOLUTE_ISOLATION_TRUSTED_IN_PROCESS;
    };
}
