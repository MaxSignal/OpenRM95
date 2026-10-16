#include "rpgdata.h"
#include "../util/fs.h"
#include "../util/reader.h"
#include "../util/log.h"
#include <cstdlib>

namespace rm95 {

std::string ActorData::name() const {
    std::string s((const char*)&raw[8], 18);
    size_t z = s.find('\0');
    if (z != std::string::npos) s.resize(z);
    return s;
}

void RpgData::setDefaultAudio(AudioEntry* audio) {
    static const char* names[12] = {"WALK.wav", "WAGON.wav", "SLED.wav", "BOAT.wav", "SHIP.wav", "BALLOON.wav",
                                    "AIRSHIP.wav", "ENCOUNT.wav", "BATTLE.wav", "WIN.wav", "LEVELUP.wav", "REST.wav"};
    for (int i = 0; i < 12; i++) { audio[i].name = names[i]; audio[i].a = 0x000a0032; audio[i].b = 0xa0; }
}

static std::string cutLast(std::string s) {
    if (!s.empty()) s.pop_back();
    return s;
}

bool RpgData::load(const std::string& path, std::string& error) {
    std::vector<uint8_t> d;
    if (!fs::readFile(path, d)) { error = "Can't open file!!!"; return false; }
    Reader r(d);
    header = r.fixedString(30);
    versionString = r.fixedString(5);
    version = std::atof(versionString.c_str());
    setDefaultAudio(audio);
    int n = version < 0.96 ? 11 : 12;
    for (int i = 0; i < n; i++) {
        audio[i].a = r.s32();
        audio[i].b = r.s32();
        audio[i].name = cutLast(r.fgets(0x100));
    }
    for (int i = 0; i < 3; i++) transitions[i] = r.s32();
    coinName = r.fixedString(0x15);
    itemMax = r.s32();
    flags = r.u32();
    for (int i = 0; i < 8; i++) r.bytes(actors[i].raw.data(), 0x3a5);
    for (int i = 0; i < 100; i++) startItems[i] = r.s16();
    startMoney = r.s32();
    startMap = r.s32();
    startX = r.s32();
    startY = r.s32();
    maxEventId = 0;
    while (r.remaining() >= 4) {
        int32_t id = r.s32();
        if (id < 0 || id >= 500) { LOG_WARN("map id %d out of range", id); break; }
        MapData& m = maps[id];
        m = MapData();
        m.exists = true;
        r.bytes(m.raw.data(), 0x51);
        Reader mr(m.raw.data(), m.raw.size());
        m.name = mr.fixedString(0x19);
        m.type = mr.s32();
        m.areaX = mr.s32();
        m.areaY = mr.s32();
        m.width = mr.u16();
        m.height = mr.u16();
        m.parent = mr.s32();
        m.nextSibling = mr.u16();
        m.firstChild = mr.u16();
        mr.s32();
        m.exitX = mr.s32();
        m.exitY = mr.s32();
        for (int i = 0; i < 10; i++) m.encounters[i] = mr.u16();
        if (m.type == kMapArea) continue;
        for (int i = 0; i < 4; i++) m.chipsets[i] = cutLast(r.fgets(0x100));
        size_t cnt = (size_t)m.width * m.height;
        m.cells.assign(cnt, 0);
        size_t i = 0;
        while (i < cnt && !r.eof()) {
            uint16_t v = r.u16();
            uint16_t t = v & 0x3ff;
            int rep = (v >> 10) + 1;
            for (int k = 0; k < rep && i < cnt; k++) m.cells[i++] = t;  // original writes past the end (never in practice)
        }
        i = 0;
        while (i < cnt && !r.eof()) {
            uint16_t v = r.u16();
            if (v & 0x8000) { i += (v & 0x7fff) + 1; }
            else {
                if ((int)v > maxEventId) maxEventId = v;
                m.cells[i] |= (uint32_t)v << 16;
                i++;
            }
        }
    }
    return true;
}

}  // namespace rm95
