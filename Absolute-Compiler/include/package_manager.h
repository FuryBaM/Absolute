#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include <unordered_map>

namespace Absolute {

    struct SemVer {
        int major = 0;
        int minor = 0;
        int patch = 0;
        std::string prerelease;

        static SemVer Parse(const std::string& str);
        static bool Satisfies(const std::string& versionStr, const std::string& constraintStr);
        std::string ToString() const;
    };

    struct PackageTarget {
        std::string name;
        std::string type = "app"; // "app" or "lib"
        std::filesystem::path entry;
        std::vector<std::filesystem::path> sourceDirectories;
    };

    struct PackageManifest {
        std::filesystem::path root;
        std::string name;
        std::string version;
        std::string targetType = "app"; // "app" or "lib"
        std::filesystem::path entry;
        std::vector<std::filesystem::path> sourceDirectories;
        std::vector<PackageTarget> targets;
        std::vector<std::filesystem::path> registries;
        std::map<std::string, std::string> dependencies;
    };

    struct PackageLockfile {
        std::map<std::string, std::string> resolvedVersions;
        std::map<std::string, std::string> resolvedPaths;
    };

    class PackageManager {
    public:
        static PackageManifest LoadManifest(const std::filesystem::path& manifestPath);
        static PackageLockfile LoadLockfile(const std::filesystem::path& lockfilePath);
        static void SaveLockfile(const PackageLockfile& lockfile, const std::filesystem::path& lockfilePath);
        
        static PackageLockfile ResolveDependencies(
            const PackageManifest& manifest,
            const std::vector<std::filesystem::path>& registryPaths,
            const PackageLockfile& existingLockfile);
    };

    class ModuleCache {
    public:
        explicit ModuleCache(const std::filesystem::path& cacheDir = ".absolute/cache");
        bool IsUpToDate(const std::filesystem::path& sourceFile) const;
        std::filesystem::path GetCachedArtifactPath(const std::filesystem::path& sourceFile) const;
        void Update(const std::filesystem::path& sourceFile, const std::string& contentOrHash = "");
        void Clear();

    private:
        std::filesystem::path cacheDir;
        std::unordered_map<std::string, std::filesystem::file_time_type> mtimes;
        std::string ComputeFileHash(const std::filesystem::path& sourceFile) const;
    };

}
