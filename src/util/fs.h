#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace rm95 {

// File system helpers. Game data refers to files by Shift_JIS names that are looked up
// case-insensitively (Windows semantics). File names on disk may be stored either as UTF-8
// or as raw Shift_JIS bytes; both are matched.
namespace fs {

// Finds a file `name` (Shift_JIS) inside directory `dir` (native path). Returns the native path or "".
std::string find(const std::string& dir, const std::string& sjisName);

// Like find() but `relPath` may contain '\\' separated subdirectories (e.g. "BMP\\a.bmp").
std::string findPath(const std::string& dir, const std::string& sjisRelPath);

// The file name part after the last '\\' or '/'.
std::string baseName(const std::string& path);

bool readFile(const std::string& path, std::vector<uint8_t>& out);
bool writeFile(const std::string& path, const std::vector<uint8_t>& data);
bool exists(const std::string& path);

// Lists files in dir whose names end with ext (case-insensitive, ext like ".rpg").
std::vector<std::string> listByExtension(const std::string& dir, const std::string& ext);

std::string join(const std::string& dir, const std::string& name);

}  // namespace fs
}  // namespace rm95
