#pragma once
#include "../util/sjis.h"
#include <string>
#include <unordered_map>

namespace rm95 {

// Shift_JIS version of a UTF-8 string literal (converted once and cached).
inline const std::string& J(const char* utf8) {
    static std::unordered_map<const char*, std::string> cache;
    auto it = cache.find(utf8);
    if (it != cache.end()) return it->second;
    return cache.emplace(utf8, utf8ToSjis(utf8)).first->second;
}

}  // namespace rm95
