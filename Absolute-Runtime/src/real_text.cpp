// The host half of the shared real-to-text routine. The algorithm is in
// real_text.h, which the wasm shim includes as well; this file only gives it
// external linkage under the names generated code and the standard library
// call. See docs/known-defects.md section 25.
#include "real_text.h"

extern "C" int32_t absolute_double_text(double value, char* out, int32_t capacity) {
    return AbsoluteDoubleTextImpl(value, out, capacity);
}

extern "C" int32_t absolute_float_text(float value, char* out, int32_t capacity) {
    return AbsoluteFloatTextImpl(value, out, capacity);
}
