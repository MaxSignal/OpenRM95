#pragma once
#include <cstdio>

namespace rm95 {
extern int g_logLevel;  // 0 errors, 1 warnings, 2 info, 3 debug
}

#define RM_LOG(level, ...) do { if (::rm95::g_logLevel >= (level)) { std::fprintf(stderr, __VA_ARGS__); std::fputc('\n', stderr); } } while (0)
#define LOG_ERROR(...) RM_LOG(0, "[error] " __VA_ARGS__)
#define LOG_WARN(...)  RM_LOG(1, "[warn] " __VA_ARGS__)
#define LOG_INFO(...)  RM_LOG(2, "[info] " __VA_ARGS__)
#define LOG_DEBUG(...) RM_LOG(3, "[debug] " __VA_ARGS__)
