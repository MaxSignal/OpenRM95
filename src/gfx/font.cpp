#include "font.h"
#include "../util/sjis.h"
#include "../util/fs.h"
#include "../util/log.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <algorithm>
#include <cstring>

namespace rm95 {

Font& uiFont() {
    static Font f;
    return f;
}

static bool facePreferred(FT_Face f) {
    std::string fam = f->family_name ? f->family_name : "";
    return fam.find("PGothic") != std::string::npos || fam.find("P Gothic") != std::string::npos ||
           fam.find("PGothic") != std::string::npos;
}

bool Font::load(const std::vector<std::string>& candidates) {
    FT_Library lib;
    if (!lib_) {
        if (FT_Init_FreeType(&lib)) return false;
        lib_ = lib;
    }
    lib = (FT_Library)lib_;
    for (const std::string& cand : candidates) {
        std::string path = cand;
        int index = -1;
        size_t colon = cand.rfind('#');
        if (colon != std::string::npos) { path = cand.substr(0, colon); index = atoi(cand.c_str() + colon + 1); }
        if (!fs::exists(path)) continue;
        FT_Face face;
        if (FT_New_Face(lib, path.c_str(), index < 0 ? 0 : index, &face)) continue;
        if (index < 0 && face->num_faces > 1) {
            for (int i = 0; i < face->num_faces; i++) {
                FT_Face f2;
                if (FT_New_Face(lib, path.c_str(), i, &f2)) continue;
                if (facePreferred(f2)) { FT_Done_Face(face); face = f2; break; }
                FT_Done_Face(f2);
            }
        }
        FT_Set_Pixel_Sizes(face, 0, 16);
        face_ = face;
        path_ = cand;
        height_ = forceHeight > 0 ? forceHeight : (int)((face->size->metrics.ascender - face->size->metrics.descender + 63) >> 6);
        ascent_ = forceAscent > 0 ? forceAscent : (int)((face->size->metrics.ascender + 63) >> 6);
        maxWidth_ = forceMaxWidth > 0 ? forceMaxWidth : (int)((face->size->metrics.max_advance + 63) >> 6) + (bold ? 1 : 0);
        LOG_INFO("font: %s (%s)", cand.c_str(), face->family_name ? face->family_name : "?");
        cache_.clear();
        return true;
    }
    return false;
}

const Font::Glyph& Font::glyph(char32_t c) {
    auto it = cache_.find(c);
    if (it != cache_.end()) return it->second;
    Glyph g;
    FT_Face face = (FT_Face)face_;
    if (face) {
        if (c == 0x3000) {  // ideographic space
            g.advance = 16;
        } else if (FT_Load_Char(face, c, FT_LOAD_RENDER | FT_LOAD_TARGET_MONO | FT_LOAD_MONOCHROME) == 0) {
            FT_GlyphSlot s = face->glyph;
            g.w = (int)s->bitmap.width;
            g.h = (int)s->bitmap.rows;
            g.left = s->bitmap_left;
            g.top = s->bitmap_top;
            g.advance = (int)((s->advance.x + 32) >> 6);
            g.bits.assign((size_t)g.w * g.h, 0);
            for (int y = 0; y < g.h; y++)
                for (int x = 0; x < g.w; x++) {
                    const uint8_t* row = s->bitmap.buffer + y * s->bitmap.pitch;
                    bool on = s->bitmap.pixel_mode == FT_PIXEL_MODE_MONO ? (row[x >> 3] >> (7 - (x & 7))) & 1 : row[x] >= 128;
                    g.bits[(size_t)y * g.w + x] = on;
                }
        }
    } else {
        g.advance = c < 0x100 || (c >= 0xff61 && c <= 0xff9f) ? 8 : 16;
    }
    if (bold) {
        // GDI synthesized bold: glyph overstruck 1 px to the right, advance + 1
        if (g.w > 0) {
            Glyph b = g;
            b.w = g.w + 1;
            b.bits.assign((size_t)b.w * b.h, 0);
            for (int y = 0; y < g.h; y++)
                for (int x = 0; x < g.w; x++)
                    if (g.bits[(size_t)y * g.w + x]) { b.bits[(size_t)y * b.w + x] = 1; b.bits[(size_t)y * b.w + x + 1] = 1; }
            g = b;
        }
        g.advance += 1;
    }
    return cache_.emplace(c, std::move(g)).first->second;
}

int Font::textWidth(const std::u32string& s) {
    int w = 0;
    for (char32_t c : s) w += glyph(c).advance;
    return w;
}

void Font::drawLine(Surface& dst, int x, int baseline, const std::u32string& s, uint8_t color, Rect clip) {
    for (char32_t c : s) {
        const Glyph& g = glyph(c);
        for (int y = 0; y < g.h; y++) {
            int py = baseline - g.top + y;
            if (py < clip.top || py >= clip.bottom || py < 0 || py >= dst.height) continue;
            for (int xx = 0; xx < g.w; xx++) {
                if (!g.bits[(size_t)y * g.w + xx]) continue;
                int px = x + g.left + xx;
                if (px < clip.left || px >= clip.right || px < 0 || px >= dst.width) continue;
                dst.px[(size_t)py * dst.width + px] = color;
            }
        }
        x += g.advance;
    }
}

void Font::drawText(Surface& dst, Rect rect, const std::string& sjis, unsigned fmt, uint8_t color, Rect clip) {
    std::u32string text = sjisToU32(sjis);
    std::vector<std::u32string> lines;
    if (fmt & DT_SINGLELINE_) {
        std::u32string l;
        for (char32_t c : text) if (c != '\r' && c != '\n') l.push_back(c);
        lines.push_back(l);
    } else {
        std::u32string cur;
        for (size_t i = 0; i < text.size(); i++) {
            char32_t c = text[i];
            if (c == '\r' || c == '\n') {
                lines.push_back(cur); cur.clear();
                if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') i++;
                continue;
            }
            cur.push_back(c);
        }
        lines.push_back(cur);
        if (fmt & DT_WORDBREAK_) {
            int maxw = rect.width();
            std::vector<std::u32string> wrapped;
            for (auto& l : lines) {
                std::u32string cur2;
                int w = 0;
                size_t i = 0;
                while (i < l.size()) {
                    char32_t c = l[i];
                    int cw = charWidth(c);
                    if (w + cw > maxw && !cur2.empty()) {
                        // break at the last space if the line contains one (ASCII word wrap) and the overflowing
                        // character belongs to a word; Japanese text breaks between characters.
                        size_t sp = cur2.find_last_of(U' ');
                        bool ascii = c < 0x80 && c != ' ';
                        if (ascii && sp != std::u32string::npos && sp > 0) {
                            std::u32string rest = cur2.substr(sp + 1);
                            cur2.resize(sp);
                            wrapped.push_back(cur2);
                            cur2 = rest;
                            w = textWidth(cur2);
                        } else {
                            wrapped.push_back(cur2);
                            cur2.clear();
                            w = 0;
                            while (i < l.size() && l[i] == ' ') i++;  // spaces at the break are dropped
                            continue;
                        }
                    }
                    cur2.push_back(c);
                    w += cw;
                    i++;
                }
                wrapped.push_back(cur2);
            }
            lines.swap(wrapped);
        }
    }
    int lh = height_;
    int y = rect.top;
    if (fmt & DT_SINGLELINE_) {
        if (fmt & DT_VCENTER_) y = rect.top + (rect.height() - lh) / 2;
        else if (fmt & DT_BOTTOM_) y = rect.bottom - lh;
    }
    Rect c = clip;
    if (!(fmt & DT_NOCLIP_)) {
        c.left = std::max(c.left, rect.left); c.top = std::max(c.top, rect.top);
        c.right = std::min(c.right, rect.right); c.bottom = std::min(c.bottom, rect.bottom);
    }
    for (auto& l : lines) {
        int w = textWidth(l);
        int x = rect.left;
        if (fmt & DT_CENTER_) x = rect.left + (rect.width() - w) / 2;
        else if (fmt & DT_RIGHT_) x = rect.right - w;
        drawLine(dst, x, y + ascent_, l, color, c);
        y += lh;
        if (y >= c.bottom && !(fmt & DT_NOCLIP_)) break;
    }
}

}  // namespace rm95
