#include "sjis.h"
#include <cstdint>
#include <unordered_map>

namespace rm95 {

char32_t sjisNext(const std::string& s, size_t& i) {
    unsigned char c = (unsigned char)s[i++];
    if (c < 0x80) return c;
    if (c >= 0xA1 && c <= 0xDF) return 0xFF61 + (c - 0xA1);
    if (isSjisLead(c) && i < s.size()) {
        unsigned char t = (unsigned char)s[i++];
        char32_t u = sjisPairToUnicode(c, t);
        return u ? u : U'・';
    }
    return U'?';
}

std::u32string sjisToU32(const std::string& s) {
    std::u32string r;
    size_t i = 0;
    while (i < s.size()) r.push_back(sjisNext(s, i));
    return r;
}

static void appendUtf8(std::string& o, char32_t c) {
    if (c < 0x80) o.push_back((char)c);
    else if (c < 0x800) { o.push_back((char)(0xC0 | (c >> 6))); o.push_back((char)(0x80 | (c & 0x3F))); }
    else if (c < 0x10000) {
        o.push_back((char)(0xE0 | (c >> 12))); o.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
        o.push_back((char)(0x80 | (c & 0x3F)));
    } else {
        o.push_back((char)(0xF0 | (c >> 18))); o.push_back((char)(0x80 | ((c >> 12) & 0x3F)));
        o.push_back((char)(0x80 | ((c >> 6) & 0x3F))); o.push_back((char)(0x80 | (c & 0x3F)));
    }
}

std::string u32ToUtf8(const std::u32string& s) {
    std::string o;
    for (char32_t c : s) appendUtf8(o, c);
    return o;
}

std::string sjisToUtf8(const std::string& s) { return u32ToUtf8(sjisToU32(s)); }

static const std::unordered_map<char32_t, uint16_t>& reverseTable() {
    static std::unordered_map<char32_t, uint16_t> m;
    if (m.empty()) {
        for (int li = 0; li < 60; li++) {
            int lb = li < 31 ? 0x81 + li : 0xE0 + li - 31;
            for (int t = 0x40; t < 0x100; t++) {
                char32_t u = sjisPairToUnicode((unsigned char)lb, (unsigned char)t);
                if (u && !m.count(u)) m[u] = (uint16_t)(lb << 8 | t);
            }
        }
    }
    return m;
}

std::string utf8ToSjis(const std::string& s) {
    std::string o;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        char32_t u;
        if (c < 0x80) { u = c; i++; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) { u = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F); i += 2; }
        else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
            u = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F); i += 3;
        } else if (i + 3 < s.size()) {
            u = ((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F); i += 4;
        } else { i++; continue; }
        if (u < 0x80) o.push_back((char)u);
        else if (u >= 0xFF61 && u <= 0xFF9F) o.push_back((char)(0xA1 + (u - 0xFF61)));
        else {
            auto it = reverseTable().find(u);
            if (it != reverseTable().end()) { o.push_back((char)(it->second >> 8)); o.push_back((char)(it->second & 0xFF)); }
            else o.push_back('?');
        }
    }
    return o;
}

int sjisLength(const std::string& s) {
    int n = 0;
    for (size_t i = 0; i < s.size(); i++) {
        if (isSjisLead((unsigned char)s[i]) && i + 1 < s.size()) i++;
        n++;
    }
    return n;
}

}  // namespace rm95
