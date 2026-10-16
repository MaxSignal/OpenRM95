// BGM / sound effect logic of GAME.EXE (FUN_0042f898, FUN_00430c07, FUN_004209f7 ...), see spec/render_audio.md §6
#include "game.h"
#include "../audio/audio.h"
#include "../platform/platform.h"
#include "../util/fs.h"
#include "../util/log.h"
#include <cstring>

namespace rm95 {

static const char* kStopName = "\x89\x89\x91\x74\x92\xe2\x8e\x7e";  // 演奏停止

std::string Game::resolveMedia(const std::string& name) {
    // FUN_00434890: <dir of GAME.EXE>\<text after the last '\'> (byte-wise strrchr like the original)
    size_t p = name.rfind('\\');
    std::string base = p == std::string::npos ? name : name.substr(p + 1);
    return fs::find(gameDir, base);
}

void Game::applyAudioParams(int ch, int a, int b) {
    int att = a >> 16, pan = a & 0xffff;
    int type = b & 3;
    if (type == 1) audio().waveSetParams(ch, att, pan, (int)(((uint32_t)b >> 16) * 70));
    else if (type != 0) audio().midiSetVolume(att, pan);
}

void Game::stopMusic() {
    audio().midiStop();
    audio().midiClose();
    audio().waveStop(0);
}

void Game::playBgm(const std::string& name, const AudioEntry* params, bool force) {
    bool failed = false;
    if (currentBgm.name != name || force) {
        if (name == kStopName) {
            stopMusic();
            currentBgm.name = name;
            return;
        }
        stopMusic();
        if (name.find(".mid") != std::string::npos) {
            std::string p = resolveMedia(name);
            if (p.empty()) failed = true;
            else audio().midiOpenPlay(p);
        } else if (name.find(".wav") != std::string::npos) {
            std::string p = resolveMedia(name);
            if (p.empty() || !audio().available()) failed = true;
            else audio().wavePlay(0, p, true);
        } else {
            failed = true;  // CD audio is not supported
        }
    }
    if (params && !failed) {
        int type = params->b & 3;
        if (type == 1) audio().waveSetParams(0, params->a >> 16, params->a & 0xffff, (int)(((uint32_t)params->b >> 16) * 70));
        else if (type != 0) audio().midiSetVolume(params->a >> 16, params->a & 0xffff);
        currentBgm.a = params->a;
        currentBgm.b = params->b;
    }
    currentBgm.name = name;
}

void Game::playSe(const std::string& name, const AudioEntry* params) {
    std::string p = resolveMedia(name);
    if (p.empty()) return;
    if (params && (params->b & 4)) {
        for (int i = 1; i < 4; i++) { audio().waveStop(i); seChannelBusy[i] = 0; }
    }
    for (int i = 1; i < 4; i++) if (!audio().wavePlaying(i)) seChannelBusy[i] = 0;
    int ch = -1;
    for (int i = 1; i < 4; i++) if (!seChannelBusy[i]) { ch = i; break; }
    if (ch < 0) ch = 1;
    if (audio().wavePlay(ch, p, false)) {
        seChannelBusy[ch] = 1;
        if (params) audio().waveSetParams(ch, params->a >> 16, params->a & 0xffff, (int)(((uint32_t)params->b >> 16) * 70));
    }
}

void Game::playSystemSe(int index) {
    static const char* names[53] = {
        "flamer.wav", "sword.wav", "metal.wav", "sword3.wav", "arrowhit.wav", "ropewhip.wav", "fastarw.wav", "kyudo.wav",
        "ninjahit.wav", "shot1.wav", "knock.wav", "punch.wav", "whack.wav", "banger.wav", "boom.wav", "noizehit.wav",
        "snarehit.wav", "powerup.wav", "electro.wav", "bomb.wav", "explos3.wav", "electrix.wav", "thunder.wav", "thunder2.wav",
        "monster.wav", "monster2.wav", "crunch.wav", "magic.wav", "paku.wav", "sludge.wav", "arrrg.wav", "monster3.wav",
        "swordhai.wav", "glsmash.wav", "mizu.wav", "close.wav", "creak.wav", "creak2.wav", "creakdoa.wav", "doashut3.wav",
        "doorshut.wav", "footstep.wav", "footstp3.wav", "latch.wav", "obakedoa.wav", "warpout.wav", "wibble.wav",
        "hitpoint.wav", "healer.wav", "refresh1.wav", "zap64.wav", "zapper.wav", "error.wav"};
    if (index < 0 || index >= 53) return;
    playSe(names[index], nullptr);
}

void Game::saveBgm() {
    savedBgm = currentBgm;
}

void Game::restoreBgm(bool force) {
    if (savedBgm.name.empty()) return;
    AudioEntry e;
    e.a = savedBgm.a;
    e.b = savedBgm.b;
    std::string n = savedBgm.name;
    savedBgm.name.clear();
    playBgm(n, &e, force);
}

void Game::playVehicleBgm(int slot, bool asSe) {
    if (slot < 0 || slot >= 12) return;
    AudioEntry& e = audioSlots[slot];
    if (e.name.empty()) return;
    if (!asSe) {
        playBgm(e.name, &e, false);
    } else if (e.name.find(".mid") == std::string::npos) {
        playSe(e.name, &e);
    } else {
        audio().midiSetVolume(e.a >> 16, e.a & 0xffff);
        std::string p = resolveMedia(e.name);
        if (p.empty()) midiOnce = false;
        else audio().midiOpenPlay(p);
    }
}

void Game::waitJingle(bool midi) {
    if (midi && !audio().available()) { midiOnce = false; return; }
    while (!quit) {
        if (keyHeldAsync(VK_SPACE_) || keyHeldAsync(VK_RETURN_)) {
            playBgm(kStopName, nullptr, false);
            for (int i = 1; i < 4; i++) audio().waveStop(i);
            midiOnce = false;
            return;
        }
        pumpMessages();
        bool busy = false;
        if (!midi) { for (int i = 1; i < 4; i++) if (audio().wavePlaying(i)) busy = true; }
        else busy = midiOnce;
        if (!busy) break;
        platform().sleep(1);
    }
}

}  // namespace rm95
