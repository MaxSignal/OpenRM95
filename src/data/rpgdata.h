#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace rm95 {

// One of the 12 system audio entries {name, a, b} (DAT_0046eb0c).
//  a = (attenuation << 16) | pan(0..100, 50 centre)
//  b = type(bits0-1: 0 none, 1 wav, 2 midi, 3 cd) | exclusive SE (bit2) | target slot (bits 8-15) | freq/70 (bits 16-31)
struct AudioEntry {
    std::string name;
    int32_t a = 0x000a0032;
    int32_t b = 0xa0;
};

enum AudioSlot {
    kAudioWalk = 0, kAudioWagon, kAudioSled, kAudioBoat, kAudioShip, kAudioBalloon, kAudioAirship,
    kAudioEncounter, kAudioBattle, kAudioVictory, kAudioLevelUp, kAudioInn, kAudioSlotCount
};

// Actor database record (0x3a5 bytes in the .RPG)
struct ActorData {
    std::array<uint8_t, 0x3a5> raw{};
    int32_t graphic() const { return i32(0x000); }
    int32_t startMember() const { return i32(0x004); }
    std::string name() const;
    int32_t startLevel() const { return i32(0x01c); }
    int32_t hp() const { return i32(0x020); }
    int32_t mp() const { return i32(0x024); }
    int32_t vit() const { return i32(0x028); }
    int32_t pow() const { return i32(0x02c); }
    int32_t intel() const { return i32(0x030); }
    int32_t agi() const { return i32(0x034); }
    int32_t luck() const { return i32(0x038); }
    int32_t attack() const { return i32(0x03c); }
    int32_t defence() const { return i32(0x040); }
    // value of the curve at level L (1..100); curves: 0 pow, 1 vit, 2 int, 3 agi, 4 luck
    uint8_t curve(int which, int level) const { return raw[0x44 + which * 100 + (level - 1)]; }
    int8_t equip(int slot) const { return (int8_t)raw[0x2d8 + slot]; }
    uint8_t learnLevel(int spell) const { return raw[0x2dd + spell]; }
    uint8_t magicDamage(int spell) const { return raw[0x341 + spell]; }
    int32_t i32(size_t o) const { return (int32_t)(raw[o] | raw[o + 1] << 8 | raw[o + 2] << 16 | (uint32_t)raw[o + 3] << 24); }
    // name field is writable by nothing in GAME.EXE; the actor db is constant.
};

enum MapType { kMapNormal = 0, kMapLooping = 1, kMapArea = 2 };

struct MapData {
    bool exists = false;                // read from the file (DAT_0046e33c)
    std::array<uint8_t, 0x51> raw{};
    std::string name;
    int32_t type = 0;
    int32_t areaX = 0, areaY = 0;
    uint16_t width = 0, height = 0;
    int32_t parent = -1;
    uint16_t nextSibling = 0, firstChild = 0;
    int32_t exitX = 0, exitY = 0;
    uint16_t encounters[10] = {};
    std::string chipsets[4];
    std::vector<uint32_t> cells;        // low 16 bits tile id, high 16 bits event id
    uint16_t tile(int x, int y) const { return (uint16_t)(cells[y * width + x] & 0xffff); }
    uint16_t eventAt(int x, int y) const { return (uint16_t)(cells[y * width + x] >> 16); }
};

// Contents of the *.RPG file (LoadRPG 0041bc94)
struct RpgData {
    std::string header;                 // "RPGツクール95" (Shift_JIS)
    std::string versionString;          // e.g. "1.23"
    double version = 0;
    AudioEntry audio[kAudioSlotCount];
    int32_t transitions[3] = {5, 12, 14};
    std::string coinName;               // 21 bytes
    int32_t itemMax = 99;               // max count per item (DAT_0046a26c)
    uint32_t flags = 0;                 // raw flags word (DAT_0046a268 before masking)
    int msgSpeed() const { return (int)(flags & 0xffff); }
    bool transparentWindows() const { return (flags & 0x10000) != 0; }
    bool saveEnabled() const { return (flags & 0x20000) != 0; }
    ActorData actors[8];
    int16_t startItems[100] = {};
    int32_t startMoney = 0;
    int32_t startMap = 0, startX = 0, startY = 0;
    std::vector<MapData> maps = std::vector<MapData>(500);
    int maxEventId = 0;                 // DAT_00461d5c

    bool load(const std::string& path, std::string& error);
    static void setDefaultAudio(AudioEntry* audio);
};

}  // namespace rm95
