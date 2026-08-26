/*
 * The shortest decimal text that reads back as the same value.
 *
 * One implementation, compiled into both runtimes: the host's C++ runtime
 * includes it from real_text.cpp and the wasm shim includes it from
 * absolute_wasm_std.c. That is the point of it. Before this existed each
 * target answered for itself -- the host borrowed the C library's "%g" and
 * printed six significant digits of a value, and the shim's freestanding
 * formatter had no float directive at all and printed the text "%g" -- so a
 * program's output depended on which backend built it. Matching two separate
 * formatters digit for digit is harder than having one.
 *
 * The algorithm is Steele & White's Dragon4 in the Burger & Dybvig
 * formulation: exact integer arithmetic over the value's rounding interval,
 * emitting digits until what has been emitted can only read back as this
 * value and no other. So the text is both the shortest that round-trips and
 * correctly rounded, without a table of powers and without touching the
 * host's floating point.
 *
 * Written in the common subset of C11 and C++20, with every function static,
 * because it is included as a source file by exactly one translation unit per
 * build. Freestanding: no libc, no libm.
 */
#ifndef ABSOLUTE_REAL_TEXT_H
#define ABSOLUTE_REAL_TEXT_H

#include <stdint.h>

/* Enough for the longest result: "0." plus six zeros plus seventeen digits
 * plus a sign is 26 characters, and the exponent form is shorter. Callers
 * hand in at least this much; generated code allocates exactly this. */
#define ABSOLUTE_REAL_TEXT_CAPACITY 32

/* 2560 bits. The widest intermediate is the smallest subnormal scaled up for
 * digit generation, which needs about 1140. */
#define ABSOLUTE_BIG_LIMBS 80

typedef struct {
    uint32_t limb[ABSOLUTE_BIG_LIMBS];
    int length;
} AbsoluteBig;

static void AbsoluteBigSet(AbsoluteBig* value, uint64_t initial) {
    value->length = 0;
    while (initial != 0 && value->length < ABSOLUTE_BIG_LIMBS) {
        value->limb[value->length++] = (uint32_t)(initial & 0xFFFFFFFFu);
        initial >>= 32;
    }
}

static void AbsoluteBigCopy(AbsoluteBig* target, const AbsoluteBig* source) {
    int index;
    target->length = source->length;
    for (index = 0; index < source->length; ++index)
        target->limb[index] = source->limb[index];
}

static void AbsoluteBigTrim(AbsoluteBig* value) {
    while (value->length > 0 && value->limb[value->length - 1] == 0)
        --value->length;
}

static int AbsoluteBitLength(uint64_t value) {
    int bits = 0;
    while (value != 0) {
        ++bits;
        value >>= 1;
    }
    return bits;
}

static void AbsoluteBigShiftLeft(AbsoluteBig* value, int bits) {
    int limbShift = bits / 32;
    int bitShift = bits % 32;
    int index;
    if (value->length == 0 || bits <= 0) return;
    if (bitShift != 0) {
        uint32_t carry = 0;
        for (index = 0; index < value->length; ++index) {
            uint64_t wide = ((uint64_t)value->limb[index] << bitShift) | carry;
            value->limb[index] = (uint32_t)(wide & 0xFFFFFFFFu);
            carry = (uint32_t)(wide >> 32);
        }
        if (carry != 0 && value->length < ABSOLUTE_BIG_LIMBS)
            value->limb[value->length++] = carry;
    }
    if (limbShift != 0) {
        int moved = value->length + limbShift;
        if (moved > ABSOLUTE_BIG_LIMBS) moved = ABSOLUTE_BIG_LIMBS;
        for (index = moved - 1; index >= limbShift; --index)
            value->limb[index] = value->limb[index - limbShift];
        for (index = 0; index < limbShift && index < ABSOLUTE_BIG_LIMBS; ++index)
            value->limb[index] = 0;
        value->length = moved;
    }
    AbsoluteBigTrim(value);
}

static void AbsoluteBigMultiplySmall(AbsoluteBig* value, uint32_t factor) {
    uint32_t carry = 0;
    int index;
    for (index = 0; index < value->length; ++index) {
        uint64_t wide = (uint64_t)value->limb[index] * factor + carry;
        value->limb[index] = (uint32_t)(wide & 0xFFFFFFFFu);
        carry = (uint32_t)(wide >> 32);
    }
    if (carry != 0 && value->length < ABSOLUTE_BIG_LIMBS)
        value->limb[value->length++] = carry;
}

/* Multiplying by a power of ten a chunk at a time: 10^9 is the largest that
 * fits a limb, so a scale of 10^323 costs thirty-six multiplies rather than
 * three hundred and twenty-three. */
static void AbsoluteBigMultiplyPowerOfTen(AbsoluteBig* value, int power) {
    static const uint32_t chunk[10] = {
        1u, 10u, 100u, 1000u, 10000u,
        100000u, 1000000u, 10000000u, 100000000u, 1000000000u
    };
    while (power >= 9) {
        AbsoluteBigMultiplySmall(value, chunk[9]);
        power -= 9;
    }
    if (power > 0) AbsoluteBigMultiplySmall(value, chunk[power]);
}

static int AbsoluteBigCompare(const AbsoluteBig* left, const AbsoluteBig* right) {
    int index;
    if (left->length != right->length)
        return left->length < right->length ? -1 : 1;
    for (index = left->length - 1; index >= 0; --index) {
        if (left->limb[index] != right->limb[index])
            return left->limb[index] < right->limb[index] ? -1 : 1;
    }
    return 0;
}

static void AbsoluteBigAdd(AbsoluteBig* sum, const AbsoluteBig* left,
                           const AbsoluteBig* right) {
    int longest = left->length > right->length ? left->length : right->length;
    uint32_t carry = 0;
    int index;
    for (index = 0; index < longest; ++index) {
        uint64_t wide = carry;
        if (index < left->length) wide += left->limb[index];
        if (index < right->length) wide += right->limb[index];
        sum->limb[index] = (uint32_t)(wide & 0xFFFFFFFFu);
        carry = (uint32_t)(wide >> 32);
    }
    sum->length = longest;
    if (carry != 0 && sum->length < ABSOLUTE_BIG_LIMBS)
        sum->limb[sum->length++] = carry;
    AbsoluteBigTrim(sum);
}

static void AbsoluteBigAddSmall(AbsoluteBig* value, uint32_t addend) {
    int index = 0;
    uint64_t carry = addend;
    while (carry != 0 && index < ABSOLUTE_BIG_LIMBS) {
        uint64_t wide = carry;
        if (index < value->length) wide += value->limb[index];
        if (index >= value->length) value->length = index + 1;
        value->limb[index] = (uint32_t)(wide & 0xFFFFFFFFu);
        carry = wide >> 32;
        ++index;
    }
    AbsoluteBigTrim(value);
}

/* left -= right, where left >= right. */
static void AbsoluteBigSubtract(AbsoluteBig* left, const AbsoluteBig* right) {
    int64_t borrow = 0;
    int index;
    for (index = 0; index < left->length; ++index) {
        int64_t wide = (int64_t)left->limb[index] - borrow;
        if (index < right->length) wide -= (int64_t)right->limb[index];
        if (wide < 0) {
            wide += ((int64_t)1 << 32);
            borrow = 1;
        } else {
            borrow = 0;
        }
        left->limb[index] = (uint32_t)wide;
    }
    AbsoluteBigTrim(left);
}

/* floor(numerator / denominator) for a quotient the scaling guarantees is a
 * single decimal digit. Nine subtractions at worst. */
static int AbsoluteBigDivideDigit(AbsoluteBig* numerator,
                                  const AbsoluteBig* denominator) {
    int digit = 0;
    while (digit < 9 && AbsoluteBigCompare(numerator, denominator) >= 0) {
        AbsoluteBigSubtract(numerator, denominator);
        ++digit;
    }
    return digit;
}

/* floor(exponent * log10(2)). 1233/4096 is the usual approximation; it can be
 * one out either way, and the scaling loops below correct that. */
static int AbsoluteEstimatePowerOfTen(int exponent) {
    int64_t scaled = (int64_t)exponent * 1233;
    int64_t quotient = scaled / 4096;
    if (scaled < 0 && quotient * 4096 != scaled) quotient -= 1;
    return (int)quotient;
}

/*
 * Digits of the shortest decimal that reads back as this value, written into
 * `digits` without a point. Returns how many there are, and writes into
 * `pointPosition` where the decimal point belongs: the value is
 * 0.<digits> * 10^pointPosition.
 *
 * `significand` and `exponent` describe the value exactly as
 * significand * 2^exponent, and `atLowerBoundary` says the value sits at a
 * power of two, where the neighbour below is half a step away rather than a
 * whole one and the rounding interval is lopsided.
 */
static int AbsoluteShortestDigits(uint64_t significand, int exponent,
                                  int atLowerBoundary,
                                  char* digits, int* pointPosition) {
    AbsoluteBig numerator;
    AbsoluteBig denominator;
    AbsoluteBig above;
    AbsoluteBig below;
    AbsoluteBig scratch;
    /* Ties go to the even significand, so an even one owns both ends of its
     * interval and an odd one owns neither. */
    const int closed = (significand & 1u) == 0;
    int power;
    int count = 0;
    int index;

    if (exponent >= 0) {
        AbsoluteBigSet(&numerator, significand);
        AbsoluteBigShiftLeft(&numerator, exponent + (atLowerBoundary ? 2 : 1));
        AbsoluteBigSet(&denominator, atLowerBoundary ? 4u : 2u);
        AbsoluteBigSet(&above, 1);
        AbsoluteBigShiftLeft(&above, exponent + (atLowerBoundary ? 1 : 0));
        AbsoluteBigSet(&below, 1);
        AbsoluteBigShiftLeft(&below, exponent);
    } else {
        AbsoluteBigSet(&numerator, significand);
        AbsoluteBigShiftLeft(&numerator, atLowerBoundary ? 2 : 1);
        AbsoluteBigSet(&denominator, 1);
        AbsoluteBigShiftLeft(&denominator, (atLowerBoundary ? 2 : 1) - exponent);
        AbsoluteBigSet(&above, atLowerBoundary ? 2u : 1u);
        AbsoluteBigSet(&below, 1);
    }

    /* Jump most of the way with the estimate, then let the loops settle it.
     * The bit length is the significand's own, not the format's, so a
     * subnormal starts as close to the answer as a normal value does. */
    power = AbsoluteEstimatePowerOfTen(
        exponent + AbsoluteBitLength(significand) - 1) + 1;
    if (power > 0) {
        AbsoluteBigMultiplyPowerOfTen(&denominator, power);
    } else if (power < 0) {
        AbsoluteBigMultiplyPowerOfTen(&numerator, -power);
        AbsoluteBigMultiplyPowerOfTen(&above, -power);
        AbsoluteBigMultiplyPowerOfTen(&below, -power);
    }

    // Settle the estimate. The two corrections run one after the other and
    // never alternate, which matters: written as one loop that can step either
    // way, a value whose upper bound is exactly a power of ten oscillates for
    // ever. 1e23 is such a value -- its upper bound is exactly 2 * 10^23 -- and
    // it hung the formatter until the two were separated.
    //
    // Down first, while the scale is too large to put a digit before the point.
    for (;;) {
        AbsoluteBigAdd(&scratch, &numerator, &above);
        AbsoluteBigMultiplySmall(&scratch, 10u);
        if (AbsoluteBigCompare(&scratch, &denominator) > 0) break;
        AbsoluteBigMultiplySmall(&numerator, 10u);
        AbsoluteBigMultiplySmall(&above, 10u);
        AbsoluteBigMultiplySmall(&below, 10u);
        --power;
    }
    // Then up, while it is too small. Each step multiplies the denominator by
    // ten and so cannot be undone by the test that sent us here.
    for (;;) {
        AbsoluteBigAdd(&scratch, &numerator, &above);
        if (closed ? AbsoluteBigCompare(&scratch, &denominator) < 0
                   : AbsoluteBigCompare(&scratch, &denominator) <= 0) break;
        AbsoluteBigMultiplySmall(&denominator, 10u);
        ++power;
    }

    for (;;) {
        int digit;
        int low;
        int high;
        AbsoluteBigMultiplySmall(&numerator, 10u);
        AbsoluteBigMultiplySmall(&above, 10u);
        AbsoluteBigMultiplySmall(&below, 10u);
        digit = AbsoluteBigDivideDigit(&numerator, &denominator);

        low = closed ? AbsoluteBigCompare(&numerator, &below) <= 0
                     : AbsoluteBigCompare(&numerator, &below) < 0;
        AbsoluteBigAdd(&scratch, &numerator, &above);
        high = closed ? AbsoluteBigCompare(&scratch, &denominator) >= 0
                      : AbsoluteBigCompare(&scratch, &denominator) > 0;

        if (!low && !high && count < 20) {
            digits[count++] = (char)('0' + digit);
            continue;
        }
        if (low && high) {
            /* Both ends are in reach: take whichever the remainder is
             * nearer. A value exactly between the two representations of this
             * length -- 2^51 - 1/4, whose digits end ...47.75 -- goes to the
             * even one, which is the rule the arithmetic itself rounds by and
             * the one the C library prints these by. */
            int order;
            AbsoluteBigCopy(&scratch, &numerator);
            AbsoluteBigMultiplySmall(&scratch, 2u);
            order = AbsoluteBigCompare(&scratch, &denominator);
            if (order > 0 || (order == 0 && (digit & 1))) ++digit;
        } else if (high) {
            ++digit;
        }
        if (count < 24) digits[count++] = (char)('0' + digit);
        break;
    }

    /* That last digit is the only one that can have been rounded up, so a
     * carry out of it -- "999" becoming "1000" -- moves the point rather than
     * lengthening the string. */
    if (count > 0 && digits[count - 1] > '9') {
        index = count - 1;
        digits[index] = '0';
        --index;
        while (index >= 0 && digits[index] == '9') {
            digits[index] = '0';
            --index;
        }
        if (index < 0) {
            digits[0] = '1';
            count = 1;
            ++power;
        } else {
            ++digits[index];
        }
    }
    while (count > 1 && digits[count - 1] == '0') --count;

    *pointPosition = power;
    return count;
}

static int32_t AbsoluteWriteText(const char* text, char* out, int32_t capacity) {
    int32_t length = 0;
    int32_t index;
    while (text[length] != '\0') ++length;
    if (out == 0 || capacity <= length) {
        if (out != 0 && capacity > 0) out[0] = '\0';
        return 0;
    }
    for (index = 0; index < length; ++index) out[index] = text[index];
    out[length] = '\0';
    return length;
}

/*
 * Lay the digits out. The thresholds are ECMA-262's for Number::toString,
 * because JSON is one of the consumers and JavaScript is the reference reader
 * for it: a plain decimal while the point sits within the digits or close by,
 * an exponent once writing the zeros out would be absurd.
 */
static int32_t AbsoluteComposeReal(int negative, const char* digits,
                                   int digitCount, int pointPosition,
                                   char* out, int32_t capacity) {
    char buffer[ABSOLUTE_REAL_TEXT_CAPACITY * 2];
    int length = 0;
    int index;

    if (negative) buffer[length++] = '-';

    if (digitCount <= pointPosition && pointPosition <= 21) {
        for (index = 0; index < digitCount; ++index) buffer[length++] = digits[index];
        for (index = digitCount; index < pointPosition; ++index) buffer[length++] = '0';
    } else if (0 < pointPosition && pointPosition <= 21) {
        for (index = 0; index < pointPosition; ++index) buffer[length++] = digits[index];
        buffer[length++] = '.';
        for (index = pointPosition; index < digitCount; ++index) buffer[length++] = digits[index];
    } else if (-6 < pointPosition && pointPosition <= 0) {
        buffer[length++] = '0';
        buffer[length++] = '.';
        for (index = 0; index < -pointPosition; ++index) buffer[length++] = '0';
        for (index = 0; index < digitCount; ++index) buffer[length++] = digits[index];
    } else {
        int exponent = pointPosition - 1;
        int magnitude;
        char exponentDigits[8];
        int exponentCount = 0;
        buffer[length++] = digits[0];
        if (digitCount > 1) {
            buffer[length++] = '.';
            for (index = 1; index < digitCount; ++index) buffer[length++] = digits[index];
        }
        buffer[length++] = 'e';
        buffer[length++] = exponent < 0 ? '-' : '+';
        magnitude = exponent < 0 ? -exponent : exponent;
        if (magnitude == 0) exponentDigits[exponentCount++] = '0';
        while (magnitude > 0) {
            exponentDigits[exponentCount++] = (char)('0' + magnitude % 10);
            magnitude /= 10;
        }
        while (exponentCount > 0) buffer[length++] = exponentDigits[--exponentCount];
    }
    buffer[length] = '\0';
    return AbsoluteWriteText(buffer, out, capacity);
}

static int32_t AbsoluteRealText(uint64_t bits, int mantissaBits, int exponentWidth,
                                int exponentBias, char* out, int32_t capacity) {
    const uint64_t mantissaMask = ((uint64_t)1 << mantissaBits) - 1;
    const int maximumExponent = (1 << exponentWidth) - 1;
    const int biasedExponent =
        (int)((bits >> mantissaBits) & (uint64_t)maximumExponent);
    const int negative = (int)((bits >> (mantissaBits + exponentWidth)) & 1u);
    const uint64_t mantissa = bits & mantissaMask;
    uint64_t significand;
    int exponent;
    int atLowerBoundary;
    char digits[32];
    int digitCount;
    int pointPosition;

    if (biasedExponent == maximumExponent) {
        if (mantissa != 0) return AbsoluteWriteText("nan", out, capacity);
        return AbsoluteWriteText(negative ? "-inf" : "inf", out, capacity);
    }
    if (biasedExponent == 0 && mantissa == 0)
        return AbsoluteWriteText(negative ? "-0" : "0", out, capacity);

    if (biasedExponent == 0) {
        significand = mantissa;
        exponent = 1 - exponentBias - mantissaBits;
        atLowerBoundary = 0;
    } else {
        significand = mantissa | ((uint64_t)1 << mantissaBits);
        exponent = biasedExponent - exponentBias - mantissaBits;
        atLowerBoundary = (mantissa == 0 && biasedExponent > 1);
    }

    digitCount = AbsoluteShortestDigits(significand, exponent, atLowerBoundary,
                                        digits, &pointPosition);
    return AbsoluteComposeReal(negative, digits, digitCount, pointPosition,
                               out, capacity);
}

static int32_t AbsoluteDoubleTextImpl(double value, char* out, int32_t capacity) {
    union { double real; uint64_t bits; } view;
    view.real = value;
    return AbsoluteRealText(view.bits, 52, 11, 1023, out, capacity);
}

static int32_t AbsoluteFloatTextImpl(float value, char* out, int32_t capacity) {
    union { float real; uint32_t bits; } view;
    view.real = value;
    /* A float's shortest text is the shortest that reads back as that float,
     * not as the double it widens to: 1.1f is "1.1", never
     * "1.100000023841858". So it is decomposed at its own width. */
    return AbsoluteRealText((uint64_t)view.bits, 23, 8, 127, out, capacity);
}


/*
 * ...and the other direction: the nearest double to a decimal, which has to
 * be exact for the same reason. The wasm shim used to build its JSON numbers
 * by accumulating `number * 10 + digit` and then multiplying by ten as many
 * times as the exponent said, so "1e-7" on wasm was a different double than
 * "1e-7" on the host, and a document did not survive a round trip between
 * them.
 *
 * The value is compared against candidate doubles exactly -- cross-multiplied
 * into big integers, no division -- and the estimate is walked one ulp at a
 * time until it brackets. The estimate is within a dozen ulps, so the walk is
 * short.
 */

/* The most significant digits kept exactly. Beyond this the input is marked
 * as "and more", which decides a tie but cannot decide a value that sits
 * within 10^-400 of a rounding boundary. Nothing a program writes is that
 * long; a document crafted to be is the documented limit. */
#define ABSOLUTE_DECIMAL_DIGITS 400

static const double kAbsolutePowersOfTen[23] = {
    1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22
};

static int AbsoluteIsInfinite(double value) {
    union { double real; uint64_t bits; } view;
    view.real = value;
    return ((view.bits >> 52) & 0x7FFu) == 0x7FFu && (view.bits & 0xFFFFFFFFFFFFFull) == 0;
}

static double AbsoluteNextUp(double value) {
    union { double real; uint64_t bits; } view;
    view.real = value;
    ++view.bits;
    return view.real;
}

static double AbsoluteNextDown(double value) {
    union { double real; uint64_t bits; } view;
    view.real = value;
    if (view.bits == 0) return 0.0;
    --view.bits;
    return view.real;
}

/* value == significand * 2^exponent, exactly, for a finite non-negative value. */
static void AbsoluteDecompose(double value, uint64_t* significand, int* exponent) {
    union { double real; uint64_t bits; } view;
    int biased;
    uint64_t mantissa;
    view.real = value;
    biased = (int)((view.bits >> 52) & 0x7FFu);
    mantissa = view.bits & 0xFFFFFFFFFFFFFull;
    if (biased == 0) {
        *significand = mantissa;
        *exponent = -1074;
    } else {
        *significand = mantissa | ((uint64_t)1 << 52);
        *exponent = biased - 1075;
    }
}

/* sign of (decimal * 10^exponent10) - (significand * 2^exponent2) */
static int AbsoluteCompareDecimalBinary(const AbsoluteBig* decimal, int exponent10,
                                        uint64_t significand, int exponent2) {
    AbsoluteBig left;
    AbsoluteBig right;
    AbsoluteBigCopy(&left, decimal);
    AbsoluteBigSet(&right, significand);
    if (exponent10 > 0) AbsoluteBigMultiplyPowerOfTen(&left, exponent10);
    else if (exponent10 < 0) AbsoluteBigMultiplyPowerOfTen(&right, -exponent10);
    if (exponent2 > 0) AbsoluteBigShiftLeft(&right, exponent2);
    else if (exponent2 < 0) AbsoluteBigShiftLeft(&left, -exponent2);
    return AbsoluteBigCompare(&left, &right);
}

/* `low` and `high` are adjacent doubles with low < value < high. */
static double AbsoluteRoundBetween(const AbsoluteBig* decimal, int exponent10,
                                   int truncated, double low, double high) {
    uint64_t significand;
    int exponent;
    int side;
    AbsoluteDecompose(low, &significand, &exponent);
    /* The midpoint is exact: (2s+1) * 2^(e-1), and 2s+1 still fits. */
    side = AbsoluteCompareDecimalBinary(decimal, exponent10,
                                        significand * 2u + 1u, exponent - 1);
    if (side > 0) return high;
    if (side < 0) return low;
    if (truncated) return high;          /* the digits we dropped put it above */
    return (significand & 1u) ? high : low;   /* an exact tie goes to even */
}

/* The nearest double to decimal * 10^exponent10, for a non-negative decimal.
 * `truncated` says digits were dropped off the end of `decimal`. */
static double AbsoluteDecimalToDouble(const AbsoluteBig* decimal, uint64_t head,
                                      int headDigits, int trailingDigits,
                                      int truncated, int exponent10) {
    double approx;
    int power;
    int step;

    if (decimal->length == 0) return 0.0;

    /* Wildly out of range before doing any work, so the walk below always
     * starts somewhere finite and near. */
    if (headDigits + trailingDigits + exponent10 > 310) {
        union { double real; uint64_t bits; } infinity;
        infinity.bits = 0x7FF0000000000000ull;
        return infinity.real;
    }
    if (headDigits + trailingDigits + exponent10 < -400) return 0.0;

    approx = (double)head;
    power = exponent10 + trailingDigits;
    while (power > 22) { approx *= kAbsolutePowersOfTen[22]; power -= 22; }
    while (power < -22) { approx /= kAbsolutePowersOfTen[22]; power += 22; }
    if (power > 0) approx *= kAbsolutePowersOfTen[power];
    else if (power < 0) approx /= kAbsolutePowersOfTen[-power];

    if (AbsoluteIsInfinite(approx)) {
        union { double real; uint64_t bits; } largest;
        largest.bits = 0x7FEFFFFFFFFFFFFFull;
        approx = largest.real;
    }

    for (step = 0; step < 400; ++step) {
        uint64_t significand;
        int exponent;
        int side;
        AbsoluteDecompose(approx, &significand, &exponent);
        side = AbsoluteCompareDecimalBinary(decimal, exponent10, significand, exponent);
        if (side == 0) {
            if (!truncated) return approx;
            side = 1;
        }
        if (side > 0) {
            double up = AbsoluteNextUp(approx);
            uint64_t upSignificand;
            int upExponent;
            if (AbsoluteIsInfinite(up))
                return AbsoluteRoundBetween(decimal, exponent10, truncated, approx, up);
            AbsoluteDecompose(up, &upSignificand, &upExponent);
            if (AbsoluteCompareDecimalBinary(decimal, exponent10,
                                             upSignificand, upExponent) <= 0)
                return AbsoluteRoundBetween(decimal, exponent10, truncated, approx, up);
            approx = up;
        } else {
            double down = AbsoluteNextDown(approx);
            uint64_t downSignificand;
            int downExponent;
            AbsoluteDecompose(down, &downSignificand, &downExponent);
            side = AbsoluteCompareDecimalBinary(decimal, exponent10,
                                                downSignificand, downExponent);
            if (side > 0)
                return AbsoluteRoundBetween(decimal, exponent10, truncated, down, approx);
            if (side == 0 && !truncated) return down;
            approx = down;
        }
    }
    return approx;
}

/*
 * Scan the number grammar JSON uses and produce the nearest double. Returns 1
 * and sets *end past the number on success, 0 if the text does not start with
 * one.
 */
static int AbsoluteParseDecimal(const char* text, const char** end, double* result) {
    AbsoluteBig decimal;
    const char* cursor = text;
    uint64_t head = 0;
    int headDigits = 0;
    int keptDigits = 0;
    int trailingKept = 0;
    int exponentAdjust = 0;
    int explicitExponent = 0;
    int truncated = 0;
    int negative = 0;
    int sawDigit = 0;
    double value;

    AbsoluteBigSet(&decimal, 0);
    if (*cursor == '-') { negative = 1; ++cursor; }

    while (*cursor >= '0' && *cursor <= '9') {
        int digit = *cursor - '0';
        sawDigit = 1;
        if (keptDigits == 0 && digit == 0) {
            /* a leading zero is not a digit of the value */
        } else if (keptDigits < ABSOLUTE_DECIMAL_DIGITS) {
            AbsoluteBigMultiplySmall(&decimal, 10u);
            AbsoluteBigAddSmall(&decimal, (uint32_t)digit);
            ++keptDigits;
            if (headDigits < 19) { head = head * 10u + (uint64_t)digit; ++headDigits; }
            else ++trailingKept;
        } else {
            ++exponentAdjust;
            if (digit != 0) truncated = 1;
        }
        ++cursor;
    }

    if (*cursor == '.') {
        ++cursor;
        if (*cursor < '0' || *cursor > '9') return 0;
        while (*cursor >= '0' && *cursor <= '9') {
            int digit = *cursor - '0';
            sawDigit = 1;
            if (keptDigits == 0 && digit == 0) {
                /* 0.000x -- these only move the point */
                --exponentAdjust;
            } else if (keptDigits < ABSOLUTE_DECIMAL_DIGITS) {
                AbsoluteBigMultiplySmall(&decimal, 10u);
                AbsoluteBigAddSmall(&decimal, (uint32_t)digit);
                ++keptDigits;
                --exponentAdjust;
                if (headDigits < 19) { head = head * 10u + (uint64_t)digit; ++headDigits; }
                else ++trailingKept;
            } else if (digit != 0) {
                truncated = 1;
            }
            ++cursor;
        }
    }
    if (!sawDigit) return 0;

    if (*cursor == 'e' || *cursor == 'E') {
        const char* mark = cursor;
        int exponentSign = 1;
        int magnitude = 0;
        ++cursor;
        if (*cursor == '+' || *cursor == '-') {
            if (*cursor == '-') exponentSign = -1;
            ++cursor;
        }
        if (*cursor < '0' || *cursor > '9') { cursor = mark; }
        else {
            while (*cursor >= '0' && *cursor <= '9') {
                if (magnitude < 100000) magnitude = magnitude * 10 + (*cursor - '0');
                ++cursor;
            }
            explicitExponent = exponentSign * magnitude;
        }
    }

    value = AbsoluteDecimalToDouble(&decimal, head, headDigits, trailingKept,
                                    truncated, explicitExponent + exponentAdjust);
    if (end) *end = cursor;
    if (result) *result = negative ? -value : value;
    return 1;
}

#endif /* ABSOLUTE_REAL_TEXT_H */
