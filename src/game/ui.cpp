// Window helpers, message loop and input (FUN_0042ecd0, FUN_00428135, FUN_0042dd9b ...), see spec/ui.md
#include "game.h"
#include "../audio/audio.h"
#include "../platform/platform.h"
#include "../util/log.h"
#include "../util/sjis.h"
#include <algorithm>
#include <cmath>

namespace rm95 {

int Game::lineH() const { return uiFont().lineHeight(); }
int Game::charW() const { return uiFont().maxCharWidth(); }

RpgWindow* Game::createWindow(int type, int x, int y, int w, int h, const WindowLayout* layout, int itemCount) {
    windows[type].reset();
    focusedType = type;
    windows[type] = std::make_unique<RpgWindow>(*this, type, x, y, w, h, transparentWindows, inBattle, layout, itemCount);
    return windows[type].get();
}

void Game::closeWindow(int type, bool redraw_) {
    if (type < 0 || type >= 13 || !windows[type]) return;
    windows[type].reset();
    if (redraw_) {
        platform().markDirty();
        pumpMessages();
    }
}

void Game::closeAllWindows(bool closeMenu) {
    for (int i = 0; i < 13; i++) closeWindow(i, false);
    pumpMessages();
    if (closeMenu) menuRequested = false;
}

static int roundWidth(int w) {
    if (w < 0) w += 0x1f;
    return ((w >> 5) + 1) * 32;
}

RpgWindow* Game::menuWindow(int x, int y, int w, int rows, const std::string& text, int maxLen, unsigned fmt, int l0, int type,
                            unsigned disableMask, bool colourByState) {
    selectedIndex = -1;
    int h = lineH() * rows + 6;
    if (maxLen > 0) w = (maxLen + 1) * charW();
    int W = roundWidth(w);
    if (y + h > 480) y = 440 - h;
    if (x < 0) x = (640 - W) >> 1;
    RpgWindow* win_ = nullptr;
    if (!windows[type]) {
        WindowLayout l;
        l.l0 = l0; l.rows = rows; l.cols = 1; l.mode = 1; l.fmt = fmt; l.text = text; l.hasText = true;
        win_ = createWindow(type, x, y, W, h, &l, rows);
        if (Label* lb = win_->label(0)) lb->fmt = fmt;
        for (int i = 0; i < rows; i++)
            if ((1u << i) & disableMask) if (Label* lb = win_->label(i + 1)) lb->disabled = true;
        if (colourByState)
            for (int i = 0; i < rows; i++) {
                Battler& m = member(i);
                if (Label* lb = win_->label(i + 1)) lb->color = (m.status() == 0 && m.hp() > 0) ? (transparentWindows ? kIdxWhite : kIdxBlack) : kIdxRed;
            }
        win_->repaint();
    }
    return windows[type].get();
}

RpgWindow* Game::partyWindow(int x, int y, int w, int rows, const std::string& text, int maxLen, unsigned fmt, int l0, int type,
                             unsigned disableMask, bool mp) {
    selectedIndex = -1;
    int h = lineH() * rows + 6;
    if (maxLen > 0) w = charW() * (maxLen + 1);
    int W = roundWidth(w);
    if (y + h > 480) y = 440 - h;
    if (x < 0) x = (640 - W) >> 1;
    if (!windows[type]) {
        WindowLayout l;
        l.l0 = l0; l.rows = rows; l.cols = 1; l.mode = 1; l.fmt = fmt; l.text = text; l.hasText = true;
        RpgWindow* wn = createWindow(type, x, y, W, h, &l, rows);
        if (Label* lb = wn->label(0)) lb->fmt = fmt;
        for (int i = 0; i < rows; i++)
            if ((1u << i) & disableMask) if (Label* lb = wn->label(i + 1)) lb->disabled = true;
        for (int i = 0; i < rows; i++) {
            Battler& m = member(i);
            if (Label* lb = wn->label(i + 1)) lb->color = (m.status() == 0 && m.hp() > 0) ? (transparentWindows ? kIdxWhite : kIdxBlack) : kIdxRed;
        }
        for (int i = 0; i < rows; i++) {
            Battler& m = member(i);
            char b[32];
            wn->labels[i * 4 + rows + 1] ? (void)0 : (void)0;
            if (Label* lb = wn->label(i * 4 + rows + 1)) lb->text = mp ? "MP" : "HP";
            snprintf(b, sizeof b, "%d", mp ? m.mp() : m.hp());
            if (Label* lb = wn->label(i * 4 + rows + 2)) lb->text = b;
            if (Label* lb = wn->label(i * 4 + rows + 3)) lb->text = "/";
            snprintf(b, sizeof b, "%d", mp ? m.maxMp() : m.maxHp());
            if (Label* lb = wn->label(i * 4 + rows + 4)) lb->text = b;
        }
        wn->repaint();
    }
    return windows[type].get();
}

void Game::popup(int ownerType, const std::string& text) {
    RpgWindow* owner = win(ownerType);
    int ox = owner ? owner->x : 0, oy = owner ? owner->y : 0;
    if (mouseSelX != -1) oy = mouseSelY - 10;
    int w = sjisLength(text) * charW();
    if (owner) owner->deactivate();
    createWindow(12, ox + 0x3c, oy + 10, w, 6 + lineH() + 8, nullptr, -1);
    win(12)->setText(1, text);
    waitTenths(15);
    closeWindow(12, true);
    if (RpgWindow* o = win(ownerType)) { o->activate(); }
}

void Game::sysMessage(const std::string& text, double factor, bool bottom) {
    waitTenths(1);
    if (!win(0)) createWindow(0, 0x5c, bottom ? 0x168 : 0x20, 0x1c8, 0x5c, nullptr, -1);
    win(0)->setText(1, text);
    platform().refresh(true);
    int n = (int)(msgSpeed * factor);
    if (factor > 0 && n == 0) n = 1;
    waitTenths(n);
    if (!menuRequested && !eventRunning && !inBattle) closeWindow(0, true);
}

// ------------------------------------------------------------------------------------------
// message loop

void Game::pollAudio() {
    // MM_MCINOTIFY (FUN_0042fa38): replay the MIDI when it ends unless "play once"
    if (audio().midiEndedNotify()) {
        if (!midiOnce) audio().midiReplay();
        else midiOnce = false;
    }
}

void Game::pumpMessages() {
    platform().poll();
    pollAudio();
    while (true) {
        if (!posted.empty()) {
            PostedMessage m = posted.front();
            posted.pop_front();
            handlePosted(m);
            continue;
        }
        InputEvent e;
        if (!platform().popEvent(e)) break;
        switch (e.type) {
        case InputEvent::KeyDown: onKeyDown(e.vk); break;
        case InputEvent::KeyUp: onKeyUp(e.vk); break;
        case InputEvent::Quit: quit = true; abortTick = true; break;
        case InputEvent::MouseDown:
        case InputEvent::MouseDblClick: {
            lastMouseX = e.x;
            lastMouseY = e.y;
            // a click on a window selects the item under the cursor
            for (int t = 12; t >= 0; t--) {
                RpgWindow* w = win(t);
                if (!w) continue;
                int cx = e.x - w->x - 3, cy = e.y - w->y - 3;
                if (cx < 0 || cy < 0 || cx >= w->clientW() || cy >= w->clientH()) continue;
                if (w->inactive) break;
                if (w->itemCount == -1) { post(0x401, t, 0); break; }
                int row = cy / w->lineH;
                if (row < w->rows && cy < 180) {
                    w->drawCursor(w->curCol, w->curRow, false);
                    w->curRow = row;
                    w->drawCursor(w->curCol, w->curRow, true);
                    int idx = w->page * 10 + row * w->cols + w->curCol;
                    Label* l = w->label(1 + idx % 10);
                    if (idx >= 0 && idx < w->itemCount && l && !l->disabled) { w->selected = true; post(0x401, t, idx); }
                }
                break;
            }
            if (keyReleased) okPressed = true;
            break;
        }
        case InputEvent::RightDown:
            if (RpgWindow* w = win(focusedType)) post(0x402, w->type, 0);
            else onKeyDown(VK_ESCAPE_), onKeyUp(VK_ESCAPE_);
            break;
        default: break;
        }
    }
}

void Game::onKeyUp(int) { keyReleased = true; }

void Game::onKeyDown(int vk) {
    pumpMessages();
    if (vk == VK_F4_) { platform().toggleFullscreen(); return; }
    if (vk == VK_F12_) { platform().saveScreenshot("screenshot.ppm"); return; }
    if ((vk == VK_SPACE_ || vk == VK_RETURN_) && keyReleased) okPressed = true;
    if (heroChara && heroChara->moveType == 9) heroChara->moveType = 5;
    if (!menuRequested && !eventRunning && !inBattle && vk != VK_ESCAPE_) {
        if ((vk != VK_SPACE_ && vk != VK_RETURN_) || lastKey == -1) lastKey = vk;
        return;
    }
    if ((vk == VK_ESCAPE_ || vk == VK_SPACE_ || vk == VK_RETURN_) && keyReleased) okPressed = true;
    if (!win(2) && vk == VK_ESCAPE_) {
        if (inBattle && autoBattle) autoBattle = false;
        if (!menuRequested && !inBattle && !eventRunning && mapParty[0] && mapParty[0]->step == 0) menuRequested = true;
        else {
            menuRequested = false;
            closeWindow(2, false);
        }
    }
    keyReleased = false;
    RpgWindow* f = win(focusedType);
    if (!f) {
        if (vk == VK_SPACE_ || vk == VK_RETURN_) {
            if (keyReleased && win(0) && !inBattle) closeWindow(0, false);
        } else if (!eventRunning && win(0) && !inBattle) {
            closeWindow(0, false);
        }
        keyReleased = false;
    } else {
        post(0x403, f->type, 0);
        f->onKey(vk);
    }
}

void Game::handlePosted(const PostedMessage& m) {
    switch (m.msg) {
    case 0x401: onSelect(m.wparam, m.lparam); break;
    case 0x402: onCancel(m.wparam); break;
    case 0x403: if (RpgWindow* w = win(m.wparam)) w->activate(); break;
    case 0x40e: if (RpgWindow* w = win(m.wparam)) w->deactivate(); break;
    case 0x40f:
    case 0x410: onPageChange(m.wparam, m.lparam); break;
    case 0x411: if (RpgWindow* w = win(m.wparam)) w->endOfList(); break;
    default: break;
    }
}

void Game::waitTenths(int n, bool movieWait) {
    okPressed = false;
    uint32_t t0 = platform().ticks();
    int elapsed = 0;
    while (true) {
        if (okPressed || (n <= elapsed && n != 0)) break;
        if (platform().keyHeld(VK_SHIFT_) && inBattle) break;
        pumpMessages();
        if (quit) break;
        uint32_t t = platform().ticks();
        if (n > 0) elapsed = (int)((t - t0) / 100);
        if (movieWait && n == 0 /* && no movie window */) break;
        platform().sleep(1);
    }
    if (movieWait && okPressed) okPressed = false;
}

void Game::flushKeys(bool wait) {
    // GetAsyncKeyState of the arrows/space/enter just clears their "pressed" bits; wait for Esc release
    while (platform().keyHeld(VK_ESCAPE_) && !quit) { platform().sleep(1); }
    if (wait) {
        while (!keyReleased && !quit) {
            pumpMessages();
            platform().sleep(1);
            bool any = false;
            for (int vk : {VK_RETURN_, VK_SPACE_, VK_ESCAPE_, VK_UP_, VK_DOWN_, VK_LEFT_, VK_RIGHT_})
                if (platform().keyHeld(vk)) any = true;
            if (!any) keyReleased = true;
        }
        platform().clearEvents(true);
    }
}

bool Game::keyHeldAsync(int vk) {
    if (platform().keyHeld(vk)) return true;
    keyReleased = true;
    return false;
}

void Game::delayMs(int ms) { display().waitMs(ms); }

}  // namespace rm95
