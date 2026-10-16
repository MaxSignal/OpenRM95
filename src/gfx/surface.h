#pragma once
#include "image.h"
#include <cstdint>
#include <vector>

namespace rm95 {

struct Rect {
    int left = 0, top = 0, right = 0, bottom = 0;
    Rect() = default;
    Rect(int l, int t, int r, int b) : left(l), top(t), right(r), bottom(b) {}
    int width() const { return right - left; }
    int height() const { return bottom - top; }
    bool empty() const { return right <= left || bottom <= top; }
    static Rect xywh(int x, int y, int w, int h) { return Rect(x, y, x + w, y + h); }
};

// 8-bit palettised surface (DirectDraw surface emulation).
struct Surface {
    int width = 0, height = 0;
    std::vector<uint8_t> px;
    int colorKey = -1;  // source colour key (palette index) or -1

    Surface() = default;
    Surface(int w, int h, uint8_t fill = 0) { create(w, h, fill); }
    void create(int w, int h, uint8_t fill = 0) { width = w; height = h; px.assign((size_t)w * h, fill); }
    uint8_t* row(int y) { return &px[(size_t)y * width]; }
    const uint8_t* row(int y) const { return &px[(size_t)y * width]; }
    uint8_t at(int x, int y) const { return px[(size_t)y * width + x]; }
    void fill(uint8_t c) { std::fill(px.begin(), px.end(), c); }
    void fillRect(Rect r, uint8_t c);
    // BltFast-like copy (clipped). useKey: skip src pixels == src.colorKey
    void blit(int dx, int dy, const Surface& src, Rect sr, bool useKey);
    // Blt with stretching (nearest neighbour, DirectDraw HEL style)
    void stretchBlit(Rect dr, const Surface& src, Rect sr, bool useKey);
};

// Nearest palette index for an RGB colour (squared distance, ties -> lowest index), restricted to [first,last].
uint8_t nearestIndex(const Palette& pal, Rgb c, int first = 0, int last = 255);

// Converts an image into a surface by GDI-like colour matching against `pal` (the realised palette).
Surface imageToSurface(const Image& img, const Palette& pal);

// Windows' 20 static system colours (entries 0-9, 246-255).
const Palette& systemPalette();

}  // namespace rm95
