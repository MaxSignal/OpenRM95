// Save files (*.sav) compatible with GAME.EXE 1.23: writer FUN_00424576, reader FUN_00424957.
// Layout: spec/ui.md §7.
#include "game.h"
#include "jtext.h"
#include "../platform/platform.h"
#include "../util/fs.h"
#include "../util/log.h"
#include "../util/reader.h"
#include "../util/sjis.h"
#include <algorithm>

namespace rm95 {

static const char kSaveMagic[] = "RPG\xc2\xb8\xb0\xd9\x39\x35";  // "RPGﾂｸｰﾙ95"

// ------------------------------------------------------------------------------------------
// Chara (0x6e bytes in memory) <-> our struct

static void putCharaInts(Writer& w, const Chara& c) {
    // 16 ints from +0x1a
    const int32_t v[16] = {c.dirOverride, c.facing, c.moveType, c.motion, c.graphic, c.unk2e, c.step, c.animTimer,
                           c.animFrame, c.layer, c.speed, c.speedCounter, c.vehicle, c.anim, c.display, c.cooldown};
    for (int32_t x : v) w.s32(x);
}

static void getCharaInts(Reader& r, Chara& c) {
    int32_t* v[16] = {&c.dirOverride, &c.facing, &c.moveType, &c.motion, &c.graphic, &c.unk2e, &c.step, &c.animTimer,
                      &c.animFrame, &c.layer, &c.speed, &c.speedCounter, &c.vehicle, &c.anim, &c.display, &c.cooldown};
    for (int32_t* x : v) *x = r.s32();
}

static void writeRawChara(Writer& w, const Chara& c) {
    w.u32(0x465050);  // vtable
    w.s32(c.x); w.s32(c.y); w.s32(c.destX); w.s32(c.destY); w.s32(c.onScreen);
    w.u16(c.id);
    putCharaInts(w, c);
    w.s32(c.routeLen); w.s32(c.routePos); w.s32(c.routeMode); w.s32(c.routeStep);
    w.u32(c.route.empty() ? 0 : 0x00a00000);  // route pointer (only zero/non-zero matters)
}

static void readRawChara(Reader& r, Chara& c) {
    r.u32();
    c.x = r.s32(); c.y = r.s32(); c.destX = r.s32(); c.destY = r.s32(); c.onScreen = r.s32();
    c.id = r.u16();
    getCharaInts(r, c);
    c.routeLen = r.s32(); c.routePos = r.s32(); c.routeMode = r.s32(); c.routeStep = r.s32();
    r.u32();
    c.route.clear();  // the route of a party character is not saved (the pointer would dangle)
    c.routeLen = 0;
}

static void writeEChara(Writer& w, const Chara& c) {
    // EChara::Save FUN_0041dc09 (+ Chara::Save FUN_0041c80b)
    w.s32(c.x); w.s32(c.y); w.s32(c.destX); w.s32(c.destY); w.s32(c.onScreen);
    w.u16(c.id);
    putCharaInts(w, c);
    bool hasRoute = !c.route.empty();
    w.s32(hasRoute ? (int32_t)c.route.size() : c.routeLen);
    w.s32(c.routePos); w.s32(c.routeMode); w.s32(c.routeStep);
    w.u32(hasRoute ? 0x00a00000 : 0);
    if (hasRoute) w.bytes(c.route.data(), c.route.size());
    w.s32(c.resumeIndex);
    w.u8((uint8_t)c.page);
    w.bytes(c.attrs.raw.data(), 0x58);
    for (int p = 0; p < 4; p++) {
        const MoveRoute& m = c.pageRoutes[p];
        uint8_t n = (uint8_t)std::min<size_t>(m.size(), 255);
        w.u8(n);
        w.bytes(m.data(), n);
    }
}

static void readEChara(Reader& r, Chara& c) {
    c.x = r.s32(); c.y = r.s32(); c.destX = r.s32(); c.destY = r.s32(); c.onScreen = r.s32();
    c.id = r.u16();
    getCharaInts(r, c);
    c.routeLen = r.s32(); c.routePos = r.s32(); c.routeMode = r.s32(); c.routeStep = r.s32();
    uint32_t ptr = r.u32();
    c.route.clear();
    if (ptr != 0 && c.routeLen > 0) c.route = r.vec((size_t)c.routeLen);
    c.resumeIndex = r.s32();
    c.page = (int8_t)r.u8();
    r.bytes(c.attrs.raw.data(), 0x58);
    for (int p = 0; p < 4; p++) {
        uint8_t n = r.u8();
        c.pageRoutes[p] = r.vec(n);
    }
    c.isEvent = true;
    c.next = nullptr;
}

// ------------------------------------------------------------------------------------------

bool Game::saveGame(const std::string& path) {
    // FUN_00424576
    Writer w;
    w.fixedString(kSaveMagic, 0x1e);
    w.fixedString("1.23", 5);
    w.s32(curMap);
    for (int i = 0; i < 10; i++) {
        w.u16(vehicles[i].id);
        w.s32(vehicles[i].map);
        w.u32(vehicles[i].ch ? 0x00b00000u + i : 0);
    }
    w.s32(riding ? 1 : 0);
    for (int i = 0; i < 10; i++)
        if (vehicles[i].ch) writeEChara(w, *vehicles[i].ch);
    w.s32(mapCharaCount);
    w.s32(partySize);
    w.bytes(partyActor, 8);
    for (int i = 0; i < 8; i++) writeRawChara(w, partyChara[i]);
    w.bytes(&pb, sizeof pb);
    w.bytes(itemCounts, 200);
    w.s32(pb.money);
    w.bytes(switches, 128);
    auto audioLine = [&](const std::string& name, int32_t a, int32_t b) {
        w.u32(0);
        w.s32(a);
        w.s32(b);
        w.bytes(name.data(), name.size());
        w.u8('\n');
    };
    for (int i = 0; i < 12; i++) audioLine(audioSlots[i].name, audioSlots[i].a, audioSlots[i].b);
    audioLine(currentBgm.name, currentBgm.a, currentBgm.b);
    for (int i = 0; i < 3; i++) w.s32(transitions[i]);
    Display& d = display();
    w.bytes(d.tint, 4);
    const Palette& pal = haveSavedPalette ? savedPalette : d.base;
    for (int i = 0; i < 256; i++) { w.u8(pal[i].r); w.u8(pal[i].g); w.u8(pal[i].b); w.u8(0); }
    for (int i = 0; i < 100; i++) w.s16(teleportList[i]);
    w.s16(teleportDisabled);
    w.s16(escapeMap);
    w.s32(moveControl);
    return fs::writeFile(path, w.d);
}

bool Game::loadGame(const std::string& path) {
    // FUN_00424957 (after the file dialog)
    std::vector<uint8_t> data;
    if (!fs::readFile(path, data)) {
        LOG_ERROR("ファイルを開くことができません: %s", path.c_str());
        return false;
    }
    Reader r(data);
    std::string magic = r.fixedString(0x1e);
    std::string ver = r.fixedString(5);
    if (magic != kSaveMagic) {
        LOG_ERROR("現バージョンでこのデータを読み込むことはできません: %s", path.c_str());
        return false;
    }
    fadeToBlack(5);
    deleteMapEvents();
    vehicleStore.clear();
    curMap = r.s32();
    uint32_t ptrs[10];
    for (int i = 0; i < 10; i++) {
        vehicles[i].id = r.u16();
        vehicles[i].map = r.s32();
        ptrs[i] = r.u32();
        vehicles[i].ch = nullptr;
    }
    riding = r.s32() != 0;
    for (int i = 0; i < 10; i++) {
        if (vehicles[i].id == 0) { vehicles[i].map = 0; continue; }
        auto c = std::make_unique<Chara>();
        readEChara(r, *c);
        vehicles[i].ch = c.get();
        vehicleStore.push_back(std::move(c));
    }
    (void)ptrs;
    mapCharaCount = r.s32();
    partySize = std::clamp(r.s32(), 0, 8);
    r.bytes(partyActor, 8);
    for (int i = 0; i < 8; i++) readRawChara(r, partyChara[i]);
    partyMask = 0;
    for (int i = 0; i < 8; i++) mapParty[i] = &partyChara[partyActor[i] & 7];
    if (riding && vehicles[0].ch) mapParty[0] = vehicles[0].ch;
    for (int i = 0; i < partySize; i++) partyMask |= 1 << (partyActor[i] & 7);
    heroChara = mapParty[0];
    r.bytes(&pb, sizeof pb);
    r.bytes(itemCounts, 200);
    pb.money = r.s32();
    r.bytes(switches, 128);
    auto audioLine = [&](std::string& name, int32_t& a, int32_t& b) {
        r.u32();
        a = r.s32();
        b = r.s32();
        std::string l = r.fgets(0x100);
        if (!l.empty()) l.pop_back();
        name = l;
    };
    for (int i = 0; i < 12; i++) audioLine(audioSlots[i].name, audioSlots[i].a, audioSlots[i].b);
    audioLine(currentBgm.name, currentBgm.a, currentBgm.b);
    for (int i = 0; i < 3; i++) transitions[i] = r.s32();
    Display& d = display();
    r.bytes(d.tint, 4);
    for (int i = 0; i < 256; i++) { d.base[i].r = r.u8(); d.base[i].g = r.u8(); d.base[i].b = r.u8(); r.u8(); }
    for (int i = 0; i < 100; i++) teleportList[i] = r.s16();
    teleportDisabled = r.s16();
    escapeMap = r.s16();
    moveControl = r.s32();
    waitingId = -1;
    waitingChara = nullptr;
    postAction = -1;
    for (int a = 0; a < 8; a++)
        for (int j = 0; j < 100; j++)
            if (pb.learn[a][j] != 1 || rpg.actors[a].learnLevel(j) != 0) pb.learn[a][j] = rpg.actors[a].learnLevel(j);
    LOG_INFO("loaded %s (saved by runtime %s)", path.c_str(), ver.c_str());
    return true;
}

// ------------------------------------------------------------------------------------------
// file selection (the original uses the Windows common file dialog)

std::string Game::chooseSaveFile(bool save) {
    std::vector<std::string> files;
    for (const std::string& f : fs::listByExtension(gameDir, ".sav")) {
        std::vector<uint8_t> d;
        if (fs::readFile(f, d) && d.size() >= 10 && memcmp(d.data(), kSaveMagic, 10) == 0) files.push_back(f);
    }
    std::vector<std::string> paths, labels;
    for (const std::string& f : files) {
        paths.push_back(f);
        labels.push_back(utf8ToSjis(fs::baseName(f)));
    }
    if (paths.size() > 9) {
        paths.erase(paths.begin(), paths.end() - 9);
        labels.erase(labels.begin(), labels.end() - 9);
    }
    std::string newName;
    if (save) {
        for (int n = 1; n < 1000; n++) {
            char b[32];
            snprintf(b, sizeof b, "SAVE%02d.sav", n);
            bool used = false;
            for (const std::string& f : files) if (fs::baseName(f) == b) used = true;
            if (!used && fs::find(gameDir, b).empty()) { newName = b; break; }
        }
        paths.push_back(fs::join(gameDir, newName));
        labels.push_back(J("（新しいファイル）"));
    }
    bool prevEvent = eventRunning;
    eventRunning = true;
    std::string result;
    if (paths.empty()) {
        sysMessage(J("セーブデータがありません"), 1.0, false);
        closeWindow(0, true);
    } else {
        std::string text;
        int maxLen = 6;
        for (const std::string& l : labels) { text += l + "\n"; maxLen = std::max(maxLen, sjisLength(l)); }
        createWindow(0, 0x5c, 0x20, 0x1c8, 0x5c, nullptr, -1);
        win(0)->setText(1, save ? J("ゲームを保存する") : J("ゲームを読み込む"));
        menuWindow(-1, 0x96, 0x8c, (int)paths.size(), text, maxLen, 0x11, 1, 6, 0, false);
        flushKeys(true);
        selectedIndex = -1;
        waitTenths(0);
        int sel = selectedIndex;
        closeWindow(6, true);
        closeWindow(0, true);
        if (sel >= 0 && sel < (int)paths.size()) result = paths[sel];
    }
    eventRunning = prevEvent;
    flushKeys(true);
    return result;
}

void Game::saveDialog() {
    // FUN_004247e5
    while (!quit) {
        std::string p = chooseSaveFile(true);
        if (p.empty()) return;
        if (saveGame(p)) { LOG_INFO("saved %s", p.c_str()); return; }
        bool prev = eventRunning;
        eventRunning = true;
        sysMessage(J("データを書き込むことができません"), 1.0, false);
        closeWindow(0, true);
        eventRunning = prev;
    }
}

bool Game::loadDialog() {
    std::string p = chooseSaveFile(false);
    if (p.empty()) return false;
    return loadGame(p);
}

}  // namespace rm95
