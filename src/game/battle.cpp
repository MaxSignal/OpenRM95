// Battle system: FUN_00434332 (driver), FUN_00422b13 (setup), FUN_0042cbbb (turn), formulas,
// enemy AI, animations, victory and level up. See spec/battle.md.
#include "game.h"
#include "jtext.h"
#include "../audio/audio.h"
#include "../platform/platform.h"
#include "../util/fs.h"
#include "../util/log.h"
#include "../util/rng.h"
#include "../util/sjis.h"
#include <algorithm>

namespace rm95 {

// ------------------------------------------------------------------------------------------
// animation tables (FUN_0042a34b)

void Game::loadAnimationTables() {
    static const int sound[42] = {0, 18, 19, 15, 16, 28, 24, 32, 21, 23, 22, 8, 8, 25, 48, 48, 48, 52, 33, 27, 29, 14,
                                  43, 3, 34, 18, 33, 52, 16, 16, 16, 46, 46, 46, 46, 34, 25, 46, 13, 8, 18, 2};
    static const int pos[42] = {-2, -2, -4, -1, -4, -4, -1, -3, -3, -3, -3, -1, -1, -2, -4, -4, -2, -2, -2, -2, -2,
                                -3, -2, -1, -2, -2, -2, -2, -4, -4, -2, -3, -2, -4, -2, -3, -1, -3, -3, -2, -4, -2};
    static const int rep[42] = {1, 1, 1, 2, 1, 1, 2, 1, 1, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 2, 1,
                                1, 1, 1, 1, 2, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1};
    static const int start[42] = {0, -1, 0, 0, 0, 0, 0, 0, -1, -1, -1, -1, -1, 0, 0, 0, 0, 0, 0, -1, -1,
                                  -1, -1, -1, -1, -1, -1, 0, 0, 0, -1, 0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1};
    static const int anmtab[66] = {1, 40, 4, 2, 35, 6, 7, 5, 8, 10, 29, 9, 11, 12, 36, 13, 21, 20, 27, 32, 16, 16,
                                   15, 14, 30, 28, 3, 2, 35, 6, 7, 5, 23, 10, 29, 9, 11, 12, 36, 13, 21, 20, 27, 32,
                                   16, 16, 15, 14, 19, 22, 34, 18, 24, 26, 20, 20, 20, 14, 33, 16, 31, 14, 14, 14, 37, 37};
    for (int f = 0; f < 42; f++) {
        anmFiles[f].rep = rep[f];
        anmFiles[f].posX = pos[f];
        anmFiles[f].posY = -1;
        anmFiles[f].start = start[f];
        anmFiles[f].sound = sound[f];
    }
    for (int a = 0; a < 100; a++) {
        BattleAnim& an = anims[a];
        an.count = (a >= 24 && a <= 47) ? 10 : 1;
        an.delay = a == 99 ? 1 : 100;
        bool randomStart = false;
        for (int k = 0; k < an.count; k++) {
            an.file[k] = a == 99 ? 0 : anmtab[a % 66];
            if (anmFiles[an.file[k]].start == -1) randomStart = true;
        }
        an.minLen = (randomStart && an.count >= 2) ? 10 : 3;
        int mode = 6, r = 0, g = 0, b = 0, gray = 0;
        if (a < 48) {
            int m = a % 24;
            if (m < 4) { mode = 6; r = 0xb4; }
            else if (m < 8) { mode = 6; b = 0xb4; }
            else if (m < 12) { mode = 1; r = g = 0xb4; }
            else if (m < 16) { mode = 1; r = g = b = 0xb4; }
            else if (m < 20) { mode = 6; r = 0x82; }
            else { mode = 6; r = g = b = 0xb4; }
        } else if (a < 57) {
            mode = 1; r = g = b = 0x82; gray = 1;
        } else {
            mode = 6; r = g = b = 0xb4;
        }
        an.flashMode = mode;
        an.r = (uint8_t)r; an.g = (uint8_t)g; an.b = (uint8_t)b; an.gray = (uint8_t)gray;
    }
    int c0 = 0, c1 = 0x18, c11 = 0x14, c12 = 0x2c;
    for (int i = 0; i < 100; i++) {
        int t = magics[i].type();
        int a;
        if (t == 0) { a = c0; if (++c0 > 0x13) c0 = 0; }
        else if (t == 1) { a = c1; if (++c1 > 0x2f) c1 = 0x18; }
        else if (t == 0xb) { a = c11; if (++c11 > 0x17) c11 = 0x14; }
        else if (t == 0xc) { a = c12; if (++c12 > 0x30) c12 = 0x2c; }
        else if (t < 0xb) a = t + 0x2e;
        else a = t + 0x2c;
        magics[i].set16(2, (uint16_t)(magics[i].u16(2) | a));
    }
}

// ------------------------------------------------------------------------------------------
// helpers

static void fileMissing(const std::string& path) {
    LOG_ERROR("ファイル[%s]が見つかりません", path.c_str());
}

Rect Game::enemyRect(int i) const {
    if (!troop || i < 0 || i >= 10) return Rect();
    int x = troop->monsterX(i), y = troop->monsterY(i);
    return Rect(x, y, x + enemyPic[i].width, y + enemyPic[i].height);
}

void Game::drawEnemies(int skip, int lowered, bool flip) {
    Display& d = display();
    d.back.blit(0, 0, d.cache, Rect(0, 0, 640, 480), false);
    for (int i = 0; i < enemyCount; i++) {
        if (enemies[i].hp() <= 0 || i == skip || !enemyPicValid[i]) continue;
        Rect r = enemyRect(i);
        d.back.blit(r.left, r.top + (i == lowered ? 30 : 0), enemyPic[i], Rect(0, 0, enemyPic[i].width, enemyPic[i].height), true);
    }
    if (flip) d.presentRect(battleRect);
}

bool Game::allEnemiesDead() {
    for (int i = 0; i < enemyCount; i++) if (enemies[i].hp() > 0) return false;
    return true;
}

bool Game::partyDefeated() {
    for (int i = 0; i < pb.fightingSize; i++)
        if (member(i).hp() > 0 && (member(i).status() & 2) == 0) return false;
    return true;
}

int Game::maxLuck() {
    // FUN_00427d90
    int m = 0;
    for (int i = 0; i < partySize; i++)
        if (member(i).hp() != 0 && m < member(i).luck()) m = member(i).luck();
    return m;
}

// ------------------------------------------------------------------------------------------
// battle driver (FUN_00434332)

void Game::startBattle(int t) {
    if (t < 0 || t >= 250 || troops[t].count() == 0) return;
    Display& d = display();
    inBattle = true;
    closeWindow(0, true);
    saveBgm();
    playBgm(J("演奏停止"), nullptr, false);
    bool mid = audioSlots[kAudioEncounter].name.find(".mid") != std::string::npos;
    if (mid) midiOnce = true;
    playVehicleBgm(kAudioEncounter, true);
    d.effect(1, 200, 200, 200, 0, false);
    d.hide(transitions[1], true);
    d.hide(0, true);
    screenState = 3;
    waitJingle(mid);
    playVehicleBgm(kAudioBattle, false);
    battleExit = 0;
    if (setupBattle(t)) {
        platform().clearEvents(true);
        while (!quit) {
            pumpMessages();
            if (quit || allEnemiesDead() || partyDefeated() || battleExit != 0) break;
            platform().sleep(1);
        }
        if (partyDefeated()) battleLost = true;
        closeAllWindows(true);
        freeBattle();
        d.hide(battleExit > 0 ? 0xd : transitions[2], true);
        screenState = 3;
        playBgm(J("演奏停止"), nullptr, false);
        for (int i = 0; i < 4; i++) { audio().waveStop(i); seChannelBusy[i] = 0; }
        if (quit) { inBattle = false; return; }
        loadMapGraphics();
        if (!battleLost && !abortTick && battleExit < 1) restoreBgm(false);
        for (int i = 0; i < 8; i++)
            if (member(i).hp() < 0) member(i).setStatus(0);
        if (battleLost) {
            inBattle = false;
            abortTick = true;
            return;
        }
    }
    if (!abortTick) {
        if (battleExit < 1) {
            inBattle = false;
            redrawTiles();
            drawCharas(false);
            d.show(transitions[1]);
            screenState = 0;
        } else {
            inBattle = false;
            int tr = transitions[0];
            transitions[0] = 0xd;
            MapData& m = rpg.maps[battleExit];
            teleport(m.parent, m.exitX, m.exitY);
            transitions[0] = tr;
            restoreBgm(false);
        }
    }
    inBattle = false;
    if (mapParty[0]) mapParty[0]->moveType = 5;
}

bool Game::setupBattle(int t) {
    // FUN_00422b13
    autoBattle = false;
    expTotal = goldTotal = 0;
    dropItem = -1;
    menuActor = 0;
    for (int i = 0; i < 8; i++) {
        savedStats[i][0] = member(i).attack();
        savedStats[i][1] = member(i).defence();
        savedStats[i][2] = member(i).agi();
    }
    troop = &troops[t];
    enemyCount = std::clamp((int)troop->count(), 0, 10);
    int i = 0;
    for (; i < enemyCount; i++) {
        int id = std::clamp((int)troop->monsterId(i), 0, 99);
        const Battler& c = monsters[id];
        Battler& e = enemies[i];
        e.setHp(c.maxHp());
        e.setMaxHp(c.maxHp());
        e.setMp(c.maxMp());
        e.setMaxMp(c.maxMp());
        e.setAgi(c.agi());
        e.set16(0x2a, c.s16(0x2a));
        e.setExp(c.exp());
        e.setGold(c.gold());
        e.set16(0x26, c.s16(0x26));
        e.setDefence(c.defence());
        e.setAttack(c.attack());
        memcpy(e.raw + 0x41, c.raw + 0x41, 100);
        memcpy(e.raw + 0xa5, c.raw + 0xa5, 10);
        e.setStatus(0);
        e.setSleepCounter(0);
        e.setName(c.name());
    }
    for (; i < 10; i++) enemies[i].setHp(0);
    for (int k = 0; k < 10; k++) { enemyPicValid[k] = false; enemyPic[k] = Surface(); }
    if (!loadBattleGraphics()) return false;
    waitTenths(3);
    createWindow(0, 0x5c, 10, 0x1c8, 0x5c, nullptr, -1);
    win(0)->setText(1, S[70]);
    platform().refresh(true);
    RpgWindow* st = createWindow(1, 0x14, 0x16a, 600, 100, nullptr, -1);
    st->setText(0xc, "Name");
    st->setText(0xd, "Hp");
    st->setText(0xe, "Mp");
    st->setText(0xf, "Status");
    battleStatus();
    st->setText(10, buildText(1, true));
    platform().refresh(true);
    static const int cmdStr[5] = {0, 1, 5, 6, 7};
    int maxLen = 0;
    for (int k : cmdStr) maxLen = std::max(maxLen, sjisLength(S[k]));
    WindowLayout l;
    l.l0 = 1; l.rows = 5; l.cols = 1; l.mode = 1; l.fmt = 0x10;
    l.text = buildText(2, false); l.hasText = true;
    createWindow(2, 10, 0x118, (maxLen + 1) * charW(), lineH() * 5 + 6, &l, 5);
    commandsDone = false;
    nextActor(false);
    battleCursor();
    if ((member(std::min(menuActor, 7)).status() & 4) && win(2)) win(2)->setDisabled(2, true);
    platform().refresh(true);
    battleRect = Rect(0, win(0)->y + win(0)->h, 640, win(1)->y);
    return true;
}

bool Game::loadBattleGraphics() {
    // FUN_00423188 + FUN_0042298f
    Display& d = display();
    char name[32];
    snprintf(name, sizeof name, "bg%02d.bmp", (troop->background() & 0xffff) + 1);
    std::string path = fs::find(gameDir, name);
    Image bg;
    if (path.empty() || !loadBmp(path, bg)) { fileMissing(fs::join(gameDir, name)); return false; }
    d.loadPaletteSource(bg, false);
    Surface bs = imageToSurface(bg, d.hw);
    d.cache.fill(d.indexOf(0));
    d.cache.blit(0, 0, bs, Rect(0, 0, bs.width, bs.height), false);
    if (monstpic.empty()) { fileMissing("MONSTPIC.DAT"); return false; }
    for (int i = 0; i < enemyCount; i++) {
        int id = troop->monsterId(i);
        if (id < 0 || id >= (int)monstpicOffsets.size()) continue;
        size_t off = monstpicOffsets[id];
        int w = monstpic[off] | monstpic[off + 1] << 8, h = monstpic[off + 2] | monstpic[off + 3] << 8;
        if (off + 4 + (size_t)w * h + 0x400 > monstpic.size()) continue;
        Image img;
        decodeRawDib(w, h, monstpic.data() + off + 4, monstpic.data() + off + 4 + (size_t)w * h, img);
        enemyPic[i] = imageToSurface(img, d.hw);
        enemyPic[i].colorKey = enemyPic[i].px.empty() ? -1 : enemyPic[i].px[0];
        enemyPicValid[i] = true;
    }
    d.back.blit(0, 0, d.cache, Rect(0, 0, 640, 480), false);
    for (int i = 0; i < enemyCount; i++) {
        if (!enemyPicValid[i]) continue;
        Rect r = enemyRect(i);
        d.back.blit(r.left, r.top, enemyPic[i], Rect(0, 0, enemyPic[i].width, enemyPic[i].height), true);
    }
    d.effect(4, nullptr, false);
    d.show(transitions[2]);
    screenState = 0;
    return true;
}

void Game::freeBattle() {
    // FUN_00423604 + FUN_0042292e
    for (int i = 0; i < 10; i++) { enemyPic[i] = Surface(); enemyPicValid[i] = false; }
    commandsDone = false;
    for (int i = 0; i < pb.fightingSize; i++) member(i).setStatus(member(i).status() & 3);
}

// ------------------------------------------------------------------------------------------
// status window and command input

void Game::battleStatus() {
    // FUN_0042c656
    RpgWindow* w = win(1);
    int fs = pb.fightingSize;
    auto column = [&](size_t off) {
        std::string s;
        for (int i = 0; i < fs && i < 4; i++) s += sjisFmt("%3d\n", (int)member(i).s16(off));
        return s;
    };
    std::string slash;
    for (int i = 0; i < fs; i++) slash += "/\n";
    if (w) {
        w->setText(6, column(0x02));
        w->setText(0x10, slash);
        w->setText(0x12, column(0x06));
        w->setText(7, column(0x0a));
        w->setText(0x11, slash);
        w->setText(0x13, column(0x0c));
        if (Label* l = w->label(8)) l->color = transparentWindows ? kIdxLtGray : kIdxGray;
    }
    for (int i = 0; i < fs; i++)
        if (member(i).hp() < 1) member(i).setStatus(member(i).status() | 0x20);
    std::string st;
    for (int i = 0; i < fs && i < 4; i++) st += S[20 + statusIndex(member(i).status())] + "\n";
    if (w) w->setText(8, st);
    for (int i = 0; i < fs; i++)
        if (member(i).hp() < 1) member(i).setStatus(0);
}

void Game::battleCursor() {
    // FUN_0042788f
    RpgWindow* w = win(1);
    if (!w) return;
    static const char* const marks[4] = {"*", "\n*", "\n\n*", "\n\n\n*"};
    if (menuActor >= pb.fightingSize || menuActor < 0 || menuActor > 3) w->setText(9, " ");
    else w->setText(9, marks[menuActor]);
}

bool Game::nextActor(bool back) {
    // FUN_004357c9
    auto able = [&](int i) { return member(i).hp() > 0 && (member(i).status() & 0x12) == 0; };
    if (!back) {
        while (menuActor < pb.fightingSize) {
            if (able(menuActor)) return true;
            menuActor++;
        }
        return false;
    }
    for (int i = menuActor - 1; i >= 0; i--)
        if (able(i)) { menuActor = i; break; }
    return true;
}

void Game::battleNext() {
    // FUN_00427747
    closeWindow(7, true);
    closeWindow(0xb, true);
    closeWindow(5, true);
    closeWindow(4, true);
    closeWindow(6, true);
    if (RpgWindow* w = win(2)) w->activate();
    menuActor++;
    if (!nextActor(false)) {
        closeWindow(2, true);
        battleCursor();
        menuActor = 0;
        executeTurn();
        if (battleExit != 0 || battleLost || allEnemiesDead()) return;
    }
    if (nextActor(false)) {
        if (RpgWindow* w = win(2)) {
            w->setDisabled(2, (member(menuActor).status() & 4) != 0);
            w->repaint();
        }
        battleCursor();
    }
}

void Game::battleCommand(int sel) {
    // FUN_004240ce
    if (allEnemiesDead() || partyDefeated() || battleExit != 0) return;
    menuMode = sel;
    int y0 = mouseSelX == -1 ? 0xfa : mouseSelY;
    buildText(0, false);
    if (win(4) || win(5)) return;
    actions[menuActor].cmd = sel;
    switch (sel) {
    case 0: {
        int y = y0 - (lineH() * enemyListCount) / 2 - 10;
        menuWindow(0x5a, y, 0xa0, enemyListCount, buildText(0, false), 0, 0x10, 1, 6, 0, false);
        break;
    }
    case 1:
        menuMember = menuActor;
        listCount = buildMagicList(0);
        if (listCount == 0) { popup(2, S[94]); break; }
        listWindow(4, 0x50, 0xaa, listCount);
        fillMagicList(0, listCount);
        if (RpgWindow* w = win(4)) {
            w->setText(0x2d, "(");
            w->setText(0x2e, std::to_string(listPage + 1));
            w->setText(0x2f, sjisFmt("/%d )", (listCount - 1) / 10 + 1));
        }
        break;
    case 2:
        listCount = buildItemList(7, 0);
        if (listCount == 0) { popup(2, S[92]); break; }
        listWindow(5, 0x50, 0xaa, listCount);
        fillItemList(0, listCount);
        if (RpgWindow* w = win(5)) {
            w->setText(0x2d, "(");
            w->setText(0x2e, std::to_string(listPage + 1));
            w->setText(0x2f, sjisFmt("/%d )", (listCount - 1) / 10 + 1));
        }
        break;
    case 3:
        battleNext();
        break;
    case 4:
        autoBattle = true;
        closeWindow(2, true);
        autoBattleRun();
        break;
    default: break;
    }
}

int Game::pickTarget(int side, int mode) {
    // FUN_00423c17: side 1 = party list (partyList), else enemy list; mode -1 lowest HP, -2 highest HP
    const int* list = side == 1 ? partyList : enemyList;
    int n = side == 1 ? partyListCount : enemyListCount;
    int lo = 9999, hi = -1, best = -1;
    for (int i = 0; i < n; i++) {
        int hp = side == 1 ? member(list[i]).hp() : enemies[list[i]].hp();
        if (mode == -1) { if (hp <= lo) { lo = hp; best = i; } }
        else if (mode == -2) { if (hi <= hp) { hi = hp; best = i; } }
    }
    if (best == -1) return n == 0 ? 0 : rng().mod(n);
    return list[best];
}

void Game::autoBattleRun() {
    // FUN_00423fac
    while (battleExit == 0 && !quit && autoBattle) {
        buildText(0, false);
        actions[menuActor].cmd = 0;
        int r = rng().mod(100);
        if (r < 50) actions[menuActor].target = pickTarget(0, -1) + 100;
        else if (r < 70) actions[menuActor].target = pickTarget(0, -2) + 100;
        else {
            int k = enemyListCount == 0 ? 0 : rng().mod(enemyListCount);
            actions[menuActor].target = enemyList[k] + 100;
        }
        battleNext();
        if (allEnemiesDead() || partyDefeated() || battleExit != 0) return;
    }
}

// ------------------------------------------------------------------------------------------
// turn execution (FUN_0042cbbb)

int Game::turnOrder(int* ids) {
    // FUN_004282b5 + FUN_004281ec
    struct E { int id, key; };
    std::vector<E> in;
    for (int i = 0; i < pb.fightingSize; i++) {
        if (member(i).hp() <= 0) continue;
        int k = rng().mod(10) + member(i).agi();
        if (k < -1000) k = -1000;
        in.push_back({i, k});
    }
    for (int i = 0; i < enemyCount; i++) {
        if (enemies[i].hp() <= 0) continue;
        int k = rng().mod(10) + enemies[i].agi();
        if (k < -1000) k = -1000;
        in.push_back({i + 10, k});
    }
    std::vector<E> out;
    for (const E& e : in) {
        size_t p = 0;
        while (p < out.size() && e.key < out[p].key) p++;
        out.insert(out.begin() + p, e);
    }
    for (size_t i = 0; i < out.size(); i++) ids[i] = out[i].id;
    return (int)out.size();
}

void Game::executeTurn() {
    windows[2].reset();
    platform().markDirty();
    while (true) {
        buildText(1, true);
        buildText(0, false);
        int ids[20];
        int n = turnOrder(ids);
        for (int i = 0; i < n; i++) {
            if (ids[i] < 10) partyAct(ids[i]);
            else enemyAct(ids[i] - 10);
            battleStatus();
            if (battleExit != 0 || quit) return;
            if (partyDefeated()) break;
        }
        if (partyDefeated()) {
            waitTenths(msgSpeed * 2);
            battleLost = true;
            return;
        }
        if (allEnemiesDead()) {
            victory();
            return;
        }
        endOfTurn();
        if (RpgWindow* w = win(0)) w->setText(1, S[131]);
        bool canInput = false;
        for (int i = 0; i < pb.fightingSize; i++)
            if (member(i).hp() > 0 && (member(i).status() & 0x12) == 0) { canInput = true; break; }
        if (canInput) break;
    }
    if (!autoBattle) {
        static const int cmdStr[5] = {5, 1, 0, 6, 7};
        int maxLen = 0;
        for (int k : cmdStr) maxLen = std::max(maxLen, sjisLength(S[k]));
        WindowLayout l;
        l.l0 = 1; l.rows = 5; l.cols = 1; l.mode = 1; l.fmt = 0x10;
        l.text = buildText(2, false); l.hasText = true;
        createWindow(2, 10, 0x118, (maxLen + 1) * charW(), lineH() * 5 + 6, &l, 5);
    }
}

void Game::victory() {
    playVehicleBgm(kAudioVictory, false);
    buildText(1, true);
    std::string text;
    if (goldTotal > 0) text = sjisFmt(J("%d%s獲得\n").c_str(), goldTotal, rpg.coinName.c_str());
    std::string e;
    if (expTotal > 0) {
        if (partyListCount == 1) e = sjisFmt(J("%sは%dpointの経験値を獲得").c_str(), member(partyList[0]).name().c_str(), expTotal);
        else if (partyListCount < pb.fightingSize) e = sjisFmt(J("生き残ったﾒﾝﾊﾞｰはそれぞれ %dpointの経験値を獲得").c_str(), expTotal);
        else e = sjisFmt(J("それぞれ %dpointの経験値を獲得").c_str(), expTotal);
    }
    text += e;
    if (!text.empty()) {
        sysMessage(text, 1.0, false);
        money() += goldTotal;
    }
    for (int i = 0; i < 8; i++) {
        member(i).setAttack(savedStats[i][0]);
        member(i).setDefence(savedStats[i][1]);
        member(i).setAgi(savedStats[i][2]);
    }
    gainExp(-1, true);
    if (dropItem != -1) {
        sysMessage(S[129], 1.0, false);
        sysMessage(items[dropItem].name() + S[130], 1.0, false);
        itemCounts[dropItem]++;
        if (itemCounts[dropItem] == 1) refreshPages();
        if (rpg.itemMax <= itemCounts[dropItem]) itemCounts[dropItem] = (int16_t)rpg.itemMax;
    }
    waitTenths(30);
}

static std::string statDiff(const char* name, int before, int after) {
    if (before == after) return "";
    if (before < after) return Game::sjisFmtS(J("%sが%dポイント上がった\n").c_str(), J(name).c_str(), after - before);
    return Game::sjisFmtS(J("%sが%d ポイント下がった\n").c_str(), J(name).c_str(), before - after);
}

void Game::gainExp(int who, bool add) {
    // FUN_0042d498
    int from = who, to = who + 1;
    if (who == -1) { from = 0; to = partySize; }
    for (int k = from; k < to; k++) {
        Battler& m = member(k);
        if (m.hp() <= 0) continue;
        if (!add) m.setExp(expTotal);
        else m.setExp(m.exp() + (k < 4 ? expTotal : expTotal / 2));
        if (m.exp() > 100000) m.setExp(100000);
        int level = m.level();
        if (level == 100) continue;
        int L = level, n;
        do { n = L; L++; } while (n * n * 10 <= m.exp());
        if (n == level) continue;
        int newL = std::min(n, 100);
        playVehicleBgm(kAudioLevelUp, false);
        sysMessage(sjisFmt(J("%sはレベルが上がった!").c_str(), m.name().c_str()), 0.7, false);
        int a = m.actorId() & 7;
        const ActorData& ad = rpg.actors[a];
        int oVit = m.vit(), oPow = m.pow(), oInt = m.intel(), oAgi = m.agi(), oLuck = m.luck();
        int oMaxHp = m.maxHp(), oMaxMp = m.maxMp(), oAtk = m.attack(), oDef = m.defence();
        auto grow = [&](size_t off, int which) {
            int16_t v = (int16_t)(m.s16(off) + (int16_t)((int)ad.curve(which, newL) - (int)ad.curve(which, level)));
            if (v > 9999) v = 9999;
            m.set16(off, v);
        };
        grow(0x10, 0);  // pow
        grow(0x0e, 1);  // vit
        grow(0x14, 2);  // int
        grow(0x12, 3);  // agi
        grow(0x16, 4);  // luck
        if (m.intel() != oInt) {
            int v = rng().mod(5) + (m.intel() - 1) * 10;
            if ((int16_t)v > 9999) v = 9999;
            m.setMaxMp((int16_t)v);
        }
        if (m.pow() != oPow) deriveStat(0, k);
        if (m.vit() != oVit) {
            int v = rng().mod(5) + m.vit() * 10;
            if ((int16_t)v > 9999) v = 9999;
            m.setMaxHp((int16_t)v);
            deriveStat(1, k);
        }
        if (m.maxHp() < m.hp()) m.setHp(m.maxHp());
        if (m.maxMp() < m.mp()) m.setMp(m.maxMp());
        std::string t = sjisFmt(J("レベルが %d から %d に上がった\n").c_str(), level, newL);
        t += statDiff("最大ＨＰ", oMaxHp, m.maxHp());
        t += statDiff("最大ＭＰ", oMaxMp, m.maxMp());
        t += statDiff("力", oPow, m.pow());
        sysMessage(t, 2.0, false);
        t = statDiff("体力", oVit, m.vit()) + statDiff("知力", oInt, m.intel()) + statDiff("素早さ", oAgi, m.agi()) + statDiff("運", oLuck, m.luck());
        if (!t.empty()) sysMessage(t, 2.0, false);
        t = statDiff("攻撃力", oAtk, m.attack()) + statDiff("防御力", oDef, m.defence());
        if (!t.empty()) sysMessage(t, 2.0, false);
        for (int l = level + 1; l <= newL; l++) {
            for (int s = 0; s < 100; s++) {
                if ((int8_t)pb.learn[a][s] != l) continue;
                if (S[133] == J("未使用")) t = sjisFmt(J("%sは %s の魔法を覚えた").c_str(), m.name().c_str(), magics[s].name().c_str());
                else t = sjisFmt(J("%sは %s %s").c_str(), m.name().c_str(), magics[s].name().c_str(), S[133].c_str());
                sysMessage(t, 2.0, false);
            }
        }
        m.setLevel(newL);
    }
}

// ------------------------------------------------------------------------------------------
// actions

int Game::randomKnownSpell(int i) {
    // FUN_0042b455
    Battler& m = member(i);
    int a = m.actorId() & 7;
    uint8_t list[100];
    int n = 0;
    for (int s = 0; s < 100; s++) {
        int8_t l = (int8_t)pb.learn[a][s];
        if (l > 0 && l <= m.level()) list[n++] = (uint8_t)s;
    }
    if (n == 0) return -1;
    int r = rng().mod(n * 10);
    return list[r / 10];
}

int Game::escapeChance() {
    // FUN_0042b4e6
    int luck = maxLuck();
    int minP = 99, maxE = 0;
    for (int i = 0; i < pb.fightingSize; i++)
        if (member(i).hp() != 0 && member(i).agi() < minP) minP = member(i).agi();
    for (int i = 0; i < enemyCount; i++)
        if (enemies[i].hp() != 0 && maxE < enemies[i].agi()) maxE = enemies[i].agi();
    int c;
    if (maxE < minP) c = 0x5f;
    else c = maxE == 0 ? 0 : (minP * 100) / maxE - 10;
    c += luck / 10;
    return c < 0 ? 0 : c;
}

int Game::autoHeal(int i, int& target) {
    // FUN_0042b616: returns the spell (or -1) and sets target
    int cHalf = 0, cQuarter = 0, deficit = 0, worst = 0;
    for (int k = 0; k < pb.fightingSize; k++) {
        Battler& m = member(k);
        int hp = m.hp();
        if (hp <= 0) continue;
        int mh = m.maxHp();
        if (hp <= mh / 2) cHalf++;
        if (hp <= mh / 4) cQuarter++;
        if (deficit < mh - hp) { deficit = mh - hp; worst = k; }
    }
    Battler& me = member(i);
    int a = me.actorId() & 7;
    auto castable = [&](int s) {
        int8_t l = (int8_t)pb.learn[a][s];
        return l >= 1 && me.level() >= l && me.mp() >= magics[s].mpCost();
    };
    std::vector<int> C, D;
    auto collect = [&](int type) {
        for (int s = 0; s < 100; s++)
            if (magics[s].type() == type && castable(s)) { C.push_back(s); D.push_back(std::abs(magics[s].power() - deficit)); }
    };
    if (cQuarter > 0) {
        if (cQuarter == 1) collect(11);
        if (C.empty()) collect(12);
    }
    if (C.empty() && cHalf > 0) {
        if (cHalf == 1) collect(11);
        if (C.empty()) collect(12);
    }
    if (C.empty()) return -1;
    int best = 0;
    if (C.size() > 1) {
        int bd = 99999;
        for (size_t k = 0; k < C.size(); k++) if (D[k] < bd) { bd = D[k]; best = (int)k; }
    }
    int spell = C[best];
    target = magics[spell].type() != 11 ? 0 : worst;
    return spell;
}

void Game::partyAct(int i) {
    // FUN_0042b8bf
    BattleAction& a = actions[i];
    Battler& me = member(i);
    if (a.cmd == -1 || me.hp() <= 0) return;
    uint16_t st = me.status();
    std::string name = me.name();
    if (st & 2) { sysMessage(name + S[49], 0.5, false); return; }
    if (st & 0x10) { sysMessage(name + S[50], 0.5, false); return; }
    int P[8], E[10], np = 0, ne = 0;
    for (int k = 0; k < pb.fightingSize; k++) if (member(k).hp() > 0) P[np++] = k;
    partyListCount = np;
    for (int k = 0; k < enemyCount; k++) if (enemies[k].hp() > 0) E[ne++] = k;
    enemyListCount = ne;
    if (np == 0 || ne == 0) return;
    bool confusedAct = false;
    if (st & 8) {
        sysMessage(name + S[51], 0.5, false);
        if (rng().mod(100) < 0x4b) {
            a.cmd = rng().mod(100) < 0x28 ? 1 : 0;
            if (a.cmd != 3) {
                if (a.cmd == 1) {
                    int s = randomKnownSpell(i);
                    if (s == -1) a.cmd = 0;
                    else a.spell = s;
                }
                if (rng().mod(100) < 0x28) a.target = P[np == 0 ? 0 : rng().mod(np)];
                else a.target = E[ne == 0 ? 0 : rng().mod(ne)] + 100;
            }
            confusedAct = true;
        }
    }
    if (a.cmd == 3) {
        if (pb.fightingSize < 2) sysMessage(name + S[123], 0.5, false);
        else sysMessage(name + J("たち") + S[123], 0.5, false);
        if (escapeForbidden == 0) {
            int r = rng().mod(100);
            if (r < escapeChance()) battleExit = -1;
            else sysMessage(S[125], 0.5, false);
        } else {
            sysMessage(S[124], 0.5, false);
        }
        return;
    }
    int t = a.target;
    if (autoBattle && !confusedAct && (st & 4) == 0) {
        int tg = 0;
        int s = autoHeal(i, tg);
        if (s != -1) { a.cmd = 1; a.spell = s; t = tg; }
    }
    bool enemySide = t >= 100;
    int k = enemySide ? t - 100 : t;
    int count = enemySide ? enemyCount : pb.fightingSize;
    Battler* side = enemySide ? enemies : pb.members;
    int stype = -1;
    if (a.cmd == 1) stype = magics[std::clamp(a.spell, 0, 99)].type();
    int itype = (a.cmd == 2) ? items[std::clamp(a.item, 0, 99)].type() : 0;
    if (a.cmd == 2 && itype > 0x11) stype = magics[std::clamp(itype - 18, 0, 99)].type();
    auto targetDead = [&]() {
        if (k >= 0 && k < (enemySide ? 10 : 8)) return side[k].hp() <= 0;
        if (k == -1) return (int16_t)(enemies[9].raw[0x32] | enemies[9].raw[0x33] << 8) <= 0;  // record -1 (quirk)
        return true;
    };
    if (stype != 0x14 && stype != 0x15 && stype != 1 && stype != 0xc && targetDead() && stype != 0x10 &&
        !(a.cmd == 2 && itype == 10)) {
        int best = -1, bk = 0;
        for (int j = 0; j < count; j++) {
            Battler& b = side[j];
            if (b.hp() > 0 && best < b.maxHp() - b.hp()) { best = b.maxHp() - b.hp(); bk = j; }
        }
        k = bk;
        t = enemySide ? bk + 100 : bk;
    }
    if (a.cmd == 0) {
        sysMessage(sjisFmt(J("%sの攻撃").c_str(), name.c_str()), 0.5, false);
        if (t > 99) playAnimation(99, k, 0);
        physicalAttack(i, t, 0);
    } else if (a.cmd == 1) {
        magicEffect(a.spell, i, t, false);
    } else if (a.cmd == 2) {
        useItem(a.item, i, t);
    }
}

void Game::enemyAct(int e) {
    // FUN_0042c114
    Battler& me = enemies[e];
    if (me.hp() <= 0) return;
    uint16_t st = me.status();
    std::string name = me.name();
    if (st & 2) { sysMessage(name + S[49], 1.0, false); return; }
    if (st & 0x10) { sysMessage(name + S[50], 1.0, false); return; }
    bool conf = (st & 8) != 0;
    if (conf) sysMessage(name + S[51], 1.0, false);
    int P[8], E[10], np = 0, ne = 0;
    for (int k = 0; k < pb.fightingSize; k++) if (member(k).hp() > 0) P[np++] = k;
    partyListCount = np;
    for (int k = 0; k < 10; k++) if (enemies[k].hp() > 0) E[ne++] = k;
    enemyListCount = ne;
    if (np == 0 || ne == 0) return;
    int m = 0;
    while (m < 10 && me.attackPattern(m) != 0) m++;
    int act = m == 0 ? 1 : me.attackPattern(rng().mod(m));
    int t = 0;
    if (conf) {
        if (rng().mod(100) < 0x32) t = P[np == 0 ? 0 : rng().mod(np)];
        else t = E[ne == 0 ? 0 : rng().mod(ne)] + 100;
    }
    bool again = true;
    while (again) {
        again = false;
        if (act >= 1 && act <= 3) {
            int hits = act == 3 ? 2 : 1;
            for (int h = 0; h < hits; h++) {
                if (h == 0) {
                    jumpEnemy(e, 0);
                    sysMessage(sjisFmt(J("%sの攻撃").c_str(), name.c_str()), 0.5, false);
                } else {
                    sysMessage(S[126], 0.5, false);
                }
                if (!conf && h == 0) {
                    int r = rng().mod(100);
                    if (r <= 0x1d && member(P[0]).hp() >= 1) t = P[0];
                    else if (r <= 0x3b && np >= 2 && member(P[1]).hp() >= 1) t = P[1];
                    else if (r < 0x5a) {
                        do { t = P[np == 0 ? 0 : rng().mod(np)]; } while (member(t).hp() < 1);
                    } else {
                        int saveN = partyListCount;
                        int saveL[4];
                        memcpy(saveL, partyList, sizeof saveL);
                        for (int q = 0; q < np && q < 4; q++) partyList[q] = P[q];
                        partyListCount = np;
                        t = pickTarget(1, -1);
                        memcpy(partyList, saveL, sizeof saveL);
                        partyListCount = saveN;
                    }
                }
                if (t > 99) playAnimation(99, t - 100, e + 100);
                physicalAttack(e + 100, t, act);
                bool alive = t > 99 ? enemies[std::min(t - 100, 9)].hp() > 0 : member(t).hp() > 0;
                bool charaHp = t > 99 ? enemies[std::min(t - 100, 9)].hp() > 0 : monsters[std::clamp(t, 0, 99)].maxHp() > 0;
                if (!((t > 99 || alive) && charaHp)) break;
            }
        } else if (act == 4) {
            sysMessage(name + S[127], 0.5, false);
        } else if (act == 5) {
            sysMessage(name + S[128], 0.5, false);
            me.setHp(0);
            removeEnemy(e, 0);
        } else {
            int s = std::clamp(act - 10, 0, 99);
            if (!conf) {
                if (magics[s].type() >= 11) {
                    int lo = 9999, best = -1;
                    for (int q = 0; q < ne; q++) if (enemies[E[q]].hp() <= lo) { lo = enemies[E[q]].hp(); best = q; }
                    t = (best == -1 ? (ne == 0 ? 0 : rng().mod(ne)) : E[best]) + 100;
                } else {
                    t = P[np == 0 ? 0 : rng().mod(np)];
                }
            }
            if (me.mp() < magics[s].mpCost()) {
                act = 1;
                again = true;
            } else {
                jumpEnemy(e, 0);
                magicEffect(s, e + 100, t, false);
            }
        }
    }
    if (act != 4 && act != 5) jumpEnemy(e, 1);
}

void Game::endOfTurn() {
    // FUN_0042c96a
    for (int i = 0; i < pb.fightingSize; i++) {
        Battler& m = member(i);
        if (m.hp() <= 0) continue;
        if (m.status() & 1) {
            sysMessage(m.name() + S[132], 0.5, false);
            display().effect(0, 0xff, 0, 0, 0, false);
            applyDamage(i, 5, 0);
            battleStatus();
        }
        if (m.status() & 0x10) {
            if (m.sleepCounter() == 0) {
                if (rng().mod(3) == 0) {
                    m.setStatus(m.status() & 0xef);
                    sysMessage(m.name() + S[44], 1.0, false);
                    battleStatus();
                }
            } else m.setSleepCounter(m.sleepCounter() - 1);
        }
    }
    for (int i = 0; i < 10; i++) {
        Battler& m = enemies[i];
        if (m.hp() <= 0) continue;
        if (m.status() & 1) {
            sysMessage(m.name() + S[132], 1.0, false);
            applyDamage(i + 100, 5, 0);
        }
        if (m.status() & 0x10) {
            if (m.sleepCounter() == 0) {
                if ((rng().next() & 1) == 0) {
                    m.setStatus(m.status() & 0xef);
                    sysMessage(m.name() + S[44], 1.0, false);
                }
            } else m.setSleepCounter(m.sleepCounter() - 1);
        }
    }
}

// ------------------------------------------------------------------------------------------
// formulas

void Game::physicalAttack(int ai, int ti, int mode) {
    // FUN_00429ccc
    Battler& A = combatant(ai);
    Battler& T = combatant(ti);
    if (T.hp() <= 0) {
        sysMessage(T.name(), 0.5, false);  // format "%s" with two arguments (original bug)
        return;
    }
    bool asleep = (T.status() & 0x10) != 0;
    if (!asleep && T.agi() - A.agi() > 10) {
        if (rng().mod(2) == 0) {
            int r = rng().mod(3);
            int base = ti > 99 ? 0x50 : 0x49;
            sysMessage(T.name() + S[r + base], 0.5, false);
            return;
        }
    }
    int dmg = A.attack() - T.defence() / 2;
    int v = dmg / 5;
    if (v < 1) v = 1;
    dmg += rng().mod(v * 2 + 1) - v;
    if (asleep) {
        if (dmg < 1) dmg = rng().mod(3);
        dmg = (int)(dmg * 1.5f);
    }
    if (dmg < 1) {
        int r = rng().mod(3);
        int base = ti > 99 ? 0x53 : 0x4c;
        sysMessage(T.name() + S[r + base], 0.5, false);
        return;
    }
    if (ai < 100) {
        if (rng().mod(100) < A.luck() / 10 + 5) {
            sysMessage(ti > 99 ? S[71] : S[72], 0.5, false);
            dmg *= 2;
        }
    } else if (mode == 2) {
        sysMessage(ti > 99 ? S[71] : S[72], 0.5, false);
        dmg *= 2;
    }
    applyDamage(ti, dmg, ai);
    if (ai < 100 || ti < 100) battleStatus();
}

void Game::applyDamage(int t, int dmg, int attacker) {
    // FUN_00427976
    int k = t > 99 ? t - 100 : t;
    playSe("punch.wav", nullptr);
    if (t < 100) {
        display().effect(2, nullptr, false);
    } else {
        for (int b = 0; b < 4; b++) {
            drawEnemies((b & 1) ? -1 : k, attacker - 100, true);
            delayMs(0x28);
        }
    }
    Battler& T = combatant(t);
    if (T.hp() < dmg) dmg = T.hp();
    if (t < 100) sysMessage(sjisFmt(J("%sは%d%s").c_str(), T.name().c_str(), dmg, S[86].c_str()), 0.5, false);
    else sysMessage(sjisFmt(J("%sに%d%s").c_str(), T.name().c_str(), dmg, S[87].c_str()), 0.5, false);
    T.setHp((int16_t)(T.hp() - dmg));
    if (T.hp() < 1) {
        if (t < 100) {
            T.setStatus(0);
            sysMessage(T.name() + S[45], 0.5, false);
        } else {
            expTotal += T.exp();
            goldTotal += T.gold();
            int prob = (int16_t)T.u16(0x26) >> 8;
            if (dropItem == -1 && prob > 0) {
                int luck = maxLuck();
                if (rng().mod(100) < prob + luck / 10) dropItem = T.u16(0x26) & 0xff;
            }
            sysMessage(T.name() + S[88], 0.2, false);
            removeEnemy(k, 3);
        }
    }
}

void Game::removeEnemy(int e, int transition) {
    // FUN_00427dd3
    drawEnemies(-1, -1, false);
    Rect r = enemyRect(e);
    display().show(transition, &r);
}

void Game::jumpEnemy(int e, int phase) {
    // FUN_0042a0f2
    static const int off[10] = {-8, -12, -6, 6, 30, 34, 28, 18, 7, 0};
    if (phase == 0) playSe("zap64.wav", nullptr);
    Display& d = display();
    for (int j = 0; j < 5; j++) {
        d.back.blit(0, 0, d.cache, Rect(0, 0, 640, 480), false);
        for (int i = 0; i < enemyCount; i++) {
            if (enemies[i].hp() <= 0 || !enemyPicValid[i]) continue;
            Rect r = enemyRect(i);
            int dy = i == e ? off[phase * 5 + j] : 0;
            d.back.blit(r.left, r.top + dy, enemyPic[i], Rect(0, 0, enemyPic[i].width, enemyPic[i].height), true);
        }
        d.presentRect(battleRect);
    }
}

void Game::playAnimation(int A, int target, int caster) {
    // FUN_0042a760
    Display& d = display();
    A = std::clamp(A, 0, 99);
    const BattleAnim& an = anims[A];
    int T = an.minLen;
    int e = target == -1 ? 0 : target;
    struct Sub { bool draw = false; Rect r; int start = 0; int file = 0; };
    Sub subs[10];
    Surface surf[42];
    bool loaded[42] = {};
    for (int k = 0; k < an.count; k++) {
        int f = std::clamp(an.file[k], 0, 41);
        char name[32];
        snprintf(name, sizeof name, "Anm%02d.bmp", f);
        std::string path = fs::find(gameDir, name);
        Image img;
        if (path.empty() || !loadBmp(path, img)) { fileMissing(fs::join(gameDir, name)); return; }
        if (!loaded[f]) {
            surf[f] = imageToSurface(img, d.hw);
            surf[f].colorKey = surf[f].px.empty() ? -1 : surf[f].px[0];
            loaded[f] = true;
        }
        anmFiles[f].frames = img.width / 128;
        Sub& s = subs[k];
        s.file = f;
        int mode = anmFiles[f].posX;
        if (an.count == 1 && mode == -1) mode = -2;
        if (mode == -1) {
            int y = rng().mod(0x78) + 0x78;
            int x = (int)(rng().next() & 0x1ff);
            s.r = Rect(x, y, x + 128, y + 128);
            s.draw = true;
        } else if (mode < -1) {
            if (!troop || troop->count() < e || e < 0 || e > 9 || enemies[e].hp() <= 0) {
                s.draw = false;
            } else {
                Rect er = enemyRect(e);
                int x = er.left + (er.width() - 128) / 2;
                int y = 0;
                if (mode == -2) y = er.top + (er.height() - 128) / 2;
                else if (mode == -3) y = er.top - 0x20;
                else if (mode == -4) y = er.bottom + 8 - 128;
                if (x < 0) x = 0;
                else if (x + 128 > 640) x = 640 - 128;
                if (y < 0) y = 0;
                else if (y + 128 > 480) y = 480 - 128;
                if (e == caster - 100) y += 30;
                s.r = Rect(x, y, x + 128, y + 128);
                s.draw = true;
            }
            if (target == -1) { e++; if (e > 9) e = 0; }
        } else {
            s.r = Rect(anmFiles[f].posX, anmFiles[f].posY, anmFiles[f].posX + 128, anmFiles[f].posY + 128);
            s.draw = true;
        }
        s.start = anmFiles[f].start;
        int len = anmFiles[f].frames * anmFiles[f].rep;
        if (T < len) T = len;
        if (s.start == -1) {
            if (k == 0) s.start = 0;
            else if (k == an.count - 1) s.start = T - len;
            else s.start = rng().mod(T - len + 1);
        }
    }
    for (int t = 0; t <= T; t++) {
        if (t > 0) d.back.blit(0, 0, d.cache, Rect(0, 0, 640, 480), false);
        for (int i = 0; i < enemyCount; i++) {
            if (enemies[i].hp() <= 0 || !enemyPicValid[i]) continue;
            Rect r = enemyRect(i);
            d.back.blit(r.left, r.top + (i == caster - 100 ? 30 : 0), enemyPic[i], Rect(0, 0, enemyPic[i].width, enemyPic[i].height), true);
        }
        if (t < T) {
            bool played = false;
            for (int k = 0; k < an.count; k++) {
                Sub& s = subs[k];
                if (!s.draw) continue;
                const AnmFile& af = anmFiles[s.file];
                int rel = t - s.start;
                int len = af.frames * af.rep;
                if (rel < 0 || rel >= len || af.frames <= 0) continue;
                if (rel == 0 && !played) { playSystemSe(af.sound); played = true; }
                int fr = rel % af.frames;
                d.back.blit(s.r.left, s.r.top, surf[s.file], Rect(fr * 128, 0, fr * 128 + 128, 128), true);
            }
        }
        d.presentRect(battleRect);
        delayMs(an.delay);
    }
}

}  // namespace rm95
