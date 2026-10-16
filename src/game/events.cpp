// Event runner (FUN_0042fb0e) and command interpreter (FUN_00432393), see spec/events.md
#include "game.h"
#include "../platform/platform.h"
#include "../util/fs.h"
#include "../util/log.h"
#include "../util/rng.h"
#include "../util/sjis.h"
#include <algorithm>
#include <cstdlib>

namespace rm95 {

int Game::rnd(int a, int b) {
    if (a < b) {
        int n = b - a + 1;
        return a + (n == 0 ? 0 : (int)rng().next() % n);
    }
    return a;
}

void Game::changeValue(int32_t* v, int a1, int a2, int rel, int lo, int hi) {
    int r = rnd(a1, a2);
    *v = rel ? *v + r : r;
    if (*v < lo) *v = lo;
    else if (*v > hi) *v = hi;
}

void Game::changeValue16(int16_t* v, int a1, int a2, int rel, int lo, int hi) {
    int16_t s1 = (int16_t)a1, s2 = (int16_t)a2;
    int16_t r;
    if (s1 < s2) {
        int n = (int)s2 - (int)s1 + 1;
        r = (int16_t)(n == 0 ? 0 : (int16_t)((int)rng().next() % n));
        r = (int16_t)(r + s1);
    } else r = s1;
    *v = rel ? (int16_t)(*v + r) : r;
    if (*v < (int16_t)lo) *v = (int16_t)lo;
    else if (*v > (int16_t)hi) *v = (int16_t)hi;
}

int Game::partySlotOfActor(int actor) {
    for (int i = 0; i < 8; i++) if (member(i).actorId() == actor) return i;
    return 8;
}

// ------------------------------------------------------------------------------------------

std::string Game::expandText(const std::string& in, bool& ok) {
    static const char* const plural[8] = {"\x82\xbd\x82\xbf", "\x82\xaa\x82\xbd", "\x82\xc7\x82\xe0", "\x82\xe7",
                                          "\x92\x42", "\x95\xfb", "\x8b\xa4", "\x93\x99"};
    static const char* const fullDigits[8] = {"\x82\x50", "\x82\x51", "\x82\x52", "\x82\x53", "\x82\x54", "\x82\x55", "\x82\x56", "\x82\x57"};
    static const char* const kanji[8] = {"\x88\xea", "\x93\xf1", "\x8e\x4f", "\x8e\x6c", "\x8c\xdc", "\x98\x5a", "\x8e\xb5", "\x94\xaa"};
    msgWait = -1;
    ok = true;
    std::string out;
    size_t i = 0;
    auto lenOk = [&](size_t extra) { return (in.size() + extra) - 2 <= 0x400 || in.size() + extra < 2; };
    while (i < in.size()) {
        char c = in[i];
        if (c != '$') { out.push_back(c); i++; continue; }
        char k = i + 1 < in.size() ? in[i + 1] : '\0';
        size_t next = i + 2;
        if (k == '$') out.push_back('$');
        else if (k >= '0' && k <= '8') {
            std::string name = k == '0' ? member(0).name() : rpg.actors[k - '1'].name();
            if (!lenOk(name.size())) { ok = false; return ""; }
            out += name;
        } else if (k == 'G' || k == 'g') {
            std::string s = std::to_string(money());
            if (!lenOk(s.size())) { ok = false; return ""; }
            out += s;
        } else if (k == 'U' || k == 'u') {
            if (!lenOk(rpg.coinName.size())) { ok = false; return ""; }
            out += rpg.coinName;
        } else if (k == 'R' || k == 'r') {
            if (!lenOk(1)) { ok = false; return ""; }
            out += "\n";
        } else if (k == 'W' || k == 'w') {
            std::string d = in.substr(next, 2);
            next += std::min<size_t>(2, in.size() - std::min(next, in.size()));
            msgWait = std::max(0, std::min(99, atoi(d.c_str())));
        } else if (k == 'S' || k == 's') {
            int d = next < in.size() ? in[next] - '0' : -1;
            next++;
            if (partySize > 1 && d > 0 && d < 5) {
                std::string s = plural[(k == 's' ? 4 : 0) + d - 1];
                if (!lenOk(s.size())) { ok = false; return ""; }
                out += s;
            }
        } else if (k == 'N' || k == 'n') {
            int n = std::max(1, std::min(8, partySize));
            out += (k == 'N' ? fullDigits : kanji)[n - 1];
        }
        if (k == '\0') break;
        i = next;
    }
    return out;
}

void Game::cmdMessage(const EventCommand& c, const EventCommand* next) {
    int y = (c.a2 & 2) ? 0x168 : 0x20;
    if (RpgWindow* w = win(0)) {
        if (w->y != y) closeWindow(0, true);
    }
    if (!win(0)) {
        int w = 0x1c8;
        createWindow(0, (640 - w) / 2, y, w, 0x5c, nullptr, -1);
    }
    bool ok;
    std::string t = expandText(script.str(c.a1), ok);
    win(0)->setText(1, ok ? t : script.str(c.a1));
    pumpMessages();
    if (msgWait < 0) {
        if (!next || next->code != 2) waitTenths((c.a2 & 1) ? 0 : msgSpeed);
    } else {
        waitTenths(msgWait);
    }
}

int Game::cmdChoice(const EventCommand& c) {
    flushKeys(false);
    std::string joined;
    int maxLen = 0;
    const std::string& src = script.str(c.a1);
    for (int i = 0; i < c.a2; i++) {
        // FUN_0042ffe1: i-th comma separated field, leading spaces/tabs skipped
        std::string field;
        size_t p = 0;
        int idx = 0;
        bool found = false;
        while (true) {
            while (p < src.size() && (src[p] == ' ' || src[p] == '\t')) p++;
            std::string f;
            while (p < src.size() && src[p] != ',') { f.push_back(src[p]); p++; }
            if (p < src.size() && src[p] == ',') p++;
            if (idx == i) { field = f; found = true; break; }
            idx++;
            if (p >= src.size()) break;
        }
        int len = found ? sjisLength(field) : -1;
        if (len > maxLen) maxLen = len;
        joined += field;
        joined += "\n";
    }
    bool ok;
    std::string t = expandText(joined, ok);
    menuWindow(-1, 100, 0x8c, c.a2, ok ? t : joined, maxLen, 0x11, 0, 6, 0, false);
    waitTenths(2);
    selectedIndex = -1;
    while (selectedIndex == -1 && !quit) waitTenths(0);
    closeWindow(6, true);
    int sel = selectedIndex < 0 ? 0 : selectedIndex;
    return (int)(((uint32_t)c.a3 >> (((3 - sel) * 8) & 0x1f)) & 0xff);
}

void Game::changeLevel(const EventCommand& c) {
    int t = (int)((uint32_t)c.a3 >> 1) - 1;
    int k, end;
    if (t == -1) { k = 0; end = partySize; }
    else { k = partySlotOfActor(t); end = k + 1; }
    for (; k < end && k < 8; k++) {
        int L = rnd(c.a1, c.a2);
        Battler& m = member(k);
        int old = m.level();
        if (c.a3 & 1) L += old;
        if (L < 1) L = 1; else if (L > 100) L = 100;
        if (old < L) m.setExp((L - 1) * (L - 1) * 10);
        m.setLevel(L);
        int a = m.actorId();
        if (a < 0 || a > 7) a = 0;
        const ActorData& ad = rpg.actors[a];
        int16_t oldVit = m.vit(), oldPow = m.pow(), oldInt = m.intel();
        m.setPow(ad.curve(0, L));
        m.setVit(ad.curve(1, L));
        m.setIntel(ad.curve(2, L));
        m.setAgi(ad.curve(3, L));
        m.setLuck(ad.curve(4, L));
        if (m.pow() != oldPow) deriveStat(0, k);
        if (m.intel() != oldInt) m.setMaxMp((int16_t)((int)rng().next() % 5) + (m.intel() - 1) * 10);
        if (m.vit() != oldVit) {
            m.setMaxHp((int16_t)((int)rng().next() % 5) + m.vit() * 10);
            deriveStat(1, k);
        }
        if (m.maxHp() < m.hp()) m.setHp(m.maxHp());
        if (m.maxMp() < m.mp()) m.setMp(m.maxMp());
    }
}

void Game::deriveStat(int which, int k) {
    Battler& m = member(k);
    int a = m.actorId();
    if (a < 0 || a > 7) a = 0;
    if (which == 0) {
        int16_t v = m.pow();
        int it = pb.equip[a][0];
        if (it != -1 && it >= 0 && it < 100) v = (int16_t)(v + items[it].power());
        if (v > 9999) v = 9999;
        m.setAttack(v);
    } else if (which == 1) {
        int16_t v = m.vit();
        for (int s = 1; s <= 3; s++) {
            int it = pb.equip[a][s];
            if (it != -1 && it >= 0 && it < 100) v = (int16_t)(v + items[it].power());
        }
        if (v > 9999) v = 9999;
        m.setDefence(v);
    }
}

void Game::changeCondition(int k, int op) {
    if (k < 0 || k > 7) return;
    Battler& m = member(k);
    if (m.hp() <= 0) return;
    if (op == 0) m.setStatus(0);
    else if (op == 1) m.setStatus(m.status() | 1);
    else if (op == 2) m.setStatus(m.status() | 2);
    else if (op == 3) { m.setHp(0); m.setStatus(0); }
}

void Game::changeParty(uint8_t mask, int mode) {
    uint8_t cur = 0;
    for (int i = 0; i < partySize; i++) cur |= 1 << (partyActor[i] & 7);
    if (mode != 2) {
        uint8_t n = 0;
        for (int a = 0; a < 8; a++) {
            bool in = (mask >> a) & 1;
            if (!in) n |= (1 << a) & cur;
            else if (mode != 1) n |= 1 << a;
        }
        mask = n;
    }
    if (mask == 0) return;
    bool used[8] = {};
    int order[8];
    for (int i = 0; i < 8; i++) order[i] = i < partySize ? partyActor[i] : -1;
    for (int a = 0; a < 8; a++) {
        if (!((mask >> a) & 1)) continue;
        for (int i = 0; i < 8; i++) {
            if (order[i] == a) { used[i] = true; break; }
            if (order[i] == -1) { order[i] = a; used[i] = true; break; }
        }
    }
    Chara* lead = mapParty[0];
    int lx = lead->x, ly = lead->y, ldx = lead->destX, ldy = lead->destY, lstep = lead->step, lface = lead->facing,
        lmotion = lead->motion, ltype = lead->moveType;
    Chara saved = *lead;
    int oldTrail0 = trail[0];
    bool idle = lstep == 0;
    Battler tmp[8];
    bool tmpUsed[8];
    for (int i = 0; i < 8; i++) {
        int a = member(i).actorId() & 7;
        tmp[a] = member(i);
        tmpUsed[a] = true;
    }
    partyMask = 0;
    int cnt = 0;
    for (int i = 0; i < 8; i++) {
        int a = order[i];
        if (!used[i] || a < 0) continue;
        partyActor[cnt] = (int8_t)a;
        if (!riding) {
            Chara& c = partyChara[a];
            mapParty[cnt] = &c;
            c.x = lx; c.y = ly; c.facing = lface; c.dirOverride = lface; c.moveType = ltype;
            if (cnt == 0) {
                int g = c.graphic;
                (void)ldx; (void)ldy; (void)lmotion;
                c = saved;
                c.graphic = g;
            } else {
                c.destX = ly;  // original bug: +0x0c is written with x then y, +0x10 untouched
                c.step = 0;
                c.motion = -1;
            }
        }
        member(cnt) = tmp[a];
        tmpUsed[a] = false;
        partyMask |= 1 << a;
        cnt++;
    }
    int k = cnt;
    for (int a = 0; a < 8; a++) {
        trail[a] = (a != 0 || idle) ? -1 : oldTrail0;
        if (tmpUsed[a]) { member(k++) = tmp[a]; }
    }
    partySize = cnt;
    pb.fightingSize = std::min(cnt, 4);
    if (!riding) {
        mapCharaCount = cnt;
        heroChara = mapParty[0];
    }
    pageRefresh = true;
}

void Game::teleportListChange(int m, int add) {
    if (add == 0) {
        int i = 0;
        while (i < 100 && teleportList[i] != m) i++;
        if (i == 100) return;
        teleportList[i] = 0;
        while (++i < 100 && teleportList[i] > 0) {
            teleportList[i - 1] = teleportList[i];
            teleportList[i] = 0;
        }
    } else if (m >= 0 && m < 500 && rpg.maps[m].type == kMapArea) {
        int i;
        for (i = 0; i < 100 && teleportList[i] > 0; i++)
            if (teleportList[i] == m) return;
        if (i != 100) teleportList[i] = (int16_t)m;
    }
}

// ------------------------------------------------------------------------------------------

void Game::runEvent(int id, Chara* ev) {
    LOG_DEBUG("runEvent %d", id);
    runEventId = id;
    eventRunning = true;
    suspend = false;
    if (!loadEventScript(gameDir, id, script)) {
        eventRunning = false;
        return;
    }
    int page = 0, index = 0;
    if (id == 0) {
        if (waitingId == 0) { page = waitingPage; index = waitingIndex; waitingId = -1; }
    } else if (id == 99999) {
        if (waitingId == 99999) { page = waitingPage; index = waitingIndex; waitingId = -1; }
        else {
            Chara tmp;
            tmp.isEvent = true;
            EventHeader h;
            if (loadEventHeader(gameDir, 99999, h)) {
                tmp.attrs = h.attrs;
                for (int p = 0; p < 4; p++) tmp.pageRoutes[p] = h.pageRoutes[p];
                selectPage(&tmp, itemCounts, (uint32_t)money());
            }
            page = tmp.page < 0 ? 0 : tmp.page;
        }
    } else if (!ev) {
        page = waitingPage; index = waitingIndex; waitingId = -1;
    } else {
        page = ev->page < 0 ? 0 : ev->page;
        index = ev->resumeIndex;
        ev->resumeIndex = 0;
        if (ev->id == (uint16_t)waitingId) waitingId = -1;
    }
    if (waitingChara == ev) {
        waitingId = -1;
        waitingChara = nullptr;
        waitingPage = 0;
        waitingIndex = 0;
    }
    runningEvent = id;
    std::vector<EventCommand>& list = script.pages[page & 3];
    size_t ci = (size_t)index;
    int lastCode = 0;
    while (ci < list.size() && list[ci].code != 0 && !quit) {
        index++;
        EventCommand* c = &list[ci];
        int code = c->code;
        lastCode = code;
        int next = ci + 1 < list.size() ? list[ci + 1].code : 0;
        if ((code == 0x21 && next != 0x23) || code == 0x2f || code == 0x30) {
            waitingId = id;
            waitingPage = page;
            waitingIndex = index;
            waitingChara = code == 0x2f ? nullptr : ev;
            suspend = true;
            if (ev) ev->resumeIndex = 0;
        } else if (ev && code == 0x23 && next != 0x21) {
            ev->resumeIndex = index;
            suspend = true;
        }
        int r = execCommand(c, ev, list, ci);
        if (r == -1) {
            int depth = (int16_t)(c->a4 >> 16);
            while (ci < list.size() && list[ci].code != 0 && !(list[ci].code == 3 && (int16_t)(list[ci].a4 >> 16) == depth - 1)) {
                ci++;
                index++;
            }
        } else if (r > 0) {
            while (ci < list.size() && list[ci].code != 0 && !(list[ci].code == 0xc && (int16_t)list[ci].a4 == r)) {
                ci++;
                index++;
            }
        }
        if (code == 5 && heroChara != ev) ev = nullptr;
        if (code == 0x21) {
            applyRouteDisplay(heroChara);
            for (int i = 0; i < 8; i++) partyChara[i].display = heroChara->display;
        }
        if (code == 0x23 && ev) applyRouteDisplay(ev);
        if ((code == 4 && battleLost) || suspend) break;
        if (ci >= list.size()) break;
        ci++;
    }
    (void)lastCode;
    closeWindow(0, false);   // 00430321 also destroys the message window
    scriptLoaded = false;
    if (ev) ev->cooldown = ev->trigger() == 2 ? -1 : 10;
    closeWindow(0, true);
    bool busy = screenState == 3 && (!heroChara->route.empty() || (ev && !ev->route.empty()));
    if (!quit && !partyDown() && !busy) showMap();
    eventRunning = false;
}

int Game::execCommand(EventCommand* c, Chara*& ev, std::vector<EventCommand>& list, size_t index) {
    Chara* h = heroChara;
    const EventCommand* nextCmd = index + 1 < list.size() ? &list[index + 1] : nullptr;
    switch (c->code) {
    case 1:
        cmdMessage(*c, nextCmd);
        break;
    case 2:
        return cmdChoice(*c);
    case 4: {
        escapeForbidden = c->a3;
        if (c->a2 == 0) c->a2 = c->a1;
        int p;
        if (c->a1 == c->a2) p = c->a1 - 1;
        else {
            int n = c->a2 - c->a1 + 1;
            p = (n == 0 ? 0 : (int)rng().next() % n) + c->a1 - 1;
        }
        startBattle(p);
        break;
    }
    case 5:
        postAction = 5;
        teleport(c->a1, c->a2, c->a3);
        abortTick = true;
        break;
    case 6: {
        AudioEntry e;
        e.a = c->a2; e.b = c->a3;
        playBgm(script.str(c->a1), &e, false);
        break;
    }
    case 10:
        changeValue(&money(), c->a1, c->a2, c->a3, 0, 100000000);
        pageRefresh = true;
        break;
    case 11: {
        int t = (int)((uint32_t)c->a3 >> 1);
        if (t == 0) {
            for (int k = 0; k < partySize; k++) { int32_t v = member(k).exp(); changeValue(&v, c->a1, c->a2, c->a3 & 1, 0, 100000); member(k).setExp(v); }
        } else {
            int k = partySlotOfActor(t - 1);
            if (k < 8) { int32_t v = member(k).exp(); changeValue(&v, c->a1, c->a2, c->a3 & 1, 0, 100000); member(k).setExp(v); }
        }
        break;
    }
    case 12:
        return -1;
    case 13:
        closeWindow(0, true);
        waitTenths(c->a1);
        break;
    case 14:
        playMovie(*c);
        break;
    case 15:
        if ((getSwitch(c->a1) ? (1 << (c->a1 & 31)) : 0) != c->a2) {   // 0041ba40 returns the masked bit
            setSwitch(c->a1, c->a2 == 1);
            pageRefresh = true;
        }
        break;
    case 16:
    case 17: {
        int t = (int)((uint32_t)c->a3 >> 1);
        auto apply = [&](int k) {
            Battler& m = member(k);
            if (c->code == 16) {
                int16_t v = m.hp();
                changeValue16(&v, c->a1, c->a2, c->a3 & 1, 0, m.maxHp());
                m.setHp(v);
                if (m.hp() < 1) m.setStatus(0);
            } else {
                int16_t v = m.mp();
                changeValue16(&v, c->a1, c->a2, c->a3 & 1, 0, m.maxMp());
                m.setMp(v);
            }
        };
        if (t == 0) for (int k = 0; k < partySize; k++) apply(k);
        else { int k = partySlotOfActor(t - 1); if (k < 8) apply(k); }
        break;
    }
    case 18:
        if (c->a2 >= 0 && c->a2 < 100) {
            if (c->a1 == 0) for (int k = 0; k < partySize; k++) pb.learn[member(k).actorId() & 7][c->a2] = 1;
            else if (c->a1 >= 1 && c->a1 <= 8) pb.learn[c->a1 - 1][c->a2] = 1;
        }
        break;
    case 19:
        changeLevel(*c);
        break;
    case 20: {
        int it = (int)((uint32_t)c->a3 >> 1);
        if (it >= 0 && it < 100) changeValue16(&itemCounts[it], c->a1, c->a2, c->a3 & 1, 0, rpg.itemMax);
        pageRefresh = true;
        break;
    }
    case 21: {
        AudioEntry e;
        e.a = c->a2; e.b = c->a3;
        playSe(script.str(c->a1), &e);
        break;
    }
    case 22:
        if (c->a1 == 0) for (int k = 0; k < partySize; k++) changeCondition(k, c->a2);
        else changeCondition(partySlotOfActor(c->a1 - 1), c->a2);
        break;
    case 23:
        changeParty((uint8_t)c->a1, c->a2);
        break;
    case 24: {
        auto rec = [&](int k) {
            if (k > 7) return;
            Battler& m = member(k);
            m.setHp((int16_t)(m.hp() + (int16_t)(((c->a2 * -25 + 100) * m.maxHp()) / 100)));
            if (m.maxHp() < m.hp()) m.setHp(m.maxHp());
            m.setMp((int16_t)(m.mp() + (int16_t)(((c->a2 * -25 + 100) * m.maxMp()) / 100)));
            if (m.maxMp() < m.mp()) m.setMp(m.maxMp());
        };
        if (c->a1 == 0) for (int k = 0; k < partySize; k++) rec(k);
        else rec(partySlotOfActor(c->a1 - 1));
        break;
    }
    case 26: showPicture(*c); break;
    case 27: inn(*c); break;
    case 28: shop(*c); break;
    case 29: screenTone(*c); break;
    case 30:
        stopMusic();
        fadeToBlack(5);
        quit = true;
        abortTick = true;
        break;
    case 31: erasePicture(c->a1); break;
    case 32: playVehicleBgm(heroChara->vehicle, false); break;
    case 33:
    case 35: {
        closeWindow(0, true);
        Chara* t = c->code == 33 ? h : ev;
        if (t && c->a1 >= 0 && c->a1 < (int)script.cmdRoutes.size()) {
            const MoveRoute& r = script.cmdRoutes[c->a1];
            t->route = r;
            t->routeLen = (int)r.size();
            t->routeMode = kRouteActive;
            t->routePos = 0;
            t->routeStep = 1;
        }
        break;
    }
    case 36: moveControl = 2; break;
    case 37: if (ev) moveControl = 3; break;
    case 38: moveControl = 1; break;
    case 39: moveControl = 0; break;
    case 40:
    case 42: {
        int g = c->a2;
        if (c->code == 40) {
            int n = riding ? 1 : mapCharaCount;
            for (int i = 0; i < n; i++) if (mapParty[i]) mapParty[i]->graphic = g < 0x100 ? (g & 0x3f) : g;
        } else if (ev) {
            ev->graphic = g < 0x100 ? (g & 0x3f) : g;
        }
        if (screenState == 0) { closeAllWindows(true); redraw(-2, true); }
        break;
    }
    case 41:
    case 43: {
        Chara* t = c->code == 41 ? h : ev;
        if (!t) break;
        int v = rnd(c->a1, c->a2);
        if (c->a3 & 1) { Chara* e2 = ev ? ev : t; e2->speed += v; }
        else t->speed = v;
        if (t->speed < 1) t->speed = 1; else if (t->speed > 20) t->speed = 20;
        if (c->code == 41) for (int i = 0; i < 8; i++) partyChara[i].speed = t->speed;
        break;
    }
    case 44: if (ev) ev->moveType = c->a1; break;
    case 45: teleportListChange(c->a1, c->a2); break;
    case 46: teleportDisabled = (int16_t)c->a1; break;
    case 47:
        if (ev && !riding) {
            closeAllWindows(true);
            boardVehicle(ev);
            moveControl = 1;
        }
        break;
    case 48:
        if (riding) {
            closeAllWindows(true);
            if (!leaveVehicle()) { waitingId = -1; suspend = false; }
        }
        break;
    case 49: {
        int s = ((uint32_t)c->a3 >> 8) & 0xff;
        if (s < 12) {
            audioSlots[s].name = script.str(c->a1);
            audioSlots[s].a = c->a2;
            audioSlots[s].b = c->a3;
        }
        break;
    }
    case 50:
        if (c->a1 >= 0 && c->a1 < 3) transitions[c->a1] = c->a2;
        break;
    case 51:
        if (!riding) {
            for (int i = 0; i < partySize; i++) mapParty[i]->graphic = rpg.actors[partyActor[i] & 7].graphic();
        } else restoreVehicleGraphic(mapParty[0]);
        if (screenState == 0) { closeAllWindows(true); redraw(-2, true); }
        break;
    case 52:
        if (c->a1 == 0xb) savingEnabled = false;
        else if (c->a1 == 0xc) savingEnabled = true;
        else saveDialog();
        break;
    case 53:
        for (int i = 0; i < 8; i++) partyChara[i].vehicle = c->a1;
        break;
    case 54:
        removeVehicles();
        escapeMap = (int16_t)c->a1;
        break;
    case 34:
        escapeMap = (int16_t)c->a1;
        break;
    default:
        break;
    }
    return 0;
}

}  // namespace rm95
