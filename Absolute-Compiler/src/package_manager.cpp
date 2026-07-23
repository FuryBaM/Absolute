#include "package_manager.h"
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>

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
        else {
            std::string targetAttr = JsonString(document, "targetType");
            if (!targetAttr.empty()) manifest.targetType = targetAttr;
        }

        std::string entryStr = JsonString(document, "entry");
        if (!entryStr.empty()) manifest.entry = manifest.root / entryStr;
        else manifest.entry = manifest.root / "src/main.abs";

        for (const std::string& dir : JsonStringArray(document, "sources")) {
            manifest.sourceDirectories.push_back(manifest.root / dir);
        }
        if (manifest.sourceDirectories.empty() && std::filesystem::exists(manifest.entry.parent_path())) {
            manifest.sourceDirectories.push_back(manifest.entry.parent_path());
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
        const std::filesystem::path& packagesDir,
        const PackageLockfile& existingLockfile) {
        
        PackageLockfile resolved = existingLockfile;
        for (const auto& [depName, constraint] : manifest.dependencies) {
            std::filesystem::path depPath;
            std::filesystem::path candidateDir = packagesDir / depName;
            if (std::filesystem::exists(candidateDir / "package.abs")) {
                depPath = candidateDir / "package.abs";
            } else if (std::filesystem::exists(candidateDir / "abspackage.json")) {
                depPath = candidateDir / "abspackage.json";
            } else if (std::filesystem::exists(candidateDir / (depName + ".absproj"))) {
                depPath = candidateDir / (depName + ".absproj");
            }

            if (depPath.empty()) {
                if (resolved.resolvedPaths.contains(depName)) {
                    depPath = resolved.resolvedPaths[depName];
                } else {
                    throw std::runtime_error("Package dependency '" + depName + "' not found in packages directory: " + packagesDir.string());
                }
            }

            PackageManifest depManifest = LoadManifest(depPath);
            if (!SemVer::Satisfies(depManifest.version, constraint)) {
                throw std::runtime_error("Package '" + depName + "' version " + depManifest.version + " does not satisfy constraint '" + constraint + "'");
            }

            if (resolved.resolvedVersions.contains(depName)) {
                const std::string& existingVer = resolved.resolvedVersions[depName];
                if (!SemVer::Satisfies(existingVer, constraint)) {
                    throw std::runtime_error("Dependency version conflict for '" + depName + "': existing " + existingVer + " vs constraint " + constraint);
                }
            } else {
                resolved.resolvedVersions[depName] = depManifest.version;
                resolved.resolvedPaths[depName] = depPath.string();
            }
        }
        return resolved;
    }

    bool ModuleCache::IsUpToDate(const std::filesystem::path& sourceFile) const {
        if (!std::filesystem::exists(sourceFile)) return false;
        auto it = mtimes.find(sourceFile.string());
        if (it == mtimes.end()) return false;
        return it->second == std::filesystem::last_write_time(sourceFile);
    }

    void ModuleCache::Update(const std::filesystem::path& sourceFile) {
        if (std::filesystem::exists(sourceFile)) {
            mtimes[sourceFile.string()] = std::filesystem::last_write_time(sourceFile);
        }
    }

    void ModuleCache::Clear() {
        mtimes.clear();
    }

}
