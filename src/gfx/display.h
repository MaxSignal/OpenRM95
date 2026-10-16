#pragma once
#include "surface.h"
#include <array>
#include <vector>

namespace rm95 {

// A GDI child window drawn on top of the DirectDraw image (message/menu windows).
struct Overlay {
    int x = 0, y = 0;
    Surface surf;          // outer window incl. frame, palette indices
    bool visible = true;
};

// Emulation of the DirectDraw wrapper object of GAME.EXE (DAT_0046ebc8): 8-bit back buffer and
// primary surface, palette management with tint, palette effects and screen transitions.
class Display {
public:
    static constexpr int W = 640, H = 480;
    Display();

    Surface back;      // DD+0x810
    Surface primary;   // what is on screen (DD+0x80c)
    Surface cache;     // DAT_0046ebcc background cache

    Palette work{};    // DD+0x000 raw palette of the palette source
    Palette base{};    // DD+0x400 committed palette
    Palette hw{};      // currently realised hardware palette
    uint8_t tint[4] = {100, 100, 100, 0};
    bool fullscreen = true;   // fade timing (steps doubled, 30 ms waits)

    std::vector<Overlay*> overlays;  // z-order: later = on top

    // --- palette ---
    void setEntries(const Palette& p);                      // SetEntries(0,0,256,p)
    void setEntriesBase() { setEntries(base); }
    // FUN_004175b8: palette from an image (entries 0-9/246-255 replaced by system colours)
    void loadPaletteSource(const Image& img, bool commit);
    // FUN_004173d2
    void setPalette(const Palette* entries, bool commit);
    // FUN_00419ab7(DD, mode, rgbg, commitTint). rgbg == nullptr -> use tint
    void effect(int mode, const uint8_t* rgbg, bool commitTint);
    void effect(int mode, int r, int g, int b, int grey, bool commitTint) {
        uint8_t v[4] = {(uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)grey}; effect(mode, v, commitTint);
    }
    void computeTinted(const uint8_t* rgbg, Palette& out, bool& changed) const;
    void fade(const Palette& target, const Palette& from, int steps, bool commit);  // FUN_00419fdf
    uint8_t indexOf(uint32_t colorref) const;              // FUN_00417641: nearest index in hw palette

    // --- presentation ---
    void present();                                         // FUN_00417733 (vsync + full Blt back->primary)
    void presentRect(Rect src, int dx, int dy);             // FUN_00416f1e
    void presentRect(Rect r) { presentRect(r, r.left, r.top); }
    void fillBack(uint32_t colorref);                        // FUN_0041a2cf
    void blackBack() { back.fill(indexOf(0)); }

    // --- transitions ---
    void show(int type, const Rect* rect = nullptr);        // FUN_00418050
    void hide(int type, bool blacken, const Rect* rect = nullptr);  // FUN_004177d9

    void waitMs(int ms);       // FUN_0041c70e busy wait (keeps the window alive)
    void vsync();              // WaitForVerticalBlank
private:
    void blitBlock(int x0, int y0, int x1, int y1);         // primary <- back block
};

Display& display();

}  // namespace rm95
