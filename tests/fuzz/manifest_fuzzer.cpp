// libFuzzer target for the package manifest, lock-file and version parsers.
//
// These read files a user can hand the compiler, so malformed input is
// expected rather than exceptional. Rejecting it with an exception is correct;
// crashing, hanging or reading out of bounds is not, and that is the whole
// distinction this target draws. Every std::exception is therefore swallowed
// and only a hard failure reaches libFuzzer.
//
// The first byte selects what to feed, so one corpus serves three parsers.
// SemVer runs on the buffer directly, while the two loaders take a path, so
// their inputs go through a temporary file. That is slower than an in-memory
// call, but it exercises the loader as the compiler actually calls it,
// including the file reading and the absolute-path handling around it.

#include "package_manager.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

using namespace Absolute;

namespace {

    // One path per process, reused across iterations. Creating a fresh
    // temporary each time would dominate the run and shrink the number of
    // inputs libFuzzer gets through.
    const std::filesystem::path& ScratchFile() {
        static const std::filesystem::path path = [] {
            std::filesystem::path candidate =
                std::filesystem::temp_directory_path() /
                "absolute-manifest-fuzz.json";
            return candidate;
        }();
        return path;
    }

    void WriteScratch(const uint8_t* data, size_t size) {
        std::ofstream out(ScratchFile(), std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data),
                  static_cast<std::streamsize>(size));
    }

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 2) return 0;
    const uint8_t selector = data[0];
    const uint8_t* body = data + 1;
    const size_t bodySize = size - 1;

    try {
        switch (selector % 3) {
        case 0: {
            // Version strings and constraints. Split the buffer so both the
            // version parser and the constraint matcher see fuzzed input,
            // rather than matching a fuzzed version against a fixed range.
            const std::string text(reinterpret_cast<const char*>(body),
                                   bodySize);
            const size_t split = text.find('\n');
            const std::string version =
                split == std::string::npos ? text : text.substr(0, split);
            const std::string constraint =
                split == std::string::npos ? std::string("^1.0.0")
                                           : text.substr(split + 1);
            SemVer::Parse(version);
            SemVer::Satisfies(version, constraint);
            break;
        }
        case 1:
            WriteScratch(body, bodySize);
            PackageManager::LoadManifest(ScratchFile());
            break;
        default:
            WriteScratch(body, bodySize);
            PackageManager::LoadLockfile(ScratchFile());
            break;
        }
    }
    catch (const std::exception&) {
        // Refusing malformed input is the contract, not a finding.
    }
    return 0;
}
