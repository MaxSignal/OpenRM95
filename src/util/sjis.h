#pragma once
#include <string>

namespace rm95 {

// True if c is a Shift_JIS lead byte.
inline bool isSjisLead(unsigned char c) { return (c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC); }

char32_t sjisPairToUnicode(unsigned char lead, unsigned char trail);

// Decodes one character at s[i] and advances i. Half-width katakana (A1-DF) map to U+FF61..FF9F.
char32_t sjisNext(const std::string& s, size_t& i);

std::u32string sjisToU32(const std::string& s);
std::string sjisToUtf8(const std::string& s);
std::string u32ToUtf8(const std::u32string& s);
std::string utf8ToSjis(const std::string& s);

// Number of characters (MBCS aware), like _mbslen.
int sjisLength(const std::string& s);

}  // namespace rm95
