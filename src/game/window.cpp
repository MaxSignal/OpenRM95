#include "window.h"
#include "game.h"
#include "../platform/platform.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace rm95 {

RpgWindow::RpgWindow(Game& g, int type_, int x_, int y_, int w_, int h_, bool transparent_, bool battle_,
                     const WindowLayout* layout, int count)
    : type(type_), x(x_), y(y_), w(w_), h(h_), transparent(transparent_), battle(battle_), g_(g) {
    itemCount = count;
    curCol = 0;
    curRow = 0;
    if (layout) {
        hasLayout = true;
        layout_ = *layout;
        rows = layout->rows;
        cols = layout->cols;
        mode = layout->mode;
        itemFmt = layout->fmt;
        xoff = layout->xoff;
        yoff = layout->yoff;
        if (yoff != 0) curRow = 0;
        colW = (mode == 3 ? 160 : w) - 6;
    } else {
        colW = w - 6;
    }
    if (count == -1) { curCol = -1; curRow = -1; }
    lineH = uiFont().lineHeight();
    overlay.x = x;
    overlay.y = y;
    overlay.surf.create(w, h, kIdxLtGray);
    // SetupWindow (FUN_00410c97)
    if (transparent) { textColor = kIdxWhite; captureBackground(); }
    createLabels();
    for (auto& l : labels) if (l) l->color = textColor;
    display().overlays.push_back(&overlay);
    repaint();
}

RpgWindow::~RpgWindow() {
    auto& o = display().overlays;
    o.erase(std::remove(o.begin(), o.end(), &overlay), o.end());
    platform().markDirty();
}

void RpgWindow::addLabel(int idx, int lx, int ly, int lw, int lh, unsigned fmt, size_t cap) {
    if (idx < 0 || idx >= 60) return;
    auto l = std::make_unique<Label>();
    l->r = Rect(lx, ly, lx + lw, ly + lh);
    l->fmt = fmt;
    l->cap = cap;
    labels[idx] = std::move(l);
}

void RpgWindow::createLabels() {
    int lh = lineH;
    auto generic = [&]() {
        int idx = 1;
        addLabel(0, 0, 0, colW, lh, itemFmt, 100);
        int pad = (itemFmt & 1) ? 0 : 10;
        if (type == 8 || type == 4 || type == 5 || type == 7) {
            for (int r = 0; r < rows; r++)
                for (int c = 0; c < cols; c++)
                    addLabel(idx++, colW * c + pad + xoff, lh * r + yoff + 1, colW / 2 + 20, lh, itemFmt, 20);
            for (int r = 0; r < rows; r++)
                for (int c = 0; c < cols; c++)
                    addLabel(idx++, colW * c + pad + colW / 2 + xoff, lh * r + yoff + 1, colW / 2 - 20, lh, itemFmt, 20);
        } else {
            for (int r = 0; r < rows; r++)
                for (int c = 0; c < cols; c++)
                    addLabel(idx++, colW * c + pad + xoff, lh * r + yoff + 1, colW, lh, itemFmt, 40);
            if (type == 6 || type == 0xb) {
                static const int xs[5] = {0, 30, 67, 75, 112};
                for (int r = 0; r < rows; r++)
                    for (int k = 0; k < 4; k++)
                        addLabel(idx++, xs[k] + xoff + 130, lh * r + yoff + 1, xs[k + 1] - xs[k], lh, 0x12, 20);
            }
        }
        if (layout_.hasText) {
            const std::string& t = layout_.text;
            size_t p = 0;
            for (int li = 1; li < idx; li++) {
                size_t e = p;
                while (e < t.size() && t[e] != '\n') e++;
                if (labels[li]) labels[li]->text = t.substr(p, e - p);
                if (e < t.size()) e++;  // skip '\n'
                p = e;
            }
        }
    };
    switch (type) {
    case 0:
        addLabel(1, 10, 10, w - 20, lh * 4 - 8, 0x10, 0x400);
        break;
    case 1:
        addLabel(10, 0, 20, 200, 100, 0x11, 100);
        addLabel(6, 200, 20, 45, 100, 0x12, 100);
        addLabel(16, 245, 20, 20, 100, 0x10, 100);
        addLabel(18, 248, 20, 45, 100, 0x12, 100);
        addLabel(7, 300, 20, 45, 100, 0x12, 100);
        addLabel(17, 345, 20, 20, 100, 0x10, 100);
        addLabel(19, 348, 20, 45, 100, 0x12, 100);
        addLabel(8, 450, 20, 100, 100, 0x11, 100);
        addLabel(9, 0, 20, 20, 100, 0x12, 100);
        addLabel(12, 0, 0, 200, 20, 0x11, 100);
        addLabel(13, 200, 0, 100, 20, 0x11, 100);
        addLabel(14, 300, 0, 100, 20, 0x11, 100);
        addLabel(15, 450, 0, 100, 20, 0x11, 100);
        break;
    case 2:
    case 6:
    case 0xb:
        generic();
        break;
    case 4:
    case 5:
    case 7:
    case 8:
        generic();
        hasButtons = true;
        prevEnabled = false;
        nextEnabled = true;
        addLabel(0x2d, 0x18, 0xb6, 0x14, 0x14, 0x12, 100);
        addLabel(0x2e, 0x28, 0xb6, 0x14, 0x14, 0x12, 100);
        addLabel(0x2f, 0x3c, 0xb6, 0x28, 0x14, 0x11, 100);
        break;
    case 9: {
        struct L { int i, x, y, w, h; unsigned f; };
        static const L ls[] = {
            {0x15, 0x1e, 10, 0x8c, 0x14, 0x34}, {0x16, 0xa0, 10, 0x5a, 0x14, 0x36}, {0x2c, 0x10e, 10, 0xa0, 0x14, 0x34},
            {0x0d, 0x28, 0x1e, 0x3c, 0x14, 0x36}, {0x06, 100, 0x1e, 0x28, 0x14, 0x36}, {0x10, 0x8c, 0x1e, 0x14, 0x14, 0x34},
            {0x12, 0x96, 0x1e, 0x28, 0x14, 0x36}, {0x0e, 0x28, 0x32, 0x3c, 0x14, 0x36}, {0x07, 100, 0x32, 0x28, 0x14, 0x36},
            {0x11, 0x8c, 0x32, 0x14, 0x14, 0x34}, {0x13, 0x96, 0x32, 0x28, 0x14, 0x36}, {0x0b, 0, 0x46, 100, 0x14, 0x36},
            {0x0c, 0x82, 0x46, 0x3c, 0x14, 0x36}, {0x30, 0, 0x5a, 100, 0x14, 0x36}, {0x31, 0x82, 0x5a, 0x3c, 0x14, 0x36},
            {0x32, 0, 0x6e, 100, 0x14, 0x36}, {0x33, 0x82, 0x6e, 0x3c, 0x14, 0x36}, {0x18, 0, 0x82, 100, 0x14, 0x36},
            {0x1b, 0x82, 0x82, 0x3c, 0x14, 0x36}, {0x19, 0, 0x96, 100, 0x14, 0x36}, {0x1c, 0x82, 0x96, 0x3c, 0x14, 0x36},
            {0x1a, 0, 0xaa, 100, 0x14, 0x36}, {0x1d, 100, 0xaa, 0x5a, 0x14, 0x36}, {0x1e, 200, 200, 0x6e, 0x14, 0x36},
            {0x1f, 0x136, 200, 0x5a, 0x14, 0x36}, {0x20, 200, 0x1e, 0x6e, 0x14, 0x36}, {0x2d, 200, 0x32, 0x6e, 0x14, 0x36},
            {0x21, 200, 0x46, 0x6e, 0x14, 0x36}, {0x22, 200, 0x5a, 0x6e, 0x14, 0x36}, {0x23, 200, 0x6e, 0x6e, 0x14, 0x36},
            {0x24, 0x136, 0x1e, 0x8c, 0x14, 0x34}, {0x2e, 0x136, 0x32, 0x8c, 0x14, 0x34}, {0x25, 0x136, 0x46, 0x8c, 0x14, 0x34},
            {0x26, 0x136, 0x5a, 0x8c, 0x14, 0x34}, {0x27, 0x136, 0x6e, 0x8c, 0x14, 0x34}};
        for (const L& l : ls) addLabel(l.i, l.x, l.y, l.w, l.h, l.f, 100);
        break;
    }
    case 10:
        for (int k = 0; k < 8; k++) addLabel(0x28 + k, 10, 10 + 20 * k, 0x82, 0x14, 0x34, 100);
        break;
    case 0xc:
        addLabel(1, 4, 4, w - 4, h - 4, 0x10, 0x400);
        break;
    }
}

void RpgWindow::captureBackground() {
    // Snapshot of what is on screen under the client area, darkened through the palette.
    int cw = clientW(), ch = clientH();
    bg_.create(std::max(cw, 1), std::max(ch, 1), 0);
    Display& d = display();
    for (int yy = 0; yy < ch; yy++)
        for (int xx = 0; xx < cw; xx++) {
            int sx = x + 3 + xx, sy = y + 3 + yy;
            uint8_t v = 0;
            if (sx >= 0 && sx < Display::W && sy >= 0 && sy < Display::H) {
                v = d.primary.at(sx, sy);
                for (Overlay* o : d.overlays) {
                    if (!o->visible) continue;
                    int ox = sx - o->x, oy = sy - o->y;
                    if (ox >= 0 && oy >= 0 && ox < o->surf.width && oy < o->surf.height) v = o->surf.at(ox, oy);
                }
            }
            bg_.px[(size_t)yy * bg_.width + xx] = v;
        }
    double kr = 0.5, kg = 0.5, kb = 0.5;
    if (type == 0) kr = kg = kb = 0.7;
    else if (type == 1) { kr = 0.6; kg = 0.6; kb = 0.8; }
    uint8_t map[256];
    const Palette& pal = d.hw;
    for (int i = 0; i < 256; i++) {
        Rgb c{(uint8_t)(int)(pal[i].r * kr), (uint8_t)(int)(pal[i].g * kg), (uint8_t)(int)(pal[i].b * kb)};
        map[i] = nearestIndex(pal, c);
    }
    for (auto& p : bg_.px) p = map[p];
    haveBg_ = true;
}

void RpgWindow::setTransparent(bool t) {
    if (t == transparent) return;
    transparent = t;
    textColor = t ? kIdxWhite : kIdxBlack;
    if (t) captureBackground();
    for (auto& l : labels) if (l) l->color = textColor;
    repaint();
}

void RpgWindow::drawFrame() {
    Surface& s = overlay.surf;
    auto hline = [&](int x0, int x1, int yy, uint8_t c) { for (int i = x0; i <= x1; i++) if (i >= 0 && i < w && yy >= 0 && yy < h) s.px[(size_t)yy * w + i] = c; };
    auto vline = [&](int xx, int y0, int y1, uint8_t c) { for (int i = y0; i <= y1; i++) if (xx >= 0 && xx < w && i >= 0 && i < h) s.px[(size_t)i * w + xx] = c; };
    // ring 0: top/left 3DLIGHT (C0C0C0), bottom/right 3DDKSHADOW (000000)
    hline(0, w - 1, 0, kIdxLtGray); vline(0, 0, h - 1, kIdxLtGray);
    hline(0, w - 1, h - 1, kIdxBlack); vline(w - 1, 0, h - 1, kIdxBlack);
    // ring 1: top/left 3DHILIGHT (FFFFFF), bottom/right 3DSHADOW (808080)
    hline(1, w - 2, 1, kIdxWhite); vline(1, 1, h - 2, kIdxWhite);
    hline(1, w - 2, h - 2, kIdxGray); vline(w - 2, 1, h - 2, kIdxGray);
    // ring 2: 3DFACE
    hline(2, w - 3, 2, kIdxLtGray); hline(2, w - 3, h - 3, kIdxLtGray);
    vline(2, 2, h - 3, kIdxLtGray); vline(w - 3, 2, h - 3, kIdxLtGray);
}

void RpgWindow::drawLabel(Label& l) {
    Surface& s = overlay.surf;
    Rect r(l.r.left + 3, l.r.top + 3, l.r.right + 3, l.r.bottom + 3);
    Rect clip(3, 3, w - 3, h - 3);
    if (l.disabled) uiFont().drawText(s, r, l.text, l.fmt, kIdxGray, clip);
    else uiFont().drawText(s, r, l.text, l.fmt, l.color, clip);
}

void RpgWindow::drawButtons() {
    if (!hasButtons) return;
    Surface& s = overlay.surf;
    auto btn = [&](int bx, int by, const char* txt, bool enabled) {
        bx += 3; by += 3;
        int bw = 40, bh = 20;
        Rect r = Rect::xywh(bx, by, bw, bh);
        s.fillRect(r, kIdxLtGray);
        for (int i = 0; i < bw; i++) { s.px[(size_t)by * w + bx + i] = kIdxWhite; s.px[(size_t)(by + bh - 1) * w + bx + i] = kIdxBlack; }
        for (int i = 0; i < bh; i++) { s.px[(size_t)(by + i) * w + bx] = kIdxWhite; s.px[(size_t)(by + i) * w + bx + bw - 1] = kIdxBlack; }
        for (int i = 1; i < bw - 1; i++) s.px[(size_t)(by + bh - 2) * w + bx + i] = kIdxGray;
        for (int i = 1; i < bh - 1; i++) s.px[(size_t)(by + i) * w + bx + bw - 2] = kIdxGray;
        Font& f = uiFont();
        bool b = f.bold;
        f.bold = false;
        if (enabled) f.drawText(s, r, txt, DT_CENTER_ | DT_VCENTER_ | DT_SINGLELINE_, kIdxBlack, r);
        else {
            Rect r2(r.left + 1, r.top + 1, r.right + 1, r.bottom + 1);
            f.drawText(s, r2, txt, DT_CENTER_ | DT_VCENTER_ | DT_SINGLELINE_, kIdxWhite, r2);
            f.drawText(s, r, txt, DT_CENTER_ | DT_VCENTER_ | DT_SINGLELINE_, kIdxGray, r);
        }
        f.bold = b;
    };
    btn(0x91, 0xb6, "<<", prevEnabled);
    btn(0xbe, 0xb6, ">>", nextEnabled);
}

void RpgWindow::repaint() {
    Surface& s = overlay.surf;
    drawFrame();
    // background
    if (!transparent || !haveBg_) s.fillRect(Rect(3, 3, w - 3, h - 3), kIdxLtGray);
    else s.blit(3, 3, bg_, Rect(0, 0, bg_.width, bg_.height), false);
    uint8_t sep = transparent ? kIdxWhite : kIdxBlack;
    if (type == 1) s.fillRect(Rect(6 + 3, 18 + 3, 589 + 3, 19 + 3), sep);
    else if (type == 9) s.fillRect(Rect(16 + 3, 198 + 3, 430 + 3, 199 + 3), sep);
    for (auto& l : labels) if (l) drawLabel(*l);
    drawButtons();
    if (labels[0] && curRow != -1 && curCol != -1) drawCursor(curCol, curRow, true);
    platform().markDirty();
}

void RpgWindow::setText(int i, const std::string& s) {
    if (Label* l = label(i)) {
        l->text = s;
        repaint();
    }
}

void RpgWindow::drawCursor(int col, int row, bool show) {
    Surface& s = overlay.surf;
    int lh = lineH;
    Rect cell(col * colW + 3, row * lh + 3, col * colW + colW + 3, row * lh + lh + 3);
    Rect clip(3, 3, w - 3, h - 3);
    cell.left = std::max(cell.left, clip.left); cell.top = std::max(cell.top, clip.top);
    cell.right = std::min(cell.right, clip.right); cell.bottom = std::min(cell.bottom, clip.bottom);
    if (show) {
        if (!transparent) {
            for (int yy = cell.top; yy < cell.bottom; yy++)
                for (int xx = cell.left; xx < cell.right; xx++) s.px[(size_t)yy * w + xx] ^= 0xff;
        } else {
            s.fillRect(cell, kIdxBlack);
            auto redraw = [&](int li) {
                Label* l = label(li);
                if (!l) return;
                uint8_t old = l->color;
                l->color = (old == textColor) ? kIdxGray : kIdxBlue;
                drawLabel(*l);
                l->color = old;
            };
            int base = cols * row + col + 1 - (yoff != 0 ? 1 : 0);
            redraw(base);
            if (type == 8 || type == 4 || type == 5) redraw(base + 10);
            if (type == 6 || type == 0xb)
                for (int k = 0; k < 4; k++) redraw(1 + (row * 4 + rows + k));
        }
        curCol = col;
        curRow = row;
    } else {
        if (!transparent) {
            for (int yy = cell.top; yy < cell.bottom; yy++)
                for (int xx = cell.left; xx < cell.right; xx++) s.px[(size_t)yy * w + xx] ^= 0xff;
        } else {
            Rect src(cell.left - 3, cell.top - 3, cell.right - 3, cell.bottom - 3);
            s.blit(cell.left, cell.top, bg_, src, false);
            for (auto& l : labels) if (l) drawLabel(*l);
        }
    }
    platform().markDirty();
}

bool RpgWindow::validTarget(int row) const {
    int col = curCol;
    if (mode != 3) return row != curRow && row < rows;
    if (row == curRow && col == curCol) return false;
    if (col >= cols || row >= rows) return false;
    const Label* l = labels[1 + col + row * cols].get();
    return l && !l->text.empty();
}

void RpgWindow::changePage(int delta) {
    if (delta < 0) {
        nextEnabled = true;
        page--;
        g_.post(0x40f, type, page);
        if (page == 0) prevEnabled = false;
        lastPage = false;
    } else {
        prevEnabled = true;
        page++;
        g_.post(0x410, type, page);
        if (page == 9) nextEnabled = false;
    }
    char b[32];
    snprintf(b, sizeof b, "%d", page + 1);
    setText(0x2e, b);
    int total = 0;
    if (Label* l = label(0x2f)) total = atoi(l->text.c_str() + (l->text.empty() ? 0 : 1));
    snprintf(b, sizeof b, "/%d )", total);
    setText(0x2f, b);
}

void RpgWindow::onKey(int vk) {
    if (inactive) return;
    if (vk == VK_ESCAPE_) { g_.post(0x402, type, 0); return; }
    if (vk == VK_RETURN_ || vk == VK_SPACE_) {
        if (itemCount == -1) { g_.post(0x401, type, 0); return; }
        lastSel = page * 10 + curRow * cols + curCol;
        if (lastSel < 0 || lastSel >= itemCount) return;
        Label* l = label(1 + lastSel % 10);
        if (!l || l->disabled) return;
        selected = true;
        g_.post(0x401, type, lastSel + 1000);
        return;
    }
    if (vk == VK_NUMPAD4_ || vk == VK_LEFT_) {
        if (selected) return;
        if (type != 4 && type != 5 && type != 8 && type != 7) return;
        if (page > 0) changePage(-1);
    }
    if (vk == VK_NUMPAD8_ || vk == VK_UP_) {
        if (selected || !labels[0]) return;
        int target = curRow < 1 ? curRow + rows - 1 : curRow - 1;
        drawCursor(curCol, curRow, false);
        if (validTarget(target)) curRow = target;
        drawCursor(curCol, curRow, true);
    }
    if (vk == VK_NUMPAD6_ || vk == VK_RIGHT_) {
        if (selected) return;
        if (type != 4 && type != 5 && type != 8 && type != 7) return;
        if (page < 9 && !lastPage) changePage(1);
    }
    if (vk == VK_NUMPAD2_ || vk == VK_DOWN_) {
        if (selected || !labels[0]) return;
        int target = curRow < rows - 1 ? curRow + 1 : curRow - (rows - 1);
        drawCursor(curCol, curRow, false);
        if (validTarget(target)) curRow = target;
        drawCursor(curCol, curRow, true);
    }
}

void RpgWindow::activate() {
    inactive = false;
    selected = false;
    g_.focusedType = type;
}

void RpgWindow::deactivate() { inactive = true; }

void RpgWindow::endOfList() {
    nextEnabled = false;
    lastPage = true;
    repaint();
}

}  // namespace rm95
