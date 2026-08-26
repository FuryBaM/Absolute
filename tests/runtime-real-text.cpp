// The proof for Absolute-Runtime/src/real_text.h, which is the one routine
// both targets turn a real number into text with and back again. It is
// checked against the C library rather than against itself, on properties
// rather than on a list of expected strings, because the values that break a
// converter are not the ones anyone writes down by hand: every one of the
// three defects this found in the first draft -- an infinite loop on 1e23, and
// an exact tie rounded the wrong way twice -- is outside any hand-written set.
// docs/known-defects.md section 22.
//
// Properties, on each value:
//   1. the text reads back as the value it came from
//   2. nothing shorter reads back as it
//   3. among the representations of its own length it is the nearest, with an
//      exact tie going to the even digit
// and in the other direction, that the decimal-to-double conversion agrees
// with strtod exactly.
#include "../Absolute-Runtime/src/real_text.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

namespace {

long long checked = 0;
long long failures = 0;

void fail(const char* what, double value, const char* text) {
    if (failures < 20)
        std::printf("FAIL %s: %.17g -> [%s]\n", what, value, text);
    ++failures;
}

// The digits of `text` that carry information: no sign, no point, no exponent,
// no leading or trailing zeros.
int significantDigits(const char* text, char* digits) {
    int count = 0;
    int seen = 0;
    for (const char* cursor = text; *cursor; ++cursor) {
        if (*cursor == 'e' || *cursor == 'E') break;
        if (*cursor < '0' || *cursor > '9') continue;
        if (*cursor == '0' && !seen) continue;
        seen = 1;
        digits[count++] = *cursor;
    }
    while (count > 1 && digits[count - 1] == '0') --count;
    digits[count] = '\0';
    return count;
}

// Can any decimal of `width` significant digits read back as `value`? The
// nearest one is the obvious candidate and not the only one: where a value
// sits just above a power of two its rounding interval is lopsided -- half a
// step below, a whole step above -- so the nearest can fall outside it while
// the next one along falls inside. Checking only the nearest calls a correct
// answer too long, on about one value in five thousand.
bool someWidthReaches(double value, int width) {
    char text[64];
    char mantissa[48];
    std::size_t count = 0;
    std::snprintf(text, sizeof(text), "%.*e", width - 1, value);
    const char* marker = std::strchr(text, 'e');
    if (!marker) return false;
    const int exponent = std::atoi(marker + 1);
    const bool negative = text[0] == '-';
    for (const char* cursor = text; cursor < marker; ++cursor)
        if (*cursor >= '0' && *cursor <= '9') mantissa[count++] = *cursor;
    mantissa[count] = '\0';
    const long long digits = std::atoll(mantissa);
    for (int delta = -1; delta <= 1; ++delta) {
        char candidate[80];
        std::snprintf(candidate, sizeof(candidate), "%s%llde%d",
                      negative ? "-" : "", digits + delta,
                      exponent - static_cast<int>(count - 1));
        if (std::strtod(candidate, nullptr) == value) return true;
    }
    return false;
}

void checkDouble(double value) {
    char text[ABSOLUTE_REAL_TEXT_CAPACITY];
    char mine[40];
    char theirs[40];
    char reference[64];
    ++checked;
    AbsoluteDoubleTextImpl(value, text, sizeof(text));

    if (std::strtod(text, nullptr) != value) { fail("round trip", value, text); return; }

    const int width = significantDigits(text, mine);
    if (width > 1 && someWidthReaches(value, width - 1)) {
        fail("not shortest", value, text);
        return;
    }
    std::snprintf(reference, sizeof(reference), "%.*e", width - 1, value);
    if (std::strtod(reference, nullptr) == value &&
        significantDigits(reference, theirs) == width &&
        std::strcmp(mine, theirs) != 0) {
        fail("not nearest", value, text);
    }
}

void checkFloat(float value) {
    char text[ABSOLUTE_REAL_TEXT_CAPACITY];
    char digits[40];
    char reference[64];
    ++checked;
    AbsoluteFloatTextImpl(value, text, sizeof(text));
    if (std::strtof(text, nullptr) != value) {
        fail("float round trip", static_cast<double>(value), text);
        return;
    }
    const int width = significantDigits(text, digits);
    if (width > 1) {
        std::snprintf(reference, sizeof(reference), "%.*e", width - 2,
                      static_cast<double>(value));
        if (std::strtof(reference, nullptr) == value)
            fail("float not shortest", static_cast<double>(value), text);
    }
}

void checkParse(const char* text) {
    double mine = 0.0;
    ++checked;
    if (!AbsoluteParseDecimal(text, nullptr, &mine)) {
        std::printf("FAIL refused: [%s]\n", text);
        ++failures;
        return;
    }
    const double theirs = std::strtod(text, nullptr);
    std::uint64_t left = 0;
    std::uint64_t right = 0;
    std::memcpy(&left, &mine, sizeof(left));
    std::memcpy(&right, &theirs, sizeof(right));
    if (left != right) {
        if (failures < 20)
            std::printf("FAIL parse [%s]: %.17g, strtod %.17g\n", text, mine, theirs);
        ++failures;
    }
}

}  // namespace

int main() {
    // Layout, which is ECMA-262's for Number::toString.
    struct { double value; const char* text; } layout[] = {
        {0.0, "0"}, {1.0, "1"}, {-1.0, "-1"}, {3.5, "3.5"}, {0.1, "0.1"},
        {1.0 / 3.0, "0.3333333333333333"}, {123456789.25, "123456789.25"},
        {1e20, "100000000000000000000"}, {1e21, "1e+21"},
        {1e-6, "0.000001"}, {1e-7, "1e-7"}, {5e-324, "5e-324"},
        {1.7976931348623157e308, "1.7976931348623157e+308"},
    };
    for (const auto& entry : layout) {
        char text[ABSOLUTE_REAL_TEXT_CAPACITY];
        ++checked;
        AbsoluteDoubleTextImpl(entry.value, text, sizeof(text));
        if (std::strcmp(text, entry.text) != 0) {
            std::printf("FAIL layout %.17g: [%s], expected [%s]\n",
                        entry.value, text, entry.text);
            ++failures;
        }
    }
    // -0 keeps its sign, and what is not a number says so.
    {
        char text[ABSOLUTE_REAL_TEXT_CAPACITY];
        AbsoluteDoubleTextImpl(-0.0, text, sizeof(text));
        if (std::strcmp(text, "-0") != 0) { std::printf("FAIL -0: [%s]\n", text); ++failures; }
        AbsoluteDoubleTextImpl(std::nan(""), text, sizeof(text));
        if (std::strcmp(text, "nan") != 0) { std::printf("FAIL nan: [%s]\n", text); ++failures; }
        AbsoluteDoubleTextImpl(-HUGE_VAL, text, sizeof(text));
        if (std::strcmp(text, "-inf") != 0) { std::printf("FAIL -inf: [%s]\n", text); ++failures; }
        checked += 3;
    }

    // Every power of ten and its neighbours. 1e23 is here on purpose: its upper
    // bound is exactly 2*10^23, which made the scaling step oscillate.
    for (int power = -320; power <= 308; ++power) {
        char literal[32];
        std::snprintf(literal, sizeof(literal), "1e%d", power);
        const double value = std::strtod(literal, nullptr);
        checkDouble(value);
        checkDouble(-value);
        checkDouble(std::nextafter(value, HUGE_VAL));
        checkDouble(std::nextafter(value, -HUGE_VAL));
    }

    // Every power of two and its neighbours: the lopsided-interval case.
    for (int power = -1074; power <= 1023; ++power) {
        const double value = std::ldexp(1.0, power);
        checkDouble(value);
        checkDouble(-value);
        checkDouble(std::nextafter(value, HUGE_VAL));
        checkDouble(std::nextafter(value, -HUGE_VAL));
    }

    // Values a person would notice being wrong, including the exact ties.
    for (int n = -20000; n <= 20000; ++n) {
        checkDouble(static_cast<double>(n));
        checkDouble(static_cast<double>(n) / 8.0);
        checkDouble(static_cast<double>(n) / 100.0);
        checkDouble(static_cast<double>(n) / 3.0);
    }

    std::mt19937_64 rng(20260826);
    for (long long index = 0; index < 200000; ++index) {
        const std::uint64_t bits = rng();
        double value;
        std::memcpy(&value, &bits, sizeof(value));
        if (std::isnan(value) || std::isinf(value)) continue;
        checkDouble(value);
    }
    std::uniform_real_distribution<double> ordinary(-1e6, 1e6);
    for (long long index = 0; index < 200000; ++index) checkDouble(ordinary(rng));

    // Floats, at their own width: 1.1f is 1.1, not 1.100000023841858.
    for (std::uint64_t bits = 0; bits <= 0xFFFFFFFFull; bits += 1021) {
        float value;
        const std::uint32_t narrow = static_cast<std::uint32_t>(bits);
        std::memcpy(&value, &narrow, sizeof(value));
        if (std::isnan(value) || std::isinf(value)) continue;
        checkFloat(value);
    }

    // The other direction, against strtod. The named ones are the cases this
    // conversion is classically got wrong on.
    const char* hard[] = {
        "0", "-0", "1e-7", "1e300", "1e-300", "1e308", "1e-324", "5e-324",
        "2.2250738585072011e-308", "2.2250738585072012e-308",
        "8.98846567431158e307", "1.7976931348623158e308",
        "7.8459735791271921e65", "3.5844466002796428e+298",
        "9.881312916824931e-324", "9007199254740993", "9007199254740995",
        "0.49999999999999994", "0.5000000000000001", "1e23", "1e22",
        "1.000000000000000055511151231257827021181583404541015625",
        "0.500000000000000027755575615628913510590791702270507812500",
    };
    for (const char* text : hard) checkParse(text);
    for (int power = -330; power <= 310; ++power) {
        for (int lead = 1; lead <= 9; ++lead) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%d.234567890123456789e%d", lead, power);
            checkParse(buffer);
            std::snprintf(buffer, sizeof(buffer), "%de%d", lead, power);
            checkParse(buffer);
        }
    }
    // What the formatter writes, read back: the round trip both directions
    // exist for.
    for (long long index = 0; index < 200000; ++index) {
        const std::uint64_t bits = rng();
        double value;
        std::memcpy(&value, &bits, sizeof(value));
        if (std::isnan(value) || std::isinf(value)) continue;
        char text[ABSOLUTE_REAL_TEXT_CAPACITY];
        AbsoluteDoubleTextImpl(value, text, sizeof(text));
        checkParse(text);
        double back = 0.0;
        AbsoluteParseDecimal(text, nullptr, &back);
        if (std::memcmp(&back, &value, sizeof(value)) != 0)
            fail("value -> text -> value", value, text);
    }

    std::printf("checked=%lld failures=%lld\n", checked, failures);
    if (failures != 0) return 1;
    std::printf("runtime-real-text=ok\n");
    return 0;
}
