#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace rm95 {

struct Rgb { uint8_t r = 0, g = 0, b = 0; bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; } };
using Palette = std::array<Rgb, 256>;

// A decoded BMP: either indexed (bpp <= 8, `pixels` are palette indices, `palette` filled)
// or true colour (`rgb` filled). Rows are top-down.
struct Image {
    int width = 0, height = 0;
    bool indexed = true;
    int colors = 0;                    // number of colour table entries for indexed images
    std::vector<uint8_t> pixels;       // indexed
    std::vector<Rgb> rgb;              // true colour
    Palette palette{};
    Rgb colorAt(int x, int y) const { return indexed ? palette[pixels[y * width + x]] : rgb[y * width + x]; }
};

bool loadBmp(const std::string& path, Image& out);
bool decodeBmp(const uint8_t* data, size_t size, Image& out);
// Raw 8-bit DIB as in MONSTPIC.DAT: bottom-up rows, RGBQUAD palette.
void decodeRawDib(int w, int h, const uint8_t* bits, const uint8_t* rgbquad, Image& out);

}  // namespace rm95
