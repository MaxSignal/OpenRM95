#pragma once
#include "surface.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace rm95 {

// DrawText format flags (Win32 values)
enum : unsigned {
    DT_LEFT_ = 0x00, DT_CENTER_ = 0x01, DT_RIGHT_ = 0x02, DT_VCENTER_ = 0x04, DT_BOTTOM_ = 0x08,
    DT_WORDBREAK_ = 0x10, DT_SINGLELINE_ = 0x20, DT_NOCLIP_ = 0x100
};

// Emulation of the single GDI font GAME.EXE uses: "ＭＳ Ｐゴシック", lfHeight -16, bold (weight 700),
// rendered without anti-aliasing. Glyphs come from a TrueType/OpenType font through FreeType
// (MS PGothic itself when available, otherwise a metric-compatible substitute such as IPAPGothic).
class Font {
public:
    struct Glyph {
        int w = 0, h = 0, left = 0, top = 0, advance = 0;
        std::vector<uint8_t> bits;  // w*h, 1 = set
    };
    bool load(const std::vector<std::string>& candidates);
    bool loaded() const { return face_ != nullptr; }
    const std::string& path() const { return path_; }

    int height() const { return height_; }          // tmHeight
    int ascent() const { return ascent_; }          // tmAscent
    int maxCharWidth() const { return maxWidth_; }  // tmMaxCharWidth
    int lineHeight() const { return height_ + 2; }  // GAME.EXE: tmHeight + 2

    const Glyph& glyph(char32_t c);
    int charWidth(char32_t c) { return glyph(c).advance; }
    int textWidth(const std::u32string& s);

    // DrawText(hdc, text(Shift_JIS), -1, rect, fmt) with SetBkMode(TRANSPARENT) and text colour index `color`.
    void drawText(Surface& dst, Rect rect, const std::string& sjis, unsigned fmt, uint8_t color, Rect clip);
    void drawText(Surface& dst, Rect rect, const std::string& sjis, unsigned fmt, uint8_t color) {
        drawText(dst, rect, sjis, fmt, color, Rect(0, 0, dst.width, dst.height));
    }

    // overrides for metrics (MS PGothic 16px bold: 16 / 14 / 17)
    int forceHeight = 16, forceAscent = 14, forceMaxWidth = 17;
    bool bold = true;

private:
    void drawLine(Surface& dst, int x, int baseline, const std::u32string& s, uint8_t color, Rect clip);
    void* lib_ = nullptr;
    void* face_ = nullptr;
    std::string path_;
    int height_ = 16, ascent_ = 14, maxWidth_ = 17;
    std::unordered_map<char32_t, Glyph> cache_;
};

Font& uiFont();

}  // namespace rm95
