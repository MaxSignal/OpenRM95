#pragma once
#include <cstdint>
#include <cstring>
#include <string>

namespace rm95 {

// Helper base for records that GAME.EXE keeps as raw memory (and dumps raw into save files).
template <size_t N>
struct RawRecord {
    uint8_t raw[N] = {};
    int16_t s16(size_t o) const { return (int16_t)(raw[o] | raw[o + 1] << 8); }
    uint16_t u16(size_t o) const { return (uint16_t)(raw[o] | raw[o + 1] << 8); }
    int32_t s32(size_t o) const { return (int32_t)(raw[o] | raw[o + 1] << 8 | raw[o + 2] << 16 | (uint32_t)raw[o + 3] << 24); }
    void set16(size_t o, int v) { raw[o] = (uint8_t)v; raw[o + 1] = (uint8_t)(v >> 8); }
    void set32(size_t o, int32_t v) { for (int i = 0; i < 4; i++) raw[o + i] = (uint8_t)((uint32_t)v >> (8 * i)); }
    std::string str(size_t o, size_t n) const {
        std::string s((const char*)raw + o, n);
        size_t z = s.find('\0'); if (z != std::string::npos) s.resize(z);
        return s;
    }
    void setStr(size_t o, size_t n, const std::string& s) {
        memset(raw + o, 0, n);
        memcpy(raw + o, s.data(), s.size() < n ? s.size() : n - 1);
    }
};

// Battler record (0xB8 bytes): party members (G+0x1083e + i*0xb8), enemies (G+0x100de + i*0xb8), CHARA.DAT.
struct Battler : RawRecord<0xb8> {
    int16_t level() const { return s16(0x00); }      void setLevel(int v) { set16(0x00, v); }
    int16_t hp() const { return s16(0x02); }         void setHp(int v) { set16(0x02, v); }
    int16_t magicDef() const { return s16(0x04); }   void setMagicDef(int v) { set16(0x04, v); }
    int16_t maxHp() const { return s16(0x06); }      void setMaxHp(int v) { set16(0x06, v); }
    int16_t mp() const { return s16(0x0a); }         void setMp(int v) { set16(0x0a, v); }
    int16_t maxMp() const { return s16(0x0c); }      void setMaxMp(int v) { set16(0x0c, v); }
    int16_t vit() const { return s16(0x0e); }        void setVit(int v) { set16(0x0e, v); }
    int16_t pow() const { return s16(0x10); }        void setPow(int v) { set16(0x10, v); }
    int16_t agi() const { return s16(0x12); }        void setAgi(int v) { set16(0x12, v); }
    int16_t intel() const { return s16(0x14); }      void setIntel(int v) { set16(0x14, v); }
    int16_t luck() const { return s16(0x16); }       void setLuck(int v) { set16(0x16, v); }
    int16_t defence() const { return s16(0x18); }    void setDefence(int v) { set16(0x18, v); }
    int16_t attack() const { return s16(0x1a); }     void setAttack(int v) { set16(0x1a, v); }
    int16_t sleepCounter() const { return s16(0x1c); } void setSleepCounter(int v) { set16(0x1c, v); }
    int32_t exp() const { return s32(0x1e); }        void setExp(int32_t v) { set32(0x1e, v); }
    int32_t gold() const { return s32(0x22); }       void setGold(int32_t v) { set32(0x22, v); }
    uint8_t dropItem() const { return raw[0x26]; }
    uint8_t dropRate() const { return raw[0x27]; }
    uint16_t status() const { return u16(0x28); }    void setStatus(int v) { set16(0x28, v); }
    std::string name() const { return str(0x2c, 18); }
    void setName(const std::string& n) { setStr(0x2c, 18, n); }
    uint8_t magicDamage(int s) const { return raw[0x41 + s]; }
    uint8_t attackPattern(int i) const { return raw[0xa5 + i]; }
    int8_t actorId() const { return (int8_t)raw[0xaf]; } void setActorId(int v) { raw[0xaf] = (uint8_t)v; }
};

struct ItemData : RawRecord<28> {
    int16_t cost() const { return s16(0); }
    int16_t type() const { return s16(2); }
    int16_t power() const { return s16(4); }
    int16_t equipBy() const { return s16(6); }
    std::string name() const { return str(8, 20); }
};

struct MagicData : RawRecord<28> {
    int16_t type() const { return s16(0); }
    int16_t anim() const { return s16(2); }
    int16_t mpCost() const { return s16(4); }
    int16_t power() const { return s16(6); }
    std::string name() const { return str(8, 20); }
};

struct TroopData : RawRecord<128> {
    int16_t background() const { return s16(0); }
    int16_t terrain() const { return s16(2); }
    int32_t count() const { return s32(4); }
    int32_t monsterId(int i) const { return s32(8 + i * 12); }
    int32_t monsterX(int i) const { return s32(12 + i * 12); }
    int32_t monsterY(int i) const { return s32(16 + i * 12); }
};

}  // namespace rm95
