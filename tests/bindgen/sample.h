#ifndef ABSOLUTE_BINDGEN_SAMPLE_H
#define ABSOLUTE_BINDGEN_SAMPLE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t sample_add(int32_t left, int32_t right);
void sample_increment(int32_t* value);
const char* sample_name(void);

typedef void* SampleHandle;
SampleHandle sample_create(int32_t tag);
void sample_destroy(SampleHandle handle);
bool sample_ready(SampleHandle handle);

/* Unsupported in v1: skipped with a comment in the output. */
void sample_printf(const char* fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
