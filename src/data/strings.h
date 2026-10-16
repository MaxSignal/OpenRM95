#pragma once
#include <string>
#include <vector>

namespace rm95 {

extern const char* const kDefaultStrings[150];

// strings.dat: 150 lines (Shift_JIS). Loaded like FUN_0041c420: fgets(256) per line and the last
// character of each line cut; built-in defaults when the file is missing.
class StringTable {
public:
    void load(const std::string& gameDir);
    const std::string& operator[](int i) const;
    void set(int i, const std::string& s) { if (i >= 0 && i < 150) s_[i] = s; }
private:
    std::vector<std::string> s_ = std::vector<std::string>(150);
};

}  // namespace rm95
