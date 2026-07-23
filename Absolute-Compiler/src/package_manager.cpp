#include "package_manager.h"
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace Absolute {

    namespace {
        std::string JsonString(const std::string& document, const std::string& key) {
            const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
            std::smatch match;
            if (std::regex_search(document, match, pattern)) return match[1].str();
            return {};
        }

        std::vector<std::string> JsonStringArray(const std::string& document, const std::string& key) {
            std::vector<std::string> result;
            const std::regex arrayPattern("\\\"" + key + "\\\"\\s*:\\s*\\[([^\\]]*)\\]");
            std::smatch arrayMatch;
            if (!std::regex_search(document, arrayMatch, arrayPattern)) return result;
            const std::string values = arrayMatch[1].str();
            const std::regex valuePattern("\\\"([^\\\"]+)\\\"");
            for (std::sregex_iterator it(values.begin(), values.end(), valuePattern), end; it != end; ++it)
                result.push_back((*it)[1].str());
            return result;
        }

        std::map<std::string, std::string> JsonObjectMap(const std::string& document, const std::string& key) {
            std::map<std::string, std::string> result;
            const std::regex objectPattern("\\\"" + key + "\\\"\\s*:\\s*\\{([^\\}]*)\\}");
            std::smatch objectMatch;
            if (!std::regex_search(document, objectMatch, objectPattern)) return result;
            const std::string pairs = objectMatch[1].str();
            const std::regex pairPattern("\\\"([^\\\"]+)\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
            for (std::sregex_iterator it(pairs.begin(), pairs.end(), pairPattern), end; it != end; ++it)
                result[(*it)[1].str()] = (*it)[2].str();
            return result;
        }

        std::string ReadFileContent(const std::filesystem::path& path) {
            std::ifstream file(path);
            if (!file.is_open())
                throw std::runtime_error("Cannot open file: " + path.string());
            std::stringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }

        void ResolveRecursive(
            const PackageManifest& currentManifest,
            const std::vector<std::filesystem::path>& registryPaths,
            PackageLockfile& resolved,
            std::unordered_set<std::string>& activeStack) {
            
            if (activeStack.contains(currentManifest.name)) {
                throw std::runtime_error("Cyclic dependency detected involving package '" + currentManifest.name + "'");
            }
            activeStack.insert(currentManifest.name);

            for (const auto& [depName, constraint] : currentManifest.dependencies) {
                std::filesystem::path depPath;

                // Search in configured registry paths
                for (const auto& registry : registryPaths) {
                    std::filesystem::path candidateDir = registry / depName;
                    if (std::filesystem::exists(candidateDir / "package.abs")) {
                        depPath = candidateDir / "package.abs"; break;
                    } else if (std::filesystem::exists(candidateDir / "abspackage.json")) {
                        depPath = candidateDir / "abspackage.json"; break;
                    } else if (std::filesystem::exists(candidateDir / (depName + ".absproj"))) {
                        depPath = candidateDir / (depName + ".absproj"); break;
                    }
                }

                if (depPath.empty()) {
                    if (resolved.resolvedPaths.contains(depName)) {
                        depPath = resolved.resolvedPaths[depName];
                    } else {
                        throw std::runtime_error("Package dependency '" + depName + "' not found in any package registry path");
                    }
                }

                PackageManifest depManifest = PackageManager::LoadManifest(depPath);
                if (!SemVer::Satisfies(depManifest.version, constraint)) {
                    throw std::runtime_error("Package '" + depName + "' version " + depManifest.version + 
                        " does not satisfy constraint '" + constraint + "' requested by '" + currentManifest.name + "'");
                }

                if (resolved.resolvedVersions.contains(depName)) {
                    const std::string& existingVer = resolved.resolvedVersions[depName];
                    if (!SemVer::Satisfies(existingVer, constraint)) {
                        throw std::runtime_error("Version conflict for package '" + depName + "': existing " + 
                            existingVer + " conflicts with constraint '" + constraint + "' from '" + currentManifest.name + "'");
                    }
                } else {
                    resolved.resolvedVersions[depName] = depManifest.version;
                    resolved.resolvedPaths[depName] = depPath.string();
                    // Transitive resolution
                    ResolveRecursive(depManifest, registryPaths, resolved, activeStack);
                }
            }

            activeStack.erase(currentManifest.name);
        }
    }

    SemVer SemVer::Parse(const std::string& str) {
        SemVer result;
        const std::regex pattern("^v?(\\d+)(?:\\.(\\d+))?(?:\\.(\\d+))?(?:-(.+))?$");
        std::smatch match;
        if (std::regex_match(str, match, pattern)) {
            result.major = std::stoi(match[1].str());
            result.minor = match[2].matched ? std::stoi(match[2].str()) : 0;
            result.patch = match[3].matched ? std::stoi(match[3].str()) : 0;
            result.prerelease = match[4].matched ? match[4].str() : "";
        }
        return result;
    }

    std::string SemVer::ToString() const {
        std::string result = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
        if (!prerelease.empty()) result += "-" + prerelease;
        return result;
    }

    bool SemVer::Satisfies(const std::string& versionStr, const std::string& constraintStr) {
        if (constraintStr == "*" || constraintStr.empty()) return true;
        SemVer version = SemVer::Parse(versionStr);

        std::string constraint = constraintStr;
        if (constraint.starts_with("^")) {
            SemVer target = SemVer::Parse(constraint.substr(1));
            return version.major == target.major &&
                   (version.minor > target.minor || (version.minor == target.minor && version.patch >= target.patch));
        }
        if (constraint.starts_with("~")) {
            SemVer target = SemVer::Parse(constraint.substr(1));
            return version.major == target.major && version.minor == target.minor && version.patch >= target.patch;
        }
        if (constraint.starts_with(">=")) {
            SemVer target = SemVer::Parse(constraint.substr(2));
            if (version.major != target.major) return version.major > target.major;
            if (version.minor != target.minor) return version.minor > target.minor;
            return version.patch >= target.patch;
        }
        if (constraint.starts_with("<=")) {
            SemVer target = SemVer::Parse(constraint.substr(2));
            if (version.major != target.major) return version.major < target.major;
            if (version.minor != target.minor) return version.minor < target.minor;
            return version.patch <= target.patch;
        }
        SemVer target = SemVer::Parse(constraint);
        return version.major == target.major && version.minor == target.minor && version.patch == target.patch;
    }

    PackageManifest PackageManager::LoadManifest(const std::filesystem::path& manifestPath) {
        PackageManifest manifest;
        manifest.root = std::filesystem::absolute(manifestPath).parent_path();
        const std::string document = ReadFileContent(manifestPath);

        manifest.name = JsonString(document, "name");
        manifest.version = JsonString(document, "version");
        if (manifest.version.empty()) manifest.version = "1.0.0";

        std::string target = JsonString(document, "type");
        if (!target.empty()) manifest.targetType = target;

        std::string entryStr = JsonString(document, "entry");
        if (!entryStr.empty()) manifest.entry = manifest.root / entryStr;
        else manifest.entry = manifest.root / "src/main.abs";

        for (const std::string& dir : JsonStringArray(document, "sources")) {
            manifest.sourceDirectories.push_back(manifest.root / dir);
        }
        if (manifest.sourceDirectories.empty() && std::filesystem::exists(manifest.entry.parent_path())) {
            manifest.sourceDirectories.push_back(manifest.entry.parent_path());
        }

        for (const std::string& reg : JsonStringArray(document, "registries")) {
            manifest.registries.push_back(std::filesystem::path(reg).is_absolute() ? std::filesystem::path(reg) : manifest.root / reg);
        }
        if (manifest.registries.empty()) {
            manifest.registries.push_back(manifest.root / "packages");
        }

        manifest.dependencies = JsonObjectMap(document, "dependencies");
        return manifest;
    }

    PackageLockfile PackageManager::LoadLockfile(const std::filesystem::path& lockfilePath) {
        PackageLockfile lockfile;
        if (!std::filesystem::exists(lockfilePath)) return lockfile;
        const std::string document = ReadFileContent(lockfilePath);

        lockfile.resolvedVersions = JsonObjectMap(document, "versions");
        lockfile.resolvedPaths = JsonObjectMap(document, "paths");
        return lockfile;
    }

    void PackageManager::SaveLockfile(const PackageLockfile& lockfile, const std::filesystem::path& lockfilePath) {
        std::stringstream ss;
        ss << "{\n";
        ss << "  \"versions\": {\n";
        size_t count = 0;
        for (const auto& [name, ver] : lockfile.resolvedVersions) {
            ss << "    \"" << name << "\": \"" << ver << "\"" << (++count < lockfile.resolvedVersions.size() ? ",\n" : "\n");
        }
        ss << "  },\n";
        ss << "  \"paths\": {\n";
        count = 0;
        for (const auto& [name, path] : lockfile.resolvedPaths) {
            ss << "    \"" << name << "\": \"" << path << "\"" << (++count < lockfile.resolvedPaths.size() ? ",\n" : "\n");
        }
        ss << "  }\n";
        ss << "}\n";

        std::ofstream file(lockfilePath);
        if (!file.is_open())
            throw std::runtime_error("Cannot write lockfile: " + lockfilePath.string());
        file << ss.str();
    }

    PackageLockfile PackageManager::ResolveDependencies(
        const PackageManifest& manifest,
        const std::vector<std::filesystem::path>& registryPaths,
        const PackageLockfile& existingLockfile) {
        
        PackageLockfile resolved = existingLockfile;
        std::unordered_set<std::string> activeStack;
        ResolveRecursive(manifest, registryPaths, resolved, activeStack);
        return resolved;
    }

    ModuleCache::ModuleCache(const std::filesystem::path& cacheDir)
        : cacheDir(cacheDir) {
        std::filesystem::create_directories(cacheDir);
    }

    bool ModuleCache::IsUpToDate(const std::filesystem::path& sourceFile) const {
        if (!std::filesystem::exists(sourceFile)) return false;
        auto cachedArtifact = GetCachedArtifactPath(sourceFile);
        if (!std::filesystem::exists(cachedArtifact)) return false;
        return std::filesystem::last_write_time(cachedArtifact) >= std::filesystem::last_write_time(sourceFile);
    }

    std::filesystem::path ModuleCache::GetCachedArtifactPath(const std::filesystem::path& sourceFile) const {
        std::string hash = ComputeFileHash(sourceFile);
        return cacheDir / (sourceFile.stem().string() + "_" + hash + ".obj");
    }

    void ModuleCache::Update(const std::filesystem::path& sourceFile, const std::string& contentOrHash) {
        if (std::filesystem::exists(sourceFile)) {
            mtimes[sourceFile.string()] = std::filesystem::last_write_time(sourceFile);
            auto artifact = GetCachedArtifactPath(sourceFile);
            std::ofstream f(artifact);
            if (f.is_open()) {
                f << (contentOrHash.empty() ? "CACHE_ENTRY_OK" : contentOrHash);
            }
        }
    }

    void ModuleCache::Clear() {
        mtimes.clear();
        if (std::filesystem::exists(cacheDir)) {
            std::filesystem::remove_all(cacheDir);
            std::filesystem::create_directories(cacheDir);
        }
    }

    std::string ModuleCache::ComputeFileHash(const std::filesystem::path& sourceFile) const {
        std::hash<std::string> hasher;
        size_t h = hasher(std::filesystem::absolute(sourceFile).string());
        return std::to_string(h);
    }

}
