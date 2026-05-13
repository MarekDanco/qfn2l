#pragma once
#include <cstdio>
#include <string>

// Global verbosity level — set once at startup from --verbose/-v.
extern int g_verbosity;

// Print [TAG] msg ... to stdout if lev <= g_verbosity.
// Usage: LOG("abs", 3, "message %d", value);
#define LOG(tag, lev, ...)                                                             \
    do {                                                                               \
        if ((lev) <= g_verbosity) {                                                    \
            std::printf("[%s] ", (tag));                                               \
            std::printf(__VA_ARGS__);                                                  \
            std::printf("\n");                                                         \
            std::fflush(stdout);                                                       \
        }                                                                              \
    } while (0)

// Convenience wrapper that captures the tag as a compile-time string.
// Define LOG_TAG in each .cpp file, then call TLOG(lev, ...).
#define TLOG(lev, ...) LOG(LOG_TAG, lev, __VA_ARGS__)
