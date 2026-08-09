#pragma once

// Android's bionic libc exposes sched_{get,set}affinity(), but does not expose
// glibc's non-standard pthread_{get,set}affinity_np() helpers.  Absolute's task
// runtime only needs affinity for the calling worker thread, for which pid 0 is
// the correct sched_* target on Linux/Android.
#if defined(__ANDROID__)
#include <cerrno>
#include <cstddef>
#include <pthread.h>
#include <sched.h>

inline int absolute_android_pthread_getaffinity_np(
    pthread_t, std::size_t cpuSetSize, cpu_set_t* cpuSet) {
    const int result = sched_getaffinity(0, cpuSetSize, cpuSet);
    return result == 0 ? 0 : errno;
}

inline int absolute_android_pthread_setaffinity_np(
    pthread_t, std::size_t cpuSetSize, const cpu_set_t* cpuSet) {
    const int result = sched_setaffinity(0, cpuSetSize, cpuSet);
    return result == 0 ? 0 : errno;
}

#define pthread_getaffinity_np absolute_android_pthread_getaffinity_np
#define pthread_setaffinity_np absolute_android_pthread_setaffinity_np
#endif
