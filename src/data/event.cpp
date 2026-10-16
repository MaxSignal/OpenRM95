#include "event.h"
#include "../util/fs.h"
#include "../util/reader.h"
#include <cstdio>

namespace rm95 {

std::string eventFileName(int id) {
    char b[32];
    std::snprintf(b, sizeof b, "eve%05d.dat", id);
    return b;
}

static bool readFileFor(const std::string& gameDir, int id, std::vector<uint8_t>& d) {
    std::string p = fs::find(gameDir, eventFileName(id));
    if (p.empty()) return false;
    return fs::readFile(p, d);
}

bool loadEventHeader(const std::string& gameDir, int id, EventHeader& out) {
    std::vector<uint8_t> d;
    if (!readFileFor(gameDir, id, d) || d.size() < 0x88) return false;
    Reader r(d);
    if (r.s32() != 0x88) return false;
    r.seek(0x0c);
    r.bytes(out.attrs.raw.data(), 0x58);
    r.seek(0x88);
    for (int p = 0; p < 4; p++) {
        uint8_t n = r.u8();
        out.pageRoutes[p] = r.vec(n);
    }
    return true;
}

bool loadEventScript(const std::string& gameDir, int id, EventScript& out) {
    std::vector<uint8_t> d;
    if (!readFileFor(gameDir, id, d) || d.size() < 0x88) return false;
    Reader r(d);
    if (r.s32() != 0x88) return false;
    r.seek(0);
    r.bytes(out.header.data(), 0x88);
    Reader h(out.header.data(), 0x88);
    int32_t counts[4];
    h.seek(0x74);
    for (int p = 0; p < 4; p++) counts[p] = h.s32();
    int32_t strSize = h.s32();
    for (int p = 0; p < 4; p++) { uint8_t n = r.u8(); r.seek(r.pos() + n); }
    for (int p = 0; p < 4; p++) {
        out.pages[p].clear();
        if (counts[p] < 0) return false;
        for (int i = 0; i < counts[p]; i++) {
            EventCommand c;
            c.code = r.s32(); c.a1 = r.s32(); c.a2 = r.s32(); c.a3 = r.s32(); c.a4 = r.s32();
            out.pages[p].push_back(c);
        }
    }
    // string list (0042ff3f)
    for (int i = 0; i < 512; i++) { out.strings[i].clear(); out.hasString[i] = false; }
    int consumed = 0, prev = -1;
    while (true) {
        int idx = r.eof() ? -1 : r.u8();
        if (idx == 0x1a) {
            if (prev != 0x19) break;
            if (consumed + 1 >= strSize) break;
            consumed += 1;
        }
        if (idx < 0) break;
        std::string s = r.cstring();
        consumed += 1 + (int)s.size() + 1;
        if (out.hasString[idx]) break;
        out.strings[idx] = s;
        out.hasString[idx] = true;
        prev = idx;
        if (consumed > strSize) return false;
    }
    out.cmdRoutes.clear();
    while (!r.eof()) {
        uint8_t n = r.u8();
        out.cmdRoutes.push_back(r.vec(n));
    }
    return true;
}

}  // namespace rm95
