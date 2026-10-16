#include "platform.h"
#include "../gfx/display.h"
#include "../util/log.h"
#include <SDL.h>
#include <cstring>
#include <fstream>

namespace rm95 {

Platform& platform() {
    static Platform p;
    return p;
}

bool Platform::init(const std::string& title, int scale, bool fullscreen, bool headless) {
    headless_ = headless;
    scale_ = scale < 1 ? 1 : scale;
    frame_.assign(640 * 480, 0);
    SDL_SetMainReady();
    if (headless) {
        SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
        if (SDL_Init(SDL_INIT_TIMER) != 0) { LOG_ERROR("SDL_Init: %s", SDL_GetError()); return false; }
        return true;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
        LOG_ERROR("SDL_Init: %s", SDL_GetError());
        return false;
    }
    Uint32 flags = SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    SDL_Window* w = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640 * scale_, 480 * scale_, flags);
    if (!w) { LOG_ERROR("SDL_CreateWindow: %s", SDL_GetError()); return false; }
    SDL_Renderer* r = SDL_CreateRenderer(w, -1, SDL_RENDERER_ACCELERATED);
    if (!r) r = SDL_CreateRenderer(w, -1, SDL_RENDERER_SOFTWARE);
    if (!r) { LOG_ERROR("SDL_CreateRenderer: %s", SDL_GetError()); return false; }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_RenderSetLogicalSize(r, 640, 480);
    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 640, 480);
    window_ = w; renderer_ = r; texture_ = t;
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) SDL_GameControllerOpen(i);
    return true;
}

void Platform::shutdown() {
    if (texture_) SDL_DestroyTexture((SDL_Texture*)texture_);
    if (renderer_) SDL_DestroyRenderer((SDL_Renderer*)renderer_);
    if (window_) SDL_DestroyWindow((SDL_Window*)window_);
    texture_ = renderer_ = window_ = nullptr;
    SDL_Quit();
}

uint32_t Platform::ticks() const { return fastMode ? virtualTime_ : SDL_GetTicks(); }

static int sdlToVk(SDL_Keycode k) {
    switch (k) {
    case SDLK_RETURN: case SDLK_KP_ENTER: return VK_RETURN_;
    case SDLK_SPACE: return VK_SPACE_;
    case SDLK_ESCAPE: return VK_ESCAPE_;
    case SDLK_LEFT: return VK_LEFT_;
    case SDLK_RIGHT: return VK_RIGHT_;
    case SDLK_UP: return VK_UP_;
    case SDLK_DOWN: return VK_DOWN_;
    case SDLK_KP_8: return VK_NUMPAD8_;
    case SDLK_KP_4: return VK_NUMPAD4_;
    case SDLK_KP_2: return VK_NUMPAD2_;
    case SDLK_KP_6: return VK_NUMPAD6_;
    case SDLK_LSHIFT: case SDLK_RSHIFT: return VK_SHIFT_;
    case SDLK_LCTRL: case SDLK_RCTRL: return VK_CONTROL_;
    case SDLK_F3: return VK_F3_;
    case SDLK_F4: return VK_F4_;
    case SDLK_F12: return VK_F12_;
    case SDLK_BACKSPACE: return VK_BACK_;
    default:
        if (k >= SDLK_a && k <= SDLK_z) return (int)(k - SDLK_a + 'A');
        if (k >= SDLK_0 && k <= SDLK_9) return (int)k;
        return 0;
    }
}

void Platform::pushKey(int vk, bool down, bool repeat) {
    if (vk <= 0 || vk > 255) return;
    held_[vk] = down;
    InputEvent e;
    e.type = down ? InputEvent::KeyDown : InputEvent::KeyUp;
    e.vk = vk;
    e.repeat = repeat;
    queue_.push_back(e);
}

void Platform::poll() {
    if (fastMode) {
        virtualTime_ += 1;
        if (onFrame) onFrame(virtualTime_);
        while (!script.empty() && script.front().at <= virtualTime_) {
            auto k = script.front();
            script.pop_front();
            pushKey(k.vk, k.down, false);
        }
    }
    if (!headless_) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT: quit_ = true; { InputEvent q; q.type = InputEvent::Quit; queue_.push_back(q); } break;
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_RETURN && (ev.key.keysym.mod & KMOD_ALT)) {
                    toggleFullscreen();
                    break;
                }
                int vk = sdlToVk(ev.key.keysym.sym);
                pushKey(vk, ev.type == SDL_KEYDOWN, ev.key.repeat != 0);
                break;
            }
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP: {
                int vk = 0;
                switch (ev.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_A: vk = VK_RETURN_; break;
                case SDL_CONTROLLER_BUTTON_B: vk = VK_ESCAPE_; break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP: vk = VK_UP_; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: vk = VK_DOWN_; break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: vk = VK_LEFT_; break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: vk = VK_RIGHT_; break;
                case SDL_CONTROLLER_BUTTON_X: vk = VK_SHIFT_; break;
                default: break;
                }
                pushKey(vk, ev.type == SDL_CONTROLLERBUTTONDOWN, false);
                break;
            }
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP: {
                InputEvent e;
                bool left = ev.button.button == SDL_BUTTON_LEFT;
                if (left) leftHeld_ = ev.type == SDL_MOUSEBUTTONDOWN;
                if (ev.type == SDL_MOUSEBUTTONDOWN)
                    e.type = left ? (ev.button.clicks >= 2 ? InputEvent::MouseDblClick : InputEvent::MouseDown) : InputEvent::RightDown;
                else {
                    if (!left) break;
                    e.type = InputEvent::MouseUp;
                }
                e.x = ev.button.x; e.y = ev.button.y;
                queue_.push_back(e);
                break;
            }
            case SDL_MOUSEMOTION: {
                InputEvent e;
                e.type = InputEvent::MouseMove;
                e.x = ev.motion.x; e.y = ev.motion.y;
                e.vk = (ev.motion.state & SDL_BUTTON_LMASK) ? 1 : 0;
                queue_.push_back(e);
                break;
            }
            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_EXPOSED || ev.window.event == SDL_WINDOWEVENT_RESIZED) dirty_ = true;
                break;
            default: break;
            }
        }
    }
    refresh();
}

void Platform::sleep(uint32_t ms) {
    if (fastMode) {
        for (uint32_t i = 0; i < ms; i++) poll();
        return;
    }
    poll();
    if (ms) SDL_Delay(ms);
}

bool Platform::popEvent(InputEvent& ev) {
    if (queue_.empty()) return false;
    ev = queue_.front();
    queue_.pop_front();
    return true;
}

void Platform::clearEvents(bool keysOnly) {
    if (!keysOnly) { queue_.clear(); return; }
    std::deque<InputEvent> q;
    for (auto& e : queue_) if (e.type != InputEvent::KeyDown && e.type != InputEvent::KeyUp) q.push_back(e);
    queue_.swap(q);
}

bool Platform::keyHeld(int vk) const { return vk > 0 && vk < 256 && held_[vk]; }

void Platform::compose() {
    const Palette& pal = display_->hw;
    uint32_t lut[256];
    for (int i = 0; i < 256; i++) lut[i] = 0xff000000u | pal[i].r << 16 | pal[i].g << 8 | pal[i].b;
    const Surface& p = display_->primary;
    for (int i = 0; i < 640 * 480; i++) frame_[i] = lut[p.px[i]];
    for (Overlay* o : display_->overlays) {
        if (!o->visible) continue;
        for (int y = 0; y < o->surf.height; y++) {
            int sy = o->y + y;
            if (sy < 0 || sy >= 480) continue;
            const uint8_t* s = o->surf.row(y);
            for (int x = 0; x < o->surf.width; x++) {
                int sx = o->x + x;
                if (sx < 0 || sx >= 640) continue;
                frame_[sy * 640 + sx] = lut[s[x]];
            }
        }
    }
}

void Platform::refresh(bool force) {
    if (!display_) return;
    uint32_t now = ticks();
    if (!force && !dirty_) return;
    if (!force && now - lastPresent_ < 8 && !fastMode) return;
    lastPresent_ = now;
    dirty_ = false;
    if (headless_ || !renderer_) return;
    compose();
    SDL_UpdateTexture((SDL_Texture*)texture_, nullptr, frame_.data(), 640 * 4);
    SDL_RenderClear((SDL_Renderer*)renderer_);
    SDL_RenderCopy((SDL_Renderer*)renderer_, (SDL_Texture*)texture_, nullptr, nullptr);
    SDL_RenderPresent((SDL_Renderer*)renderer_);
}

void Platform::setTitle(const std::string& utf8) {
    if (window_) SDL_SetWindowTitle((SDL_Window*)window_, utf8.c_str());
}

void Platform::toggleFullscreen() {
    if (!window_) return;
    SDL_Window* w = (SDL_Window*)window_;
    bool fs = (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
    SDL_SetWindowFullscreen(w, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    dirty_ = true;
}

bool Platform::saveScreenshot(const std::string& path) {
    // binary PPM (P6), simple and dependency free
    if (display_) compose();
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << "P6\n640 480\n255\n";
    std::vector<uint8_t> rgb(640 * 480 * 3);
    for (int i = 0; i < 640 * 480; i++) {
        rgb[i * 3] = (frame_[i] >> 16) & 0xff; rgb[i * 3 + 1] = (frame_[i] >> 8) & 0xff; rgb[i * 3 + 2] = frame_[i] & 0xff;
    }
    f.write((const char*)rgb.data(), (std::streamsize)rgb.size());
    return true;
}

}  // namespace rm95
