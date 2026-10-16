#include "fs.h"
#include "sjis.h"
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <mutex>
#include <algorithm>

namespace stdfs = std::filesystem;

namespace rm95 {
namespace fs {

static std::string foldAscii(std::string s) {
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        if (isSjisLead(c) && i + 1 < s.size()) { i++; continue; }
        if (c >= 'A' && c <= 'Z') s[i] = (char)(c + 32);
    }
    return s;
}

static bool validUtf8(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        int n = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
        if (n < 0) return false;
        if (i + n >= s.size() + (n ? 0 : 1) && n) return false;
        for (int k = 1; k <= n; k++) if (i + k >= s.size() || ((unsigned char)s[i + k] & 0xC0) != 0x80) return false;
        i += n + 1;
    }
    return true;
}

// Directory cache: folded sjis name -> native name
static std::unordered_map<std::string, std::unordered_map<std::string, std::string>> g_cache;
static std::mutex g_mutex;

static const std::unordered_map<std::string, std::string>& dirIndex(const std::string& dir) {
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_cache.find(dir);
    if (it != g_cache.end()) return it->second;
    auto& m = g_cache[dir];
    std::error_code ec;
    for (auto& e : stdfs::directory_iterator(stdfs::u8path(dir), ec)) {
        std::string native = e.path().filename().string();
        std::string sj = validUtf8(native) ? utf8ToSjis(native) : native;
        m.emplace(foldAscii(sj), native);
    }
    return m;
}

std::string join(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    if (dir.back() == '/' || dir.back() == '\\') return dir + name;
    return dir + "/" + name;
}

std::string find(const std::string& dir, const std::string& sjisName) {
    if (sjisName.empty()) return "";
    const auto& m = dirIndex(dir);
    auto it = m.find(foldAscii(sjisName));
    if (it == m.end()) return "";
    return join(dir, it->second);
}

std::string findPath(const std::string& dir, const std::string& rel) {
    std::string cur = dir;
    size_t start = 0;
    while (true) {
        size_t p = rel.find_first_of("\\/", start);
        std::string part = rel.substr(start, p == std::string::npos ? std::string::npos : p - start);
        if (!part.empty()) {
            cur = find(cur, part);
            if (cur.empty()) return "";
        }
        if (p == std::string::npos) break;
        start = p + 1;
    }
    return cur;
}

std::string baseName(const std::string& path) {
    // Shift_JIS aware: trail bytes can be 0x5C ('\\')
    size_t last = std::string::npos;
    for (size_t i = 0; i < path.size(); i++) {
        unsigned char c = (unsigned char)path[i];
        if (isSjisLead(c) && i + 1 < path.size()) { i++; continue; }
        if (c == '\\' || c == '/') last = i;
    }
    return last == std::string::npos ? path : path.substr(last + 1);
}

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(stdfs::u8path(path), std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamoff n = f.tellg();
    f.seekg(0);
    out.resize((size_t)n);
    if (n > 0) f.read((char*)out.data(), n);
    return (bool)f || n == 0;
}

bool writeFile(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(stdfs::u8path(path), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write((const char*)data.data(), (std::streamsize)data.size());
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_cache.erase(stdfs::u8path(path).parent_path().string());
        std::string parent = path.substr(0, path.find_last_of('/'));
        g_cache.erase(parent);
    }
    return (bool)f;
}

bool exists(const std::string& path) {
    std::error_code ec;
    return !path.empty() && stdfs::exists(stdfs::u8path(path), ec);
}

std::vector<std::string> listByExtension(const std::string& dir, const std::string& ext) {
    std::vector<std::string> r;
    std::error_code ec;
    std::string fe = foldAscii(ext);
    for (auto& e : stdfs::directory_iterator(stdfs::u8path(dir), ec)) {
        std::string n = e.path().filename().string();
        if (n.size() >= ext.size() && foldAscii(n.substr(n.size() - ext.size())) == fe) r.push_back(join(dir, n));
    }
    std::sort(r.begin(), r.end());
    return r;
}

}  // namespace fs
}  // namespace rm95
