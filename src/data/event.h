#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace rm95 {

struct EventCommand {
    int32_t code = 0, a1 = 0, a2 = 0, a3 = 0, a4 = 0;
};

// Per-page attribute arrays as stored in the eve file header at 0x0C..0x63 (copied to EChara+0x77).
struct EventPageAttrs {
    std::array<uint8_t, 0x58> raw{};
    uint8_t trigger(int p) const { return raw[0x00 + p]; }
    uint32_t cond(int p) const { return u32(0x04 + p * 4); }
    uint8_t display(int p) const { return raw[0x14 + p]; }
    uint8_t layer(int p) const { return raw[0x18 + p]; }
    uint8_t moveType(int p) const { return raw[0x1c + p]; }
    uint8_t anim(int p) const { return raw[0x20 + p]; }
    uint8_t speed(int p) const { return raw[0x24 + p]; }
    uint8_t vehicle(int p) const { return raw[0x28 + p]; }
    uint16_t item(int p) const { return u16(0x2c + p * 2); }
    uint8_t party(int p) const { return raw[0x34 + p]; }
    uint32_t gold(int p) const { return u32(0x38 + p * 4); }
    uint16_t switchId(int p) const { return u16(0x48 + p * 2); }
    uint16_t graphic(int p) const { return u16(0x50 + p * 2); }
    uint16_t u16(size_t o) const { return (uint16_t)(raw[o] | raw[o + 1] << 8); }
    uint32_t u32(size_t o) const { return raw[o] | raw[o + 1] << 8 | raw[o + 2] << 16 | (uint32_t)raw[o + 3] << 24; }
};

using MoveRoute = std::vector<uint8_t>;

// Map-object part of an eve file (loader 0041df04)
struct EventHeader {
    EventPageAttrs attrs;
    MoveRoute pageRoutes[4];
};

// Script part (loader 00430050)
struct EventScript {
    std::array<uint8_t, 0x88> header{};
    std::vector<EventCommand> pages[4];
    std::vector<const std::string*> strIndex;  // unused
    std::vector<std::string> strings = std::vector<std::string>(512);
    std::vector<bool> hasString = std::vector<bool>(512, false);
    std::vector<MoveRoute> cmdRoutes;
    const std::string& str(int i) const {
        static const std::string empty;
        return (i >= 0 && i < 512 && hasString[i]) ? strings[i] : empty;
    }
};

std::string eventFileName(int id);  // "eve%05d.dat"
bool loadEventHeader(const std::string& gameDir, int id, EventHeader& out);
bool loadEventScript(const std::string& gameDir, int id, EventScript& out);

}  // namespace rm95
