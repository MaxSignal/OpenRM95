// Game object, start-up, title and the map main loop (FUN_0041e342, FUN_0041ec93, FUN_0041f744)
#include "game.h"

#include "../audio/audio.h"
#include "../platform/platform.h"
#include "../util/fs.h"
#include "../util/log.h"
#include "../util/reader.h"
#include "../util/rng.h"
#include "../util/sjis.h"
#include <algorithm>
#include <cstdarg>

namespace rm95 {

Game::Game() {}
Game::~Game() {}

std::string Game::sjisFmtS(const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return buf;
}

std::string Game::sjisFmt(const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return buf;
}

bool Game::load(const std::string& dir, std::string& error) {
    gameDir = dir;
    auto rpgs = fs::listByExtension(dir, ".rpg");
    if (rpgs.empty()) { error = "No *.RPG file found in " + dir; return false; }
    rpgPath = rpgs[0];
    if (!rpg.load(rpgPath, error)) return false;
    for (int i = 0; i < 12; i++) audioSlots[i] = rpg.audio[i];
    for (int i = 0; i < 3; i++) transitions[i] = rpg.transitions[i];
    msgSpeed = rpg.msgSpeed();
    transparentWindows = rpg.transparentWindows();
    savingEnabled = rpg.saveEnabled();
    S.load(dir);
    // FUN_0041ec93: EPARTY.DAT, MONSTPIC.DAT, CHARA/ITEM/MAGIC.DAT
    std::vector<uint8_t> d;
    if (fs::readFile(fs::find(dir, "EPARTY.DAT"), d)) {
        Reader r(d);
        for (int i = 0; i < 250; i++) r.bytes(troops[i].raw, 128);
    }
    if (fs::readFile(fs::find(dir, "MONSTPIC.DAT"), monstpic)) {
        size_t off = 0;
        while (off + 4 <= monstpic.size() && monstpicOffsets.size() < 100) {
            int w = monstpic[off] | monstpic[off + 1] << 8, h = monstpic[off + 2] | monstpic[off + 3] << 8;
            size_t next = off + 4 + (size_t)w * h + 0x400;
            if (next > monstpic.size()) break;
            monstpicOffsets.push_back(off);
            off = next;
        }
    }
    if (fs::readFile(fs::find(dir, "CHARA.DAT"), d)) { Reader r(d); for (int i = 0; i < 100; i++) r.bytes(monsters[i].raw, 0xb8); }
    if (fs::readFile(fs::find(dir, "ITEM.DAT"), d)) { Reader r(d); for (int i = 0; i < 100; i++) r.bytes(items[i].raw, 28); }
    if (fs::readFile(fs::find(dir, "MAGIC.DAT"), d)) { Reader r(d); for (int i = 0; i < 100; i++) r.bytes(magics[i].raw, 28); }
    loadAnimationTables();
    initPartyRecords();
    return true;
}

void Game::initPartyRecords() {
    // FUN_004225b6
    pb = PartyBlock{};
    pb.fightingSize = 0;
    for (int a = 0; a < 8; a++) if (rpg.actors[a].startMember()) pb.fightingSize++;
    int nm = 0, non = pb.fightingSize;
    for (int a = 0; a < 8; a++) {
        const ActorData& ad = rpg.actors[a];
        int k = ad.startMember() ? nm++ : non++;
        Battler& m = pb.members[k];
        m.setActorId(a);
        m.setLevel((int16_t)ad.startLevel());
        m.setHp((int16_t)ad.hp()); m.setMaxHp((int16_t)ad.hp());
        m.setMp((int16_t)ad.mp()); m.setMaxMp((int16_t)ad.mp());
        m.setVit((int16_t)ad.vit()); m.setPow((int16_t)ad.pow()); m.setIntel((int16_t)ad.intel());
        m.setLuck((int16_t)ad.luck()); m.setAgi((int16_t)ad.agi());
        m.setAttack((int16_t)ad.attack()); m.setDefence((int16_t)ad.defence());
        m.setStatus(0);
        memcpy(m.raw + 0x41, &ad.raw[0x341], 100);
        for (int s = 0; s < 5; s++) pb.equip[a][s] = ad.equip(s);
        int other = ad.equip(4);
        m.setMagicDef(other == -1 ? 0 : items[other & 0x7f].power());
        memcpy(pb.learn[a], &ad.raw[0x2dd], 100);
        m.setSleepCounter(0);
        m.setName(ad.name());
        m.setExp((ad.startLevel() - 1) * (ad.startLevel() - 1) * 10);
        m.set32(0xb0, 0);
        m.set32(0xb4, 0);
    }
    if (pb.fightingSize > 4) pb.fightingSize = 4;
    for (int i = 0; i < 100; i++) itemCounts[i] = rpg.startItems[i];
    pb.money = rpg.startMoney;
    // FUN_004217f7
    partySize = 0;
    partyMask = 0;
    for (int a = 0; a < 8; a++) {
        partyChara[a] = Chara();
        partyChara[a].graphic = rpg.actors[a].graphic();
        if (rpg.actors[a].startMember()) {
            partyChara[a].x = partyChara[a].destX = rpg.startX;
            partyChara[a].y = partyChara[a].destY = rpg.startY;
            partyActor[partySize] = (int8_t)a;
            partyMask |= 1 << a;
            mapParty[partySize] = &partyChara[a];
            partySize++;
        }
    }
    if (partySize == 0) mapParty[0] = &partyChara[0];
}

void Game::reloadPalette(bool fromFile) {
    // FUN_004352a6
    Display& d = display();
    if (!fromFile) {
        d.setEntries(d.base);
        return;
    }
    Image img;
    if (loadBmp(fs::find(gameDir, inBattle ? "BG01.BMP" : "CHARA.BMP"), img)) d.loadPaletteSource(img, true);
    d.effect(4, nullptr, false);
}

void Game::fadeToBlack(int mode) {
    // FUN_00432042
    display().effect(mode, 0, 0, 0, 0, false);
    display().primary.fill(display().indexOf(0));
    platform().markDirty();
    screenState = 3;
}

// ------------------------------------------------------------------------------------------
// vehicles

void Game::boardVehicle(Chara* ev) {
    // FUN_00421957 + the rest of event command 47
    int k = 0;
    bool fresh = true;
    while (k < 10 && vehicles[k].ch && vehicles[k].id != 0) {
        if (vehicles[k].id == ev->id) { fresh = false; break; }
        k++;
    }
    if (k < 10 && vehicles[k].id == 0 && vehicles[k].ch) fresh = true;
    if (k > 0) {
        int n = k;
        if (n == 10) {
            n = 9;
            Chara* last = vehicles[9].ch;
            vehicleStore.erase(std::remove_if(vehicleStore.begin(), vehicleStore.end(), [&](auto& p) { return p.get() == last; }), vehicleStore.end());
        }
        for (; n > 0; n--) vehicles[n] = vehicles[n - 1];
    }
    vehicles[0].ch = ev;
    vehicles[0].id = 0;
    vehicles[0].map = 0;
    if (fresh) {
        for (auto it = events.begin(); it != events.end(); ++it)
            if (it->get() == ev) { vehicleStore.push_back(std::move(*it)); events.erase(it); break; }
    }
    vehicles[0].map = curMap;
    Chara* h = heroChara;
    h->moveType = 6;
    startMotion(h, 0x11);
    h->display = kDispInvisible;
    moveControl = 1;
}

bool Game::leaveVehicle() {
    Chara* v = mapParty[0];
    int x = v->x, y = v->y;
    int fx = x + (v->facing == 1 ? 1 : v->facing == 3 ? -1 : 0);
    int fy = y + (v->facing == 2 ? 1 : v->facing == 0 ? -1 : 0);
    uint32_t cell = tileAt(x, y);
    if (v->vehicle == 5 || v->vehicle == 6) {
        int u = occupantTile(x, y);
        if (u == -2) return false;
        uint32_t t = u >= 0 ? (uint32_t)u : cell;
        if (!passable(1, atr[t & 0x3ff]) && !passable(2, atr[t & 0x3ff])) return false;
    }
    if (map->type == 0 && (fx < 0 || fx >= mapW || fy < 0 || fy >= mapH)) return false;
    fx = (mapW + fx) % mapW;
    fy = (fy + mapH) % mapH;
    int u = occupantTile(fx, fy);
    uint32_t t = u >= 0 ? (uint32_t)u : tileAt(fx, fy);
    if (!passable(0, atr[t & 0x3ff])) return false;
    if (u == -2) return false;
    mapCharaCount = partySize;
    for (int i = 0; i < mapCharaCount; i++) {
        int a = partyActor[i];
        mapParty[i] = &partyChara[a];
        partyChara[a].x = partyChara[a].destX = x;
        partyChara[a].y = partyChara[a].destY = y;
        partyChara[a].facing = v->facing;
        trail[i] = -1;
    }
    abortTick = true;
    mapParty[0]->moveType = 7;
    startMotion(mapParty[0], v->facing);
    mapParty[0]->display = kDispNormal;
    v->moveType = 0;
    moveControl = 1;
    riding = false;
    return true;
}

void Game::restoreVehicleGraphic(Chara* v) {
    if (v->page < 0) return;
    uint16_t g = v->attrs.graphic(v->page);
    v->graphic = g < 0x100 ? (g & 0x3f) : g;
}

void Game::removeVehicles() {
    for (int k = riding ? 1 : 0; k < 10; k++) {
        if (vehicles[k].ch) {
            Chara* c = vehicles[k].ch;
            vehicleStore.erase(std::remove_if(vehicleStore.begin(), vehicleStore.end(), [&](auto& p) { return p.get() == c; }), vehicleStore.end());
            vehicles[k] = VehicleSlot();
        }
    }
}

void Game::finishBoarding() {
    Chara* lead = mapParty[0];
    lead->step = 0;
    moveControl = 0;
    for (int i = 0; i < partySize; i++) { startMotion(mapParty[i], -1); trail[i] = -1; }
    mapCharaCount = 1;
    Chara* v = vehicles[0].ch;
    mapParty[0] = v;
    heroChara = v;
    v->moveType = 5;
    vehicles[0].id = v->id;
    v->id = 0;
    v->layer = v->vehicle > 4 ? 2 : 1;
    v->anim = 0x12;
    riding = true;
    redraw(-2, true);
    playVehicleBgm(v->vehicle, false);
    if (waitingId != -1) runEvent(waitingId, nullptr);
}

void Game::finishAlighting() {
    Chara* lead = mapParty[0];
    lead->moveType = 5;
    lead->step = 0;
    moveControl = 0;
    Chara* v = vehicles[0].ch;
    if (v) {
        applyPage(v);
        v->id = vehicles[0].id;
    }
    for (int i = 0; i < mapCharaCount; i++) { startMotion(mapParty[i], -1); trail[i] = -1; }
    heroChara = mapParty[0];
    redraw(-2, true);
    playVehicleBgm(mapParty[0]->vehicle, false);
    actionButton(1);
    if (waitingId != -1) {
        if (waitingChara) { waitingChara->resumeIndex = waitingIndex; waitingChara->page = (int8_t)waitingPage; }
        runEvent(waitingId, waitingChara);
    }
}

// ------------------------------------------------------------------------------------------
// title / start

void Game::logo(int which) { (void)which; }

int Game::titleSequence() {
    bool shown = false;
    while (!quit) {
        // FUN_00434a5c(2)
        Image img;
        std::string tp = fs::find(gameDir, "title.bmp");
        bool haveTitle = !tp.empty() && loadBmp(tp, img);
        Display& d = display();
        if (haveTitle) {
            d.blackBack();
            d.loadPaletteSource(img, true);
            Surface s = imageToSurface(img, d.hw);
            int w = std::min(img.width, 640), h = std::min(img.height, 480);
            int x = (640 - w) / 2, y = (480 - h) / 2;
            d.back.blit(x, y, s, Rect(0, 0, w, h), false);
            menuY = y + h - h / 4;
            d.show(0);
            if (!shown) waitTenths(0);
            flushKeys(true);
            shown = true;
        } else {
            menuY = 250;
        }
        eventRunning = true;
        menuWindow(-1, menuY, 0x8c, 2, "\x82\xcd\x82\xb6\x82\xdf\x82\xa9\x82\xe7\n\x91\xb1\x82\xab\x82\xa9\x82\xe7\n", 0, 0x11, 0, 6, 0, false);
        waitTenths(0);
        closeWindow(6, false);
        if (shown) {
            d.present();
            fadeToBlack(5);
            d.blackBack();
            d.primary.fill(d.indexOf(0));
            reloadPalette(true);
        }
        eventRunning = false;
        if (selectedIndex != 1) return 0;
        if (loadDialog()) return 1;
    }
    return 0;
}

void Game::run() {
    Display& d = display();
    // FUN_0041f744 start
    midiOnce = false;
    encounterSteps = 0;
    lastTickTime = 0;
    scrollStep = 0;
    skipTick = true;
    curMap = -1;
    loadMapGraphics();
    loadAtr();
    battleLost = false;
    heroChara = mapParty[0];
    mapCharaCount = partySize;
    events.clear();
    d.hide(0, true);
    screenState = 3;
    waitingId = -1;
    waitingIndex = waitingPage = 0;
    waitingChara = nullptr;
    riding = false;
    moveControl = 0;
    currentBgm.name = "\x89\x89\x91\x74\x92\xe2\x8e\x7e";
    savingEnabled = rpg.saveEnabled();
    eventRunning = true;
    // (the original shows two ASCII logos here, 3 s each)
    fadeToBlack(5);
    reloadPalette(true);
    int r;
    if (!startSave.empty() && loadGame(startSave)) { r = 1; eventRunning = false; }
    else r = titleSequence();
    if (quit) return;
    if (r == 0) {
        screenState = -1;
        curMap = -1;
        teleport(rpg.startMap, rpg.startX, rpg.startY);
        runEvent(0, nullptr);
        playVehicleBgm(heroChara->vehicle, false);
        showMap();
    } else {
        int m = curMap;
        curMap = -2;
        teleport(m, mapParty[0]->x, mapParty[0]->y);
        AudioEntry e; e.a = currentBgm.a; e.b = currentBgm.b;
        playBgm(currentBgm.name, &e, true);
        reloadPalette(false);
    }
    if (quit) return;
    flushKeys(false);
    lastKey = -1;
    mainLoop();
    d.hide(0, true);
}

// ------------------------------------------------------------------------------------------
// the map main loop (FUN_0041f744 from LAB_0041fd25)

void Game::mouseWalk() {
    // FUN_0041f6dd: mouse auto-walk
    Chara* h = mapParty[0];
    startMotion(h, mouseDir);
    if (h->motion != mouseDir) {
        mouseDir = 5;
        h->moveType = 5;
    }
}

void Game::mainLoop() {
    int scrollDir = -1;
    while (true) {
        if (quit) return;
        abortTick = false;
        postAction = -1;
        heroChara = mapParty[0];
        Chara* hero_ = heroChara;
        pumpMessages();
        if (quit) return;
        if (!pendingSave.empty() && !eventRunning && hero_ && hero_->step == 0) {
            saveGame(pendingSave);
            LOG_INFO("saved %s", pendingSave.c_str());
            pendingSave.clear();
        }
        if (testBattle > 0 && !eventRunning && hero_ && hero_->step == 0) {
            int t = testBattle;
            testBattle = 0;
            escapeForbidden = 0;
            startBattle(t - 1);
        }
        if (testEvent >= 0 && !eventRunning && hero_ && hero_->step == 0) {
            int t = testEvent;
            testEvent = -1;
            runEvent(t, nullptr);
        }
        bool toPost = false;
        if (abortTick) toPost = true;
        if (!toPost && menuRequested) {
            flushKeys(false);
            runMenu();
            flushKeys(false);
            menuRequested = false;
            if (quit) return;
            if (abortTick) toPost = true;
        }
        if (!toPost && (eventRunning || inBattle || busy96)) { platform().sleep(1); continue; }
        if (!toPost) {
            hero_ = heroChara;
            if (hero_->motion == -1 && !skipTick) {
                bool actionKey = moveControl <= 1 && hero_->routeMode == kRouteNone && keyReleased &&
                                 (lastKey == VK_SPACE_ || lastKey == VK_RETURN_);
                if (actionKey) {
                    if (!riding || hero_->moveType == 7) actionButton(0);
                    else leaveVehicle();
                    if (abortTick) toPost = true;
                }
                if (!toPost) {
                    hero_ = heroChara;
                    if (hero_->routeMode == kRouteNone || hero_->moveType == 6 || hero_->moveType == 7) {
                        if (hero_->moveType == 5 && moveControl < 2) {
                            if (keyHeldAsync(VK_UP_) || keyHeldAsync(VK_NUMPAD8_)) startMotion(hero_, 0);
                            else if (keyHeldAsync(VK_LEFT_) || keyHeldAsync(VK_NUMPAD4_)) startMotion(hero_, 3);
                            else if (keyHeldAsync(VK_DOWN_) || keyHeldAsync(VK_NUMPAD2_)) startMotion(hero_, 2);
                            else if (keyHeldAsync(VK_RIGHT_) || keyHeldAsync(VK_NUMPAD6_)) startMotion(hero_, 1);
                        } else if (hero_->moveType == 6 || hero_->moveType == 7) {
                            startMotion(hero_, 0x11);
                        } else if (hero_->moveType == 9) {
                            mouseWalk();
                        } else if (hero_->moveType == 3) {
                            startMotion(hero_, (int)(rng().next() & 3));
                        }
                    } else {
                        int rr = nextMove(hero_, 0, 0);
                        if (hero_->moveType != 6 && hero_->moveType != 7)
                            for (int i = 0; i < 8; i++) { partyChara[i].display = hero_->display; partyChara[i].anim = hero_->anim; }
                        if (rr && screenState == 0) { closeAllWindows(true); redraw(-2, true); }
                        if (rr && waitingId != -1) {
                            if (waitingChara) { waitingChara->resumeIndex = waitingIndex; waitingChara->page = (int8_t)waitingPage; }
                            runEvent(waitingId, waitingChara);
                            if (abortTick) toPost = true;
                        }
                    }
                }
            }
        }
        bool toEnd = false;
        if (!toPost) {
            lastKey = -1;
            uint32_t now = platform().ticks();
            if (now - lastTickTime <= 20) { toEnd = true; }
            else {
                lastTickTime = now;
                hero_ = heroChara;
                hero_->speedCounter++;
                bool doEvents = true;
                if (hero_->speedCounter >= hero_->speed) {
                    hero_->speedCounter = 0;
                    Chara* e = advanceMotion(hero_);
                    if (e) {
                        scrollDir = -1;
                        touch(e);
                        if (abortTick) { toPost = true; doEvents = false; }
                    } else if (hero_->step == 1) {
                        scrollDir = (hero_->motion >= 0 && hero_->motion <= 3) ? hero_->motion : -1;
                        pushTrail();
                        startFollowers();
                        Chara* last = mapParty[mapCharaCount - 1];
                        if (hero_->moveType == 6 && last && last->motion == 0x11) { postAction = 0x2f; toPost = true; doEvents = false; }
                        else if (hero_->moveType == 7 && last && last->motion == 0x11) { postAction = 0x30; toPost = true; doEvents = false; }
                    }
                    if (doEvents && !toPost) {
                        for (int i = 1; i < mapCharaCount; i++) {
                            advanceMotion(mapParty[i]);
                            if (hero_->moveType == 6 && mapParty[i - 1] && mapParty[i - 1]->display == kDispInvisible)
                                mapParty[i]->facing = hero_->facing;
                        }
                    }
                }
                if (doEvents && !toPost) {
                    if (!skipTick) {
                        for (int k = riding ? 1 : 0; k < 10; k++) {
                            if (!vehicles[k].ch || vehicles[k].map != curMap) break;
                            updateEvent(vehicles[k].ch);
                        }
                        for (size_t i = 0; i < events.size(); i++) {
                            updateEvent(events[i].get());
                            if (abortTick) { toPost = true; break; }
                        }
                    }
                }
                if (doEvents && !toPost) {
                    scrollStep = scrollDir == -1 ? 0 : heroChara->step;
                    clearGrid();
                    redraw(scrollDir, true);
                    for (size_t i = 0; i < events.size(); i++) {
                        Chara* ev = events[i].get();
                        if (ev->cooldown == 0 && ev->trigger() == 2 && ev->onScreen) runEvent(ev->id, ev);
                        if (abortTick) { toPost = true; break; }
                    }
                    if (!toPost) {
                        for (int k = riding ? 1 : 0; k < 10; k++) {
                            Chara* v = vehicles[k].ch;
                            if (!v || vehicles[k].map != curMap) break;
                            if (v->cooldown == 0 && v->trigger() == 2 && v->onScreen) runEvent(v->id, v);
                            if (abortTick) { toPost = true; break; }
                        }
                    }
                    if (!toPost) {
                        skipTick = false;
                        if (scrollStep == 4) {
                            scrollDir = -1;
                            computeScrollFlags();
                            flushKeys(false);
                            scrollStep = 0;
                            actionButton(1);
                            if (!abortTick) {
                                poisonStep();
                                if (!partyDown()) {
                                    Chara* h2 = heroChara;
                                    if (h2->display == kDispNormal && h2->routeMode == kRouteNone)
                                        encounterStep(h2->y * mapW + h2->x);
                                }
                            }
                        }
                        toEnd = true;
                    }
                }
            }
        }
        if (toPost && !toEnd) {
            if (quit) return;
            if (postAction == 0x2f) finishBoarding();
            else if (postAction == 0x30) finishAlighting();
        }
        // LAB_00420703
        if (battleLost || partyDown()) {
            runEvent(99999, nullptr);
            if (quit) return;
            if (battleLost && screenState == 3) {
                playVehicleBgm(heroChara->vehicle, false);
                redrawTiles();
                drawCharas(false);
                display().show(transitions[1]);
                screenState = 0;
            }
            battleLost = false;
        }
        if (pageRefresh) {
            pageRefresh = false;
            refreshPages();
        }
        postAction = -1;
        lastKey = -1;
        platform().sleep(1);
    }
}

}  // namespace rm95
