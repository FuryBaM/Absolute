#include "pch.h"
#include "plugin_loader.h"

#include "plugin_api.h"
#include "syntax_plugins.h"

#include <iostream>
#include <stdexcept>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

namespace Absolute {
    namespace {
        using InitFunction = const AbsoluteSyntaxPluginV1* (*)();

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

    PluginManager::~PluginManager() {
        ResetSyntaxPlugins();
        for (auto iterator = handles.rbegin(); iterator != handles.rend(); ++iterator)
            CloseLibrary(*iterator);
    }

    void PluginManager::Load(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path))
            throw std::runtime_error("Syntax plugin does not exist: " + std::filesystem::absolute(path).string());
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(path);
        if (!loadedPaths.insert(canonical.generic_string()).second) return;

        void* handle = nullptr;
        InitFunction initialize = nullptr;
#ifdef _WIN32
        handle = static_cast<void*>(LoadLibraryW(canonical.c_str()));
        if (!handle) {
            const DWORD error = GetLastError();
            loadedPaths.erase(canonical.generic_string());
            throw std::runtime_error("Cannot load syntax plugin '" + canonical.string() +
                "' (Windows error " + std::to_string(error) + ")");
        }
        initialize = reinterpret_cast<InitFunction>(FindSymbol(handle, "absolute_syntax_plugin_init_v1"));
#else
        handle = dlopen(canonical.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            const char* detail = dlerror();
            loadedPaths.erase(canonical.generic_string());
            throw std::runtime_error("Cannot load syntax plugin '" + canonical.string() +
                "': " + (detail ? detail : "unknown loader error"));
        }
        initialize = reinterpret_cast<InitFunction>(FindSymbol(handle, "absolute_syntax_plugin_init_v1"));
#endif
        if (!initialize) {
            CloseLibrary(handle);
            loadedPaths.erase(canonical.generic_string());
            throw std::runtime_error("Syntax plugin '" + canonical.string() +
                "' does not export absolute_syntax_plugin_init_v1");
        }

        try {
            const AbsoluteSyntaxPluginV1* descriptor = initialize();
            const auto prelude = reinterpret_cast<AbsoluteSyntaxPluginPreludeV1>(
                FindSymbol(handle, "absolute_syntax_plugin_prelude_v1"));
            const char* preludeSource = prelude ? prelude() : nullptr;
            const auto binaryOperators = reinterpret_cast<AbsoluteSyntaxPluginBinaryOperatorsV1>(
                FindSymbol(handle, "absolute_syntax_plugin_binary_operators_v1"));
            const AbsoluteBinaryOperatorTableV1* operatorTable = binaryOperators ? binaryOperators() : nullptr;
            RegisterSyntaxPlugin(descriptor);
            if (prelude) RegisterSyntaxPluginPrelude(descriptor->name, preludeSource);
            if (binaryOperators) RegisterPluginBinaryOperators(descriptor->name, operatorTable);
            handles.push_back(handle);
            std::cout << "Loaded syntax plugin " << descriptor->name << " from " << canonical.string() << '\n';
        }
        catch (...) {
            CloseLibrary(handle);
            loadedPaths.erase(canonical.generic_string());
            throw;
        }
    }
}
