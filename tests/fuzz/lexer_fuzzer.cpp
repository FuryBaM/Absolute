// Coverage-guided entry point for the lexer.
//
// The lexer takes arbitrary source text, so it must terminate and stay inside
// its buffers for any byte sequence at all. Anything it rejects has to be
// rejected by returning or throwing, never by leaving the process.

// parser_pch.h defines PARSER_API and the standard headers the
// public headers assume have already been included.
#include "parser_pch.h"

#include "lexer.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {
// A fuzzer that spends its budget on a megabyte of brackets learns nothing.
constexpr std::size_t kMaximumInput = 64 * 1024;
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size > kMaximumInput) return -1;
    const std::string source(reinterpret_cast<const char*>(data), size);
    try {
        const std::vector<Absolute::Token> tokens = Absolute::lexer(source);
        // Touch the result so the tokens cannot be optimized away.
        volatile std::size_t sink = tokens.size();
        (void)sink;
    } catch (const std::exception&) {
        // A rejected input is a valid outcome; a crash or a hang is not.
    }
    return 0;
}
