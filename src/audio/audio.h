#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace rm95 {

// Thin SDL_mixer backend offering the primitives GAME.EXE uses: one MCI MIDI sequencer
// ("MUSIC"), DirectSound wave channels (0 = wave BGM, 1..3 = sound effects), CD audio (unsupported).
class Audio {
public:
    bool init(const std::string& soundfont);
    void shutdown();
    bool available() const { return ok_; }

    // MIDI (MCI sequencer). onEnd semantics: when a MIDI ends the game's notify handler decides to replay.
    bool midiOpenPlay(const std::string& path);   // close all + open + play from 0
    void midiReplay();                             // play MUSIC from 0
    void midiStop();                               // stop MUSIC
    void midiClose();                              // close all
    bool midiEndedNotify();                        // returns true once after the MIDI reached its end
    bool midiPlaying() const;
    void midiSetVolume(int att, int pan);          // mixer emulation

    // wave channels
    bool wavePlay(int channel, const std::string& path, bool loop);
    void waveStop(int channel);
    bool wavePlaying(int channel) const;
    void waveSetParams(int channel, int att, int pan, int freqHz);  // freqHz 0 = original
    int waveCount() const { return 4; }

private:
    bool ok_ = false;
    void* music_ = nullptr;
    struct Wave {
        void* chunk = nullptr;       // Mix_Chunk at current frequency
        std::string path;
        int srcRate = 0, rate = 0;
        std::vector<int16_t>* pcm = nullptr;
        bool loop = false;
    };
    Wave waves_[4];
    int deviceRate_ = 44100;
};

Audio& audio();

}  // namespace rm95
