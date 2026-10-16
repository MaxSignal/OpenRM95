#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace rm95 {

class Display;

// Windows virtual key codes used by GAME.EXE
enum Vk {
    VK_BACK_ = 0x08, VK_RETURN_ = 0x0d, VK_SHIFT_ = 0x10, VK_CONTROL_ = 0x11, VK_ESCAPE_ = 0x1b, VK_SPACE_ = 0x20,
    VK_LEFT_ = 0x25, VK_UP_ = 0x26, VK_RIGHT_ = 0x27, VK_DOWN_ = 0x28,
    VK_NUMPAD2_ = 0x62, VK_NUMPAD4_ = 0x64, VK_NUMPAD6_ = 0x66, VK_NUMPAD8_ = 0x68,
    VK_F3_ = 0x72, VK_F4_ = 0x73, VK_F12_ = 0x7b
};

struct InputEvent {
    enum Type { KeyDown, KeyUp, MouseDown, MouseUp, MouseMove, MouseDblClick, RightDown, Quit } type;
    int vk = 0;
    int x = 0, y = 0;
    bool repeat = false;
};

// SDL backed platform: window, timing, input queue, presentation.
class Platform {
public:
    bool init(const std::string& title, int scale, bool fullscreen, bool headless);
    void shutdown();

    uint32_t ticks() const;            // GetTickCount / timeGetTime equivalent (ms)
    // Polls the OS event queue into the internal input queue and refreshes the screen if needed.
    void poll();
    // Sleeps about `ms` milliseconds while polling (busy waits of the original).
    void sleep(uint32_t ms);
    // Pops one queued input event (the "Windows message queue").
    bool popEvent(InputEvent& ev);
    void clearEvents(bool keysOnly);
    bool keyHeld(int vk) const;         // GetAsyncKeyState(vk) & 0x8000
    bool quitRequested() const { return quit_; }
    void requestQuit() { quit_ = true; }

    void setDisplay(Display* d) { display_ = d; }
    void markDirty() { dirty_ = true; }
    void refresh(bool force = false);   // composes and presents if dirty
    void setTitle(const std::string& utf8);
    void toggleFullscreen();
    bool saveScreenshot(const std::string& path);

    // automation (for testing): a script of timed key presses, see main.cpp --keys
    struct ScriptedKey { uint32_t at; int vk; bool down; };
    std::deque<ScriptedKey> script;
    std::function<void(uint32_t)> onFrame;  // called on every refresh (screenshots in tests)
    bool fastMode = false;                   // headless test mode: virtual clock
    void advanceVirtual(uint32_t ms) { virtualTime_ += ms; }

private:
    void pushKey(int vk, bool down, bool repeat);
    void compose();
    Display* display_ = nullptr;
    bool quit_ = false;
    bool dirty_ = true;
    bool headless_ = false;
    uint32_t lastPresent_ = 0;
    uint32_t virtualTime_ = 0;
    std::deque<InputEvent> queue_;
    bool held_[256] = {};
    void* window_ = nullptr;
    void* renderer_ = nullptr;
    void* texture_ = nullptr;
    int scale_ = 1;
    std::vector<uint32_t> frame_;
};

Platform& platform();

}  // namespace rm95
