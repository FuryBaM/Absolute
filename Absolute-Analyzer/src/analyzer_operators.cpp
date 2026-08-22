#include "analyzer_internal.h"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <unordered_map>
#include <utility>

namespace Absolute {
    namespace {
        // The width a shift happens at, which is the width of the result type:
        // both operands are widened to it before the shift, so `1 << 40` is out
        // of range for an int32 result and in range for an int64 one.
        unsigned IntegerWidth(const std::string& type) {
            if (type == "int8" || type == "uint8" || type == "char") return 8;
            if (type == "int16" || type == "uint16") return 16;
            if (type == "int64" || type == "uint64") return 64;
            return 32;
        }

        // A shift amount the source writes out, so it can be judged here where
        // the message carries a file and a line. Anything computed is checked
        // at runtime instead.
        bool WrittenShiftAmount(Expression* expression,
            unsigned long long& magnitude, bool& negative) {
            negative = false;
            const auto* number = dynamic_cast<const NumberLiteralExpr*>(expression);
            if (const auto* unary = dynamic_cast<const PrefixUnaryExpr*>(expression);
                unary && (unary->op == "-" || unary->op == "+")) {
                number = dynamic_cast<const NumberLiteralExpr*>(unary->operand.get());
                negative = unary->op == "-";
            }
            if (!number || IsFloatingLiteral(number->value)) return false;
            try { magnitude = std::stoull(number->value); }
            catch (const std::exception&) { return false; }
            return true;
        }
    }

    // A shift by the width of the value being shifted, or more, is undefined in
    // LLVM rather than a wrap or a zero: `1 << 32` printed -1780665664 on one
    // build, `1 << 40` produced a value the formatter could not print at all,
    // and each rebuild was free to choose differently. Refused rather than
    // defined, which is the same answer division overflow got -- a program that
    // shifts a 32-bit value by 32 has a bug in it, and a plausible-looking 0
    // would hide it.
    void Analyzer::CheckShiftAmount(
        Expression* amount, const std::string& resultType, const std::string& op) {
        if (!amount || !IsInteger(resultType)) return;
        unsigned long long magnitude = 0;
        bool negative = false;
        if (!WrittenShiftAmount(amount, magnitude, negative)) return;
        const unsigned width = IntegerWidth(resultType);
        if (magnitude < width && !(negative && magnitude != 0)) return;
        Report("shift amount " + std::string(negative ? "-" : "") +
            std::to_string(magnitude) + " is out of range for '" + op +
            "' on '" + resultType + "', which is " + std::to_string(width) +
            " bits wide", "E_SHIFT_OUT_OF_RANGE");
    }

    void Analyzer::Visit(BinaryExpr* expr) {
        const Result left = Evaluate(expr->left.get());
        const Result right = Evaluate(expr->right.get());
        const std::string& op = expr->op;
        if (const PluginBinaryOperator* pluginOperator =
                FindPluginBinaryOperator(left.type, op, right.type)) {
            const std::vector<std::string> expectedParameters{left.type, right.type};
            const Symbol* function = FindFunctionSymbol(pluginOperator->functionName, expectedParameters);
            const SymbolId functionId = function ? function->id : InvalidSymbolId;
            if (!function || function->kind != SymbolKind::Function ||
                function->parameterTypes != expectedParameters || function->type != pluginOperator->resultType) {
                Report("plugin operator '" + left.type + " " + op + " " + right.type +
                    "' requires function '" + pluginOperator->functionName + "(" + left.type + ", " +
                    right.type + ") -> " + pluginOperator->resultType + "'", "E_PLUGIN_OPERATOR_SIGNATURE");
                Save(expr, {InvalidSymbolId, "error", false});
                return;
            }
            const bool managedResult = IsManagedPointerType(pluginOperator->resultType);
            Save(expr, {functionId, pluginOperator->resultType, false, managedResult, false,
                InitializationState::Initialized,
                managedResult ? PointerValidity::Live : PointerValidity::NotPointer});
            return;
        }
        const bool leftPointer = IsPointerType(left.type);
        const bool rightPointer = IsPointerType(right.type);
        if ((leftPointer || rightPointer) && (op == "+" || op == "-")) {
            if (IsManagedPointerType(left.type) || IsManagedPointerType(right.type)) {
                Report("managed pointers do not support arithmetic; use raw T* when address arithmetic is required",
                    "E_MANAGED_POINTER_ARITHMETIC");
                Save(expr, {InvalidSymbolId, "error", false});
            }
            else if (op == "+" && IsRawPointerType(left.type) && IsInteger(right.type))
                Save(expr, {InvalidSymbolId, left.type, false, false, false,
                    left.initialization, left.pointerValidity, left.pointerOwner});
            else if (op == "+" && IsInteger(left.type) && IsRawPointerType(right.type))
                Save(expr, {InvalidSymbolId, right.type, false, false, false,
                    right.initialization, right.pointerValidity, right.pointerOwner});
            else if (op == "-" && IsRawPointerType(left.type) && IsInteger(right.type))
                Save(expr, {InvalidSymbolId, left.type, false, false, false,
                    left.initialization, left.pointerValidity, left.pointerOwner});
            else if (op == "-" && IsRawPointerType(left.type) && left.type == right.type)
                Save(expr, {InvalidSymbolId, "int64", false});
            else {
                Report("invalid pointer arithmetic between '" + left.type + "' and '" + right.type + "'",
                    "E_INVALID_POINTER_ARITHMETIC");
                Save(expr, {InvalidSymbolId, "error", false});
            }
        }
        else if (op == "&&" || op == "||") {
            if (!IsConditionType(left.type) || !IsConditionType(right.type)) Report("logical operands must be boolean-compatible",
                "E_LOGICAL_OPERAND_TYPE");
            Save(expr, {InvalidSymbolId, "bool", false});
        }
        else if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
            const bool managedOperand =
                IsManagedPointerType(left.type) || IsManagedPointerType(right.type);
            if (op != "==" && op != "!=" && managedOperand)
                Report("managed pointers only support equality and null comparisons",
                    "E_MANAGED_POINTER_COMPARISON");
            // `<`, `<=`, `>` and `>=` order their operands, and the backend
            // orders numbers, characters, enum members and raw addresses --
            // nothing else. A string and a struct reached it as
            // "assignable to each other" and failed there with "binary
            // operator requires numeric operands": the backend naming its own
            // mechanism, with no file and no line. Equality is not asked,
            // because it is defined for more than these -- a string compares
            // by its bytes.
            //
            // A managed pointer is left to the message above rather than given
            // a second one for the same mistake.
            // An open `T` is not judged here -- it is not a type yet. A
            // generic class records the question and each instantiation
            // answers it; a generic function's parameter is left to the
            // instantiation itself, which is where the backend meets a
            // concrete type.
            //
            // A qualifier in front of an open name is not part of that
            // question either: ordering asks about the value, and `sub T`
            // orders exactly when `T` does. Unwrapped rather than refused, or
            // a container's own algorithms would be judged on the qualifier
            // its indexer hands back rather than on the element.
            const auto judge = [&](const std::string& type) {
                const std::string asked =
                    CanonicalOpenOwnership(type) == OwnershipKind::None
                        ? type : CanonicalOpenBaseName(type);
                if (IsOpenGenericParameter(asked)) {
                    RecordGenericBodyFact(GenericBodyFact::Shape::OrdersValues,
                        asked, op, expr);
                    return true;
                }
                return IsOrderableType(asked);
            };
            if (op != "==" && op != "!=" && !managedOperand) {
                const bool leftOrders = judge(left.type);
                const bool rightOrders = judge(right.type);
                if (!leftOrders || !rightOrders)
                    Report("operator '" + op + "' orders numbers, characters, "
                        "enum members and raw pointers, not '" +
                        left.type + "' and '" + right.type + "'",
                        "E_ORDERING_OPERAND_TYPE");
            }
            if (!IsAssignable(left.type, right.type) && !IsAssignable(right.type, left.type))
                Report("cannot compare '" + left.type + "' with '" + right.type + "'",
                    "E_INCOMPARABLE_TYPES");
            Save(expr, {InvalidSymbolId, "bool", false});
        }
        else if (op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>") {
            if (!IsInteger(left.type) || !IsInteger(right.type)) Report("bitwise operands must be integers",
                "E_BITWISE_OPERAND_TYPE");
            const std::string resultType = CommonType(left.type, right.type);
            if (op == "<<" || op == ">>")
                CheckShiftAmount(expr->right.get(), resultType, op);
            Save(expr, {InvalidSymbolId, resultType, false});
        }
        else {
            if (!IsNumeric(left.type) || !IsNumeric(right.type))
                Report("operator '" + op + "' requires numeric operands",
                    "E_NUMERIC_OPERAND_REQUIRED");
            Save(expr, {InvalidSymbolId, CommonType(left.type, right.type), false});
        }
    }

    void Analyzer::Visit(TernaryExpr* expr) {
        // The type the conditional is being read as, if there is one. Held
        // across the arms because evaluating one sets it for whatever is
        // nested inside it.
        const std::string target = expectedType;
        const Result condition = Evaluate(expr->condition.get());
        if (!IsConditionType(condition.type)) Report("ternary condition must be boolean-compatible",
            "E_TERNARY_CONDITION_TYPE");
        const ValueFlowMap baseValues = valueFlow;
        const Result trueResult = EvaluateExpected(expr->trueExpr.get(), target);
        const ValueFlowMap trueValues = valueFlow;
        valueFlow = baseValues;
        const Result falseResult = EvaluateExpected(expr->falseExpr.get(), target);
        const ValueFlowMap falseValues = valueFlow;
        MergeValueFlowPaths(baseValues, {trueValues, falseValues});

        // Read as T when there is a T to read it as and both arms are one:
        // `Shape* s = left ? new Bigger(3) : new Square(3)` needs no cast on
        // either arm, because going from a type to one it derives from is
        // always safe and writing it twice says nothing the declaration has
        // not already said.
        std::string type = "error";
        const bool targeted = !target.empty() && target != "auto" &&
            target != "error" && trueResult.type != "error" &&
            falseResult.type != "error" &&
            IsAssignable(target, trueResult.type) &&
            IsAssignable(target, falseResult.type);
        if (targeted) type = ValueReferenceBaseType(target);
        else type = CommonType(trueResult.type, falseResult.type);
        if (type == "error" && IsPointerType(trueResult.type) && falseResult.type == "null") type = trueResult.type;
        if (type == "error" && IsPointerType(falseResult.type) && trueResult.type == "null") type = falseResult.type;
        // With no type to read it as, the arms decide: the least of the types
        // both can be seen as. `auto s = left ? new Bigger(3) : new Square(3)`
        // is a `Square*` because that is the nearest name that covers both.
        bool ambiguousBound = false;
        if (type == "error") {
            const std::string bound = CommonPointerType(
                trueResult.type, falseResult.type, ambiguousBound);
            if (!bound.empty()) type = bound;
        }
        if (ambiguousBound)
            Report("ternary branches '" + trueResult.type + "' and '" +
                falseResult.type + "' share several unrelated types and no "
                "nearest one; name the type the conditional is read as",
                "E_AMBIGUOUS_CONDITIONAL_TYPE");
        else if (type == "error" && trueResult.type != "error" && falseResult.type != "error")
            Report("ternary branches have incompatible types '" + trueResult.type + "' and '" + falseResult.type + "'",
                "E_TERNARY_BRANCH_TYPES");
        PointerValidity pointerValidity = PointerValidity::NotPointer;
        if (IsPointerType(type)) {
            if (trueResult.pointerValidity == falseResult.pointerValidity)
                pointerValidity = trueResult.pointerValidity;
            else if ((trueResult.pointerValidity == PointerValidity::Null &&
                falseResult.pointerValidity == PointerValidity::Live) ||
                (falseResult.pointerValidity == PointerValidity::Null &&
                    trueResult.pointerValidity == PointerValidity::Live))
                pointerValidity = PointerValidity::MaybeNull;
            else pointerValidity = PointerValidity::MaybeInvalid;
        }
        SymbolId pointerOwner = trueResult.pointerOwner == falseResult.pointerOwner
            ? trueResult.pointerOwner : InvalidSymbolId;
        if (trueResult.pointerValidity == PointerValidity::Null) pointerOwner = falseResult.pointerOwner;
        if (falseResult.pointerValidity == PointerValidity::Null) pointerOwner = trueResult.pointerOwner;
        Result combined{InvalidSymbolId, type, false,
            trueResult.createsManagedOwner && falseResult.createsManagedOwner,
            trueResult.referencesManagedOwner && falseResult.referencesManagedOwner,
            InitializationState::Initialized, pointerValidity, pointerOwner};
        combined.createsRawOwner = trueResult.createsRawOwner && falseResult.createsRawOwner;
        Save(expr, std::move(combined));
    }

    void Analyzer::Visit(NullExpr* expr) {
        Save(expr, {InvalidSymbolId, "null", false, false, false,
            InitializationState::Initialized, PointerValidity::Null, InvalidSymbolId});
    }
    void Analyzer::Visit(BooleanLiteralExpr* expr) { Save(expr, {InvalidSymbolId, "bool", false}); }
    // An integer literal takes the narrowest type that can hold it, which is
    // what the backend has always done when emitting one. The analyzer called
    // every integer literal int32, and the two only agreed while the literal
    // fit: wherever the analyzer's type decided the storage -- a generic
    // parameter inferred from the argument, most visibly -- a wider literal
    // was truncated to i32 without a word. identity(10000000000) returned
    // 1410065408.
    static std::string IntegerLiteralType(const std::string& text, bool negated) {
        unsigned long long parsed = 0;
        try {
            parsed = std::stoull(text);
        }
        catch (const std::exception&) {
            // Out of 64-bit range entirely; the backend reports it by value.
            return "uint64";
        }
        // A minus in front of the literal is part of the constant, so the type
        // is chosen for the value the source writes. Without this,
        // -9223372036854775808 -- int64's minimum -- had a magnitude that only
        // uint64 could hold, so the negation wrapped and printing it produced
        // 9223372036854775808. The sign was gone and nothing said so.
        const unsigned long long signedMaximum = negated
            ? static_cast<unsigned long long>(std::numeric_limits<std::int64_t>::max()) + 1
            : static_cast<unsigned long long>(std::numeric_limits<std::int64_t>::max());
        const unsigned long long narrowMaximum = negated
            ? static_cast<unsigned long long>(std::numeric_limits<std::int32_t>::max()) + 1
            : static_cast<unsigned long long>(std::numeric_limits<std::int32_t>::max());
        if (parsed <= narrowMaximum) return "int32";
        if (parsed <= signedMaximum) return "int64";
        return "uint64";
    }

    // The widest magnitude each integer type can hold, positive and negative.
    // Narrowing between values is allowed on purpose (docs/implicit-conversions
    // .md), but a literal is not a value the compiler has to take on trust: it
    // can see that 4294967296 is not an int32, and truncating it to 0 without a
    // word is the same silent wrongness the type rules exist to prevent.
    static bool IntegerLiteralFits(
        const std::string& target, unsigned long long magnitude, bool negative) {
        static const std::unordered_map<std::string, std::pair<unsigned long long, unsigned long long>>
            limits = {
                // {largest positive, largest magnitude when negated}
                {"int8", {127ull, 128ull}},
                {"int16", {32767ull, 32768ull}},
                {"int32", {2147483647ull, 2147483648ull}},
                {"int64", {9223372036854775807ull, 9223372036854775808ull}},
                {"uint8", {255ull, 256ull}},
                {"uint16", {65535ull, 65536ull}},
                {"uint32", {4294967295ull, 4294967296ull}},
                {"uint64", {18446744073709551615ull, 18446744073709551615ull}},
                {"char", {255ull, 256ull}},
            };
        const auto found = limits.find(target);
        if (found == limits.end()) return true;
        // A negated literal is allowed up to the wrapped magnitude of the
        // width, which is what an unsigned mask written as -1 relies on.
        return magnitude <= (negative ? found->second.second : found->second.first);
    }

    void Analyzer::Visit(NumberLiteralExpr* expr) {
        if (IsFloatingLiteral(expr->value)) {
            // Reported here rather than only in the backend so the message
            // carries a file, a line and a column. A subnormal is a value a
            // double holds and is accepted; only a literal that keeps nothing
            // of what was written is refused.
            errno = 0;
            char* end = nullptr;
            const double parsed = std::strtod(expr->value.c_str(), &end);
            if (end && *end == '\0' && errno == ERANGE) {
                if (std::isinf(parsed))
                    Report("floating literal " + expr->value +
                        " is larger than a double can hold", "E_LITERAL_OUT_OF_RANGE");
                else if (parsed == 0.0)
                    Report("floating literal " + expr->value +
                        " is smaller than a double can hold", "E_LITERAL_OUT_OF_RANGE");
            }
            Save(expr, {InvalidSymbolId, "double", false});
            return;
        }
        const std::string literalType = IntegerLiteralType(expr->value, literalSign < 0);
        if (IsInteger(expectedType)) {
            unsigned long long magnitude = 0;
            bool parsed = true;
            try { magnitude = std::stoull(expr->value); }
            catch (const std::exception&) { parsed = false; }
            if (!parsed || !IntegerLiteralFits(expectedType, magnitude, literalSign < 0))
                Report("integer literal " + std::string(literalSign < 0 ? "-" : "") +
                    expr->value + " does not fit in '" + expectedType + "'",
                    "E_LITERAL_OUT_OF_RANGE");
        }
        Save(expr, {InvalidSymbolId, literalType, false});
    }
    void Analyzer::Visit(StringLiteralExpr* expr) { Save(expr, {InvalidSymbolId, "string", false}); }
    void Analyzer::Visit(CharLiteralExpr* expr) { Save(expr, {InvalidSymbolId, "char", false}); }

    void Analyzer::Visit(ArrayExpr* expr) {
        for (const auto& size : expr->sizes) {
            if (!size) continue;
            const Result resolved = Evaluate(size.get());
            if (!IsInteger(resolved.type)) Report("array size must be an integer",
                "E_ARRAY_SIZE_TYPE");
        }
        const bool hasExpectedArrayType = expectedType.ends_with("[]");
        const size_t expectedRank = hasExpectedArrayType ? ArrayRank(expectedType) : 0;
        const std::string expectedElementType = hasExpectedArrayType
            ? ArrayElementType(expectedType) : std::string{};
        const auto literalShape = InferArrayShape(*expr);
        if (!literalShape) Report("array literal must be rectangular",
            "E_ARRAY_LITERAL_NOT_RECTANGULAR");
        const bool groupedFlatLiteral = expectedRank == 1 && literalShape &&
            literalShape->size() > 1;
        std::string elementType;
        const auto evaluateElement = [&](Expression* value) {
            const Result resolved = hasExpectedArrayType
                ? EvaluateExpected(value, expectedElementType)
                : Evaluate(value);
            if (hasExpectedArrayType && !IsAssignable(expectedElementType, resolved.type))
                Report("array element has type '" + resolved.type + "', expected '" + expectedElementType + "'",
                    "E_ARRAY_ELEMENT_TYPE");
            const std::string common = elementType.empty()
                ? resolved.type : CommonType(elementType, resolved.type);
            if (!elementType.empty() && common == "error" &&
                elementType != "error" && resolved.type != "error")
                Report("array literal mixes incompatible element types '" + elementType +
                    "' and '" + resolved.type + "'", "E_ARRAY_LITERAL_MIXED_TYPES");
            elementType = common;
        };
        if (groupedFlatLiteral) {
            const auto flatten = [&](const auto& self, ArrayExpr& array) -> void {
                for (const auto& value : array.values) {
                    if (auto* nested = dynamic_cast<ArrayExpr*>(value.get())) self(self, *nested);
                    else evaluateElement(value.get());
                }
            };
            flatten(flatten, *expr);
        }
        else {
            for (const auto& value : expr->values) evaluateElement(value.get());
        }
        // A literal is an owner. It is the only array expression whose storage
        // is made on the spot, and saying so here is what lets it be moved,
        // returned, or put in a field -- which the backend was already
        // prepared for and the analyzer refused, the one place the two halves
        // did not say the same thing about what a literal is.
        //
        // Where the storage belongs to the declaration instead -- a sized
        // declarator, a global -- the declaration says so and does not take
        // this answer; see Analyzer::Visit(VarDeclExpr*).
        Result literal{InvalidSymbolId,
            hasExpectedArrayType ? expectedType
                : (elementType.empty() ? std::string{"dynamic"} : elementType) + "[]",
            false};
        literal.createsArrayOwner = true;
        Save(expr, std::move(literal));
    }

}
