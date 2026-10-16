#include "game/game.h"
#include "audio/audio.h"
#include "gfx/font.h"
#include "platform/platform.h"
#include "util/fs.h"
#include "util/log.h"
#include "util/rng.h"
#include "util/sjis.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace rm95 {
int g_logLevel = 1;
Random& rng() { static Random r; return r; }
Game& game() { static Game g; return g; }
}

using namespace rm95;

static void usage() {
    std::printf(
        "OpenRM95 - RPG Maker 95 (RPG Tsukuru 95) game engine\n"
        "usage: openrm95 [options] [game directory]\n"
        "  --window            start in a window (default)\n"
        "  --fullscreen        start in fullscreen\n"
        "  --scale N           window scale factor (default 2)\n"
        "  --font PATH         TrueType/OpenType font to use (MS PGothic recommended)\n"
        "  --soundfont PATH    SoundFont for MIDI playback\n"
        "  --no-audio          disable audio\n"
        "  --verbose           more log output\n"
        "  --load FILE         start by loading a save file (skips the title screen)\n"
        "testing:\n"
        "  --headless          no window, virtual clock\n"
        "  --keys SPEC         scripted input, e.g. \"3000:RET,5000:DOWN:400\" (ms:key[:hold ms])\n"
        "  --shot MS:FILE      save a screenshot (PPM) at virtual time MS (repeatable)\n"
        "  --quit-at MS        quit at virtual time MS\n"
        "  --save-at MS:FILE   save the game at virtual time MS (when the map is idle)\n"
        "  --test-battle N     start a battle with troop N (1-based) when the map is shown\n"
        "  --test-event N      run the script of event N (eveN.dat) when the map is shown\n"
        "  --test-noquit       ignore the quit command of the system menu\n");
}

static int keyByName(const std::string& n) {
    if (n == "RET" || n == "ENTER") return VK_RETURN_;
    if (n == "SPC" || n == "SPACE") return VK_SPACE_;
    if (n == "ESC") return VK_ESCAPE_;
    if (n == "UP") return VK_UP_;
    if (n == "DOWN") return VK_DOWN_;
    if (n == "LEFT") return VK_LEFT_;
    if (n == "RIGHT") return VK_RIGHT_;
    if (n == "SHIFT") return VK_SHIFT_;
    return 0;
}

int main(int argc, char** argv) {
    std::string dir = ".";
    int scale = 2;
    bool fullscreen = false, headless = false, noAudio = false;
    std::string font, soundfont, keys;
    std::vector<std::pair<uint32_t, std::string>> shots;
    uint32_t quitAt = 0;
    std::string loadFile;
    std::vector<std::pair<uint32_t, std::string>> saves;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--help" || a == "-h") { usage(); return 0; }
        else if (a == "--window") fullscreen = false;
        else if (a == "--fullscreen") fullscreen = true;
        else if (a == "--scale") scale = std::atoi(next().c_str());
        else if (a == "--font") font = next();
        else if (a == "--soundfont") soundfont = next();
        else if (a == "--no-audio") noAudio = true;
        else if (a == "--verbose") g_logLevel = 3;
        else if (a == "--headless") headless = true;
        else if (a == "--keys") keys = next();
        else if (a == "--shot") { std::string s = next(); size_t c = s.find(':'); if (c != std::string::npos) shots.push_back({(uint32_t)std::atoi(s.c_str()), s.substr(c + 1)}); }
        else if (a == "--quit-at") quitAt = (uint32_t)std::atoi(next().c_str());
        else if (a == "--load") loadFile = next();
        else if (a == "--test-battle") game().testBattle = std::atoi(next().c_str());
        else if (a == "--test-event") game().testEvent = std::atoi(next().c_str());
        else if (a == "--test-noquit") game().testNoQuit = true;
        else if (a == "--save-at") { std::string s = next(); size_t c = s.find(':'); if (c != std::string::npos) saves.push_back({(uint32_t)std::atoi(s.c_str()), s.substr(c + 1)}); }
        else dir = a;
    }
    Platform& pf = platform();
    if (!pf.init("OpenRM95", scale, fullscreen, headless)) return 1;
    pf.setDisplay(&display());
    if (headless) {
        pf.fastMode = true;
        size_t p = 0;
        while (p < keys.size()) {
            size_t e = keys.find(',', p);
            std::string item = keys.substr(p, e == std::string::npos ? std::string::npos : e - p);
            p = e == std::string::npos ? keys.size() : e + 1;
            uint32_t at = (uint32_t)std::atoi(item.c_str());
            size_t c1 = item.find(':');
            if (c1 == std::string::npos) continue;
            std::string rest = item.substr(c1 + 1);
            size_t c2 = rest.find(':');
            std::string kn = rest.substr(0, c2);
            uint32_t hold = c2 == std::string::npos ? 100 : (uint32_t)std::atoi(rest.c_str() + c2 + 1);
            int vk = keyByName(kn);
            if (!vk) continue;
            pf.script.push_back({at, vk, true});
            pf.script.push_back({at + hold, vk, false});
        }
        std::sort(pf.script.begin(), pf.script.end(), [](auto& a, auto& b) { return a.at < b.at; });
        std::sort(shots.begin(), shots.end());
        pf.onFrame = [&](uint32_t t) {
            while (!shots.empty() && t >= shots.front().first) {
                pf.saveScreenshot(shots.front().second);
                shots.erase(shots.begin());
            }
            while (!saves.empty() && t >= saves.front().first) {
                game().pendingSave = saves.front().second;
                saves.erase(saves.begin());
            }
            if (quitAt && t >= quitAt) { game().quit = true; pf.requestQuit(); }
        };
    }
    std::vector<std::string> fonts;
    if (!font.empty()) fonts.push_back(font);
    fonts.push_back(dir + "/font.ttf");
    fonts.push_back(dir + "/font.ttc");
    const char* sys[] = {"C:/Windows/Fonts/msgothic.ttc", "/usr/share/fonts/opentype/ipafont-gothic/ipagp.ttf",
                         "/usr/share/fonts/truetype/fonts-japanese-gothic.ttf", "/usr/share/fonts/OTF/ipagp.ttf",
                         "/usr/share/fonts/ipa-pgothic/ipagp.ttf", "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc",
                         "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"};
    for (const char* s : sys) fonts.push_back(s);
    if (!uiFont().load(fonts)) LOG_WARN("no usable font found; text will not be displayed (use --font)");
    if (!noAudio && !headless) audio().init(soundfont);
    std::string err;
    Game& g = game();
    if (!g.load(dir, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    pf.setTitle(sjisToUtf8(fs::baseName(g.rpgPath.substr(0, g.rpgPath.size() - 4))));
    g.startSave = loadFile;
    g.run();
    for (auto& w : g.windows) w.reset();  // before the display object goes away
    audio().shutdown();
    pf.shutdown();
    return 0;
}
