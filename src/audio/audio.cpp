#include "audio.h"
#include "../util/log.h"
#include "../util/fs.h"
#include <SDL.h>
#include <SDL_mixer.h>
#include <atomic>
#include <cmath>

namespace rm95 {

Audio& audio() {
    static Audio a;
    return a;
}

static std::atomic<bool> g_musicEnded{false};
static void musicFinished() { g_musicEnded = true; }

bool Audio::init(const std::string& soundfont) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { LOG_WARN("audio: %s", SDL_GetError()); return false; }
    if (!soundfont.empty()) Mix_SetSoundFonts(soundfont.c_str());
    else if (!getenv("SDL_SOUNDFONTS")) {
        const char* cands[] = {"/usr/share/sounds/sf2/FluidR3_GM.sf2", "/usr/share/sounds/sf2/default-GM.sf2",
                               "/usr/share/soundfonts/FluidR3_GM.sf2", "/usr/share/soundfonts/default.sf2"};
        for (const char* c : cands) if (fs::exists(c)) { Mix_SetSoundFonts(c); break; }
    }
    Mix_Init(MIX_INIT_MID);
    if (Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 1024) != 0) { LOG_WARN("audio: %s", Mix_GetError()); return false; }
    int freq; Uint16 fmt; int ch;
    Mix_QuerySpec(&freq, &fmt, &ch);
    deviceRate_ = freq;
    Mix_AllocateChannels(8);
    Mix_HookMusicFinished(musicFinished);
    ok_ = true;
    return true;
}

void Audio::shutdown() {
    if (!ok_) return;
    midiClose();
    for (int i = 0; i < 4; i++) waveStop(i);
    Mix_CloseAudio();
    Mix_Quit();
    ok_ = false;
}

bool Audio::midiOpenPlay(const std::string& path) {
    if (!ok_) return false;
    midiClose();
    Mix_Music* m = Mix_LoadMUS(path.c_str());
    if (!m) { LOG_WARN("midi %s: %s", path.c_str(), Mix_GetError()); return false; }
    music_ = m;
    g_musicEnded = false;
    Mix_PlayMusic(m, 0);
    return true;
}

void Audio::midiReplay() {
    if (!ok_ || !music_) return;
    g_musicEnded = false;
    Mix_PlayMusic((Mix_Music*)music_, 0);
}

void Audio::midiStop() {
    if (!ok_ || !music_) return;
    Mix_HookMusicFinished(nullptr);
    Mix_HaltMusic();
    Mix_HookMusicFinished(musicFinished);
    g_musicEnded = false;
}

void Audio::midiClose() {
    if (!ok_) return;
    midiStop();
    if (music_) Mix_FreeMusic((Mix_Music*)music_);
    music_ = nullptr;
}

bool Audio::midiEndedNotify() {
    bool e = g_musicEnded.exchange(false);
    return e;
}

bool Audio::midiPlaying() const { return ok_ && music_ && Mix_PlayingMusic(); }

void Audio::midiSetVolume(int att, int pan) {
    if (!ok_) return;
    (void)pan;
    int v = (100 - att) * MIX_MAX_VOLUME / 100;
    if (v < 0) v = 0;
    if (v > MIX_MAX_VOLUME) v = MIX_MAX_VOLUME;
    Mix_VolumeMusic(v);
}

static Mix_Chunk* buildChunk(const std::vector<int16_t>& pcm, int srcRate, int dstRate) {
    // pcm: interleaved stereo S16 at srcRate -> resample to dstRate (linear)
    size_t frames = pcm.size() / 2;
    if (frames == 0 || srcRate <= 0) return nullptr;
    size_t outFrames = (size_t)((double)frames * dstRate / srcRate);
    if (outFrames == 0) outFrames = 1;
    Uint8* buf = (Uint8*)SDL_malloc(outFrames * 4);
    int16_t* o = (int16_t*)buf;
    double step = (double)srcRate / dstRate;
    for (size_t i = 0; i < outFrames; i++) {
        double p = i * step;
        size_t a = (size_t)p;
        double f = p - a;
        size_t b = a + 1 < frames ? a + 1 : a;
        for (int c = 0; c < 2; c++) o[i * 2 + c] = (int16_t)(pcm[a * 2 + c] * (1 - f) + pcm[b * 2 + c] * f);
    }
    return Mix_QuickLoad_RAW(buf, (Uint32)(outFrames * 4));
}

bool Audio::wavePlay(int ch, const std::string& path, bool loop) {
    if (!ok_ || ch < 0 || ch > 3) return false;
    waveStop(ch);
    SDL_AudioSpec spec;
    Uint8* data = nullptr;
    Uint32 len = 0;
    if (!SDL_LoadWAV(path.c_str(), &spec, &data, &len)) { LOG_WARN("wav %s: %s", path.c_str(), SDL_GetError()); return false; }
    SDL_AudioCVT cvt;
    if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, 2, spec.freq) < 0) { SDL_FreeWAV(data); return false; }
    cvt.len = (int)len;
    cvt.buf = (Uint8*)SDL_malloc((size_t)len * cvt.len_mult);
    memcpy(cvt.buf, data, len);
    SDL_FreeWAV(data);
    SDL_ConvertAudio(&cvt);
    Wave& w = waves_[ch];
    w.pcm = new std::vector<int16_t>((int16_t*)cvt.buf, (int16_t*)(cvt.buf + cvt.len_cvt));
    SDL_free(cvt.buf);
    w.srcRate = spec.freq;
    w.rate = spec.freq;
    w.path = path;
    w.loop = loop;
    w.chunk = buildChunk(*w.pcm, w.rate, deviceRate_);
    if (!w.chunk) return false;
    Mix_Volume(ch, MIX_MAX_VOLUME);
    Mix_SetPanning(ch, 255, 255);
    Mix_PlayChannel(ch, (Mix_Chunk*)w.chunk, loop ? -1 : 0);
    return true;
}

void Audio::waveStop(int ch) {
    if (!ok_ || ch < 0 || ch > 3) return;
    Wave& w = waves_[ch];
    Mix_HaltChannel(ch);
    if (w.chunk) {
        Mix_Chunk* c = (Mix_Chunk*)w.chunk;
        Uint8* buf = c->abuf;
        Mix_FreeChunk(c);
        SDL_free(buf);
    }
    w.chunk = nullptr;
    delete w.pcm;
    w.pcm = nullptr;
}

bool Audio::wavePlaying(int ch) const {
    if (!ok_ || ch < 0 || ch > 3) return false;
    return waves_[ch].chunk && Mix_Playing(ch);
}

void Audio::waveSetParams(int ch, int att, int pan, int freqHz) {
    if (!ok_ || ch < 0 || ch > 3) return;
    Wave& w = waves_[ch];
    if (!w.chunk) return;
    // DirectSound: volume -40*att hundredths of dB, pan (pan-50)*20 hundredths of dB
    double db = -0.4 * att;
    int vol = (int)std::lround(MIX_MAX_VOLUME * std::pow(10.0, db / 20.0));
    if (vol < 0) vol = 0;
    if (vol > MIX_MAX_VOLUME) vol = MIX_MAX_VOLUME;
    Mix_Volume(ch, vol);
    double pdb = (pan - 50) * 0.2;  // dB
    int left = 255, right = 255;
    if (pdb > 0) left = (int)std::lround(255 * std::pow(10.0, -pdb / 20.0));
    else if (pdb < 0) right = (int)std::lround(255 * std::pow(10.0, pdb / 20.0));
    Mix_SetPanning(ch, (Uint8)left, (Uint8)right);
    int rate = freqHz > 0 ? freqHz : w.srcRate;
    if (rate != w.rate && w.pcm) {
        bool playing = Mix_Playing(ch);
        Mix_HaltChannel(ch);
        Mix_Chunk* old = (Mix_Chunk*)w.chunk;
        Uint8* ob = old->abuf;
        Mix_FreeChunk(old);
        SDL_free(ob);
        // playback at `rate` of data recorded at srcRate: resample from rate -> device
        w.rate = rate;
        std::vector<int16_t>& p = *w.pcm;
        w.chunk = buildChunk(p, rate, deviceRate_);
        if (playing && w.chunk) Mix_PlayChannel(ch, (Mix_Chunk*)w.chunk, w.loop ? -1 : 0);
    }
}

}  // namespace rm95
