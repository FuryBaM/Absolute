#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/socket.h>
#include <unistd.h>

#include "socket_reactor.h"
#endif

// A string the runtime handed out is reference counted behind its first
// byte, so it is released rather than freed; see
// Absolute-Runtime/src/string.cpp. TCP/UDP tests call this on Windows
// as well as on POSIX, so the name cannot live in the POSIX #if.
extern "C" void absolute_string_release(const char* text);
