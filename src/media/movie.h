#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rm95 {

// AVI movie reader for the "play movie" event command (the original runtime uses MCI "avivideo").
// Video only. Built-in decoders: uncompressed DIB (8/16/24/32 bpp), RLE8 and Cinepak; other codecs are
// decoded with FFmpeg (libavcodec) when OpenRM95 is built with it.
class Movie {
public:
    Movie();
    ~Movie();
    bool open(const std::string& path, std::string& error);
    int width() const { return w_; }
    int height() const { return h_; }
    double frameMs() const { return frameMs_; }
    int frameCount() const { return (int)frames_.size(); }
    // Decodes frame `i` (frames must be requested in increasing order; skipped frames are decoded
    // too because of inter frames). The result is 0x00RRGGBB, top-down, width()*height().
    bool frame(int i, std::vector<uint32_t>& out);

    struct Decoder;
private:
    struct Chunk { size_t off, size; bool key; };
    std::vector<uint8_t> data_;
    std::vector<Chunk> frames_;
    int w_ = 0, h_ = 0;
    double frameMs_ = 66.6;
    int next_ = 0;
    std::vector<uint32_t> cur_;
    std::unique_ptr<Decoder> dec_;
};

}  // namespace rm95
