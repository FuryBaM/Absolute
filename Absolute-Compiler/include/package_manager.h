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

    struct PackageManifest {
        std::filesystem::path root;
        std::string name;
        std::string version;
        std::string targetType = "app"; // "app" or "lib"
        std::filesystem::path entry;
        std::vector<std::filesystem::path> sourceDirectories;
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
            const std::filesystem::path& packagesDir,
            const PackageLockfile& existingLockfile);
    };

    class ModuleCache {
    public:
        bool IsUpToDate(const std::filesystem::path& sourceFile) const;
        void Update(const std::filesystem::path& sourceFile);
        void Clear();

    private:
        std::unordered_map<std::string, std::filesystem::file_time_type> mtimes;
    };

}
