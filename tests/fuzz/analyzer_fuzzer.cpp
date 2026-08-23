// Coverage-guided entry point for semantic analysis.
//
// Feeding raw bytes to the analyzer would test the parser's error path, not
// the analyzer: almost every random buffer fails to parse. The fuzzer's input
// is therefore spent as decisions in a generator that only emits syntactically
// valid programs, so what reaches the analyzer is a real AST with a shape the
// fuzzer controls.
//
// The programs are deliberately not required to be semantically valid. Type
// mismatches, unknown names and bad calls are exactly what the analyzer has to
// report, and reporting has to be a diagnostic rather than a crash.

#include "parser_pch.h"

#include "analyzer.h"
#include "parser.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kMaximumInput = 8 * 1024;
constexpr int kMaximumExpressionDepth = 6;
constexpr std::size_t kMaximumSourceBytes = 96 * 1024;

// Turns the fuzzer's buffer into a stream of bounded decisions. Running out of
// input is not an error: it just makes every later choice zero, which drives
// the generator toward its simplest alternative and terminates it.
class Decisions {
public:
    Decisions(const std::uint8_t* data, std::size_t size)
        : data_(data), size_(size) {}

    std::size_t choose(std::size_t alternatives) {
        if (alternatives <= 1) return 0;
        return next() % alternatives;
    }

    int integer() {
        return static_cast<int>(next()) | (static_cast<int>(next()) << 8);
    }

    bool exhausted() const { return position_ >= size_; }

private:
    std::uint8_t next() {
        if (position_ >= size_) return 0;
        return data_[position_++];
    }

    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t position_ = 0;
};

const char* const kTypes[] = {"int32", "int64", "double", "bool"};
constexpr std::size_t kTypeCount = sizeof(kTypes) / sizeof(kTypes[0]);

struct Variable {
    std::string name;
    std::string type;
};

class Generator {
public:
    explicit Generator(Decisions& decisions) : decisions_(decisions) {}

    std::string build() {
        const std::size_t helpers = decisions_.choose(4);
        for (std::size_t index = 0; index < helpers; ++index) emitFunction(index);
        emitMain();
        return source_;
    }

private:
    void append(const std::string& text) {
        if (source_.size() < kMaximumSourceBytes) source_ += text;
    }

    std::string pickType() { return kTypes[decisions_.choose(kTypeCount)]; }

    std::string literalFor(const std::string& type) {
        if (type == "bool") return decisions_.choose(2) ? "true" : "false";
        if (type == "double") {
            return std::to_string(decisions_.integer() % 1000) + ".5";
        }
        return std::to_string(decisions_.integer() % 10000);
    }

    // Names are generated, never taken from the input, so the program stays
    // syntactically valid whatever bytes arrive.
    std::string expression(int depth) {
        if (depth >= kMaximumExpressionDepth || decisions_.exhausted())
            return literalFor(pickType());

        switch (decisions_.choose(6)) {
        case 0:
            return literalFor(pickType());
        case 1:
            if (locals_.empty()) return literalFor(pickType());
            return locals_[decisions_.choose(locals_.size())].name;
        case 2: {
            static const char* const operators[] = {
                "+", "-", "*", "/", "%", "<", ">", "==", "!=", "&&", "||"};
            const char* chosen = operators[decisions_.choose(11)];
            return "(" + expression(depth + 1) + " " + chosen + " " +
                expression(depth + 1) + ")";
        }
        case 3:
            if (functions_.empty()) return literalFor(pickType());
            return callTo(functions_[decisions_.choose(functions_.size())],
                depth + 1);
        case 4:
            return "(" + expression(depth + 1) + ")";
        default:
            return "(" + expression(depth + 1) + " as " + pickType() + ")";
        }
    }

    std::string callTo(const std::string& name, int depth) {
        return name + "(" + expression(depth) + ", " + expression(depth) + ")";
    }

    void statement(int depth, int indent) {
        const std::string pad(static_cast<std::size_t>(indent) * 4, ' ');
        if (depth >= 3 || decisions_.exhausted()) {
            append(pad + "println(" + expression(0) + ");\n");
            return;
        }
        switch (decisions_.choose(6)) {
        case 0: {
            const std::string type = pickType();
            const std::string name = "v" + std::to_string(counter_++);
            append(pad + type + " " + name + " = " + expression(0) + ";\n");
            locals_.push_back({name, type});
            break;
        }
        case 1:
            if (!locals_.empty()) {
                const Variable& target =
                    locals_[decisions_.choose(locals_.size())];
                append(pad + target.name + " = " + expression(0) + ";\n");
            }
            break;
        case 2:
            append(pad + "if (" + expression(0) + ") {\n");
            statement(depth + 1, indent + 1);
            append(pad + "} else {\n");
            statement(depth + 1, indent + 1);
            append(pad + "}\n");
            break;
        case 3:
            // A counted loop: the generator must not emit something that runs
            // forever, because the analyzer is what is under test here.
            append(pad + "int32 i" + std::to_string(counter_) + " = 0;\n");
            append(pad + "while (i" + std::to_string(counter_) + " < 3) {\n");
            append(pad + "    i" + std::to_string(counter_) + " += 1;\n");
            ++counter_;
            statement(depth + 1, indent + 1);
            append(pad + "}\n");
            break;
        case 4:
            append(pad + "println(" + expression(0) + ");\n");
            break;
        default:
            if (!functions_.empty()) {
                append(pad +
                    callTo(functions_[decisions_.choose(functions_.size())], 0) +
                    ";\n");
            }
            break;
        }
    }

    void emitFunction(std::size_t index) {
        const std::string name = "helper" + std::to_string(index);
        const std::string returnType = pickType();
        const std::string first = pickType();
        const std::string second = pickType();
        append(returnType + " " + name + "(" + first + " a, " + second +
            " b) {\n");
        const std::vector<Variable> saved = locals_;
        locals_ = {{"a", first}, {"b", second}};
        const std::size_t count = decisions_.choose(3);
        for (std::size_t step = 0; step < count; ++step) statement(0, 1);
        append("    return " + expression(0) + ";\n}\n\n");
        locals_ = saved;
        functions_.push_back(name);
    }

    void emitMain() {
        append("int32 main() {\n");
        locals_.clear();
        const std::size_t count = 1 + decisions_.choose(5);
        for (std::size_t step = 0; step < count; ++step) statement(0, 1);
        append("    return 0;\n}\n");
    }

    Decisions& decisions_;
    std::string source_;
    std::vector<Variable> locals_;
    std::vector<std::string> functions_;
    int counter_ = 0;
};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size > kMaximumInput) return -1;
    Decisions decisions(data, size);
    Generator generator(decisions);
    const std::string source = generator.build();

    // The input is a stream of decisions, not text, so a crashing buffer is
    // not readable on its own. This prints the program it stands for, which is
    // what a report needs.
    if (std::getenv("ABSOLUTE_FUZZ_DUMP_SOURCE") != nullptr)
        std::fputs((source + "\n").c_str(), stderr);

    try {
        const std::vector<Absolute::Token> tokens = Absolute::Tokenize(source);
        std::unique_ptr<Absolute::Program> program =
            Absolute::ParseCode(tokens, "fuzz.abs");
        if (!program) return 0;
        Absolute::Analyzer analyzer({program.get()});
        // Either outcome is fine. Reporting an invalid program is the point;
        // crashing on one is the defect this looks for.
        volatile bool sink = analyzer.Analyze();
        (void)sink;
    } catch (const std::exception&) {
        // A rejected program is a valid outcome.
    }
    return 0;
}
