// Pictures, screen tone, erase screen, movies (FUN_00430ec0, FUN_00431455, FUN_004320cb, FUN_00435377)
#include "game.h"
#include "../platform/platform.h"
#include "../util/fs.h"
#include "../util/log.h"
#include "../audio/audio.h"
#include "../media/movie.h"
#include <algorithm>

namespace rm95 {

static int posX(int h, int w) {
    // FUN_00430da3
    int x = 0;
    if (h == 0) x = (640 - w < 40) ? 0 : 20;
    else if (h == 1) x = (640 - w) / 2;
    else if (h == 2) x = (640 - w) - ((640 - w < 40) ? 0 : 20);
    if (x + w > 640) x = 640 - w;
    if (x < 0) x = 0;
    return x;
}

static int posY(int v, int h) {
    int y = 0;
    if (v == 0) y = (480 - h < 40) ? 0 : 20;
    else if (v == 1) y = (480 - h) / 2;
    else if (v == 2) y = (480 - h) - ((480 - h < 40) ? 0 : 20);
    if (y + h > 480) y = 480 - h;
    if (y < 0) y = 0;
    return y;
}

void Game::showPicture(const EventCommand& c) {
    std::string path = resolveMedia(script.str(c.a1));
    if (path.empty()) return;
    closeWindow(0, true);
    Image img;
    if (!loadBmp(path, img)) return;
    Display& d = display();
    int flags = c.a2;
    int transition = flags >> 16;
    if (!(flags & 4)) {
        if (screenState == 0) screenState = 1;
    } else {
        if (!haveSavedPalette) { savedPalette = d.base; haveSavedPalette = true; }
        if (screenState == 0 || screenState == 1) fadeToBlack(5);
        d.back.fill(d.indexOf(0));
        if (screenState > 0) screenState = 2;
        d.loadPaletteSource(img, true);
    }
    Surface pic = imageToSurface(img, d.hw);
    int w = img.width, h = img.height;
    if (w > 640 || h > 480) flags |= 1;
    if (flags & 2) pic.colorKey = pic.px.empty() ? -1 : pic.px[0];
    int p = (flags >> 4) & 0xf;
    int ph = p % 3, pv = p / 3;
    Rect dst;
    if (!(flags & 1)) {
        int ww = std::min(w, 640), hh = std::min(h, 480);
        int x = posX(ph, ww), y = posY(pv, hh);
        dst = Rect(x, y, x + ww, y + hh);
    } else if (!(flags & 2)) {
        dst = Rect(0, 0, 640, 480);
    } else {
        int ww, hh, x, y;
        if ((h * 4) / 3 < w) { hh = (h * 640) / w; ww = 640; y = posY(pv, hh); x = 0; }
        else { ww = (w * 480) / h; hh = 480; y = 0; x = posX(ph, ww); }
        dst = Rect(x, y, x + ww, y + hh);
    }
    d.back.stretchBlit(dst, pic, Rect(0, 0, w, h), (flags & 2) != 0);
    d.show(transition, &dst);
    if (flags & 8) waitTenths(0);
    if (screenState == -1) screenState = -2;
}

void Game::erasePicture(int t) {
    closeWindow(0, true);
    Display& d = display();
    if (screenState == -1) return;
    if (screenState == -2) d.hide(t, true);
    else if (screenState == 1) {
        redraw(-2, false);
        d.hide(t, false);
        if (t == 13) d.show(13);
        screenState = 0;
    } else if (screenState == 2) {
        d.hide(t, true);
        reloadPalette(true);
        screenState = 3;
        showMap();
    } else {
        d.hide(t, true);
        screenState = 3;
    }
}

void Game::screenTone(const EventCommand& c) {
    pumpMessages();
    closeWindow(0, true);
    uint8_t v[4] = {(uint8_t)(c.a2 & 0xff), (uint8_t)((c.a2 >> 8) & 0xff), (uint8_t)((c.a2 >> 16) & 0xff), (uint8_t)((uint32_t)c.a2 >> 24)};
    if (screenState == 3) display().effect(4, v, true);
    else display().effect(c.a1, v, true);
}

void Game::playMovie(const EventCommand& c) {
    // FUN_00435377 (the original plays the AVI with MCI "avivideo" in a child window)
    std::string path = resolveMedia(script.str(c.a1));
    if (path.empty()) return;
    closeWindow(0, true);
    saveBgm();
    stopMusic();
    for (int i = 0; i < 4; i++) { audio().waveStop(i); seChannelBusy[i] = 0; }
    Display& d = display();
    int flags = c.a2;
    int prevState = screenState;
    if (flags & 4) {
        if (screenState == 0) d.effect(5, 0, 0, 0, 0, false);
        d.primary.fill(d.indexOf(0));
        platform().markDirty();
        screenState = 4;
    }
    flushKeys(false);
    okPressed = false;
    Movie movie;
    std::string err;
    if (!movie.open(path, err)) {
        LOG_WARN("avi open: %s (%s)", path.c_str(), err.c_str());
    } else {
        int w = movie.width(), h = movie.height();
        LOG_INFO("movie %s: %dx%d, %d frames", path.c_str(), w, h, movie.frameCount());
        int x, y;
        if (!(flags & 1)) {
            int p = (flags >> 4) & 0xf;
            int ph = p % 3, pv = p / 3;
            x = ph == 0 ? 24 : ph == 1 ? (640 - w) / 2 : std::max(0, 616 - w);
            y = pv == 0 ? 24 : pv == 1 ? (480 - h) / 2 : std::max(0, 480 - h - 24);
        } else {
            if (h < w) { h = h * 640 / w; w = 640; x = 0; y = (480 - h) / 2; }
            else { w = w * 480 / h; h = 480; y = 0; x = (640 - w) / 2; }
            w = std::min(w, 639);
            h = std::min(h, 479);
        }
        Overlay ov;
        ov.x = x;
        ov.y = y;
        ov.rgbW = w;
        ov.rgbH = h;
        ov.rgb.assign((size_t)w * h, 0);
        d.overlays.push_back(&ov);
        movieBreak = false;
        moviePlaying = true;
        uint32_t t0 = platform().ticks();
        int shown = -1;
        std::vector<uint32_t> fr;
        while (!quit && !okPressed && !movieBreak) {
            int i = (int)((platform().ticks() - t0) / movie.frameMs());
            if (i >= movie.frameCount()) break;
            if (i != shown && movie.frame(i, fr)) {
                int mw = movie.width(), mh = movie.height();
                for (int yy = 0; yy < h; yy++) {
                    const uint32_t* src = &fr[(size_t)(yy * mh / h) * mw];
                    uint32_t* dst = &ov.rgb[(size_t)yy * w];
                    for (int xx = 0; xx < w; xx++) dst[xx] = src[xx * mw / w];
                }
                shown = i;
                platform().markDirty();
            }
            pumpMessages();
            platform().sleep(1);
        }
        moviePlaying = false;
        auto& o = d.overlays;
        o.erase(std::remove(o.begin(), o.end(), &ov), o.end());
        platform().markDirty();
        okPressed = false;
    }
    if (!(flags & 4)) {
        reloadPalette(false);
        platform().markDirty();
    } else {
        d.primary.fill(d.indexOf(0));
        d.effect(4, 0, 0, 0, 0, false);
        d.present();
        d.effect(5, nullptr, false);
        screenState = prevState;
    }
    restoreBgm(false);
}

}  // namespace rm95
