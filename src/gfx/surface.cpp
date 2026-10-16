#include "surface.h"
#include <algorithm>
#include <unordered_map>

namespace rm95 {

void Surface::fillRect(Rect r, uint8_t c) {
    r.left = std::max(r.left, 0); r.top = std::max(r.top, 0);
    r.right = std::min(r.right, width); r.bottom = std::min(r.bottom, height);
    for (int y = r.top; y < r.bottom; y++) std::fill(row(y) + r.left, row(y) + r.right, c);
}

void Surface::blit(int dx, int dy, const Surface& src, Rect sr, bool useKey) {
    if (sr.left < 0) { dx -= sr.left; sr.left = 0; }
    if (sr.top < 0) { dy -= sr.top; sr.top = 0; }
    sr.right = std::min(sr.right, src.width); sr.bottom = std::min(sr.bottom, src.height);
    if (dx < 0) { sr.left -= dx; dx = 0; }
    if (dy < 0) { sr.top -= dy; dy = 0; }
    int w = std::min(sr.width(), width - dx), h = std::min(sr.height(), height - dy);
    if (w <= 0 || h <= 0) return;
    bool key = useKey && src.colorKey >= 0;
    uint8_t k = (uint8_t)src.colorKey;
    for (int y = 0; y < h; y++) {
        const uint8_t* s = src.row(sr.top + y) + sr.left;
        uint8_t* d = row(dy + y) + dx;
        if (!key) std::copy(s, s + w, d);
        else for (int x = 0; x < w; x++) if (s[x] != k) d[x] = s[x];
    }
}

void Surface::stretchBlit(Rect dr, const Surface& src, Rect sr, bool useKey) {
    if (dr.empty() || sr.empty()) return;
    if (dr.width() == sr.width() && dr.height() == sr.height()) { blit(dr.left, dr.top, src, sr, useKey); return; }
    bool key = useKey && src.colorKey >= 0;
    uint8_t k = (uint8_t)src.colorKey;
    int dw = dr.width(), dh = dr.height(), sw = sr.width(), shh = sr.height();
    for (int y = std::max(0, dr.top); y < std::min(height, dr.bottom); y++) {
        int sy = sr.top + (int)((long long)(y - dr.top) * shh / dh);
        if (sy < 0 || sy >= src.height) continue;
        const uint8_t* s = src.row(sy);
        uint8_t* d = row(y);
        for (int x = std::max(0, dr.left); x < std::min(width, dr.right); x++) {
            int sx = sr.left + (int)((long long)(x - dr.left) * sw / dw);
            if (sx < 0 || sx >= src.width) continue;
            uint8_t v = s[sx];
            if (!key || v != k) d[x] = v;
        }
    }
}

uint8_t nearestIndex(const Palette& pal, Rgb c, int first, int last) {
    int best = first; long bd = 1L << 30;
    for (int i = first; i <= last; i++) {
        long dr = (long)pal[i].r - c.r, dg = (long)pal[i].g - c.g, db = (long)pal[i].b - c.b;
        long d = dr * dr + dg * dg + db * db;
        if (d < bd) { bd = d; best = i; if (!d) break; }
    }
    return (uint8_t)best;
}

Surface imageToSurface(const Image& img, const Palette& pal) {
    Surface s(img.width, img.height);
    if (img.indexed) {
        uint8_t map[256];
        for (int i = 0; i < 256; i++) map[i] = nearestIndex(pal, img.palette[i]);
        for (size_t i = 0; i < s.px.size(); i++) s.px[i] = map[img.pixels[i]];
    } else {
        std::unordered_map<uint32_t, uint8_t> cache;
        for (size_t i = 0; i < s.px.size(); i++) {
            const Rgb& c = img.rgb[i];
            uint32_t key = c.r << 16 | c.g << 8 | c.b;
            auto it = cache.find(key);
            if (it == cache.end()) it = cache.emplace(key, nearestIndex(pal, c)).first;
            s.px[i] = it->second;
        }
    }
    return s;
}

const Palette& systemPalette() {
    static Palette p = [] {
        Palette q{};
        const uint32_t lo[10] = {0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080, 0x008080, 0xC0C0C0, 0xC0DCC0, 0xA6CAF0};
        const uint32_t hi[10] = {0xFFFBF0, 0xA0A0A4, 0x808080, 0xFF0000, 0x00FF00, 0xFFFF00, 0x0000FF, 0xFF00FF, 0x00FFFF, 0xFFFFFF};
        for (int i = 0; i < 10; i++) {
            q[i] = Rgb{(uint8_t)(lo[i] >> 16), (uint8_t)(lo[i] >> 8), (uint8_t)lo[i]};
            q[246 + i] = Rgb{(uint8_t)(hi[i] >> 16), (uint8_t)(hi[i] >> 8), (uint8_t)hi[i]};
        }
        return q;
    }();
    return p;
}

}  // namespace rm95
