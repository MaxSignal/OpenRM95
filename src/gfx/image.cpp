#include "image.h"
#include "../util/fs.h"
#include <cstring>

namespace rm95 {

static uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }

bool decodeBmp(const uint8_t* d, size_t n, Image& out) {
    if (n < 26 || d[0] != 'B' || d[1] != 'M') return false;
    uint32_t off = rd32(d + 10);
    uint32_t hs = rd32(d + 14);
    int w, h, bpp, comp = 0, clr = 0;
    const uint8_t* ct;
    int ctEntry;
    if (hs == 12) {
        w = rd16(d + 18); h = (int16_t)rd16(d + 20); bpp = rd16(d + 24); ctEntry = 3;
    } else {
        if (n < 54) return false;
        w = (int32_t)rd32(d + 18); h = (int32_t)rd32(d + 22); bpp = rd16(d + 28); comp = (int)rd32(d + 30);
        clr = (int)rd32(d + 46); ctEntry = 4;
    }
    ct = d + 14 + hs;
    bool topDown = h < 0;
    if (h < 0) h = -h;
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return false;
    out.width = w; out.height = h;
    if (bpp <= 8) {
        out.indexed = true;
        int nc = clr ? clr : (1 << bpp);
        if (nc > 256) nc = 256;
        out.colors = nc;
        out.palette = Palette{};
        for (int i = 0; i < nc; i++) {
            const uint8_t* e = ct + i * ctEntry;
            if (e + 3 > d + n) break;
            out.palette[i] = Rgb{e[2], e[1], e[0]};
        }
        out.pixels.assign((size_t)w * h, 0);
        size_t stride = (((size_t)w * bpp + 31) / 32) * 4;
        if (comp == 1 && bpp == 8) {  // RLE8
            const uint8_t* p = d + off; const uint8_t* e = d + n;
            int x = 0, y = 0;
            while (p + 1 < e && y < h) {
                uint8_t c = p[0], v = p[1]; p += 2;
                if (c) { for (int k = 0; k < c && x < w; k++) out.pixels[(size_t)(topDown ? y : h - 1 - y) * w + x++] = v; }
                else if (v == 0) { x = 0; y++; }
                else if (v == 1) break;
                else if (v == 2) { if (p + 1 >= e) break; x += p[0]; y += p[1]; p += 2; }
                else { for (int k = 0; k < v && p < e; k++, p++) if (x < w) out.pixels[(size_t)(topDown ? y : h - 1 - y) * w + x++] = *p; if (v & 1) p++; }
            }
            return true;
        }
        for (int y = 0; y < h; y++) {
            const uint8_t* row = d + off + (size_t)(topDown ? y : h - 1 - y) * stride;
            if (row + stride > d + n) { if (row >= d + n) continue; }
            uint8_t* dst = &out.pixels[(size_t)y * w];
            for (int x = 0; x < w; x++) {
                size_t bit = (size_t)x * bpp;
                if (row + bit / 8 >= d + n) break;
                uint8_t b = row[bit / 8];
                uint8_t v;
                if (bpp == 8) v = b;
                else if (bpp == 4) v = (x & 1) ? (b & 0xF) : (b >> 4);
                else if (bpp == 2) v = (b >> (6 - 2 * (x & 3))) & 3;
                else v = (b >> (7 - (x & 7))) & 1;
                dst[x] = v;
            }
        }
        return true;
    }
    out.indexed = false;
    out.rgb.assign((size_t)w * h, Rgb{});
    int bypp = bpp / 8;
    size_t stride = (((size_t)w * bpp + 31) / 32) * 4;
    uint32_t rm = 0xff0000, gm = 0xff00, bm = 0xff;
    if (bpp == 16) { rm = 0x7c00; gm = 0x3e0; bm = 0x1f; }
    if (comp == 3 && hs >= 40 && n >= 14 + hs + 12) { rm = rd32(d + 54); gm = rd32(d + 58); bm = rd32(d + 62); }
    auto ext = [](uint32_t v, uint32_t m) -> uint8_t {
        if (!m) return 0;
        int s = 0; while (!(m & 1)) { m >>= 1; v >>= 1; s++; }
        uint32_t x = v & m; uint32_t mx = m;
        return (uint8_t)(x * 255 / mx);
    };
    for (int y = 0; y < h; y++) {
        const uint8_t* row = d + off + (size_t)(topDown ? y : h - 1 - y) * stride;
        for (int x = 0; x < w; x++) {
            const uint8_t* p = row + (size_t)x * bypp;
            if (p + bypp > d + n) break;
            if (bpp == 24) out.rgb[(size_t)y * w + x] = Rgb{p[2], p[1], p[0]};
            else if (bpp == 32) out.rgb[(size_t)y * w + x] = Rgb{p[2], p[1], p[0]};
            else { uint32_t v = rd16(p); out.rgb[(size_t)y * w + x] = Rgb{ext(v, rm), ext(v, gm), ext(v, bm)}; }
        }
    }
    return true;
}

bool loadBmp(const std::string& path, Image& out) {
    std::vector<uint8_t> d;
    if (path.empty() || !fs::readFile(path, d)) return false;
    return decodeBmp(d.data(), d.size(), out);
}

void decodeRawDib(int w, int h, const uint8_t* bits, const uint8_t* q, Image& out) {
    out.width = w; out.height = h; out.indexed = true; out.colors = 256;
    for (int i = 0; i < 256; i++) out.palette[i] = Rgb{q[i * 4 + 2], q[i * 4 + 1], q[i * 4 + 0]};
    out.pixels.assign((size_t)w * h, 0);
    size_t stride = (size_t)w;  // all MONSTPIC widths are multiples of 4 (DIB rows unpadded)
    for (int y = 0; y < h; y++) memcpy(&out.pixels[(size_t)y * w], bits + (size_t)(h - 1 - y) * stride, w);
}

}  // namespace rm95
