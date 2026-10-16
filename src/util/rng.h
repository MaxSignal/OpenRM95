#pragma once
#include <cstdint>

namespace rm95 {

// Borland C++ RTL random generator as used by GAME.EXE (lrand at 0x44fa70):
// 64-bit LCG state = state * 0x000015A400004E35 + 1 (verified in the disassembly), returns (state >> 32) & 0x7fffffff.
// GAME.EXE never seeds it, so the initial state is 1 (low) / 0 (high).
class Random {
public:
    uint32_t next() {
        lo_hi_ = lo_hi_ * 0x000015A400004E35ull + 1ull;
        return (uint32_t)(lo_hi_ >> 32) & 0x7fffffff;
    }
    // C semantics of lrand() % n with signed n (n == 0 -> 0, as guarded by the original code paths)
    int mod(int n) { int r = (int)next(); return n == 0 ? 0 : r % n; }
    uint64_t state() const { return lo_hi_; }
    void setState(uint64_t s) { lo_hi_ = s; }
private:
    uint64_t lo_hi_ = 1;
};

Random& rng();

}  // namespace rm95
