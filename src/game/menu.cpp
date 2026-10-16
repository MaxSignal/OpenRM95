// Field menu, item/magic/equipment/status/order/system submenus, shop and inn.
// FUN_0042dba9, FUN_0042739a, FUN_00433d2e, FUN_00434102 ... (see spec/ui.md §3, §4)
#include "game.h"
#include "jtext.h"
#include "../platform/platform.h"
#include "../util/log.h"
#include "../util/rng.h"
#include "../util/sjis.h"
#include <algorithm>

namespace rm95 {

static std::string itos(int v) { return std::to_string(v); }

// ------------------------------------------------------------------------------------------
// text and list builders

std::string Game::buildText(int mode, bool all) {
    // FUN_00423709
    std::string out;
    switch (mode) {
    case 0: {
        enemyListCount = 0;
        for (int i = 0; i < enemyCount; i++)
            if (enemies[i].hp() > 0) enemyList[enemyListCount++] = i;
        for (int i = 0; i < enemyListCount - 1; i++)
            for (int j = 0; j < enemyListCount - 1; j++)
                if (troop && troop->monsterX(enemyList[j + 1]) < troop->monsterX(enemyList[j])) std::swap(enemyList[j], enemyList[j + 1]);
        for (int i = 0; i < enemyListCount; i++) out += enemies[enemyList[i]].name() + "\n";
        for (int i = enemyListCount; i < 10; i++) enemyList[i] = 0;
        break;
    }
    case 1:
        partyListCount = 0;
        for (int i = 0; i < pb.fightingSize; i++) {
            if (member(i).hp() > 0 || all) {
                out += member(i).name() + "\n";
                partyList[partyListCount++] = i;
            }
        }
        for (int i = partyListCount; i < 4; i++) partyList[i] = 0;
        break;
    case 6:
        for (int i = 0; i < partySize; i++) out += member(i).name() + "\n";
        break;
    case 2: {
        static const int cmds[5] = {5, 1, 0, 6, 7};
        for (int k : cmds) out += S[k] + "\n";
        break;
    }
    case 3:
        for (int i = 0; i < 6; i++) out += (i == 5 ? J("システム") : S[i]) + "\n";
        break;
    case 4:
        out = J("ゲームを保存する\nゲームを読み込む\nスクリーンモードの変更\nバージョン情報\n終了する\n");
        break;
    case 5:
        for (int i = 0; i < 5; i++) out += S[10 + i] + "\n";
        break;
    default: break;
    }
    if (out.size() > 255) out.resize(255);
    return out;
}

int Game::buildItemList(int mode, int k) {
    // FUN_00426b6e
    int n = 0;
    listPage = 0;
    if (mode >= 1 && mode <= 5) {
        int a = member(k).actorId() & 0x1f;
        for (int i = 0; i < 100; i++)
            if (itemCounts[i] > 0 && items[i].type() == mode - 1 && ((1 << a) & items[i].equipBy()) != 0) listIndex[n++] = i;
    } else if (mode == 0) {
        for (int i = 0; i < 100; i++) listIndex[n++] = i;
    } else if (mode == 8) {
        for (int i = 0; i < 100; i++) if (itemCounts[i] > 0) listIndex[n++] = i;
    } else if (mode == 6) {
        for (int i = 0; i < 100; i++) if (itemCounts[i] > 0 && items[i].type() < 5) listIndex[n++] = i;
    } else if (mode == 7) {
        for (int i = 0; i < 100; i++) if (itemCounts[i] > 0 && items[i].type() > 5) listIndex[n++] = i;
    }
    return n;
}

int Game::buildMagicList(int mode) {
    // FUN_00426cb4: spells known by member G+0x326e (learn level in 1..level); mode 1: support spells only
    Battler& m = member(menuMember);
    int level = m.level();
    int a = m.actorId();
    if (a < 0 || a > 7) a = 0;
    int n = 0;
    listPage = 0;
    for (int i = 0; i < 100; i++) {
        int8_t l = (int8_t)pb.learn[a][i];
        if (mode == 1 && magics[i].type() < 11) continue;
        if (l > 0 && l <= level) listIndex[n++] = i;
    }
    return n;
}

int Game::buildShopList(const std::string& s) {
    // FUN_00426b38
    int n = 0;
    listPage = 0;
    for (int i = 0; i < 100; i++)
        if (i < (int)s.size() && s[i] == 'X') listIndex[n++] = i;
    return n;
}

int Game::firstActiveMember() {
    // FUN_00435797
    for (int i = 0; i < partySize; i++)
        if (member(i).hp() > 0 && (member(i).status() & 2) == 0) return i;
    return 0;
}

bool Game::menuLocked() {
    // FUN_00435881
    if (eventRunning) return true;
    return !(heroChara && heroChara->routeMode == kRouteNone && waitingId == -1);
}

void Game::gatherParty() {
    // FUN_004356f4
    for (int i = 1; i < mapCharaCount; i++) {
        mapParty[i]->facing = mapParty[0]->facing;
        mapParty[i]->x = mapParty[0]->x;
        mapParty[i]->y = mapParty[0]->y;
    }
    for (int i = 0; i < 8; i++) trail[i] = -1;
}

// ------------------------------------------------------------------------------------------
// list windows (types 4, 5, 7, 8)

RpgWindow* Game::listWindow(int type, int x, int y, int count) {
    WindowLayout l;
    l.l0 = 1; l.rows = 10; l.cols = 1; l.mode = 1; l.fmt = 0x10;
    return createWindow(type, x, y, 0xf0, 0xd2, &l, count);
}

static void setPageLabels(RpgWindow* w, int page, int count) {
    if (!w) return;
    w->setText(0x2d, "(");
    w->setText(0x2e, itos(page + 1));
    char b[32];
    snprintf(b, sizeof b, "/%d )", (count - 1) / 10 + 1);
    w->setText(0x2f, b);
}

void Game::fillMagicList(int page, int count) {
    // FUN_00426dad
    RpgWindow* w = win(4);
    if (!w) return;
    int base = page * 10;
    if (count == 0) { post(0x411, 4, 0); return; }
    if (count == -1) count = listTotal[4];
    listTotal[4] = count;
    for (int i = 0; i < 10; i++) {
        Label* a = w->label(i + 1);
        Label* b = w->label(i + 11);
        if (a) { a->disabled = false; a->fmt = 0; }
        if (b) b->disabled = false;
        const MagicData& m = magics[listIndex[std::min(base + i, 99)]];
        w->setText(i + 1, m.name());
        w->setText(0x2d, "(");
        if (b) b->fmt = 2;
        w->setText(i + 11, sjisFmt("%dpts.", (int)m.mpCost()));
        if (member(menuMember).mp() < m.mpCost()) {
            if (a) a->disabled = true;
            if (b) b->disabled = true;
            w->repaint();
        }
        if (count - 1 <= i + base) {
            for (int j = i + 1; j < 10; j++) { w->setText(j + 1, ""); w->setText(j + 11, ""); }
            post(0x411, 4, 0);
            return;
        }
    }
}

void Game::fillItemList(int page, int count) {
    // FUN_0042723d
    RpgWindow* w = win(5);
    if (!w) return;
    int base = page * 10;
    if (count == 0) { post(0x411, 5, 0); return; }
    if (count == -1) count = listTotal[5];
    listTotal[5] = count;
    for (int i = 0; i < 10; i++) {
        int it = listIndex[std::min(base + i, 99)];
        if (Label* a = w->label(i + 1)) a->fmt = 0;
        w->setText(i + 1, items[it].name());
        if (Label* b = w->label(i + 11)) b->fmt = 2;
        w->setText(i + 11, sjisFmt("(%d)", (int)itemCounts[it]));
        if (count - 1 <= i + base) {
            for (int j = i + 1; j < 10; j++) { w->setText(j + 1, ""); w->setText(j + 11, ""); }
            post(0x411, 5, 0);
            return;
        }
    }
}

void Game::fillTeleportList(int page, int count) {
    // FUN_0042713e
    RpgWindow* w = win(7);
    if (!w) return;
    if (count == 0) { post(0x411, 7, 0); return; }
    if (count == -1) count = listTotal[7];
    listTotal[7] = count;
    for (int i = 0; i < 10; i++) {
        if (Label* a = w->label(i + 1)) a->fmt = 0;
        int id = teleportList[std::min(i + page * 10, 99)];
        w->setText(i + 1, (id >= 0 && id < 500) ? rpg.maps[id].name : std::string());
        if (count - 1 <= i + page * 10) {
            for (int j = i + 1; j < 10; j++) { w->setText(j + 1, ""); w->setText(j + 11, ""); }
            post(0x411, 7, 0);
            return;
        }
    }
}

void Game::fillShopList(int page, int count, bool sell) {
    // FUN_00426fb9 (page changes call it with sell == false: the sell list then shows full prices)
    RpgWindow* w = win(8);
    if (!w || count == 0) return;
    int base = page * 10;
    if (count == -1) count = listTotal[8];
    listTotal[8] = count;
    for (int i = 0; i < 10; i++) {
        int it = listIndex[std::min(base + i, 99)];
        if (Label* a = w->label(i + 1)) a->fmt = 0;
        w->setText(i + 1, items[it].name());
        int price = items[it].cost();
        if (sell) price /= 2;
        if (Label* b = w->label(i + 11)) b->fmt = 2;
        w->setText(i + 11, itos(price));
        if (count - 1 <= i + base) {
            for (int j = i + 1; j < 10; j++) { w->setText(j + 1, ""); w->setText(j + 11, ""); }
            post(0x411, 8, 0);
            return;
        }
    }
}

void Game::onPageChange(int type, int page) {
    // FUN_0042f63c / FUN_0042f69c
    listPage = page;
    if (type == 4) fillMagicList(page, -1);
    else if (type == 5) fillItemList(page, -1);
    else if (type == 7) fillTeleportList(page, -1);
    else if (type == 8) fillShopList(page, -1, false);
}

// ------------------------------------------------------------------------------------------
// main menu

void Game::runMenu() {
    // FUN_0042dba9
    int maxLen = 4;
    for (int i = 0; i < 5; i++) maxLen = std::max(maxLen, sjisLength(S[i]));
    menuMember = 0;
    WindowLayout l;
    l.l0 = 1; l.rows = 6; l.cols = 1; l.mode = 1; l.fmt = 0x10;
    l.text = buildText(3, false); l.hasText = true;
    RpgWindow* w = createWindow(2, 0x28, 0x6e, (maxLen + 1) * charW(), lineH() * 6 + 6, &l, 6);
    if (partySize < 2 || (heroChara && heroChara->step != 0)) w->setDisabled(5, true);
    if (menuLocked()) {
        w->setDisabled(1, true);
        w->setDisabled(2, true);
        w->setDisabled(3, true);
        w->setDisabled(5, true);
    }
    while (!quit && menuRequested) {
        pumpMessages();
        platform().sleep(1);
    }
    closeWindow(2, true);
}

void Game::openSubmenu(int idx) {
    // FUN_0042739a
    menuMode = idx;
    menuActor = 0;
    actions[0].cmd = idx != 0;
    if (RpgWindow* w = win(2)) w->deactivate();
    switch (idx) {
    case 0:
        itemMenu();
        break;
    case 1: {
        partyWindow(0x5a, 0x69, 0xf0, partySize, buildText(6, true), 0, 0x10, 1, 6, 0, true);
        RpgWindow* w = win(6);
        for (int i = 0; i < pb.fightingSize; i++) {
            Battler& m = member(i);
            if (m.hp() < 1 || (m.status() & 0x7a) != 0)
                if (w) w->setDisabled(i + 1, true);
        }
        break;
    }
    case 2:
        menuWindow(0x50, 0x69, 0xa0, partySize, buildText(6, true), 0, 0x10, 1, 6, 0, true);
        break;
    case 3:
        menuWindow(0x50, 0x69, 0x8c, partySize, buildText(6, true), 0, 0x10, 1, 6, 0, true);
        break;
    case 4:
        orderMenu();
        break;
    case 5:
        menuWindow(0x50, 0xb4, 0xa0, 5, buildText(4, false), 0, 0x10, 1, 6, 0, false);
        if (!savingEnabled || !heroChara || heroChara->routeMode != kRouteNone)
            if (RpgWindow* w = win(6)) w->setDisabled(1, true);
        break;
    default: break;
    }
}

// ------------------------------------------------------------------------------------------
// selection / cancel dispatch (FUN_00433d2e, FUN_00434102)

void Game::onSelect(int type, int lparam) {
    if (lparam < 1000) {
        mouseSelX = lastMouseX;
        mouseSelY = lastMouseY;
    } else {
        lparam -= 1000;
        mouseSelX = mouseSelY = -1;
    }
    if (type == 0 && win(6)) return;
    if (type == 10) return;
    selectedIndex = lparam;
    okPressed = true;
    flushKeys(true);
    okPressed = true;
    if (!menuRequested && !inBattle) return;
    if (!inBattle) {
        switch (type) {
        case 2: openSubmenu(lparam); break;
        case 6:
            if (menuMode == 3) statusWindow(selectedIndex);
            else if (menuMode == 1) magicMenu();
            else if (menuMode == 2) equipSlots();
            else if (menuMode == 5) systemMenu();
            break;
        case 0xb:
            if (menuMode == 2) equipCandidates();
            else if (menuMode == 0) useItem(actions[menuMember].item, menuMember, lparam);
            else if (menuMode == 1) magicEffect(actions[menuMember].spell, menuMember, lparam, false);
            break;
        case 7:
            if (menuMode == 0) useItem(actions[menuMember].item, menuMember, teleportList[std::min(lparam, 99)]);
            else if (menuMode == 1) magicEffect(actions[menuMember].spell, menuMember, teleportList[std::min(lparam, 99)], false);
            break;
        case 8: break;
        case 5:
            if (menuMode == 0) itemSelected(lparam);
            else if (menuMode == 2) equipItem();
            break;
        case 4: magicSelected(lparam); break;
        case 9: closeAllWindows(true); break;
        default: break;
        }
        return;
    }
    // battle
    switch (type) {
    case 2: battleCommand(lparam); break;
    case 6:
        if (menuMode == 0) {
            actions[menuActor].target = enemyList[std::min(selectedIndex, 9)] + 100;
            battleNext();
        }
        break;
    case 4: magicSelected(lparam); break;
    case 5: itemSelected(lparam); break;
    case 0xb:
        if (actions[menuActor].target == 0) actions[menuActor].target = partyList[std::min(selectedIndex, 3)];
        else actions[menuActor].target = enemyList[std::min(selectedIndex, 9)] + 100;
        battleNext();
        break;
    case 7:
        actions[menuActor].target = teleportList[std::min(selectedIndex, 99)];
        battleNext();
        break;
    default: break;
    }
}

void Game::onCancel(int type) {
    if (type == 0 && win(6)) return;
    if (type == 1) return;
    okPressed = true;
    if (type == 0 && menuMode == 2) return;
    if (!inBattle && !menuRequested) return;
    int target = -1;
    if (!inBattle) {
        if (type == 2) { closeAllWindows(true); return; }
        if (win(10)) return;
        if (type == 6) target = 2;
        else if (type == 4 || type == 9 || (type == 0xb && menuMode == 2)) target = 6;
        else if (type == 5) target = menuMode == 2 ? 0xb : 2;
        else if (type == 0xb || type == 7) target = menuMode == 0 ? 5 : 4;
        if (target != -1) if (RpgWindow* w = win(target)) w->activate();
        closeWindow(type, true);
        if (type == 4) actions[menuMember].spell = -1;
        if (type == 5) actions[menuMember].item = -1;
        return;
    }
    if (type == 2) {
        nextActor(true);
        battleCursor();
    } else if (type == 6 || type == 5 || type == 4) {
        target = 2;
    } else if (type == 0xb) {
        target = menuMode == 1 ? 4 : 5;
    }
    if (target != -1) if (RpgWindow* w = win(target)) w->activate();
    if (type == 7) {
        if (RpgWindow* w = win(5)) w->activate();
        if (RpgWindow* w = win(4)) w->activate();
    }
    if (type != 2) closeWindow(type, true);
}

// ------------------------------------------------------------------------------------------
// items

void Game::itemMenu() {
    // FUN_0043300e
    if (win(5)) return;
    listCount = buildItemList(8, 0);
    if (listCount == 0) { popup(2, S[90]); return; }
    listWindow(5, 0x78, 0x50, listCount);
    fillItemList(0, listCount);
    setPageLabels(win(5), listPage, listCount);
}

void Game::itemSelected(int idx) {
    // FUN_004331d7
    int item = listIndex[std::clamp(idx, 0, 99)];
    int t = items[item].type();
    int st = (t - 18 < 0 || t - 18 >= 100) ? -1 : magics[t - 18].type();
    if (!inBattle && st >= 0 && st < 11) { popup(5, S[93]); return; }
    actions[menuActor].item = item;
    actions[menuActor].target = -1;
    if (t < 6) {
        if (!inBattle) {
            closeAllWindows(false);
            useItem(item, firstActiveMember(), -1);
            closeAllWindows(true);
        } else {
            battleNext();
        }
    } else if (st == 12 || st == 1 || st == 21) {
        int tg = st == 1 ? 100 : 0;
        if (!inBattle) useItem(item, firstActiveMember(), tg);
        else { actions[menuActor].target = tg; battleNext(); }
    } else if (st == 20) {
        teleportMenu(5);
    } else {
        if (!inBattle) menuActor = 0;
        if (RpgWindow* w = win(5)) w->deactivate();
        bool party = st < 0 || st > 10;
        actions[menuActor].target = party ? 0 : 100;
        int mode = !inBattle ? 6 : (party ? 1 : 0);
        buildText(mode, mode != 0);
        int n = !inBattle ? partySize : (party ? partyListCount : enemyListCount);
        int y = mouseSelX == -1 ? (inBattle ? 0xfa : 100) : mouseSelY;
        y = y - (lineH() * n) / 2 - 10;
        if (mode == 0) menuWindow(0xb4, y, 0xa0, n, buildText(0, false), 0, 0x10, 1, 0xb, 0, false);
        else partyWindow(0xb4, y, 0xf0, n, buildText(mode, true), 0, 0x10, 1, 0xb, 0, st == 13 || t == 7 || t == 15);
    }
}

void Game::useItem(int item, int user, int target) {
    // FUN_004283b1
    if (!inBattle) closeAllWindows(false);
    if (item < 0 || item >= 100) item = 0;
    int t = items[item].type();
    int s = t - 18;
    int st = (s < 0 || s >= 100) ? -1 : magics[s].type();
    Battler& u = member(std::clamp(user, 0, 7));
    if (itemCounts[item] < 1) {
        sysMessage(u.name() + J("は") + items[item].name() + S[122], 1.0, false);
    } else {
        sysMessage(u.name() + J("は") + items[item].name() + S[121], 0.5, false);
        if (t < 6) {
            sysMessage(S[64], 0.5, false);
        } else {
            itemCounts[item]--;
            if (itemCounts[item] < 1) {
                itemCounts[item] = 0;
                refreshPages();
            }
            if (!inBattle && st >= 0 && (st < 11 || (st > 16 && st < 20))) sysMessage(S[64], 0.5, false);
            else if (t > 17) {
                if (target == -1) sysMessage(S[64], 0.5, false);
                else magicEffect(s, user, target, true);
            } else {
                itemEffect(t, target, items[item].power());
            }
        }
    }
    if (!inBattle) closeAllWindows(true);
}

void Game::itemEffect(int type, int target, int p) {
    // FUN_004285e3
    Battler& X = member(std::clamp(target, 0, 7));
    if (X.hp() < 1 && type != 10) { sysMessage(S[65], 0.5, false); return; }
    std::string name = X.name();
    if (type < 11) {
        if (type == 6) {
            int16_t old = X.hp();
            X.setHp((int16_t)(X.hp() + p));
            if (X.maxHp() < X.hp()) X.setHp(X.maxHp());
            sysMessage(old == X.hp() ? name + S[67] : sjisFmt(J("%sのHPが回復した").c_str(), name.c_str()), 0.5, false);
        } else if (type == 7) {
            int16_t old = X.mp();
            X.setMp((int16_t)(X.mp() + p));
            if (X.maxMp() < X.mp()) X.setMp(X.maxMp());
            sysMessage(old == X.mp() ? name + S[67] : sjisFmt(J("%sのMPが回復した").c_str(), name.c_str()), 0.5, false);
        } else if (type == 8) {
            std::string m = (X.status() & 1) ? name + S[32] : name + S[67];
            X.setStatus(X.status() & 0xfffe);
            sysMessage(m, 0.5, false);
        } else if (type == 9) {
            cureStatus(X);
        } else {
            std::string m;
            if (X.hp() < 1) {
                if (p > 100) p = 100;
                X.setHp((int16_t)((X.maxHp() * p) / 100));
                if (X.hp() < 1) X.setHp(1);
                m = name + S[46];
                X.setStatus(0);
            } else {
                m = name + S[67];
            }
            sysMessage(m, 0.5, false);
        }
        return;
    }
    size_t off;
    const char* statName;
    switch (type) {
    case 11: off = 0x06; statName = "最大ＨＰ"; break;
    case 12: off = 0x0c; statName = "最大ＭＰ"; break;
    case 13: off = 0x10; statName = "力"; break;
    case 14: off = 0x0e; statName = "体力"; break;
    case 15: off = 0x14; statName = "知力"; break;
    case 16: off = 0x12; statName = "素早さ"; break;
    default: off = 0x16; statName = "運"; break;
    }
    std::string text = sjisFmt(J("%sの%sが%dポイント上がった").c_str(), name.c_str(), J(statName).c_str(), p);
    int16_t v = (int16_t)(X.s16(off) + (int16_t)p);
    if (v >= 0 && v > 9999) v = 9999;
    X.set16(off, v);
    auto clampField = [&](size_t o, int add, int lo) {
        int16_t f = (int16_t)(X.s16(o) + (int16_t)add);
        if (f < lo) f = (int16_t)lo;
        else if (f > 9999) f = 9999;
        X.set16(o, f);
    };
    if (type == 13) {
        text += sjisFmt(J("\n攻撃力が%dポイント上がった").c_str(), p);
        clampField(0x1a, p, 0);
    }
    if (type == 14) {
        text += sjisFmt(J("\n防御力が%dポイント上がった").c_str(), p);
        clampField(0x18, p, 0);
        text += sjisFmt(J("\n最大ＨＰが%dポイント上がった").c_str(), p * 10);
        clampField(0x06, (int16_t)(p * 10), 1);
    }
    if (type == 15) {
        text += sjisFmt(J("\n最大ＭＰが%dポイント上がった").c_str(), p * 10);
        clampField(0x0c, (int16_t)(p * 10), 0);
    }
    sysMessage(text, 1.0, false);
}

void Game::cureStatus(Battler& X) {
    // FUN_00429b7b
    std::string name = X.name();
    if ((X.status() & 0x1e) == 0) {
        sysMessage(name + S[67], 0.5, false);
        return;
    }
    std::string t;
    if (X.status() & 2) t = name + J("は") + S[35] + "\n";
    if (X.status() & 4) t += name + S[38] + "\n";
    if (X.status() & 8) t += name + S[41] + "\n";
    if (X.status() & 0x10) t += name + S[44] + "\n";
    X.setStatus(X.status() & 0xffe1);
    sysMessage(t, 1.0, false);
}

// ------------------------------------------------------------------------------------------
// magic

void Game::magicMenu() {
    // FUN_0043387a
    listPage = 0;
    menuMember = std::clamp(selectedIndex, 0, 7);
    listCount = buildMagicList(1);
    if (listCount == 0) { popup(6, S[95]); return; }
    if (RpgWindow* w = win(6)) w->deactivate();
    listWindow(4, 0x8c, 0x50, listCount);
    fillMagicList(0, listCount);
    setPageLabels(win(4), listPage, listCount);
}

void Game::magicSelected(int idx) {
    // FUN_00433a62
    int s = listIndex[std::clamp(idx, 0, 99)];
    int type = magics[s].type();
    if (RpgWindow* w = win(4)) w->deactivate();
    if (!menuRequested) actions[menuActor].spell = s;
    else actions[menuMember].spell = s;
    int y0 = mouseSelX == -1 ? (inBattle ? 0xfa : 100) : mouseSelY;
    if (!menuRequested) {
        actions[menuActor].target = 0;
        if (type < 11) {
            actions[menuActor].target = 100;
            if (type == 1) battleNext();
            else {
                buildText(0, false);
                int y = y0 - (lineH() * enemyListCount) / 2 - 10;
                menuWindow(0xb4, y, 0xa0, enemyListCount, buildText(0, false), 0, 0x10, 1, 0xb, 0, false);
            }
        } else {
            actions[menuActor].target = 0;
            if (type == 12 || type == 21) battleNext();
            else if (type == 20) teleportMenu(4);
            else {
                int y = y0 - (lineH() * partyListCount) / 2 - 10;
                std::string t = buildText(1, true);
                menuWindow(0xb4, y, 0xa0, partyListCount, t, 0, 0x10, 1, 0xb, 0, true);
            }
        }
    } else if (type == 12 || type == 21) {
        closeAllWindows(true);
        magicEffect(s, menuMember, 0, false);
    } else if (type == 20) {
        teleportMenu(4);
    } else {
        int y = y0 - (lineH() * partySize) / 2 - 10;
        partyWindow(0xb4, y, 0xf0, partySize, buildText(6, true), 0, 0x10, 1, 0xb, 0, type == 13);
    }
}

void Game::teleportMenu(int owner) {
    // FUN_00433682
    listPage = 0;
    int n = 0;
    while (n < 100 && teleportList[n] > 0) n++;
    if (teleportDisabled == 1 || riding) { popup(owner, S[96]); return; }
    if (n == 0) { popup(owner, S[97]); return; }
    listCount = n;
    if (RpgWindow* w = win(owner)) w->deactivate();
    listWindow(7, 0xb4, 0x8c, n);
    fillTeleportList(0, n);
    setPageLabels(win(7), listPage, n);
}

bool Game::escapeTo(int area) {
    // FUN_0043475f
    if (area < 0 || area >= 500) return false;
    MapData& m = rpg.maps[area];
    if (m.type != 2 || riding) return false;
    int p = m.parent;
    if (p < 0 || p >= 500) return false;
    MapData& pm = rpg.maps[p];
    if (pm.type < 2 && m.exitX < (int)pm.width && m.exitY < (int)pm.height) {
        playSe("wibble.wav", nullptr);
        if (!inBattle) {
            abortTick = true;
            closeWindow(0, true);
            int t = transitions[0];
            transitions[0] = 0xd;
            teleport(p, m.exitX, m.exitY);
            transitions[0] = t;
        } else {
            battleExit = area;
        }
        return true;
    }
    return false;
}

void Game::magicEffect(int s, int caster, int t, bool isItem) {
    // FUN_00428a9c
    if (!inBattle && !isItem) closeAllWindows(false);
    if (s < 0 || s >= 100) s = 0;
    const MagicData& M = magics[s];
    int type = M.type();
    int dest = 0;
    if (type == 20) { dest = t; t = 0; }
    else if (type == 21) t = 0;
    Battler& C = combatant(caster < 100 ? std::clamp(caster, 0, 7) : std::clamp(caster, 100, 109));
    if (!isItem) {
        sysMessage(C.name() + J("は") + M.name() + S[60], 0.5, false);
        if (C.status() & 4) { sysMessage(S[61], 0.5, false); return; }
        if (C.mp() < M.mpCost()) { sysMessage(S[62], 0.5, false); return; }
        C.setMp((int16_t)(C.mp() - M.mpCost()));
    }
    bool partySide = t < 100;
    int k = partySide ? t : t - 100;
    int sideCount = partySide ? pb.fightingSize : enemyCount;
    Battler* side = partySide ? pb.members : enemies;
    k = std::clamp(k, 0, partySide ? 7 : 9);
    int A = std::clamp((int)M.anim(), 0, 99);
    if (partySide) {
        const BattleAnim& an = anims[A];
        if (type != 20 && type != 21) playSystemSe(anmFiles[std::clamp(an.file[0], 0, 41)].sound);
        display().effect(an.flashMode, an.r, an.g, an.b, an.gray, false);
        pumpMessages();
    } else if (type == 12 || type == 1) {
        playAnimation(A, -1, caster);
    } else {
        playAnimation(A, k, caster);
    }
    waitTenths(2);
    Battler& X = side[k];
    std::string xn = X.name();
    int rate = 0, amount = 0;
    if (type != 12 && type != 1) {
        rate = X.magicDamage(s);
        if (partySide && s >= 0 && s < 11) {
            rate -= X.magicDef();
            if (rate < 0) rate = 0;
            else if (rate > 199) rate = 200;
        }
        if (type != 20 && type != 21) {
            if (type != 16 && X.hp() < 1) { sysMessage(S[65], 0.5, false); return; }
            amount = (M.power() * rate) / 100;
            int v = amount / 5;
            if (v < 1) v = 1;
            if (rate > 0) amount += rng().mod(v * 2 + 1) - v;
            if (amount < 1 && type != 7 && type != 15) {
                sysMessage(xn + S[66], 0.5, false);
                return;
            }
        }
    }
    auto statusSpell = [&](int bit, int newS, int already) {
        std::string m;
        if (rng().mod(100) < rate) {
            if ((X.status() & bit) == 0) { m = xn + S[newS]; X.setStatus(X.status() | bit); }
            else m = xn + S[already];
        } else m = xn + S[66];
        sysMessage(m, 0.5, false);
    };
    switch (type) {
    case 0:
        applyDamage(t, amount, caster);
        break;
    case 1:
        for (int j = 0; j < sideCount; j++) {
            Battler& Y = side[j];
            if (Y.hp() <= 0) continue;
            int r = Y.magicDamage(s);
            if (partySide) { r -= Y.magicDef(); if (r < 0) r = 0; }
            int am = (M.power() * r) / 100;
            int v = am / 5;
            if (v < 1) v = 1;
            if (r > 0) am += rng().mod(v * 2 + 1) - v;
            if (am < 1) sysMessage(Y.name() + S[66], 0.5, false);
            else applyDamage(partySide ? j : j + 100, am, caster);
        }
        break;
    case 2: statusSpell(1, 30, 31); break;
    case 3: statusSpell(2, 33, 34); break;
    case 4: statusSpell(4, 36, 37); break;
    case 5: statusSpell(8, 39, 40); break;
    case 6: statusSpell(0x10, 42, 43); break;
    case 7:
        if (rng().mod(100) < rate) {
            X.setHp(0);
            sysMessage(xn + S[45], 0.5, false);
            if (!partySide) removeEnemy(k, 3);
        } else sysMessage(xn + S[66], 0.5, false);
        break;
    case 8: X.setAttack((int16_t)(X.attack() - amount)); sysMessage(sjisFmt(J("%sの攻撃力が下がった").c_str(), xn.c_str()), 0.5, false); break;
    case 9: X.setDefence((int16_t)(X.defence() - amount)); sysMessage(sjisFmt(J("%sの防御力が下がった").c_str(), xn.c_str()), 0.5, false); break;
    case 10: X.setAgi((int16_t)(X.agi() - amount)); sysMessage(sjisFmt(J("%sの素早さが下がった").c_str(), xn.c_str()), 0.5, false); break;
    case 11: {
        int16_t old = X.hp();
        X.setHp((int16_t)(X.hp() + amount));
        if (X.maxHp() < X.hp()) X.setHp(X.maxHp());
        sysMessage(old == X.hp() ? xn + S[67] : sjisFmt(J("%sのHPが回復した").c_str(), xn.c_str()), 0.5, false);
        break;
    }
    case 12:
        for (int j = 0; j < sideCount; j++) {
            Battler& Y = side[j];
            if (Y.hp() <= 0) continue;
            int am = (M.power() * (int)Y.magicDamage(s)) / 100;
            int v = am / 5;
            if (v < 1) v = 1;
            am += rng().mod(v * 2 + 1) - v;
            if (am < 1) sysMessage(Y.name() + S[66], 0.5, false);
            else {
                int16_t old = Y.hp();
                Y.setHp((int16_t)(Y.hp() + am));
                if (Y.maxHp() < Y.hp()) Y.setHp(Y.maxHp());
                if (old != Y.hp()) sysMessage(sjisFmt(J("%sのHPが回復した").c_str(), Y.name().c_str()), 0.5, false);
            }
        }
        break;
    case 13: {
        int16_t old = X.mp();
        X.setMp((int16_t)(X.mp() + amount));
        if (X.maxMp() < X.mp()) X.setMp(X.maxMp());
        sysMessage(old == X.mp() ? xn + S[67] : sjisFmt(J("%sのMPが回復した").c_str(), xn.c_str()), 0.5, false);
        break;
    }
    case 14: {
        std::string m;
        if (rng().mod(100) < rate) {
            if ((X.status() & 1) == 0) m = xn + S[67];
            else { m = xn + S[32]; X.setStatus(X.status() & 0xfffe); }
        } else m = xn + S[66];
        sysMessage(m, 0.5, false);
        break;
    }
    case 15:
        if (rng().mod(100) < rate) cureStatus(X);
        else sysMessage(xn + S[66], 0.5, false);
        break;
    case 16:
        if (X.hp() < 1) {
            if (rng().mod(100) < rate) {
                int p = M.power();
                if (p > 100) p = 100;
                int16_t mh = X.maxHp();
                int hp = (mh * p) / 100 + rng().mod(7) - 3;
                if (hp < 0) hp = 1;
                else if (X.maxHp() < hp) hp = X.maxHp();
                X.setHp((int16_t)hp);
                if (!partySide) drawEnemies(-1, -1, false);
                sysMessage(xn + S[46], 0.5, false);
            } else {
                sysMessage(S[47] + xn + S[48], 0.5, false);
            }
        } else sysMessage(xn + S[67], 0.5, false);
        break;
    case 17: X.setAttack((int16_t)(X.attack() + amount)); sysMessage(sjisFmt(J("%sの攻撃力が上がった").c_str(), xn.c_str()), 0.5, false); break;
    case 18: X.setDefence((int16_t)(X.defence() + amount)); sysMessage(sjisFmt(J("%sの防御力が上がった").c_str(), xn.c_str()), 0.5, false); break;
    case 19: X.setAgi((int16_t)(X.agi() + amount)); sysMessage(sjisFmt(J("%sの素早さが上がった").c_str(), xn.c_str()), 0.5, false); break;
    case 20:
        if (!inBattle || escapeForbidden == 0) {
            if (!escapeTo(dest)) sysMessage(S[64], 0.5, false);
        } else sysMessage(S[63], 0.5, false);
        break;
    case 21: {
        bool ok = false;
        if (escapeMap > 0) {
            if (inBattle && escapeForbidden != 0) { sysMessage(S[63], 0.5, false); break; }
            ok = escapeTo(escapeMap);
        }
        if (!ok) sysMessage(S[64], 0.5, false);
        break;
    }
    default: break;
    }
    if (!inBattle && !isItem) closeAllWindows(true);
}

// ------------------------------------------------------------------------------------------
// equipment

void Game::equipSlots() {
    // FUN_0042669d
    equipMember = selectedIndex;
    if (selectedIndex < partySize) {
        if (RpgWindow* w = win(6)) w->deactivate();
        menuWindow(0x6e, 0x73, 0xa0, 5, buildText(5, false), 0, 0x10, 1, 0xb, 0, false);
    }
}

void Game::equipCandidates() {
    // FUN_00426710
    listPage = 0;
    equipSlot = selectedIndex;
    if (RpgWindow* w = win(0xb)) w->deactivate();
    listCount = buildItemList(equipSlot + 1, equipMember);
    if (listCount == 0) { popup(0xb, S[91]); return; }
    menuActor = equipMember;
    listWindow(5, 0xb4, 0x82, listCount);
    fillItemList(0, listCount);
    setPageLabels(win(5), listPage, listCount);
}

static std::string statDiff(const char* name, int before, int after) {
    // FUN_0042db5d
    if (before == after) return "";
    if (before < after) return Game::sjisFmtS(J("%sが%dポイント上がった\n").c_str(), J(name).c_str(), after - before);
    return Game::sjisFmtS(J("%sが%d ポイント下がった\n").c_str(), J(name).c_str(), before - after);
}

void Game::equipItem() {
    // FUN_00426913
    closeWindow(5, true);
    if (RpgWindow* w = win(0xb)) w->deactivate();
    int it = listIndex[std::clamp(selectedIndex, 0, 99)];
    Battler& m = member(std::clamp(equipMember, 0, 7));
    int oldAtk = m.attack(), oldDef = m.defence(), oldMdef = 0;
    int a = m.actorId() & 7;
    int slot = std::clamp(equipSlot, 0, 4);
    int prev = pb.equip[a][slot];
    if (slot == 4) oldMdef = prev == -1 ? 0 : items[prev & 0x7f].power();
    itemCounts[it]--;
    pb.equip[a][slot] = (int8_t)it;
    if (prev != -1) itemCounts[prev & 0x7f]++;
    sysMessage(m.name() + J("は") + items[it].name() + S[120], 0.5, true);
    std::string t;
    if (slot == 0) {
        deriveStat(0, equipMember);
        t = statDiff("攻撃力", oldAtk, m.attack());
    } else if (slot == 4) {
        m.setMagicDef(items[it].power());
        t = statDiff("魔法防御力", oldMdef, m.magicDef());
    } else {
        deriveStat(1, equipMember);
        t = statDiff("防御力", oldDef, m.defence());
    }
    if (!t.empty()) sysMessage(t, 1.0, true);
    closeWindow(0, true);
    selectedIndex = -1;
    if (RpgWindow* w = win(0xb)) w->activate();
}

// ------------------------------------------------------------------------------------------
// status

void Game::statusWindow(int k) {
    // FUN_004252a3
    if (RpgWindow* w = win(6)) w->deactivate();
    k = std::clamp(k, 0, 7);
    RpgWindow* w = createWindow(9, 0x88, 0x50, 0x1c2, 0xe6, nullptr, -1);
    Battler& m = member(k);
    w->setText(0x15, m.name());
    w->setText(0x16, sjisFmt("Level %d", (int)m.level()));
    w->setText(0x0d, "Hp:");
    w->setText(6, itos(m.hp()));
    w->setText(0x10, "/");
    w->setText(0x12, itos(m.maxHp()));
    w->setText(0x0e, "Mp:");
    w->setText(7, itos(m.mp()));
    w->setText(0x11, "/");
    w->setText(0x13, itos(m.maxMp()));
    w->setText(0x0b, J("経験値:"));
    w->setText(0x0c, itos(m.exp()));
    w->setText(0x30, J("ﾚﾍﾞﾙUPまで:"));
    int L = m.level();
    int rest = L * L * 10 - m.exp();
    if (rest < 0) rest = 0;
    w->setText(0x31, itos(rest));  // the "-----" for level 100 is overwritten (original behaviour)
    w->setText(0x18, J("攻撃力:"));
    w->setText(0x19, J("防御力:"));
    w->setText(0x1a, J("すばやさ:"));
    w->setText(0x32, J("知力:"));
    w->setText(0x1b, itos(m.attack()));
    w->setText(0x1c, itos(m.defence()));
    w->setText(0x1d, itos(m.agi()));
    w->setText(0x33, itos(m.intel()));
    w->setText(0x1e, rpg.coinName + ":");
    w->setText(0x1f, itos(money()));
    static const int slotLabels[5] = {0x20, 0x2d, 0x21, 0x22, 0x23};
    static const int itemLabels[5] = {0x24, 0x2e, 0x25, 0x26, 0x27};
    for (int i = 0; i < 5; i++) w->setText(slotLabels[i], S[10 + i] + ":");
    int a = m.actorId() & 7;
    for (int i = 0; i < 5; i++) {
        int e = pb.equip[a][i];
        w->setText(itemLabels[i], e < 0 ? std::string("---") : items[e].name());
    }
    if (m.hp() < 1) w->setText(0x2c, S[26]);
    else w->setText(0x2c, S[20 + statusIndex(m.status())]);
}

int Game::statusIndex(int status) const {
    // FUN_0042c0b5(status, 0)
    for (int i = 7; i >= 0; i--)
        if (status & (1 << i)) return i + 1;
    return 0;
}

// ------------------------------------------------------------------------------------------
// party order

void Game::orderMenu() {
    // FUN_0042600b
    int n = partySize;
    bool abort = false;
    int k = 0;
    Battler saved[8];
    int order[8];
    for (int i = 0; i < 8; i++) order[i] = -1;
    unsigned mask = 0;
    int h = lineH() * (n + 2) + 6;
    if (RpgWindow* w = win(2)) w->deactivate();
    createWindow(10, 200, 0xbe - h, 200, h, nullptr, -1);
    win(10)->setText(0x28, "1");
    pumpMessages();
    menuWindow(0x50, 0xb4, 0xa0, n, buildText(6, true), 0, 0x10, 1, 6, 0, true);
    while (k < n - 1) {
        selectedIndex = -1;
        waitTenths(0);
        if (quit) return;
        RpgWindow* ow = win(10);
        if (!ow) { abort = true; break; }
        int sel = selectedIndex;
        if (sel >= 0 && sel < 8) {
            if (mask & (1u << sel)) continue;
            ow->setText(0x28 + k, sjisFmt("%d:%s", k + 1, member(sel).name().c_str()));
            saved[k] = member(sel);
            order[k] = sel;
            k++;
            if (k < n) ow->setText(0x28 + k, itos(k + 1));
            if (RpgWindow* mw = win(6)) mw->setDisabled(sel + 1, true);
            mask |= 1u << sel;
        } else if (sel == -1) {
            if (k == 0) { abort = true; break; }
            ow->setText(0x28 + k, " ");
            ow->setText(0x27 + k, itos(k));
            k--;
            mask &= ~(1u << order[k]);
            if (RpgWindow* mw = win(6)) mw->setDisabled(order[k] + 1, false);
        }
        if (RpgWindow* mw = win(6)) { mw->activate(); mw->repaint(); }
    }
    if (abort) {
        closeWindow(6, true);
        closeWindow(10, true);
        if (RpgWindow* w = win(2)) w->activate();
        return;
    }
    closeAllWindows(true);
    for (int i = 0; i < n; i++)
        if ((mask & (1u << i)) == 0) { saved[n - 1] = member(i); break; }
    for (int i = 0; i < partySize; i++) pb.members[i] = saved[i];
    Chara& lead = partyChara[partyActor[0] & 7];
    int oldMove = lead.moveType, oldSpeed = lead.speed;
    Chara old[8];
    for (int i = 0; i < 8; i++) old[i] = partyChara[i];
    int8_t oldActor[8];
    memcpy(oldActor, partyActor, 8);
    for (int i = 0; i < partySize; i++) {
        int a = member(i).actorId() & 7;
        if (!riding) {
            const Chara& o = old[oldActor[i] & 7];
            partyChara[a].x = o.x;
            partyChara[a].y = o.y;
            partyChara[a].destX = o.destX;
            partyChara[a].destY = o.destY;
            partyChara[a].step = o.step;
            partyChara[a].speedCounter = o.speedCounter;
            mapParty[i] = &partyChara[a];
        }
        partyActor[i] = (int8_t)a;
    }
    Chara& nl = partyChara[partyActor[0] & 7];
    nl.moveType = oldMove;
    nl.speed = oldSpeed;
}

// ------------------------------------------------------------------------------------------
// system menu

void Game::systemMenu() {
    // FUN_00427595
    closeAllWindows(true);
    switch (selectedIndex) {
    case 0:
        saveDialog();
        break;
    case 1:
        if (loadDialog()) {
            fadeToBlack(5);
            reloadPalette(true);
            stopMusic();
            abortTick = true;
            int m = curMap;
            curMap = -2;
            int x = mapParty[0]->x, y = mapParty[0]->y;
            AudioEntry e;
            e.a = currentBgm.a;
            e.b = currentBgm.b;
            std::string n = currentBgm.name;
            playBgm(n, &e, true);
            teleport(m, x, y);
        }
        break;
    case 2:
        toggleScreenMode();
        break;
    case 3:
        versionDialog();
        break;
    case 4:
        LOG_INFO("quit selected in the system menu");
        quit = true;
        abortTick = true;
        break;
    default: break;
    }
}

void Game::versionDialog() {
    // FUN_00434975: modeless dialog (resource 22000) filled from the VERSIONINFO of GAME.EXE 1.23
    std::string t = J("ＲＰＧツクール９５\nRuntime Routine ver 1.23\n\nCopyright (c) 1997 by ASCII Corporation\n"
                      "Copyright (c) 1997 by You & Tsune\nAll Rights Reserved.");
    int w = 420, h = 191;
    eventRunning = true;
    RpgWindow* d = createWindow(12, (640 - w) / 2, 69, w, h, nullptr, -1);
    if (Label* l = d->label(1)) { l->r = Rect(4, 16, w - 10, h - 10); l->fmt = 0x11; }
    d->setText(1, t);
    flushKeys(true);
    waitTenths(0);
    closeWindow(12, true);
    flushKeys(true);
    eventRunning = false;
}

// ------------------------------------------------------------------------------------------
// shop and inn (event commands 0x1c / 0x1b)

static std::string textLine(const std::string& s, int n) {
    // FUN_00431558: line n of a '\n' separated text
    size_t p = 0;
    for (int i = 0; i < n; i++) {
        size_t e = s.find('\n', p);
        if (e == std::string::npos) return "";
        p = e + 1;
    }
    size_t e = s.find('\n', p);
    return s.substr(p, e == std::string::npos ? std::string::npos : e - p);
}

void Game::inn(const EventCommand& c) {
    // FUN_00431583
    const std::string& msg = script.str(c.a1);
    std::string text = sjisFmt(J("%s（一泊%d%s）").c_str(), textLine(msg, 0).c_str(), c.a2, rpg.coinName.c_str());
    bool ok;
    std::string ex = expandText(text, ok);
    sysMessage(ok ? ex : text, 0.1, false);
    int maxLen = std::max(sjisLength(S[112]), sjisLength(S[113]));
    menuWindow(-1, 100, 0x8c, 2, S[112] + "\n" + S[113], maxLen, 0x11, 1, 6, 0, false);
    waitTenths(0);
    closeWindow(6, true);
    if (selectedIndex == 0) {
        if (money() < c.a2) {
            text = S[114];
        } else {
            money() -= c.a2;
            closeWindow(0, true);
            fadeToBlack(7);
            saveBgm();
            playBgm(J("演奏停止"), nullptr, false);
            bool mid = audioSlots[11].name.find(".mid") != std::string::npos;
            if (mid) midiOnce = true;
            playVehicleBgm(11, true);
            uint8_t b0 = (uint8_t)c.a3, b1 = (uint8_t)(c.a3 >> 8);
            for (int i = 0; i < partySize; i++) {
                Battler& m = member(i);
                if (b1 & 0x40) m.setHp(m.maxHp());
                if (b1 & 0x20) m.setMp(m.maxMp());
                if (b0 & 2) m.setStatus(m.status() & 0xdd);
                if (b0 & 1) m.setStatus(m.status() & 0xde);
            }
            waitTenths(10);
            waitJingle(mid);
            restoreBgm(false);
            gatherParty();
            redraw(-2, true);
            display().present();
            display().effect(7, 100, 100, 100, 0, true);
            screenState = 0;
            text = textLine(msg, 1);
        }
    } else {
        text = textLine(msg, 2);
    }
    ex = expandText(text, ok);
    sysMessage(ok ? ex : text, 1.0, false);
    closeAllWindows(true);
}

void Game::shop(const EventCommand& c) {
    // FUN_004318f5
    const std::string& msg = script.str(c.a2);
    int deals = 0;
    menuActor = 0;
    bool ok;
    bool skipClose = false;
    while (!quit) {
        if (deals < 1) {
            std::string l = textLine(msg, 0);
            std::string ex = expandText(l, ok);
            sysMessage(ok ? ex : l, 0.1, false);
        } else {
            sysMessage(S[105], 0.1, false);
        }
        std::string text = c.a3 == 1 ? S[100] + "\n" + S[102] : S[100] + "\n" + S[101] + "\n" + S[102];
        int maxLen = sjisLength(S[100]);
        if (c.a3 == 0 && maxLen <= sjisLength(S[101])) maxLen = sjisLength(S[101]);
        if (maxLen <= sjisLength(S[102])) maxLen = sjisLength(S[102]);
        if (maxLen < 6) maxLen = 6;
        if (!win(6)) menuWindow(-1, 0x66, 0x8c, c.a3 == 1 ? 2 : 3, text, maxLen, 0x11, 1, 6, 0, false);
        waitTenths(0);
        int sel = selectedIndex;
        if (sel == -1 || (c.a3 != 1 && sel == 2) || (c.a3 == 1 && sel == 1)) break;
        listPage = 0;
        bool buying = sel == 0;
        int n;
        if (buying) {
            n = buildShopList(script.str(c.a1));
            listCount = n;
            if (n == 0) { popup(6, S[106]); continue; }
            closeWindow(6, true);
            sysMessage(sjisFmt(J("%s （所持金: %d%s）").c_str(), S[103].c_str(), money(), rpg.coinName.c_str()), 0.1, false);
        } else {
            n = buildItemList(8, 0);
            listCount = n;
            if (n == 0) { popup(6, S[90]); continue; }
            closeWindow(6, true);
            sysMessage(sjisFmt(J("%s (所持金: %d%s)").c_str(), S[104].c_str(), money(), rpg.coinName.c_str()), 0.1, false);
        }
        selectedIndex = -1;
        if (!win(8)) listWindow(8, 200, 0x66, n);
        fillShopList(0, n, !buying);
        setPageLabels(win(8), listPage, n);
        waitTenths(0);
        closeWindow(8, true);
        int s = selectedIndex;
        if (s == -1) continue;
        if (s < 0 || s >= listCount) { skipClose = true; break; }
        int it = listIndex[s];
        if (!buying) {
            if (items[it].type() == 5 || items[it].cost() < 2) {
                sysMessage(S[110], 1.0, false);
            } else {
                itemCounts[it]--;
                money() += items[it].cost() / 2;
                if (money() > 100000000) money() = 100000000;
                deals++;
            }
        } else {
            if (itemCounts[it] < rpg.itemMax) {
                if (money() < items[it].cost()) {
                    sysMessage(S[108], 1.0, false);
                } else {
                    itemCounts[it]++;
                    money() -= items[it].cost();
                    deals++;
                }
            } else {
                sysMessage(S[107], 1.0, false);
            }
        }
    }
    if (!skipClose) closeWindow(6, true);
    std::string l;
    if (deals < 1) l = textLine(msg, 2);
    else { l = textLine(msg, 1); pageRefresh = true; }
    std::string ex = expandText(l, ok);
    sysMessage(ok ? ex : l, 1.0, false);
    closeAllWindows(true);
}

}  // namespace rm95
