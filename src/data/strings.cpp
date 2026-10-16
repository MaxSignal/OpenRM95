#include "strings.h"
#include "../util/fs.h"
#include "../util/reader.h"

namespace rm95 {

void StringTable::load(const std::string& gameDir) {
    std::string p = fs::find(gameDir, "strings.dat");
    std::vector<uint8_t> d;
    bool have = !p.empty() && fs::readFile(p, d);
    Reader r(d);
    std::string buf;
    for (int i = 0; i < 150; i++) {
        if (!have) { s_[i] = kDefaultStrings[i]; continue; }
        if (!r.eof()) buf = r.fgets(256);       // fgets failing at EOF keeps the previous buffer
        if (!buf.empty()) buf.pop_back();
        s_[i] = buf;
    }
}

const std::string& StringTable::operator[](int i) const {
    static const std::string empty;
    if (i < 0 || i >= 150) return empty;
    return s_[i];
}

}  // namespace rm95
