#include "display.h"
#include "../platform/platform.h"
#include "../util/rng.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace rm95 {

Display& display() {
    static Display d;
    return d;
}

Display::Display() {
    back.create(W, H, 0);
    primary.create(W, H, 0);
    cache.create(W, H, 0);
    work = systemPalette();
    base = work;
    hw = work;
}

void Display::waitMs(int ms) {
    if (ms <= 0) return;
    uint32_t t0 = platform().ticks();
    platform().refresh();
    while ((int)(platform().ticks() - t0) < ms) platform().sleep(1);
}

void Display::vsync() {
    // WaitForVerticalBlank(DDWAITVB_BLOCKBEGIN): wait for the next 60 Hz retrace
    uint32_t t = platform().ticks();
    uint32_t frame = (uint32_t)(t * 60ull / 1000ull);
    uint32_t next = (uint32_t)(((uint64_t)frame + 1) * 1000ull / 60ull);
    platform().refresh();
    while (platform().ticks() < next) platform().sleep(1);
}

// ---------------------------------------------------------------------------------------------
// palette

void Display::setEntries(const Palette& p) {
    hw = p;
    platform().markDirty();
}

void Display::loadPaletteSource(const Image& img, bool commit) {
    const Palette& sys = systemPalette();
    for (int i = 0; i < 256; i++) {
        if (i < 10 || i >= 246) work[i] = sys[i];
        else work[i] = img.indexed ? img.palette[i] : Rgb{0, 0, 0};
    }
    setEntries(work);
    if (commit) base = work;
}

void Display::setPalette(const Palette* entries, bool commit) {
    setEntries(entries ? *entries : base);
    if (entries && commit && entries != &base) base = *entries;
}

static int roundHalfEven(double v) {
    double f = std::floor(v);
    double d = v - f;
    if (d > 0.5) return (int)f + 1;
    if (d < 0.5) return (int)f;
    return ((long long)f % 2 == 0) ? (int)f : (int)f + 1;
}

void Display::computeTinted(const uint8_t* rgbg, Palette& out, bool& changed) const {
    changed = false;
    out = work;
    int p[3] = {rgbg[0], rgbg[1], rgbg[2]};
    int grey = rgbg[3];
    for (int i = 10; i < 246; i++) {
        int c[3] = {work[i].r, work[i].g, work[i].b};
        int lum = roundHalfEven((c[0] * 1.2 + c[1] * 1.5 + c[2]) / 3.0);
        int v[3];
        for (int k = 0; k < 3; k++) {
            if (grey == 0) v[k] = p[k] < 100 ? p[k] * c[k] / 100 : c[k] + (255 - c[k]) * (p[k] - 100) / 100;
            else v[k] = std::min(255, lum * (p[k] * 255 / 200) / 255);
        }
        const Rgb& b = base[i];
        if (v[0] != b.r || v[1] != b.g || v[2] != b.b) changed = true;
        out[i] = Rgb{(uint8_t)v[0], (uint8_t)v[1], (uint8_t)v[2]};
    }
}

void Display::fade(const Palette& target, const Palette& from, int n, bool commit) {
    if (fullscreen) n *= 2;
    Palette cur = from;
    Palette tgt = target, frm = from;
    for (int k = 0; k <= n - 2; k++) {
        for (int i = 10; i < 246; i++) {
            cur[i].r = (uint8_t)(cur[i].r + (uint8_t)((tgt[i].r - frm[i].r) / n));
            cur[i].g = (uint8_t)(cur[i].g + (uint8_t)((tgt[i].g - frm[i].g) / n));
            cur[i].b = (uint8_t)(cur[i].b + (uint8_t)((tgt[i].b - frm[i].b) / n));
        }
        setEntries(cur);
        if (fullscreen) waitMs(30);
        else platform().refresh();
    }
    setEntries(tgt);
    if (commit) base = tgt;
    platform().refresh();
}

void Display::effect(int mode, const uint8_t* rgbg, bool commitTint) {
    const uint8_t* src = rgbg ? rgbg : tint;
    uint8_t col[4];
    memcpy(col, src, 4);
    Palette T;
    bool changed;
    computeTinted(col, T, changed);
    switch (mode) {
    case -1:
        base = T;
        break;
    case 0:
    case 1:
        if (rgbg && changed) {
            for (int k = 0; k <= mode; k++) {
                setEntries(T); waitMs(20);
                setEntries(base); waitMs(20);
            }
        }
        break;
    case 2:
    case 3: {
        int A = mode == 2 ? 8 : 16;
        for (int k = 0; k < 4; k++) {
            int dx = (k == 3) ? 0 : ((k & 1) ? -A : A);
            int dy = (k == 3) ? 0 : (rng().mod(2 * A) - A);
            Rect s(std::max(-dx, 0), std::max(-dy, 0), std::max(-dx, 0) + W - std::abs(dx), std::max(-dy, 0) + H - std::abs(dy));
            presentRect(s, std::max(dx, 0), std::max(dy, 0));
            waitMs(70);
        }
        break;
    }
    case 4:
        if (changed) { setEntries(T); base = T; }
        break;
    case 5:
    case 7:
        if (changed) fade(T, base, 5, true);
        break;
    case 6:
        if (changed) { fade(T, base, 3, false); setEntries(base); }
        break;
    default:
        break;
    }
    if ((mode == 4 || mode == 5 || mode == 7) && commitTint && rgbg && rgbg != tint) memcpy(tint, rgbg, 4);
    platform().refresh();
}

uint8_t Display::indexOf(uint32_t c) const {
    return nearestIndex(hw, Rgb{(uint8_t)(c & 0xff), (uint8_t)((c >> 8) & 0xff), (uint8_t)((c >> 16) & 0xff)});
}

// ---------------------------------------------------------------------------------------------
// presentation

void Display::present() {
    vsync();
    primary.blit(0, 0, back, Rect(0, 0, W, H), false);
    platform().markDirty();
    platform().refresh();
}

void Display::presentRect(Rect s, int dx, int dy) {
    if (s.right <= 0 || s.bottom <= 0 || s.left >= W || s.top >= H) return;
    Rect c(std::max(0, s.left), std::max(0, s.top), std::min(W, s.right), std::min(H, s.bottom));
    dx += c.left - s.left;
    dy += c.top - s.top;
    vsync();
    primary.blit(dx, dy, back, c, false);
    platform().markDirty();
    platform().refresh();
}

void Display::fillBack(uint32_t colorref) { back.fill(indexOf(colorref)); }

void Display::blitBlock(int x0, int y0, int x1, int y1) {
    x0 = std::max(0, x0); y0 = std::max(0, y0); x1 = std::min(W, x1); y1 = std::min(H, y1);
    if (x1 <= x0 || y1 <= y0) return;
    primary.blit(x0, y0, back, Rect(x0, y0, x1, y1), false);
    platform().markDirty();
}

// Spreads `total` undelayed block copies over roughly `ms` milliseconds (the original runs as fast
// as Blt allows; on period hardware such transitions took a few hundred ms).
struct Pacer {
    int total, ms, done = 0, lastChunk = 0;
    uint32_t t0;
    Pacer(int total_, int ms_) : total(total_ ? total_ : 1), ms(ms_), t0(platform().ticks()) {}
    void step() {
        done++;
        int chunk = done * 24 / total;
        if (chunk != lastChunk) {
            lastChunk = chunk;
            uint32_t target = t0 + (uint32_t)(ms * done / total);
            platform().refresh();
            while (platform().ticks() < target) platform().sleep(1);
        }
    }
};

static void shrinkStretch(const Surface& src, Rect sr, Surface& dst, int dw, int dh) {
    // nearest-neighbour stretch of src rect into dst (0,0,dw,dh)
    dst.stretchBlit(Rect(0, 0, dw, dh), src, sr, false);
}

void Display::show(int type, const Rect* rectp) {
    Rect rect = rectp ? *rectp : Rect(0, 0, W, H);
    int w = rect.width(), h = rect.height();
    switch (type) {
    default:
    case 0:
        vsync();
        blitBlock(rect.left, rect.top, rect.right, rect.bottom);
        platform().refresh();
        break;
    case 1:
    case 2:
    case 3: {
        int bs = (w == W) ? 8 : (fullscreen ? 2 : 4);
        int cols = w / bs, rows = h / bs, N = cols * rows;
        if (N <= 0) break;
        std::vector<int> a(N);
        for (int i = 0; i < N; i++) a[i] = (type == 3) ? N - 1 - i : i;
        for (int i = 0; i < N; i++) {
            int j;
            if (type == 1) j = (int)(rng().next() % (uint32_t)N);
            else { j = i + (int)(rng().next() % (uint32_t)(cols * 8)); if (j >= N) j = N - 1; }
            std::swap(a[i], a[j]);
        }
        Pacer pc(N, 400);
        for (int i = 0; i < N; i++) {
            int x = rect.left + (a[i] % cols) * bs, y = rect.top + (a[i] / cols) * bs;
            blitBlock(x, y, x + bs, y + bs);
            pc.step();
        }
        break;
    }
    case 4: {
        int perm[64];
        for (int i = 0; i < 64; i++) perm[i] = i;
        for (int i = 0; i < 64; i++) { int j = (int)(rng().next() & 63); std::swap(perm[i], perm[j]); }
        for (int k = 0; k < 64; k++) {
            for (int sy = 0; sy < 8; sy++)
                for (int sx = 0; sx < 11; sx++) {
                    int cx = sx * 8 + perm[k] % 8, cy = sy * 8 + perm[k] / 8;
                    if (cx < 80 && cy < 60) blitBlock(cx * 8, cy * 8, cx * 8 + 8, cy * 8 + 8);
                }
            waitMs(5);
        }
        break;
    }
    case 5:
        for (int k = 0; k < 16; k++) {
            for (int j = 0; j <= 30; j++) { int y = j * 16 + k; if (y + 1 > H) break; blitBlock(0, y, W, y + 1); }
            waitMs(60);
        }
        break;
    case 6:
        for (int k = 0; k < 16; k++) {
            for (int j = 0; j <= 40; j++) { int x = j * 16 + k; if (x + 1 > W) break; blitBlock(x, 0, x + 1, H); }
            waitMs(60);
        }
        break;
    case 7:
        for (int i = 0; i <= 58; i += 2) {
            blitBlock(0, 8 * i, W, std::min(8 * i + 8, H));
            blitBlock(0, H - 8 * i - 8, W, std::min(H - 8 * i, H));
            waitMs(30);
        }
        break;
    case 8:
    case 9:
        for (int i = 0; i <= 78; i += 2) { blitBlock(8 * i, 0, std::min(8 * i + 8, W), H); waitMs(10); }
        if (type == 8) for (int i = 1; i <= 79; i += 2) { blitBlock(8 * i, 0, std::min(8 * i + 8, W), H); waitMs(10); }
        else for (int i = 79; i >= 1; i -= 2) { blitBlock(8 * i, 0, std::min(8 * i + 8, W), H); waitMs(10); }
        break;
    case 10: {
        int x = 4, y = 4;
        blitBlock(x * 64, y * 48, x * 64 + 64, y * 48 + 48);
        waitMs(3);
        int dir = 1;
        for (int n = 2; n <= 21; n++) {
            for (int k = 0; k < n / 2; k++) {
                x += (dir == 1) - (dir == 3);
                y += (dir == 2) - (dir == 0);
                if (x >= 0 && y >= 0) {
                    blitBlock(x * 64, y * 48, std::min(x * 64 + 64, W), std::min(y * 48 + 48, H));
                    waitMs(3);
                }
            }
            dir = (dir + 1) & 3;
        }
        break;
    }
    case 11: {
        auto blk = [&](int x, int y) {
            for (int by = 0; by < 8; by++)
                for (int bx = 0; bx <= 10; bx++) {
                    int px = bx * 64 + x * 8, py = by * 64 + y * 8;
                    if (px < W && py < H) blitBlock(px, py, std::min(px + 8, W), std::min(py + 8, H));
                }
        };
        Pacer pc(64, 400);
        int x = 3, y = 3;
        blk(x, y);
        pc.step();
        int d = 0;
        for (int n = 2; n <= 17; n++) {
            d = (d + 1) & 3;
            for (int k = 0; k < n / 2; k++) {
                x += (d == 1) - (d == 3);
                y += (d == 2) - (d == 0);
                if (x >= 0 && y >= 0) { blk(x, y); pc.step(); }
            }
        }
        platform().refresh();
        break;
    }
    case 12:
        for (int i = 1; i <= 16; i++) {
            vsync();
            blitBlock(320 - 20 * i, 240 - 15 * i, 320 + 20 * i, 240 + 15 * i);
            waitMs(40);
        }
        break;
    case 13: {
        if (w < 4 || h < 4) { blitBlock(rect.left, rect.top, rect.right, rect.bottom); break; }
        Surface S1(w, h), S2(std::max(1, w / 2), std::max(1, h / 2));
        Rect src(rect.left + 1, rect.top + 1, rect.right - 1, rect.bottom - 1);
        int x, y;
        if (w / 2 > h / 2) { y = 8; x = (w / 2) * 8 / (h / 2); }
        else { x = 8; y = (h / 2) * 8 / (w / 2); }
        while (x <= w / 2 && y <= h / 2) {
            S2.fill(0);
            shrinkStretch(back, src, S2, x, y);
            S1.stretchBlit(Rect(0, 0, w, h), S2, Rect(0, 0, x, y), false);
            vsync();
            primary.blit(rect.left, rect.top, S1, Rect(0, 0, w, h), false);
            platform().markDirty();
            x = x * 3 / 2; y = y * 3 / 2;
            waitMs(2);
        }
        if (w == W && h == H) present();
        else blitBlock(rect.left, rect.top, rect.right, rect.bottom);
        break;
    }
    case 14: {
        Palette saved = base;
        effect(5, 0, 0, 0, 0, false);
        present();
        Palette black = base;
        fade(saved, black, 8, true);
        base = saved;
        break;
    }
    }
    platform().refresh();
}

void Display::hide(int type, bool blacken, const Rect* rect) {
    uint8_t black = indexOf(0);
    switch (type) {
    case 12: {
        Surface S(W, H, black);
        if (!blacken) S = back;
        for (int i = 0; i < 16; i++) {
            Rect rs[4] = {Rect(20 * i, 15 * i, 640 - 20 * i, 15 * i + 15), Rect(20 * i, 465 - 15 * i, 640 - 20 * i, 480 - 15 * i),
                          Rect(20 * i, 15 * i, 20 * i + 20, 480 - 15 * i), Rect(620 - 20 * i, 15 * i, 640 - 20 * i, 480 - 15 * i)};
            for (auto& r : rs) back.blit(r.left, r.top, S, r, false);
            present();
            waitMs(40);
        }
        break;
    }
    case 13: {
        Surface S1(W, H), S2(320, 240);
        Rect src(1, 1, 639, 479);
        int x = 320, y = 240;
        while (x >= 4 && y >= 4) {
            shrinkStretch(back, src, S2, x, y);
            S1.stretchBlit(Rect(0, 0, W, H), S2, Rect(0, 0, x, y), false);
            vsync();
            primary.blit(0, 0, S1, Rect(0, 0, W, H), false);
            platform().markDirty();
            platform().refresh();
            x = x * 2 / 3; y = y * 2 / 3;
        }
        if (blacken) { back.fill(black); show(0); }
        break;
    }
    case 14: {
        Palette blk = base;
        for (int i = 10; i < 246; i++) blk[i] = Rgb{0, 0, 0};
        fade(blk, base, 8, false);
        primary.fill(black);
        back.fill(black);
        present();
        setEntries(base);
        break;
    }
    default:
        if (blacken) back.fill(black);
        show(type, nullptr);
        break;
    }
    (void)rect;
    platform().refresh();
}

}  // namespace rm95
