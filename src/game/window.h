#pragma once
#include "../gfx/display.h"
#include "../gfx/font.h"
#include <memory>
#include <string>
#include <vector>

namespace rm95 {

class Game;

// COLORREF constants (OWL TColor) and their static palette indices
enum : uint8_t { kIdxBlack = 0, kIdxLtGray = 7, kIdxGray = 248, kIdxRed = 249, kIdxBlue = 252, kIdxWhite = 255 };

struct Label {
    Rect r;              // client coordinates
    uint8_t color = kIdxBlack;
    unsigned fmt = 0;
    std::string text;
    size_t cap = 100;
    bool disabled = false;
};

// Layout block passed to the window constructor (FUN_0042e50b): {l0, rows, cols, mode, fmt, text, xoff, yoff}
struct WindowLayout {
    int l0 = 0, rows = 1, cols = 1, mode = 1;
    unsigned fmt = 0x10;
    std::string text;
    bool hasText = false;
    int xoff = 0, yoff = 0;
};

// The RPG window class of GAME.EXE (ctor FUN_0041072b): an OWL child window with up to 60 labels.
class RpgWindow {
public:
    RpgWindow(Game& g, int type, int x, int y, int w, int h, bool transparent, bool battle, const WindowLayout* layout, int itemCount);
    ~RpgWindow();

    int type;
    int x, y, w, h;               // outer rect
    int rows = 0, cols = 1, mode = 1;
    unsigned itemFmt = 0x10;
    int xoff = 0, yoff = 0;
    int itemCount = -1;            // +0x316
    int curCol = -1, curRow = -1;  // +0x1e6, +0x1ea
    int lastSel = 0;               // +0x1ee
    int colW = 0;                  // +0x1f2
    int page = 0;                  // +0x202
    bool transparent = false;      // +0x302
    bool battle = false;
    uint8_t textColor = kIdxBlack; // +0x1ca
    bool selected = false;         // +0x2fa
    bool inactive = false;         // +0x30a
    bool lastPage = false;         // +0x306
    int lineH = 18;                // +0x30e
    bool hasLayout = false;
    std::unique_ptr<Label> labels[60];
    // << (id 1) and >> (id 2) push buttons of list windows
    bool hasButtons = false, prevEnabled = false, nextEnabled = true;

    Label* label(int i) { return (i >= 0 && i < 60) ? labels[i].get() : nullptr; }
    void setText(int i, const std::string& s);        // FUN_004140ff
    void setLabelColor(int i, uint8_t c) { if (Label* l = label(i)) { l->color = c; repaint(); } }
    void setDisabled(int i, bool d) { if (Label* l = label(i)) { l->disabled = d; repaint(); } }
    void setFormat(int i, unsigned f) { if (Label* l = label(i)) { l->fmt = f; } }
    void repaint();                                    // WM_PAINT
    void drawCursor(int col, int row, bool show);      // FUN_00411a9b
    void onKey(int vk);                                // FUN_00414268
    void activate();                                   // msg 0x403
    void deactivate();                                 // msg 0x40e
    void endOfList();                                  // msg 0x411
    void setTransparent(bool t);                       // FUN_00413dba / FUN_00413de0
    int clientW() const { return w - 6; }
    int clientH() const { return h - 6; }
    Overlay overlay;

private:
    void createLabels();                                // FUN_0041239f / FUN_00411f73
    void addLabel(int idx, int x, int y, int w, int h, unsigned fmt, size_t cap);
    void captureBackground();                           // FUN_00410c97
    void drawFrame();
    void drawLabel(Label& l);
    void drawButtons();
    bool validTarget(int row) const;                    // FUN_00413f4a
    void changePage(int delta);
    Game& g_;
    Surface bg_;                                        // transparent background snapshot (client size)
    bool haveBg_ = false;
    WindowLayout layout_;
};

}  // namespace rm95
