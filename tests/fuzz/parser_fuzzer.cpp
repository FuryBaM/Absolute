// Coverage-guided entry point for the lexer and parser together.
//
// The parser is recursive descent, so nesting in the source becomes nesting on
// the machine stack. It carries its own depth limit; this harness additionally
// caps the input so the fuzzer spends its budget on shapes rather than on one
// enormous token run. Rejecting input must happen by throwing, never by
// leaving the process.

// parser_pch.h defines PARSER_API and the standard headers the
// public headers assume have already been included.
#include "parser_pch.h"

#include "parser.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {
constexpr std::size_t kMaximumInput = 64 * 1024;
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size > kMaximumInput) return -1;
    const std::string source(reinterpret_cast<const char*>(data), size);
    try {
        const std::vector<Absolute::Token> tokens = Absolute::Tokenize(source);
        const std::unique_ptr<Absolute::Program> program =
            Absolute::ParseCode(tokens, "fuzz.abs");
        volatile bool sink = program != nullptr;
        (void)sink;
    } catch (const std::exception&) {
        // Rejecting malformed input is the expected outcome.
    }
    return 0;
}
