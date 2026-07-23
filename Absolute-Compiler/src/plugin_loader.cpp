#include "pch.h"
#include "plugin_loader.h"

#include "plugin_api.h"
#include "syntax_plugins.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

namespace Absolute {
    namespace {
        using InitFunction = const AbsoluteSyntaxPluginV1* (*)();

        struct Version {
            int major = 0;
            int minor = 0;
            int patch = 0;
        };

        struct Dependency {
            std::string name;
            std::string version;
            std::filesystem::path path;
        };

        struct Manifest {
            std::string name;
            std::string version;
            unsigned abi = 0;
            std::filesystem::path library;
            std::vector<Dependency> dependencies;
            std::vector<Dependency> optionalDependencies;
            std::vector<std::string> conflicts;
            std::vector<std::string> targets;
            std::vector<std::string> permissions;
            std::string ruleNamespace;
            std::vector<std::string> provides;
            std::vector<std::string> requiresCapabilities;
            std::vector<std::filesystem::path> nativeLibraries;
        };

        bool PermissionEnabled(const std::string& permission, const ::AbsolutePermissionPolicyV1& policy) {
            if (permission == "filesystem.read" || permission == "fs_read") return policy.allow_filesystem_read != 0;
            if (permission == "filesystem.write" || permission == "fs_write") return policy.allow_filesystem_write != 0;
            if (permission == "network.http" || permission == "network.tcp" || permission == "network") return policy.allow_network != 0;
            if (permission == "environment.read" || permission == "environment.write" || permission == "env") return policy.allow_environment != 0;
            if (permission == "toolchain.exec" || permission == "toolchain") return policy.allow_toolchain_exec != 0;
            if (permission == "native.library" || permission == "native") return policy.allow_native_libraries != 0;
            return false;
        }

        void ValidatePermissions(const std::vector<std::string>& permissions, const std::filesystem::path& manifestPath,
            const ::AbsolutePermissionPolicyV1& policy) {
            static const std::unordered_set<std::string> supportedPermissions = {
                "filesystem.read", "fs_read",
                "filesystem.write", "fs_write",
                "network.http", "network.tcp", "network",
                "environment.read", "environment.write", "env",
                "toolchain.exec", "toolchain",
                "native.library", "native"
            };
            for (const std::string& permission : permissions) {
                if (!supportedPermissions.contains(permission)) {
                    throw std::runtime_error("Unsupported plugin permission '" + permission + "' in manifest: " +
                        manifestPath.string());
                }
                if (!PermissionEnabled(permission, policy)) {
                    throw std::runtime_error("Plugin manifest requires disallowed permission '" + permission + "' in manifest: " +
                        manifestPath.string());
                }
            }
        }

        std::string ReadText(const std::filesystem::path& path) {
            std::ifstream file(path, std::ios::binary);
            if (!file) throw std::runtime_error("Cannot open plugin manifest: " + path.string());
            std::ostringstream content;
            content << file.rdbuf();
            return content.str();
        }

        std::optional<std::size_t> PropertyPosition(const std::string& document, const std::string& key) {
            std::size_t first = 0;
            while (first < document.size() && std::isspace(static_cast<unsigned char>(document[first]))) ++first;
            const int propertyDepth = first < document.size() && document[first] == '{' ? 1 : 0;
            int depth = 0;
            for (std::size_t position = 0; position < document.size();) {
                if (document[position] == '{' || document[position] == '[') { ++depth; ++position; continue; }
                if (document[position] == '}' || document[position] == ']') { --depth; ++position; continue; }
                if (document[position] != '"') { ++position; continue; }
                const std::size_t start = ++position;
                bool escaped = false;
                while (position < document.size()) {
                    if (escaped) escaped = false;
                    else if (document[position] == '\\') escaped = true;
                    else if (document[position] == '"') break;
                    ++position;
                }
                if (position >= document.size()) throw std::runtime_error("Unterminated string in plugin manifest");
                const std::string candidate = document.substr(start, position - start);
                ++position;
                if (depth != propertyDepth || candidate != key) continue;
                while (position < document.size() && std::isspace(static_cast<unsigned char>(document[position]))) ++position;
                if (position >= document.size() || document[position] != ':') continue;
                ++position;
                while (position < document.size() && std::isspace(static_cast<unsigned char>(document[position]))) ++position;
                return position;
            }
            return std::nullopt;
        }

        std::optional<std::string> StringProperty(const std::string& document, const std::string& key) {
            const auto valuePosition = PropertyPosition(document, key);
            if (!valuePosition || *valuePosition >= document.size() || document[*valuePosition] != '"') return std::nullopt;
            std::string result;
            bool escaped = false;
            for (std::size_t position = *valuePosition + 1; position < document.size(); ++position) {
                const char current = document[position];
                if (escaped) {
                    switch (current) {
                    case '"': case '\\': case '/': result += current; break;
                    case 'b': result += '\b'; break;
                    case 'f': result += '\f'; break;
                    case 'n': result += '\n'; break;
                    case 'r': result += '\r'; break;
                    case 't': result += '\t'; break;
                    default: throw std::runtime_error("Unsupported JSON escape in property '" + key + "'");
                    }
                    escaped = false;
                }
                else if (current == '\\') escaped = true;
                else if (current == '"') return result;
                else result += current;
            }
            throw std::runtime_error("Unterminated string property '" + key + "' in plugin manifest");
        }

        std::optional<unsigned> UnsignedProperty(const std::string& document, const std::string& key) {
            const auto valuePosition = PropertyPosition(document, key);
            if (!valuePosition) return std::nullopt;
            std::size_t end = *valuePosition;
            while (end < document.size() && std::isdigit(static_cast<unsigned char>(document[end]))) ++end;
            if (end == *valuePosition) return std::nullopt;
            unsigned value = 0;
            const std::string text = document.substr(*valuePosition, end - *valuePosition);
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            if (parsed.ec != std::errc()) return std::nullopt;
            return value;
        }

        std::optional<std::string> PropertyValue(const std::string& document, const std::string& key,
            char opening, char closing) {
            const auto valuePosition = PropertyPosition(document, key);
            if (!valuePosition) return std::nullopt;
            std::size_t position = *valuePosition;
            if (position >= document.size() || document[position] != opening) return std::nullopt;
            const std::size_t begin = position++;
            int depth = 1;
            bool inString = false;
            bool escaped = false;
            while (position < document.size()) {
                const char current = document[position];
                if (inString) {
                    if (escaped) escaped = false;
                    else if (current == '\\') escaped = true;
                    else if (current == '"') inString = false;
                }
                else if (current == '"') inString = true;
                else if (current == opening) ++depth;
                else if (current == closing && --depth == 0)
                    return document.substr(begin, position - begin + 1);
                ++position;
            }
            throw std::runtime_error("Unterminated property '" + key + "' in plugin manifest");
        }

        std::vector<std::string> StringArrayProperty(const std::string& document, const std::string& key) {
            const auto value = PropertyValue(document, key, '[', ']');
            if (!value) return {};
            std::vector<std::string> result;
            const std::regex stringPattern("\\\"([^\\\"]+)\\\"");
            for (std::sregex_iterator it(value->begin(), value->end(), stringPattern), end; it != end; ++it)
                result.push_back((*it)[1].str());
            return result;
        }

        Version ParseVersion(const std::string& text, const std::string& context) {
            Version result;
            const std::regex pattern("^([0-9]+)\\.([0-9]+)\\.([0-9]+)$");
            std::smatch match;
            if (!std::regex_match(text, match, pattern))
                throw std::runtime_error("Invalid semantic version '" + text + "' in " + context +
                    "; expected MAJOR.MINOR.PATCH");
            auto parsePart = [&](std::size_t index) {
                int value = 0;
                const std::string part = match[index].str();
                const auto parsed = std::from_chars(part.data(), part.data() + part.size(), value);
                if (parsed.ec != std::errc()) throw std::runtime_error("Version component is too large in " + context);
                return value;
            };
            result.major = parsePart(1);
            result.minor = parsePart(2);
            result.patch = parsePart(3);
            return result;
        }

        int Compare(const Version& left, const Version& right) {
            if (left.major != right.major) return left.major < right.major ? -1 : 1;
            if (left.minor != right.minor) return left.minor < right.minor ? -1 : 1;
            if (left.patch != right.patch) return left.patch < right.patch ? -1 : 1;
            return 0;
        }

        bool Satisfies(const std::string& versionText, const std::string& rangeText) {
            if (rangeText.empty() || rangeText == "*") return true;
            const Version version = ParseVersion(versionText, "plugin version");
            std::string normalized = rangeText;
            std::replace(normalized.begin(), normalized.end(), ',', ' ');
            std::istringstream tokens(normalized);
            std::string token;
            while (tokens >> token) {
                std::string op;
                if (token.starts_with(">=") || token.starts_with("<=")) op = token.substr(0, 2);
                else if (token.starts_with('>') || token.starts_with('<') || token.starts_with('^') ||
                    token.starts_with('~') || token.starts_with('=')) op = token.substr(0, 1);
                const std::string requiredText = token.substr(op.size());
                if (requiredText.empty())
                    throw std::runtime_error("Invalid version range '" + rangeText + "'");
                const Version required = ParseVersion(requiredText, "version range '" + rangeText + "'");
                const int comparison = Compare(version, required);
                bool matches = false;
                if (op.empty() || op == "=") matches = comparison == 0;
                else if (op == ">=") matches = comparison >= 0;
                else if (op == "<=") matches = comparison <= 0;
                else if (op == ">") matches = comparison > 0;
                else if (op == "<") matches = comparison < 0;
                else if (op == "^") {
                    Version upper = required;
                    if (required.major != 0) { ++upper.major; upper.minor = upper.patch = 0; }
                    else if (required.minor != 0) { ++upper.minor; upper.patch = 0; }
                    else ++upper.patch;
                    matches = comparison >= 0 && Compare(version, upper) < 0;
                }
                else if (op == "~") {
                    Version upper = required;
                    ++upper.minor;
                    upper.patch = 0;
                    matches = comparison >= 0 && Compare(version, upper) < 0;
                }
                if (!matches) return false;
            }
            return true;
        }

        Manifest ParseManifest(const std::filesystem::path& path,
            const ::AbsolutePermissionPolicyV1& policy) {
            const std::string document = ReadText(path);
            Manifest result;
            const auto name = StringProperty(document, "name");
            const auto version = StringProperty(document, "version");
            const auto abi = UnsignedProperty(document, "abi");
            const auto library = StringProperty(document, "library");
            if (!name || name->empty()) throw std::runtime_error("Plugin manifest requires string property 'name': " + path.string());
            if (!version) throw std::runtime_error("Plugin manifest requires string property 'version': " + path.string());
            if (!abi) throw std::runtime_error("Plugin manifest requires integer property 'abi': " + path.string());
            if (!library || library->empty()) throw std::runtime_error("Plugin manifest requires string property 'library': " + path.string());
            ParseVersion(*version, "plugin manifest '" + path.string() + "'");
            result.name = *name;
            result.version = *version;
            result.abi = *abi;
            result.library = *library;
            result.provides = StringArrayProperty(document, "provides");
            result.requiresCapabilities = StringArrayProperty(document, "requires");
            result.conflicts = StringArrayProperty(document, "conflicts");
            result.targets = StringArrayProperty(document, "targets");
            result.permissions = StringArrayProperty(document, "permissions");
            ValidatePermissions(result.permissions, path, policy);
            result.ruleNamespace = StringProperty(document, "namespace").value_or("");
            for (const std::string& nativeLibrary : StringArrayProperty(document, "nativeLibraries"))
                result.nativeLibraries.emplace_back(nativeLibrary);

            const auto dependencyArray = PropertyValue(document, "dependencies", '[', ']');
            const auto dependencyObject = PropertyValue(document, "dependencies", '{', '}');
            if (dependencyArray) {
                const std::regex objectPattern("\\{([^{}]*)\\}");
                for (std::sregex_iterator it(dependencyArray->begin(), dependencyArray->end(), objectPattern), end;
                    it != end; ++it) {
                    const std::string object = (*it)[1].str();
                    const auto dependencyName = StringProperty(object, "name");
                    if (!dependencyName || dependencyName->empty())
                        throw std::runtime_error("Plugin dependency requires string property 'name': " + path.string());
                    Dependency dependency;
                    dependency.name = *dependencyName;
                    dependency.version = StringProperty(object, "version").value_or("*");
                    if (const auto dependencyPath = StringProperty(object, "path")) dependency.path = *dependencyPath;
                    result.dependencies.push_back(std::move(dependency));
                }
            }
            else if (dependencyObject) {
                const std::regex pairPattern("\\\"([^\\\"]+)\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
                for (std::sregex_iterator it(dependencyObject->begin(), dependencyObject->end(), pairPattern), end;
                    it != end; ++it)
                    result.dependencies.push_back({(*it)[1].str(), (*it)[2].str(), {}});
            }
            return result;
        }

        void* FindSymbol(void* handle, const char* name) {
#ifdef _WIN32
            return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), name));
#else
            return dlsym(handle, name);
#endif
        }

        void CloseLibrary(void* handle) {
            if (!handle) return;
#ifdef _WIN32
            FreeLibrary(static_cast<HMODULE>(handle));
#else
            dlclose(handle);
#endif
        }
    }

    PluginManager::PluginManager() {
        permissionPolicy.struct_size = sizeof(AbsolutePermissionPolicyV1);
        permissionPolicy.allow_filesystem_read = 1;
        permissionPolicy.allow_filesystem_write = 1;
        permissionPolicy.allow_network = 1;
        permissionPolicy.allow_environment = 1;
        permissionPolicy.allow_toolchain_exec = 1;
        permissionPolicy.allow_native_libraries = 1;
    }

    PluginManager::~PluginManager() {
        ResetSyntaxPlugins();
        for (auto iterator = handles.rbegin(); iterator != handles.rend(); ++iterator)
            CloseLibrary(*iterator);
    }

    void PluginManager::AddSearchPath(const std::filesystem::path& path) {
        const std::filesystem::path absolute = std::filesystem::absolute(path);
        if (!std::filesystem::exists(absolute) || !std::filesystem::is_directory(absolute))
            throw std::runtime_error("Plugin search path is not a directory: " + absolute.string());
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(absolute);
        if (std::find(searchPaths.begin(), searchPaths.end(), canonical) == searchPaths.end())
            searchPaths.push_back(canonical);
    }

    void PluginManager::Load(const std::filesystem::path& path) {
        if (path.extension() == ".absplugin") LoadManifest(path);
        else LoadDynamicLibrary(path);
    }

    void PluginManager::Unload(const std::string& pluginName) {
        const auto it = loadedPlugins.find(pluginName);
        if (it == loadedPlugins.end()) {
            throw std::runtime_error("Plugin '" + pluginName + "' is not loaded");
        }
        const LoadedPlugin& plugin = it->second;
        if (plugin.compilerPlugin && plugin.compilerPlugin->shutdown) {
            plugin.compilerPlugin->shutdown(nullptr);
        }
        if (plugin.handle) {
            const auto handleIt = std::find(handles.begin(), handles.end(), plugin.handle);
            if (handleIt != handles.end()) {
                handles.erase(handleIt);
            }
            CloseLibrary(plugin.handle);
        }
        for (const std::string& cap : plugin.capabilities) {
            capabilityProviders.erase(cap);
        }
        for (auto libIt = loadedLibraries.begin(); libIt != loadedLibraries.end(); ) {
            if (libIt->second == pluginName) libIt = loadedLibraries.erase(libIt);
            else ++libIt;
        }
        loadedPlugins.erase(it);
    }

    void PluginManager::Reload(const std::filesystem::path& path) {
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(path);
        std::string foundName;
        for (const auto& [name, plugin] : loadedPlugins) {
            if (plugin.library == canonical || plugin.manifest == canonical) {
                foundName = name;
                break;
            }
        }
        if (!foundName.empty()) {
            Unload(foundName);
        }
        Load(path);
    }

    void PluginManager::SetPermissionPolicy(const ::AbsolutePermissionPolicyV1& policy) {
        permissionPolicy = policy;
    }

    const ::AbsoluteEditorPluginV1* PluginManager::GetEditorPlugin(const std::string& pluginName) const {
        const auto it = loadedPlugins.find(pluginName);
        if (it != loadedPlugins.end()) return it->second.editorPlugin;
        return nullptr;
    }

    const ::AbsoluteCompilerPluginV1* PluginManager::GetCompilerPlugin(const std::string& pluginName) const {
        const auto it = loadedPlugins.find(pluginName);
        if (it != loadedPlugins.end()) return it->second.compilerPlugin;
        return nullptr;
    }

    std::vector<std::string> PluginManager::LoadedPluginNames() const {
        std::vector<std::string> result;
        for (const auto& [name, _] : loadedPlugins) {
            result.push_back(name);
        }
        return result;
    }

    void PluginManager::LoadManifest(const std::filesystem::path& path,
        const std::string& requestedName, const std::string& versionRange) {
        if (!std::filesystem::exists(path))
            throw std::runtime_error("Plugin manifest does not exist: " + std::filesystem::absolute(path).string());
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(path);
        const std::string key = canonical.generic_string();
        const auto state = manifestStates.find(key);
        if (state != manifestStates.end()) {
            if (state->second == ManifestState::Visiting) {
                std::ostringstream cycle;
                cycle << "Plugin dependency cycle: ";
                const auto begin = std::find(manifestStack.begin(), manifestStack.end(), canonical);
                for (auto it = begin; it != manifestStack.end(); ++it) cycle << it->filename().string() << " -> ";
                cycle << canonical.filename().string();
                throw std::runtime_error(cycle.str());
            }
            const Manifest loaded = ParseManifest(canonical, permissionPolicy);
            if (!requestedName.empty() && loaded.name != requestedName)
                throw std::runtime_error("Plugin dependency expected '" + requestedName + "' but manifest declares '" + loaded.name + "'");
            if (!Satisfies(loaded.version, versionRange))
                throw std::runtime_error("Plugin '" + loaded.name + "' version " + loaded.version +
                    " does not satisfy '" + versionRange + "'");
            return;
        }

        const Manifest manifest = ParseManifest(canonical, permissionPolicy);
        if (!requestedName.empty() && manifest.name != requestedName)
            throw std::runtime_error("Plugin dependency expected '" + requestedName + "' but manifest '" +
                canonical.string() + "' declares '" + manifest.name + "'");
        if (!Satisfies(manifest.version, versionRange))
            throw std::runtime_error("Plugin '" + manifest.name + "' version " + manifest.version +
                " does not satisfy required range '" + versionRange + "'");
        if (manifest.abi != ABSOLUTE_SYNTAX_PLUGIN_ABI_VERSION)
            throw std::runtime_error("Plugin '" + manifest.name + "' requires ABI " + std::to_string(manifest.abi) +
                ", compiler provides ABI " + std::to_string(ABSOLUTE_SYNTAX_PLUGIN_ABI_VERSION));
        if (const auto loaded = loadedPlugins.find(manifest.name); loaded != loadedPlugins.end()) {
            if (loaded->second.version.empty())
                throw std::runtime_error("Plugin '" + manifest.name +
                    "' was already loaded directly; load its .absplugin manifest to enable versioned dependencies");
            if (loaded->second.manifest != canonical)
                throw std::runtime_error("Conflicting manifests for plugin '" + manifest.name + "': '" +
                    loaded->second.manifest.string() + "' and '" + canonical.string() + "'");
            return;
        }

        manifestStates[key] = ManifestState::Visiting;
        manifestStack.push_back(canonical);
        try {
            for (const Dependency& dependency : manifest.dependencies) {
                std::filesystem::path dependencyManifest;
                if (!dependency.path.empty()) {
                    dependencyManifest = dependency.path.is_absolute() ? dependency.path : canonical.parent_path() / dependency.path;
                }
                else {
                    std::vector<std::string> names = {dependency.name + ".absplugin"};
                    std::string dashed = dependency.name;
                    std::replace(dashed.begin(), dashed.end(), '.', '-');
                    if (dashed != dependency.name) names.push_back(dashed + ".absplugin");
                    std::vector<std::filesystem::path> directories = {canonical.parent_path()};
                    directories.insert(directories.end(), searchPaths.begin(), searchPaths.end());
                    for (const auto& directory : directories) {
                        for (const std::string& name : names) {
                            const std::filesystem::path candidate = directory / name;
                            if (std::filesystem::exists(candidate)) { dependencyManifest = candidate; break; }
                        }
                        if (!dependencyManifest.empty()) break;
                    }
                    if (dependencyManifest.empty())
                        throw std::runtime_error("Cannot resolve plugin dependency '" + dependency.name +
                            "' required by '" + manifest.name + "'; add its directory with --plugin-path or pluginSearchPaths");
                }
                try {
                    LoadManifest(dependencyManifest, dependency.name, dependency.version);
                }
                catch (const std::exception& error) {
                    throw std::runtime_error("While resolving '" + manifest.name + "' -> '" +
                        dependency.name + "' (" + dependency.version + "): " + error.what());
                }
            }
            for (const std::string& conflict : manifest.conflicts) {
                if (loadedPlugins.contains(conflict)) {
                    throw std::runtime_error("Plugin '" + manifest.name + "' conflicts with loaded plugin '" + conflict + "'");
                }
            }
            for (const Dependency& dependency : manifest.optionalDependencies) {
                std::filesystem::path dependencyManifest;
                if (!dependency.path.empty()) {
                    dependencyManifest = dependency.path.is_absolute() ? dependency.path : canonical.parent_path() / dependency.path;
                }
                else {
                    std::vector<std::string> names = {dependency.name + ".absplugin"};
                    std::string dashed = dependency.name;
                    std::replace(dashed.begin(), dashed.end(), '.', '-');
                    if (dashed != dependency.name) names.push_back(dashed + ".absplugin");
                    std::vector<std::filesystem::path> directories = {canonical.parent_path()};
                    directories.insert(directories.end(), searchPaths.begin(), searchPaths.end());
                    for (const auto& directory : directories) {
                        for (const std::string& name : names) {
                            const std::filesystem::path candidate = directory / name;
                            if (std::filesystem::exists(candidate)) { dependencyManifest = candidate; break; }
                        }
                        if (!dependencyManifest.empty()) break;
                    }
                }
                if (!dependencyManifest.empty() && std::filesystem::exists(dependencyManifest)) {
                    try { LoadManifest(dependencyManifest, dependency.name, dependency.version); } catch (...) {}
                }
            }
            for (const std::string& capability : manifest.requiresCapabilities) {
                if (!capabilityProviders.contains(capability))
                    throw std::runtime_error("Plugin '" + manifest.name + "' requires missing capability '" + capability + "'");
            }
            const std::filesystem::path library = manifest.library.is_absolute()
                ? manifest.library : canonical.parent_path() / manifest.library;
            LoadDynamicLibrary(library, manifest.name, manifest.version, canonical, manifest.provides);
            std::vector<std::filesystem::path> resolvedNativeLibraries;
            for (const std::filesystem::path& nativeLibrary : manifest.nativeLibraries) {
                const std::filesystem::path relative = canonical.parent_path() / nativeLibrary;
                if (!nativeLibrary.is_absolute() && (nativeLibrary.has_parent_path() || std::filesystem::exists(relative)))
                    resolvedNativeLibraries.push_back(relative);
                else resolvedNativeLibraries.push_back(nativeLibrary);
            }
            nativeLibraries.insert(nativeLibraries.begin(),
                resolvedNativeLibraries.begin(), resolvedNativeLibraries.end());
            manifestStates[key] = ManifestState::Loaded;
            manifestStack.pop_back();
        }
        catch (...) {
            manifestStates.erase(key);
            manifestStack.pop_back();
            throw;
        }
    }

    void PluginManager::LoadDynamicLibrary(const std::filesystem::path& path,
        const std::string& expectedName, const std::string& version,
        const std::filesystem::path& manifest, const std::vector<std::string>& capabilities) {
        if (!std::filesystem::exists(path))
            throw std::runtime_error("Syntax plugin does not exist: " + std::filesystem::absolute(path).string());
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(path);
        const std::string key = canonical.generic_string();
        if (const auto loaded = loadedLibraries.find(key); loaded != loadedLibraries.end()) {
            if (!expectedName.empty() && loaded->second != expectedName)
                throw std::runtime_error("Plugin library already loaded as '" + loaded->second +
                    "', expected '" + expectedName + "'");
            return;
        }

        void* handle = nullptr;
        InitFunction initialize = nullptr;
#ifdef _WIN32
        handle = static_cast<void*>(LoadLibraryW(canonical.c_str()));
        if (!handle) {
            const DWORD error = GetLastError();
            throw std::runtime_error("Cannot load syntax plugin '" + canonical.string() +
                "' (Windows error " + std::to_string(error) + ")");
        }
        initialize = reinterpret_cast<InitFunction>(FindSymbol(handle, "absolute_syntax_plugin_init_v1"));
#else
        handle = dlopen(canonical.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            const char* detail = dlerror();
            throw std::runtime_error("Cannot load syntax plugin '" + canonical.string() +
                "': " + (detail ? detail : "unknown loader error"));
        }
        initialize = reinterpret_cast<InitFunction>(FindSymbol(handle, "absolute_syntax_plugin_init_v1"));
#endif
        const auto compilerPluginInit = reinterpret_cast<AbsoluteCompilerPluginInitV1>(
            FindSymbol(handle, "absolute_compiler_plugin_v1"));
        const auto languagePluginInit = reinterpret_cast<AbsoluteLanguagePluginInitV1>(
            FindSymbol(handle, "absolute_language_plugin_v1"));
        const auto editorPluginInit = reinterpret_cast<AbsoluteEditorPluginInitV1>(
            FindSymbol(handle, "absolute_editor_plugin_v1"));

        if (!initialize && !compilerPluginInit && !languagePluginInit && !editorPluginInit) {
            CloseLibrary(handle);
            throw std::runtime_error("Syntax plugin '" + canonical.string() +
                "' does not export any valid Absolute plugin ABI v1 entry point");
        }

        try {
            std::string pluginName;
            const AbsoluteCompilerPluginV1* compilerDescPtr = nullptr;
            const AbsoluteEditorPluginV1* editorDescPtr = nullptr;

            if (compilerPluginInit) {
                compilerDescPtr = compilerPluginInit();
            }
            if (editorPluginInit) {
                editorDescPtr = editorPluginInit();
            }

            if (initialize) {
                const AbsoluteSyntaxPluginV1* descriptor = initialize();
                if (!descriptor || !descriptor->name || !*descriptor->name)
                    throw std::runtime_error("Syntax plugin returned an invalid descriptor: " + canonical.string());
                pluginName = descriptor->name;
                RegisterSyntaxPlugin(descriptor);
            }
            else if (compilerDescPtr) {
                if (!compilerDescPtr->name || !*compilerDescPtr->name)
                    throw std::runtime_error("Compiler plugin returned an invalid descriptor: " + canonical.string());
                pluginName = compilerDescPtr->name;
                if (compilerDescPtr->initialize) compilerDescPtr->initialize(nullptr);
            }
            else if (languagePluginInit) {
                const AbsoluteLanguagePluginV1* langDesc = languagePluginInit();
                if (!langDesc)
                    throw std::runtime_error("Language plugin returned an invalid descriptor: " + canonical.string());
                pluginName = expectedName.empty() ? "language_plugin" : expectedName;
                if (langDesc->binary_operator_table) RegisterPluginBinaryOperators(pluginName, langDesc->binary_operator_table);
                if (langDesc->resource_table) RegisterPluginResources(pluginName, langDesc->resource_table);
            }
            else if (!expectedName.empty()) {
                pluginName = expectedName;
            }

            if (!expectedName.empty() && expectedName != pluginName)
                throw std::runtime_error("Plugin manifest declares '" + expectedName +
                    "' but library descriptor declares '" + pluginName + "'");
            if (const auto duplicate = loadedPlugins.find(pluginName); duplicate != loadedPlugins.end())
                throw std::runtime_error("Plugin name '" + pluginName +
                    "' is already provided by " + duplicate->second.library.string());
            for (const std::string& capability : capabilities) {
                if (const auto provider = capabilityProviders.find(capability); provider != capabilityProviders.end())
                    throw std::runtime_error("Plugin capability '" + capability + "' is provided by both '" +
                        provider->second + "' and '" + pluginName + "'");
            }
            const auto prelude = reinterpret_cast<AbsoluteSyntaxPluginPreludeV1>(
                FindSymbol(handle, "absolute_syntax_plugin_prelude_v1"));
            const char* preludeSource = prelude ? prelude() : nullptr;
            const auto binaryOperators = reinterpret_cast<AbsoluteSyntaxPluginBinaryOperatorsV1>(
                FindSymbol(handle, "absolute_syntax_plugin_binary_operators_v1"));
            const AbsoluteBinaryOperatorTableV1* operatorTable = binaryOperators ? binaryOperators() : nullptr;
            const auto opaqueRules = reinterpret_cast<AbsoluteSyntaxPluginOpaqueRulesV1>(
                FindSymbol(handle, "absolute_syntax_plugin_opaque_rules_v1"));
            const AbsoluteOpaqueSyntaxTableV1* opaqueTable = opaqueRules ? opaqueRules() : nullptr;
            const auto pluginResources = reinterpret_cast<AbsoluteSyntaxPluginResourcesV1>(
                FindSymbol(handle, "absolute_syntax_plugin_resources_v1"));
            const AbsoluteResourceTableV1* resourceTable = pluginResources ? pluginResources() : nullptr;
            const auto virtualModules = reinterpret_cast<AbsolutePluginVirtualModulesV1>(
                FindSymbol(handle, "absolute_plugin_virtual_modules_v1"));
            const AbsoluteVirtualModuleTableV1* vmoduleTable = virtualModules ? virtualModules() : nullptr;
            if (prelude) RegisterSyntaxPluginPrelude(pluginName, preludeSource);
            if (binaryOperators) RegisterPluginBinaryOperators(pluginName, operatorTable);
            if (opaqueRules) RegisterOpaqueSyntaxRules(pluginName, opaqueTable);
            if (pluginResources) RegisterPluginResources(pluginName, resourceTable);
            if (virtualModules) RegisterPluginVirtualModules(pluginName, vmoduleTable);
            if (languagePluginInit) {
                const AbsoluteLanguagePluginV1* langDesc = languagePluginInit();
                if (langDesc && langDesc->virtual_module_table) RegisterPluginVirtualModules(pluginName, langDesc->virtual_module_table);
            }
            handles.push_back(handle);
            loadedLibraries.emplace(key, pluginName);
            loadedPlugins.emplace(pluginName,
                LoadedPlugin{pluginName, version, canonical, manifest, capabilities, compilerDescPtr, editorDescPtr, handle});
            for (const std::string& capability : capabilities)
                capabilityProviders.emplace(capability, pluginName);
            std::cout << "Loaded syntax plugin " << pluginName;
            if (!version.empty()) std::cout << " v" << version;
            std::cout << " from " << canonical.string() << '\n';
        }
        catch (...) {
            CloseLibrary(handle);
            throw;
        }
    }
}
