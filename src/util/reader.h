#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace rm95 {

// Little-endian sequential reader over a byte buffer (mimics fread/fgets semantics where needed).
class Reader {
public:
    Reader(const std::vector<uint8_t>& d) : d_(d.data()), n_(d.size()) {}
    Reader(const uint8_t* d, size_t n) : d_(d), n_(n) {}
    size_t pos() const { return p_; }
    void seek(size_t p) { p_ = p > n_ ? n_ : p; }
    size_t size() const { return n_; }
    bool eof() const { return p_ >= n_; }
    size_t remaining() const { return n_ - p_; }
    bool ok() const { return ok_; }
    uint8_t u8() { if (p_ + 1 > n_) { ok_ = false; p_ = n_; return 0; } return d_[p_++]; }
    int8_t s8() { return (int8_t)u8(); }
    uint16_t u16() { if (p_ + 2 > n_) { ok_ = false; p_ = n_; return 0; } uint16_t v = d_[p_] | (d_[p_ + 1] << 8); p_ += 2; return v; }
    int16_t s16() { return (int16_t)u16(); }
    uint32_t u32() {
        if (p_ + 4 > n_) { ok_ = false; p_ = n_; return 0; }
        uint32_t v = d_[p_] | (d_[p_ + 1] << 8) | (d_[p_ + 2] << 16) | ((uint32_t)d_[p_ + 3] << 24);
        p_ += 4; return v;
    }
    int32_t s32() { return (int32_t)u32(); }
    // reads n bytes (zero padded on EOF)
    void bytes(void* out, size_t n) {
        size_t a = n_ - p_ < n ? n_ - p_ : n;
        if (a) memcpy(out, d_ + p_, a);
        if (a < n) { memset((uint8_t*)out + a, 0, n - a); ok_ = false; }
        p_ += a;
    }
    std::vector<uint8_t> vec(size_t n) { std::vector<uint8_t> v(n); bytes(v.data(), n); return v; }
    // fixed length char field, cut at first NUL
    std::string fixedString(size_t n) {
        std::string s(n, '\0'); bytes(&s[0], n);
        size_t z = s.find('\0'); if (z != std::string::npos) s.resize(z);
        return s;
    }
    // fgets(buf, max, f): reads up to max-1 chars, stops after '\n' (included).
    std::string fgets(size_t max) {
        std::string s;
        while (p_ < n_ && s.size() + 1 < max) {
            char c = (char)d_[p_++];
            s.push_back(c);
            if (c == '\n') break;
        }
        return s;
    }
    std::string cstring() {
        std::string s;
        while (p_ < n_ && d_[p_]) s.push_back((char)d_[p_++]);
        if (p_ < n_) p_++;
        return s;
    }
private:
    const uint8_t* d_;
    size_t n_;
    size_t p_ = 0;
    bool ok_ = true;
};

class Writer {
public:
    std::vector<uint8_t> d;
    void u8(uint8_t v) { d.push_back(v); }
    void u16(uint16_t v) { d.push_back(v & 0xFF); d.push_back(v >> 8); }
    void u32(uint32_t v) { for (int i = 0; i < 4; i++) d.push_back((v >> (8 * i)) & 0xFF); }
    void s32(int32_t v) { u32((uint32_t)v); }
    void s16(int16_t v) { u16((uint16_t)v); }
    void bytes(const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; d.insert(d.end(), b, b + n); }
    void fixedString(const std::string& s, size_t n) {
        for (size_t i = 0; i < n; i++) d.push_back(i < s.size() ? (uint8_t)s[i] : 0);
    }
    void zeros(size_t n) { d.insert(d.end(), n, 0); }
};

}  // namespace rm95
