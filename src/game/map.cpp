// Map, characters and movement (FUN_0041c58d .. FUN_004224db), see spec/map_movement.md
#include "game.h"
#include "../platform/platform.h"
#include "../util/fs.h"
#include "../util/log.h"
#include "../util/rng.h"
#include <algorithm>

namespace rm95 {

static const int kStepFrames = 4;   // DAT_00461d68
static const int kStepPixels = 8;   // DAT_00461d6c
static const uint32_t kPassMask[7] = {0x0c8f, 0x0481, 0x0004, 0x0030, 0x0060, 0x3dff, 0x3fff};  // DAT_00461d70

static int dxOf(int d) { return d == 1 ? 1 : d == 3 ? -1 : 0; }
static int dyOf(int d) { return d == 2 ? 1 : d == 0 ? -1 : 0; }
static int mod4(int v) { int r = v % 4; return r < 0 ? r + 4 : r; }

bool Game::passable(int vehicle, int attr) const {
    if (vehicle < 0 || vehicle > 6) return false;
    return ((1u << (attr & 0x1f)) & kPassMask[vehicle]) != 0;
}

// map cell -> screen cell (with the looping-map -1 rule). Returns false when outside -1..20 / -1..15.
bool Game::screenCell(int mx, int my, int& c, int& r) const {
    int x = mx < camX ? mx + mapW : mx;
    int y = my < camY ? my + mapH : my;
    c = x - camX;
    r = y - camY;
    if (map && map->type == kMapLooping) {
        if (c == mapW - 1 && mapW > 21) c = -1;
        if (r == mapH - 1 && mapH > 16) r = -1;
    }
    return !(c < -1 || r < -1 || c > 20 || r > 15);
}

int Game::occupantTile(int mx, int my) {
    int x = mx < camX ? mx + mapW : mx;
    int y = my < camY ? my + mapH : my;
    int c = x - camX, r = y - camY;
    if (mapW > 21 && c == mapW - 1) c = -1;
    if (mapH > 16 && r == mapH - 1) r = -1;
    if (c < -1 || c > 20 || r < -1 || r > 15) return -1;
    if (gridAt(1, c, r)) return -2;
    Chara* b = gridAt(0, c, r);
    if (!b) return -1;
    if (b->graphic < 0x100) return -2;
    return b->graphic - 0x100;
}

// ------------------------------------------------------------------------------------------
// graphics

void Game::loadMapGraphics() {
    // FUN_0042eaff(-1): CHARA.BMP (palette source, commit 0) and the 4 chipsets of the current map
    Display& d = display();
    Image img;
    if (!loadBmp(fs::find(gameDir, "CHARA.BMP"), img)) {
        LOG_WARN("CHARA.BMP missing");
        img.width = 1024; img.height = 512; img.indexed = true; img.pixels.assign(1024 * 512, 0);
    }
    d.loadPaletteSource(img, false);
    charaSurf = imageToSurface(img, d.hw);
    charaSurf.colorKey = charaSurf.px.empty() ? -1 : charaSurf.px[0];
    int m = curMap >= 0 ? curMap : rpg.startMap;
    MapData* md = (m >= 0 && m < 500) ? &rpg.maps[m] : nullptr;
    for (int i = 0; i < 4; i++) {
        std::string name = md ? md->chipsets[i] : "";
        Image ci;
        std::string base = fs::baseName(name);
        bool ok = !base.empty() && loadBmp(fs::find(gameDir, base + ".BMP"), ci);
        if (!ok) {
            char b[16];
            snprintf(b, sizeof b, "MCHIP%d.BMP", i);
            ok = loadBmp(fs::find(gameDir, b), ci);
        }
        if (ok) chipsets[i] = imageToSurface(ci, d.hw);
        else chipsets[i].create(256, 1024, 0);
        chipsets[i].colorKey = charaSurf.colorKey;
    }
    d.effect(4, nullptr, false);
}

void Game::loadAtr() {
    int m = curMap == -1 ? rpg.startMap : curMap;
    if (m < 0 || m >= 500) return;
    MapData& md = rpg.maps[m];
    for (int k = 0; k < 4; k++) {
        std::string name = md.chipsets[k];
        std::vector<uint8_t> d;
        std::string base = fs::baseName(name);
        std::string p = fs::find(gameDir, base + ".ATR");
        memset(atr + k * 256, 0, 256);
        if (!p.empty() && fs::readFile(p, d)) memcpy(atr + k * 256, d.data(), std::min<size_t>(256, d.size()));
    }
}

void Game::drawTile(int sx, int sy, uint16_t t) {
    int chip = (t >> 8) & 3, n = t & 0xff;
    display().back.blit(sx * 32, sy * 32, chipsets[chip], Rect::xywh((n % 8) * 32, (n / 8) * 32, 32, 32), false);
}

void Game::redrawTiles() {
    for (int y = 0; y < 15; y++)
        for (int x = 0; x < 20; x++) drawTile(x, y, tileAt((camX + x) % mapW, (camY + y) % mapH));
    display().cache.blit(0, 0, display().back, Rect(0, 0, 640, 480), false);
}

void Game::drawScrollStrip(int dir, int s) {
    Surface& back = display().back;
    if (s <= 0) return;
    auto tile = [&](int tx, int ty, int px, int py, Rect src) {
        uint16_t t = tileAt(((tx % mapW) + mapW) % mapW, ((ty % mapH) + mapH) % mapH);
        int chip = (t >> 8) & 3, n = t & 0xff;
        Rect sr = Rect::xywh((n % 8) * 32 + src.left, (n / 8) * 32 + src.top, src.width(), src.height());
        back.blit(px, py, chipsets[chip], sr, false);
    };
    for (int i = 0; i < (dir == 1 || dir == 3 ? 15 : 20); i++) {
        switch (dir) {
        case 3: tile(camX - 1, camY + i, 0, i * 32, Rect(32 - s, 0, 32, 32)); break;
        case 1: tile(camX + 20, camY + i, 640 - s, i * 32, Rect(0, 0, s, 32)); break;
        case 0: tile(camX + i, camY - 1, i * 32, 0, Rect(0, 32 - s, 32, 32)); break;
        case 2: tile(camX + i, camY + 15, i * 32, 480 - s, Rect(0, 0, 32, s)); break;
        }
    }
}

void Game::clearGrid() { memset(grid, 0, sizeof grid); }

void Game::redraw(int param2, bool present) {
    Chara* h = heroChara;
    if (param2 == -2 && h && h->step == 4) return;
    int dir = param2;
    if (param2 == -2) {
        dir = -1;
        scrollStep = 0;
        if (h && h->motion >= 0 && h->motion < 4 && (h->step % 4) > 0) {
            dir = h->motion;
            scrollStep = h->step;
        }
    }
    if (dir != -1 && scrollFlag[dir]) {
        camPxX = (mapW * 32 + camX * 32 + dxOf(dir) * kStepPixels * scrollStep) % (mapW * 32);
        camPxY = (mapH * 32 + camY * 32 + dyOf(dir) * kStepPixels * scrollStep) % (mapH * 32);
    }
    // restore the background from the cache (shifted while scrolling)
    Display& d = display();
    int s = (dir != -1 && scrollFlag[dir]) ? scrollStep * kStepPixels : 0;
    int sx = 0, sy = 0, dx = 0, dy = 0;
    if (s) {
        if (dir == 0) dy = s;
        if (dir == 2) sy = s;
        if (dir == 3) dx = s;
        if (dir == 1) sx = s;
    }
    int w = 640 - (dir == 1 || dir == 3 ? s : 0), hgt = 480 - (dir == 0 || dir == 2 ? s : 0);
    d.back.blit(dx, dy, d.cache, Rect::xywh(sx, sy, w, hgt), false);
    if (dir != -1 && scrollFlag[dir]) drawScrollStrip(dir, s);
    if (scrollStep == kStepFrames && dir >= 0 && param2 >= 0) {
        d.cache.blit(0, 0, d.back, Rect(0, 0, 640, 480), false);
        if (scrollFlag[dir]) {
            camX = (camX + dxOf(dir) + mapW) % mapW;
            camY = (camY + dyOf(dir) + mapH) % mapH;
        }
    }
    drawCharas(param2 == -2 && !present);
    if (present && screenState == 0) d.present();
}

void Game::drawCharas(bool freeze) {
    for (int L = 0; L < 3; L++) {
        for (int r = -1; r < 16; r++) {
            int row = (camY + r + mapH) % mapH;
            for (auto& ev : events) {
                Chara* c = ev.get();
                int cy = c->step < 2 ? c->y : c->destY;
                if (c->layer == L && cy == row) drawChara(c, freeze || (moveControl == 1 && c->resumeIndex == 0));
            }
            for (int k = riding ? 1 : 0; k < 10; k++) {
                Chara* v = vehicles[k].ch;
                if (!v || vehicles[k].map != curMap) continue;
                int cy = v->step < 2 ? v->y : v->destY;
                if (v->layer == L && cy == row) drawChara(v, freeze || (moveControl == 1 && v->resumeIndex == 0));
            }
            Chara* h = mapParty[0];
            if (h) {
                int hy = h->step < 2 ? h->y : h->destY;
                if (h->layer == L && hy == row) {
                    for (int i = mapCharaCount - 1; i >= 0; i--) {
                        Chara* m = mapParty[i];
                        if (!m) continue;
                        Battler& b = member(i);
                        bool down = b.hp() < 1 || (b.status() & 2);
                        drawChara(m, freeze || down);
                    }
                }
            }
        }
    }
}

void Game::drawChara(Chara* c, bool freeze) {
    c->onScreen = 0;
    if (c->display == kDispRemoved && c->id != 0) return;
    if (!freeze) {
        c->animTimer += (c->anim == 0x13 || c->anim == 0x14) ? 8 : 1;
        c->animTimer += 1;
        if (c->animTimer > c->speed * 10 + 5) {
            c->animTimer = 0;
            if (c->anim == 0x15) c->animFrame = (int)(rng().next() & 3);
            else c->animFrame = (c->animFrame + 1) & 3;
        }
    }
    int dir = c->facing;
    if (c->step >= kStepFrames) c->step = 0;
    int W32 = mapW * 32, H32 = mapH * 32;
    int px = c->x * 32, py = c->y * 32;
    if (c->motion < 4 && c->motion != -1) {
        px += dxOf(c->motion) * kStepPixels * c->step;
        py += dyOf(c->motion) * kStepPixels * c->step;
    }
    px -= camPxX;
    py -= camPxY;
    if (px < -31) px += W32;
    if (py < -31) py += H32;
    bool twoTile = c->vehicle == 1 || c->vehicle == 2;
    bool skipMain = false;
    if (px > 639 || py > 479) {
        if (!twoTile) return;
        skipMain = true;
    }
    int col = c->animFrame & 1;
    if (c->display == kDispNormal) {
        if (c->graphic < 0x100) {
            if (c->dirOverride == -1 || c->anim == 0x16 || c->anim == 0x18) {
                if (c->motion < 4 || c->motion > 7) {
                    switch (c->anim) {
                    case 0x12: dir = (c->motion == 0x11 || c->motion == -1) ? c->facing : mod4(c->motion); break;
                    case 0x13: dir = c->animFrame; break;
                    case 0x14: dir = mod4(4 - c->animFrame); break;
                    case 0x15: dir = c->animFrame; break;
                    case 0x16: dir = c->facing; break;
                    case 0x17: dir = (c->motion == 0x11 || c->motion == -1) ? c->facing : mod4(c->motion); col = 0; break;
                    case 0x18: dir = c->facing; col = 0; break;
                    default: break;
                    }
                } else {
                    dir = mod4(c->motion);
                }
            } else {
                dir = c->dirOverride;
            }
            c->dirOverride = -1;
        }
        int g = c->graphic;
        int facing = c->facing;
        for (int part = 0; part < (twoTile ? 2 : 1); part++) {
            int x = px, y = py;
            int gg = g;
            if (part == 0 && skipMain) continue;
            if (part == 1) {
                x = (-dxOf(facing)) * kStepPixels * (kStepFrames - c->step) + c->x * 32 - camPxX;
                y = (-dyOf(facing)) * kStepPixels * (kStepFrames - c->step) + c->y * 32 - camPxY;
                if (map->type == kMapLooping && x < -31) x += W32;
                if (map->type == kMapLooping && y < -31) y += H32;
                if (x < -31 || y < -31 || x > 639 || y > 479) break;
                gg = c->vehicle == 1 ? 0x3e : 0x3f;
            }
            Rect src;
            const Surface* surf;
            if (gg < 0x100) {
                int d2 = part == 1 ? facing : dir;
                src = Rect::xywh((gg & 15) * 64 + col * 32, (gg >> 4) * 128 + d2 * 32, 32, 32);
                surf = &charaSurf;
            } else {
                int t = gg - 0x100, chip = (t >> 8) & 3, n = t & 0xff;
                src = Rect::xywh((n % 8) * 32, (n / 8) * 32, 32, 32);
                surf = &chipsets[chip];
            }
            int w = 32, h = 32;
            if (x < 0) { src.left -= x; w += x; x = 0; }
            else if (x > 608) w = 640 - x;
            if (y < 0) { src.top -= y; h += y; y = 0; }
            else if (y > 448) h = 480 - y;
            src.right = src.left + w;
            src.bottom = src.top + h;
            display().back.blit(x, y, *surf, src, true);
        }
    }
    if (!skipMain && c->display != kDispRemoved) {
        int cx, cy;
        if (c->step < kStepFrames / 2) { cx = c->x; cy = c->y; }
        else { cx = c->destX; cy = c->destY; }
        cx = cx < camX ? cx + mapW : cx;
        cy = cy < camY ? cy + mapH : cy;
        cx -= camX;
        cy -= camY;
        if (cx == mapW - 1 && mapW > 21 && map->type == kMapLooping) cx = -1;
        if (cy == mapH - 1 && mapH > 16 && map->type == kMapLooping) cy = -1;
        if (cx < 21 && cy < 16 && cx >= -1 && cy >= -1) {
            gridAt(c->layer, cx, cy) = c->moveType == 6 ? nullptr : c;
            c->onScreen = 1;
        }
        if (twoTile) {
            int bx = cx - dxOf(c->facing), by = cy - dyOf(c->facing);
            if (bx > -2 && by > -2 && bx < 21 && by < 16) {
                gridAt(c->layer, bx, by) = c;
                c->onScreen = 1;
            }
        }
        if (c->step == kStepFrames - 1) c->facing = dir;
    }
}

// ------------------------------------------------------------------------------------------
// movement

void Game::startMotion(Chara* c, int m) {
    if (c->display == kDispRemoved && c->id != 0) return;
    c->motion = m;
    if (m == -1) return;
    int type = map ? map->type : 0;
    if (m < 4) {
        int nx = c->x + dxOf(m), ny = c->y + dyOf(m);
        if (type == 0 && (nx < 0 || nx >= mapW)) nx = c->x; else nx = (nx + mapW) % mapW;
        if (type == 0 && (ny < 0 || ny >= mapH)) ny = c->y; else ny = (ny + mapH) % mapH;
        c->destX = nx;
        c->destY = ny;
        if (nx == c->x && ny == c->y) { c->motion = -1; return; }
        int sc = (nx < camX ? nx + mapW : nx) - camX;
        int sr = (ny < camY ? ny + mapH : ny) - camY;
        if (sc == mapW - 1 && mapW > 21 && type == 1) sc = -1;
        if (sr == mapH - 1 && mapH > 16 && type == 1) sr = -1;
        if (c->id != 0 || c == heroChara) {
            bool off = sc < -1 || sr < -1 || sc > 20 || sr > 15;
            if (c->id != 0 && c->routeMode == kRouteNone && off) { c->motion = -1; return; }
            Chara* below = off ? nullptr : gridAt(0, sc, sr);
            uint32_t t = tileAt(nx, ny);
            if (below && below->graphic >= 0x100) t = below->graphic - 0x100;
            bool blocked = !passable(c->vehicle, atr[t & 0x3ff]);
            if (below && (below->vehicle == 3 || below->vehicle == 4)) {
                blocked = false;
                if (c->vehicle != 0 && c->vehicle != 5 && c->vehicle != 6) blocked = true;
            }
            if (c->display == kDispRemoved) blocked = false;
            if (blocked) {
                if (c->vehicle == 1 || c->vehicle == 2 || c->anim == 0x16 || c->anim == 0x18) c->motion = -1;
                else { c->facing = c->motion; c->motion += 4; }
                c->destX = c->x;
                c->destY = c->y;
            }
            if (c->display != kDispRemoved && !off) {
                Chara* o = gridAt(1, sc, sr);
                if (o && o->graphic < 0x100 && o != c && (c->vehicle == 1 || c->vehicle == 2)) {
                    c->motion = -1;
                    c->destX = c->x;
                    c->destY = c->y;
                }
            }
        }
    } else if (m < 8) {
        c->facing = m & 3;
    }
}

Chara* Game::advanceMotion(Chara* c) {
    if ((c->display == kDispRemoved && c->id != 0) || c->motion == -1) return nullptr;
    if (c->motion >= 0 && c->motion < 0x12) c->step++;
    if (c->motion >= 4 && c->motion <= 7) {
        c->dirOverride = c->motion & 3;
        c->step = 0;
        c->motion = -1;
        return nullptr;
    }
    if (c->motion == 0x11) {
        if (c->step == kStepFrames) c->motion = -1;
        return nullptr;
    }
    int sc = (c->destX < camX ? c->destX + mapW : c->destX) - camX;
    int sr = (c->destY < camY ? c->destY + mapH : c->destY) - camY;
    if (map->type == kMapLooping) {
        if (sc == mapW - 1 && mapW > 21) sc = -1;
        if (sr == mapH - 1 && mapH > 16) sr = -1;
    }
    if (sc < -1 || sr < -1 || sc > 20 || sr > 15) {
        if (c->step == kStepFrames) { c->x = c->destX; c->y = c->destY; c->motion = -1; }
        return nullptr;
    }
    if (c->motion < 4 && c->motion != -1) {
        Chara*& slot = gridAt(c->layer, sc, sr);
        Chara* o = slot;
        if (c->id == 0) {
            if (c == heroChara && c->step == 1 && o && o->id != 0 && c->display != kDispRemoved && c->vehicle != 5 && c->vehicle != 6) {
                c->step = 0;
                if (c->vehicle != 1 && c->vehicle != 2) { c->facing = c->motion; c->motion += 4; return o; }
                c->motion = -1;
                return o;
            }
            slot = c;
        } else {
            if (c->step == kStepFrames / 2) {
                if (!o || o->id == c->id) slot = c;
                else {
                    if (c->routeMode == kRouteNone) {
                        c->motion = (c->motion + 2) & 3;
                        std::swap(c->x, c->destX);
                        std::swap(c->y, c->destY);
                        c->step = kStepFrames - c->step;
                    } else {
                        c->step--;
                    }
                    if (o == heroChara) return c;
                }
            }
        }
        if (c->step == kStepFrames) { c->x = c->destX; c->y = c->destY; c->motion = -1; }
    }
    return nullptr;
}

int Game::nextMove(Chara* c, int tx, int ty) {
    if (c->routeMode == kRouteNone) {
        switch (c->moveType) {
        case 1:
        case 2: {
            int h, v;
            if (c->moveType == 1) { h = (c->x - tx <= 0) ? 1 : 3; v = (c->y - ty <= 0) ? 2 : 0; }
            else { h = (c->x - tx >= 0) ? 1 : 3; v = (c->y - ty >= 0) ? 2 : 0; }
            int r = (int)rng().next();
            if (r % 3 == 0) startMotion(c, h);
            else if (r % 3 == 1) startMotion(c, v);
            else startMotion(c, (int)(rng().next() & 3));
            break;
        }
        case 3:
            startMotion(c, (int)(rng().next() & 3));
            break;
        default: break;
        }
        return 0;
    }
    bool seenLoop = false;
    while (true) {
        if (c->routePos < 0 && c->routeMode == kRoutePingPong) {
            c->routePos = 0;
            c->routeStep = 1;
        } else if (c->routePos < 0 || c->routePos >= c->routeLen || c->routePos >= (int)c->route.size()) {
            break;
        }
        int b = c->route[c->routePos];
        if (b < 0x12) {
            int d = b;
            if (b >= 8 && b < 16) {
                int k = b - 8;
                Chara* who = ((k >> 1) & 1) ? c : heroChara;
                d = (who->facing + (k & 1) * 2) & 3;
                if (k > 3) d += 4;
            }
            if (d == 0x10) d = c->facing;
            else if (c->routeStep == -1) d = ((d + 2) & 3) + (d > 3 ? 4 : 0);
            startMotion(c, d);
            c->routePos += c->routeStep;
            return 0;
        }
        if (b <= 0x18) { c->anim = b; c->routePos += c->routeStep; continue; }
        if (b <= 0x1b) { c->display = b; c->routePos += c->routeStep; continue; }
        if (b == 0x1c) break;
        if (b == 0x1d) {
            if (seenLoop) break;
            c->routeMode = kRouteRepeat;
            c->routePos = 0;
            seenLoop = true;
            continue;
        }
        if (b == 0x1e || b == 0x1f) {
            if (seenLoop) break;
            c->routeMode = b;
            c->routeStep = -1;
            c->routePos += c->routeStep;
            seenLoop = true;
            continue;
        }
        break;  // >= 0x20: the original loops forever
    }
    c->routeMode = kRouteNone;
    c->route.clear();
    c->routePos = 0;
    c->routeStep = 1;
    c->routeLen = 0;
    return 1;
}

void Game::applyRouteDisplay(Chara* c) {
    if (c->routeMode == kRouteNone) return;
    while (c->routePos < c->routeLen && c->route[c->routePos] > 0x18 && c->route[c->routePos] < 0x1c) {
        c->display = c->route[c->routePos];
        c->routePos++;
    }
}

void Game::updateEvent(Chara* ev) {
    if (ev->cooldown > 0) ev->cooldown--;
    if (moveControl == 1 && ev->resumeIndex == 0) return;
    Chara* h = heroChara;
    if (h->speed == ev->speed && h->speedCounter == 0) ev->speedCounter = ev->speed;
    ev->speedCounter++;
    if (ev->speedCounter < ev->speed) return;
    ev->speedCounter = 0;
    if (ev->motion == -1) {
        int r = nextMove(ev, h->x, h->y);
        if (r && screenState == 0) {
            closeAllWindows(true);
            redraw(-2, true);
        }
        if (r && ev->resumeIndex > 0) {
            runEvent(ev->id, ev);
            if (abortTick) return;
        }
    }
    Chara* e = advanceMotion(ev);
    if (e) touch(e);
}

// ------------------------------------------------------------------------------------------
// party on the map

void Game::initPartyCharas() {
    // FUN_004217f7
    for (int a = 0; a < 8; a++) {
        Chara& c = partyChara[a];
        c = Chara();
        c.x = c.destX = rpg.startX;
        c.y = c.destY = rpg.startY;
        c.graphic = rpg.actors[a].graphic();
    }
    for (int i = 0; i < 8; i++) mapParty[i] = nullptr;
    for (int i = 0; i < partySize; i++) mapParty[i] = &partyChara[(int)partyActor[i]];
    if (partySize == 0) mapParty[0] = &partyChara[0];
}

void Game::computeScrollFlags() {
    Chara* h = heroChara;
    int sx = (h->x - camX + mapW) % mapW, sy = (h->y - camY + mapH) % mapH;
    scrollFlag[0] = sy < 8;
    scrollFlag[1] = sx > 8;
    scrollFlag[2] = sy > 6;
    scrollFlag[3] = sx < 10;
    if (map->type == 0) {
        if (camY == 0) scrollFlag[0] = false;
        if (camX == mapW - 20) scrollFlag[1] = false;
        if (camY == mapH - 15) scrollFlag[2] = false;
        if (camX == 0) scrollFlag[3] = false;
    }
}

void Game::pushTrail() {
    for (int i = 7; i > 0; i--) trail[i] = trail[i - 1];
    trail[0] = heroChara->motion;
}

void Game::startFollowers() {
    Chara* h = mapParty[0];
    for (int i = mapCharaCount - 1; i > 0; i--) {
        int m = trail[i];
        startMotion(mapParty[i], m);
        if ((h->moveType == 6 && m == 0x11) || (h->moveType == 7 && m == -1)) mapParty[i]->display = kDispInvisible;
        else if (h->moveType == 7 && m != -1) mapParty[i]->display = kDispNormal;
    }
}

// ------------------------------------------------------------------------------------------
// map change

bool Game::vehicleHasId(uint16_t id) {
    for (auto& v : vehicles) if (v.ch && v.id == id) return true;
    return false;
}

void Game::deleteMapEvents() {
    events.clear();
}

void Game::createMapEvents() {
    for (int y = 0; y < mapH; y++)
        for (int x = 0; x < mapW; x++) {
            uint32_t& cell = map->cells[(size_t)y * mapW + x];
            uint16_t id = (uint16_t)(cell >> 16);
            if (!id || vehicleHasId(id)) continue;
            auto ev = std::make_unique<Chara>();
            ev->isEvent = true;
            EventHeader hdr;
            if (!loadEventHeader(gameDir, id, hdr)) { cell &= 0xffff; continue; }
            ev->id = id;
            ev->x = ev->destX = x;
            ev->y = ev->destY = y;
            ev->attrs = hdr.attrs;
            for (int p = 0; p < 4; p++) ev->pageRoutes[p] = hdr.pageRoutes[p];
            LOG_DEBUG("map %d: event %d at %d,%d", curMap, id, x, y);
            events.push_back(std::move(ev));
        }
}

void Game::buildAreaIds() {
    areaIds.assign((size_t)mapW * mapH, (uint16_t)curMap);
    int c = map->firstChild;
    int guard = 0;
    while (c != 0 && guard++ < 1000) {
        MapData& a = rpg.maps[c];
        if (a.type == kMapArea && a.areaX < mapW && a.areaY < mapH) {
            int y1 = std::min(a.areaY + (int)a.height, mapH), x1 = std::min(a.areaX + (int)a.width, mapW);
            for (int y = std::max(0, a.areaY); y < y1; y++)
                for (int x = std::max(0, a.areaX); x < x1; x++) areaIds[(size_t)y * mapW + x] = (uint16_t)c;
        }
        c = a.nextSibling;
    }
}

void Game::applyPage(Chara* ev) {
    int p = ev->page;
    if (p < 0) return;
    if (ev != heroChara) {
        ev->layer = ev->attrs.layer(p);
        ev->moveType = ev->attrs.moveType(p);
    }
    ev->anim = ev->attrs.anim(p);
    ev->display = ev->attrs.display(p);
    ev->speed = ev->attrs.speed(p);
    ev->vehicle = ev->attrs.vehicle(p);
    ev->resumeIndex = 0;
}

bool Game::selectPage(Chara* ev, const int16_t* counts, uint32_t mny) {
    const EventPageAttrs& a = ev->attrs;
    int p;
    for (p = 3; p > 0; p--) {
        uint32_t c = a.cond(p);
        if (!((c & 1) || (c & 2) || (c & 4) || ((c & 8) && a.party(p) != 0))) continue;
        if ((c & 1) && counts[a.item(p) % 100] == 0) continue;
        if ((c & 2) && a.gold(p) > mny) continue;
        if ((c & 4) && !getSwitch(a.switchId(p))) continue;
        if ((c & 8) && (a.party(p) & partyMask) != a.party(p)) continue;
        break;
    }
    if (p == ev->page) return false;
    ev->page = (int8_t)p;
    ev->setGraphicFromPage(a.graphic(p));
    ev->cooldown = 1;
    applyPage(ev);
    ev->route.clear();
    ev->routeLen = (int)ev->pageRoutes[p].size();
    ev->routeMode = kRouteNone;
    ev->routePos = 0;
    if (ev->routeLen > 0) {
        ev->route = ev->pageRoutes[p];
        ev->routeMode = kRouteActive;
        ev->routePos = 0;
        ev->routeStep = 1;
    }
    return true;
}

void Game::refreshPages() {
    int16_t counts[100];
    memcpy(counts, itemCounts, sizeof counts);
    for (int a = 0; a < 8; a++)
        for (int s = 0; s < 5; s++) {
            int it = pb.equip[a][s];
            if (it >= 0 && it < 100) counts[it]++;
        }
    for (auto& e : events) {
        Chara* ev = e.get();
        if (ev->resumeIndex != 0) continue;
        if (ev == waitingChara && waitingIndex >= 1) continue;
        if (selectPage(ev, counts, (uint32_t)money()) && ev == waitingChara) waitingChara = nullptr;
    }
    for (auto& v : vehicles) {
        if (!v.ch) break;
        if (v.ch->resumeIndex < 1) selectPage(v.ch, counts, (uint32_t)money());
    }
}

bool Game::teleport(int mapId, int x, int y) {
    int prev = curMap;
    if (mapId < 0 || mapId >= 500 || rpg.maps[mapId].width == 0) return false;
    MapData& md = rpg.maps[mapId];
    if (x >= md.width || y >= md.height) return false;
    skipTick = true;
    closeAllWindows(true);
    if (screenState != 3 && screenState != -1) {
        display().hide(transitions[0], true);
        screenState = 3;
    }
    map = &md;
    mapW = md.width;
    mapH = md.height;
    int ox = x - 9, oy = y - 6;
    if (md.type == 0) {
        camX = ox < 0 ? 0 : (ox > mapW - 20 ? mapW - 20 : ox);
        camY = oy < 0 ? 0 : (oy > mapH - 15 ? mapH - 15 : oy);
    } else {
        camX = (mapW + x - 9) % mapW;
        camY = (y + mapH - 6) % mapH;
    }
    camPxX = camX * 32;
    camPxY = camY * 32;
    for (int i = 0; i < 8; i++) {
        Chara& c = partyChara[i];
        c.motion = -1;
        c.step = 0;
        c.x = c.destX = x;
        c.y = c.destY = y;
        trail[i] = -1;
    }
    if (riding && vehicles[0].ch) {
        Chara* v = vehicles[0].ch;
        v->x = v->destX = x;
        v->y = v->destY = y;
        vehicles[0].map = mapId;
        v->motion = -1;
        v->step = 0;
    }
    if (mapId != curMap) {
        curMap = mapId;
        waitingChara = nullptr;
        waitingId = -1;
        waitingPage = 0;
        waitingIndex = 0;
        clearGrid();
        deleteMapEvents();
        createMapEvents();
        buildAreaIds();
        refreshPages();
    }
    encounterArea = curMap;
    computeScrollFlags();
    flushKeys(false);
    if (prev != -1) showMap();
    encounterSteps = 0;
    return true;
}

void Game::showMap() {
    LOG_DEBUG("showMap state=%d map=%d", screenState, curMap);
    if (screenState == 0) return;
    if (screenState == 2 || screenState == -2) {
        fadeToBlack(5);
        reloadPalette(true);
    } else if (screenState == 3 || screenState == -1) {
        reloadPalette(false);
    }
    redrawTiles();
    redraw(-2, false);
    display().show(transitions[0]);
    screenState = 0;
    haveSavedPalette = false;
}

// ------------------------------------------------------------------------------------------
// interaction

void Game::faceEachOther(Chara* e, bool checkLayer) {
    Chara* h = mapParty[0];
    if (checkLayer && e->layer != h->layer) return;
    int ex = e->x, ey = e->y;
    if (e->step >= kStepFrames / 2) { ex = e->destX; ey = e->destY; }
    if (ex == h->x && ey == h->y) return;
    int d;
    if (ex < h->x) d = 3;
    else if (h->x < ex) d = 1;
    else d = (h->y <= ey) ? 2 : 0;
    if (h->vehicle != 1 && h->vehicle != 2) h->dirOverride = d;
    if (e->vehicle != 1 && e->vehicle != 2) e->dirOverride = (d + 2) & 3;
    redraw(-2, true);
}

void Game::actionButton(int mode) {
    Chara* h = mapParty[0];
    if (h->vehicle == 5 || h->vehicle == 6) return;
    if (h->moveType == 7) return;
    if (h->step >= 1) return;
    int hx = ((h->x - camX) + mapW) % mapW, hy = ((h->y - camY) + mapH) % mapH;
    int fdx = dxOf(h->facing), fdy = dyOf(h->facing);
    int fx = hx + fdx, fy = hy + fdy;
    int tx = fx + fdx, ty = fy + fdy;
    Chara* e = nullptr;
    if (mode == 0 && fx > -2 && fx < 21 && fy > -2 && fy < 16) {
        e = gridAt(1, fx, fy);
        if (e && e->id == 0) e = nullptr;
    }
    if (!e && mode == 0 && tx > -2 && tx < 21 && ty > -2 && ty < 16) {
        int mx = (fdx + h->x + mapW) % mapW, my = (fdy + h->y + mapH) % mapH;
        if (atr[tileAt(mx, my) & 0x3ff] == 13) {
            e = gridAt(1, tx, ty);
            if (e && e->id == 0) e = nullptr;
        }
    }
    if (!e) e = gridAt(2, hx, hy);
    if (!e) e = gridAt(0, hx, hy);
    if (!e || e == h) return;
    if (mode == 1 && e->trigger() != 1) return;
    if (e->resumeIndex >= 1) return;
    if (mode == 0 && e->cooldown > 0) return;
    faceEachOther(e, true);
    runEvent(e->id, e);
    flushKeys(mode == 0);
}

void Game::touch(Chara* e) {
    Chara* h = mapParty[0];
    if (h->vehicle == 5 || h->vehicle == 6 || h->moveType == 7 || h->step >= 1 || h == e) return;
    if (e->trigger() != 1 || e->cooldown != 0 || e->resumeIndex >= 1) return;
    faceEachOther(e, false);
    runEvent(e->id, e);
    flushKeys(true);
}

bool Game::partyDown() {
    for (int i = 0; i < partySize; i++) {
        Battler& b = member(i);
        if (b.hp() >= 1 && !(b.status() & 2)) return false;
    }
    return true;
}

void Game::poisonStep() {
    bool first = true;
    for (int i = 0; i < partySize; i++) {
        Battler& b = member(i);
        if (b.hp() > 0 && (b.status() & 1)) {
            if (first) {
                display().fillBack(0x0000ff);
                display().present();
                redraw(-2, true);
                first = false;
            }
            b.setHp(b.hp() - 5);
            if (b.hp() < 1) {
                b.setHp(0);
                b.setStatus(0);
                sysMessage(b.name() + S[45], 0.5, false);
            }
        }
    }
}

void Game::encounterStep(int cell) {
    Chara* h = heroChara;
    encounterSteps = std::min(encounterSteps + 1, 10000);
    if (h->vehicle >= 5 || h->moveType == 7) return;
    int a = cell >= 0 && cell < (int)areaIds.size() ? areaIds[cell] : curMap;
    if (a != encounterArea) { encounterSteps = 0; encounterArea = a; return; }
    rng().next();
    int order[10];
    for (int i = 0; i < 10; i++) order[i] = i;
    for (int i = 0; i < 10; i++) { int j = (int)(rng().next() % 10); std::swap(order[i], order[j]); }
    MapData& m = rpg.maps[a];
    for (int i = 0; i < 10; i++) {
        uint16_t e = m.encounters[order[i]];
        int p = e & 0x1ff, r = e >> 9;
        if (p == 0 || r == 0) continue;
        int d = std::max(r / 10, 1);
        int thr = r + (int)(rng().next() % (uint32_t)(2 * d + 1)) - d;
        if (h->vehicle == 1 || h->vehicle == 2) thr *= 2;
        if (thr <= encounterSteps && p - 1 < 250 && atr[tileAt(h->x, h->y) & 0x3ff] == troops[p - 1].terrain()) {
            escapeForbidden = 0;
            encounterSteps = 0;
            startBattle(p - 1);
            return;
        }
    }
}

}  // namespace rm95
