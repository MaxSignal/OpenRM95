#include "movie.h"
#include "../util/fs.h"
#include "../util/log.h"
#include <algorithm>
#include <cstring>

#ifdef OPENRM95_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#endif

namespace rm95 {

static uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t fourcc(const char* s) { return (uint8_t)s[0] | (uint8_t)s[1] << 8 | (uint8_t)s[2] << 16 | (uint32_t)(uint8_t)s[3] << 24; }

// ------------------------------------------------------------------------------------------
// decoders

struct Movie::Decoder {
    int w = 0, h = 0;
    virtual ~Decoder() = default;
    // decodes one chunk on top of `img` (the previous frame)
    virtual bool decode(const uint8_t* d, size_t n, std::vector<uint32_t>& img) = 0;
};

namespace {

struct BitmapInfo {
    uint32_t compression = 0;
    int width = 0, height = 0, bitCount = 0;
    uint32_t palette[256] = {};
    std::vector<uint8_t> extra;   // bytes after the 40-byte header (palette / codec data)
};

// Uncompressed DIB frames
struct RawDecoder : Movie::Decoder {
    BitmapInfo bi;
    bool decode(const uint8_t* d, size_t n, std::vector<uint32_t>& img) override {
        int bpp = bi.bitCount;
        size_t stride = (((size_t)w * bpp + 31) / 32) * 4;
        bool bottomUp = bi.height > 0;
        for (int y = 0; y < h; y++) {
            size_t ro = (size_t)(bottomUp ? h - 1 - y : y) * stride;
            if (ro + stride > n) break;
            const uint8_t* r = d + ro;
            uint32_t* o = &img[(size_t)y * w];
            for (int x = 0; x < w; x++) {
                switch (bpp) {
                case 8: o[x] = bi.palette[r[x]]; break;
                case 16: {
                    uint16_t v = le16(r + x * 2);
                    int R = (v >> 10) & 31, G = (v >> 5) & 31, B = v & 31;
                    o[x] = (uint32_t)(R << 3 | R >> 2) << 16 | (G << 3 | G >> 2) << 8 | (B << 3 | B >> 2);
                    break;
                }
                case 24: o[x] = (uint32_t)r[x * 3 + 2] << 16 | r[x * 3 + 1] << 8 | r[x * 3]; break;
                case 32: o[x] = (uint32_t)r[x * 4 + 2] << 16 | r[x * 4 + 1] << 8 | r[x * 4]; break;
                default: break;
                }
            }
        }
        return true;
    }
};

// Microsoft RLE8 (BI_RLE8), frames are deltas over the previous frame
struct Rle8Decoder : Movie::Decoder {
    BitmapInfo bi;
    std::vector<uint8_t> idx;
    bool decode(const uint8_t* d, size_t n, std::vector<uint32_t>& img) override {
        if (idx.empty()) idx.assign((size_t)w * h, 0);
        int x = 0, y = h - 1;
        size_t p = 0;
        auto put = [&](uint8_t c) {
            if (x < w && y >= 0 && y < h) idx[(size_t)y * w + x] = c;
            x++;
        };
        while (p + 1 < n) {
            uint8_t a = d[p++], b = d[p++];
            if (a) { for (int i = 0; i < a; i++) put(b); continue; }
            if (b == 0) { x = 0; y--; }
            else if (b == 1) break;
            else if (b == 2) { if (p + 1 >= n) break; x += d[p]; y -= d[p + 1]; p += 2; }
            else {
                for (int i = 0; i < b && p < n; i++) put(d[p++]);
                if (b & 1) p++;
            }
        }
        for (size_t i = 0; i < idx.size(); i++) img[i] = bi.palette[idx[i]];
        return true;
    }
};

// Cinepak (Radius 'cvid')
struct CinepakDecoder : Movie::Decoder {
    struct Strip {
        uint32_t v1[256][4] = {}, v4[256][4] = {};
        int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    };
    std::vector<Strip> strips;

    static uint8_t clip(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

    void codebook(uint32_t (*cb)[4], int id, const uint8_t* d, const uint8_t* end) {
        int n = (id & 0x04) ? 4 : 6;
        uint32_t flag = 0, mask = 0;
        for (int i = 0; i < 256; i++) {
            if ((id & 0x01) && !(mask >>= 1)) {
                if (d + 4 > end) break;
                flag = be32(d);
                d += 4;
                mask = 0x80000000u;
            }
            if (!(id & 0x01) || (flag & mask)) {
                if (d + n > end) break;
                int u = 0, v = 0;
                if (n == 6) { u = (int8_t)d[4]; v = (int8_t)d[5]; }
                for (int k = 0; k < 4; k++) {
                    int y = d[k];
                    int r = y + v * 2, g = y - (u / 2) - v, b = y + u * 2;
                    cb[i][k] = (uint32_t)clip(r) << 16 | clip(g) << 8 | clip(b);
                }
                d += n;
            }
        }
    }

    void vectors(Strip& s, int id, const uint8_t* d, const uint8_t* end, std::vector<uint32_t>& img) {
        uint32_t flag = 0, mask = 0;
        auto px = [&](int x, int y, uint32_t c) {
            if (x >= 0 && y >= 0 && x < w && y < h) img[(size_t)y * w + x] = c;
        };
        for (int y = s.y1; y < s.y2; y += 4) {
            for (int x = s.x1; x < s.x2; x += 4) {
                if ((id & 0x01) && !(mask >>= 1)) {
                    if (d + 4 > end) return;
                    flag = be32(d);
                    d += 4;
                    mask = 0x80000000u;
                }
                if (!(id & 0x01) || (flag & mask)) {
                    if (!(id & 0x02) && !(mask >>= 1)) {
                        if (d + 4 > end) return;
                        flag = be32(d);
                        d += 4;
                        mask = 0x80000000u;
                    }
                    if ((id & 0x02) || (~flag & mask)) {
                        if (d >= end) return;
                        const uint32_t* c = s.v1[*d++];
                        for (int j = 0; j < 4; j++)
                            for (int i = 0; i < 4; i++) px(x + i, y + j, c[(j >> 1) * 2 + (i >> 1)]);
                    } else {
                        if (d + 4 > end) return;
                        for (int q = 0; q < 4; q++) {
                            const uint32_t* c = s.v4[d[q]];
                            int bx = x + (q & 1) * 2, by = y + (q >> 1) * 2;
                            px(bx, by, c[0]); px(bx + 1, by, c[1]);
                            px(bx, by + 1, c[2]); px(bx + 1, by + 1, c[3]);
                        }
                        d += 4;
                    }
                }
            }
        }
    }

    bool decode(const uint8_t* d, size_t n, std::vector<uint32_t>& img) override {
        if (n < 10) return false;
        int flags = d[0];
        int count = be16(d + 8);
        if ((int)strips.size() < count) strips.resize(count);
        const uint8_t* p = d + 10;
        const uint8_t* end = d + n;
        int y0 = 0;
        for (int i = 0; i < count; i++) {
            if (p + 12 > end) break;
            Strip& s = strips[i];
            if (i > 0 && !(flags & 0x01)) {
                memcpy(s.v1, strips[i - 1].v1, sizeof s.v1);
                memcpy(s.v4, strips[i - 1].v4, sizeof s.v4);
            }
            int size = be16(p + 2);
            if (size < 12 || p + size > end) size = (int)(end - p);
            s.y1 = be16(p + 4);
            if (s.y1 == 0) { s.y1 = y0; s.y2 = y0 + be16(p + 8); }
            else s.y2 = be16(p + 8);
            s.x1 = be16(p + 6);
            s.x2 = be16(p + 10);
            if (s.x2 == 0 || s.x2 > w) s.x2 = w;
            y0 = s.y2;
            const uint8_t* c = p + 12;
            const uint8_t* se = p + size;
            while (c + 4 <= se) {
                int cid = c[0];
                int cs = be16(c + 2);
                if (cs < 4 || c + cs > se) cs = (int)(se - c);
                const uint8_t* cd = c + 4;
                const uint8_t* ce = c + cs;
                switch (cid) {
                case 0x20: case 0x21: case 0x24: case 0x25: codebook(s.v4, cid, cd, ce); break;
                case 0x22: case 0x23: case 0x26: case 0x27: codebook(s.v1, cid, cd, ce); break;
                case 0x30: case 0x31: case 0x32: vectors(s, cid, cd, ce, img); break;
                default: break;
                }
                c += cs;
            }
            p += size;
        }
        return true;
    }
};

#ifdef OPENRM95_FFMPEG
struct FfmpegDecoder : Movie::Decoder {
    AVCodecContext* ctx = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
    SwsContext* sws = nullptr;
    BitmapInfo bi;
    bool first = true;
    ~FfmpegDecoder() override {
        if (sws) sws_freeContext(sws);
        if (frame) av_frame_free(&frame);
        if (pkt) av_packet_free(&pkt);
        if (ctx) avcodec_free_context(&ctx);
    }
    bool init(uint32_t tag) {
        const AVCodecTag* const tags[] = {avformat_get_riff_video_tags(), nullptr};
        AVCodecID id = av_codec_get_id(tags, tag);
        const AVCodec* c = avcodec_find_decoder(id);
        if (!c) return false;
        ctx = avcodec_alloc_context3(c);
        ctx->width = w;
        ctx->height = h;
        ctx->codec_tag = tag;
        ctx->bits_per_coded_sample = bi.bitCount;
        if (!bi.extra.empty()) {
            ctx->extradata = (uint8_t*)av_mallocz(bi.extra.size() + AV_INPUT_BUFFER_PADDING_SIZE);
            memcpy(ctx->extradata, bi.extra.data(), bi.extra.size());
            ctx->extradata_size = (int)bi.extra.size();
        }
        if (avcodec_open2(ctx, c, nullptr) < 0) return false;
        frame = av_frame_alloc();
        pkt = av_packet_alloc();
        return frame && pkt;
    }
    bool decode(const uint8_t* d, size_t n, std::vector<uint32_t>& img) override {
        if (av_new_packet(pkt, (int)n) < 0) return false;
        memcpy(pkt->data, d, n);
        if (first && bi.bitCount <= 8) {
            uint8_t* pal = av_packet_new_side_data(pkt, AV_PKT_DATA_PALETTE, AVPALETTE_SIZE);
            if (pal)
                for (int i = 0; i < 256; i++) {
                    uint32_t v = 0xff000000u | bi.palette[i];
                    memcpy(pal + i * 4, &v, 4);
                }
        }
        first = false;
        int r = avcodec_send_packet(ctx, pkt);
        av_packet_unref(pkt);
        if (r < 0) return false;
        bool got = false;
        while (avcodec_receive_frame(ctx, frame) == 0) {
            sws = sws_getCachedContext(sws, frame->width, frame->height, (AVPixelFormat)frame->format, w, h,
                                       AV_PIX_FMT_BGRA, SWS_POINT, nullptr, nullptr, nullptr);
            if (!sws) return false;
            uint8_t* dst[4] = {(uint8_t*)img.data(), nullptr, nullptr, nullptr};
            int stride[4] = {w * 4, 0, 0, 0};
            sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, stride);
            got = true;
        }
        for (uint32_t& p : img) p &= 0xffffff;
        return got || r == 0;
    }
};
#endif

}  // namespace

// ------------------------------------------------------------------------------------------

Movie::Movie() = default;
Movie::~Movie() = default;

bool Movie::open(const std::string& path, std::string& error) {
    if (!fs::readFile(path, data_) || data_.size() < 12 || memcmp(data_.data(), "RIFF", 4) != 0 ||
        memcmp(data_.data() + 8, "AVI ", 4) != 0) {
        error = "not an AVI file";
        return false;
    }
    const uint8_t* d = data_.data();
    size_t total = std::min<size_t>(data_.size(), (size_t)le32(d + 4) + 8);
    BitmapInfo bi;
    uint32_t handler = 0;
    int videoStream = -1, stream = -1;
    bool isVideo = false;
    double usPerFrame = 0, rateMs = 0;
    std::vector<Chunk> chunks;
    // recursive RIFF walk
    struct Walker {
        Movie& m;
        const uint8_t* d;
        BitmapInfo& bi;
        uint32_t& handler;
        int& videoStream;
        int& stream;
        bool& isVideo;
        double& usPerFrame;
        double& rateMs;
        void list(size_t off, size_t end, bool movi) {
            while (off + 8 <= end) {
                uint32_t id = le32(d + off);
                size_t sz = le32(d + off + 4);
                size_t body = off + 8;
                if (body + sz > end) sz = end - body;
                if (id == fourcc("LIST") || id == fourcc("RIFF")) {
                    if (sz >= 4) {
                        uint32_t type = le32(d + body);
                        if (type == fourcc("strl")) { stream++; isVideo = false; }
                        list(body + 4, body + sz, movi || type == fourcc("movi"));
                    }
                } else if (movi) {
                    char a = (char)(id & 0xff), b = (char)((id >> 8) & 0xff);
                    char c = (char)((id >> 16) & 0xff), e = (char)(id >> 24);
                    if (a >= '0' && a <= '9' && b >= '0' && b <= '9' && (a - '0') * 10 + (b - '0') == videoStream &&
                        ((c == 'd' && (e == 'c' || e == 'b')) || (c == 'i' && e == 'v')))
                        m.frames_.push_back({body, sz, false});
                } else if (id == fourcc("avih") && sz >= 4) {
                    usPerFrame = le32(d + body);
                } else if (id == fourcc("strh") && sz >= 28) {
                    if (le32(d + body) == fourcc("vids") && videoStream < 0) {
                        isVideo = true;
                        videoStream = stream;
                        handler = le32(d + body + 4);
                        uint32_t scale = le32(d + body + 20), rate = le32(d + body + 24);
                        if (scale && rate) rateMs = 1000.0 * scale / rate;
                    }
                } else if (id == fourcc("strf") && isVideo && stream == videoStream && sz >= 40) {
                    bi.width = (int32_t)le32(d + body + 4);
                    bi.height = (int32_t)le32(d + body + 8);
                    bi.bitCount = le16(d + body + 14);
                    bi.compression = le32(d + body + 16);
                    size_t hs = std::max<size_t>(le32(d + body), 40);
                    if (hs < sz) bi.extra.assign(d + body + hs, d + body + sz);
                    if (bi.bitCount <= 8) {
                        uint32_t used = le32(d + body + 32);
                        int nColors = used ? (int)std::min<uint32_t>(used, 256) : 1 << bi.bitCount;
                        for (int i = 0; i < nColors && hs + i * 4 + 4 <= sz; i++) {
                            const uint8_t* q = d + body + hs + i * 4;
                            bi.palette[i] = (uint32_t)q[2] << 16 | q[1] << 8 | q[0];
                        }
                    }
                }
                off = body + sz + (sz & 1);
            }
        }
    } walker{*this, d, bi, handler, videoStream, stream, isVideo, usPerFrame, rateMs};
    walker.list(12, total, false);
    if (videoStream < 0 || bi.width <= 0 || bi.height == 0) {
        error = "no video stream";
        return false;
    }
    w_ = bi.width;
    h_ = bi.height < 0 ? -bi.height : bi.height;
    frameMs_ = usPerFrame > 0 ? usPerFrame / 1000.0 : (rateMs > 0 ? rateMs : 66.67);
    uint32_t comp = bi.compression;
    auto lower = [](uint32_t f) {
        uint32_t r = 0;
        for (int i = 0; i < 4; i++) { uint8_t c = (uint8_t)(f >> (8 * i)); if (c >= 'A' && c <= 'Z') c += 32; r |= (uint32_t)c << (8 * i); }
        return r;
    };
    if (comp == 0 || comp == fourcc("DIB ")) {
        auto r = std::make_unique<RawDecoder>();
        r->bi = bi;
        dec_ = std::move(r);
    } else if (comp == 1 || lower(comp) == fourcc("mrle")) {
        auto r = std::make_unique<Rle8Decoder>();
        r->bi = bi;
        dec_ = std::move(r);
    } else if (lower(comp) == fourcc("cvid")) {
        dec_ = std::make_unique<CinepakDecoder>();
    } else {
#ifdef OPENRM95_FFMPEG
        auto f = std::make_unique<FfmpegDecoder>();
        f->w = w_;
        f->h = h_;
        f->bi = bi;
        if (f->init(comp)) dec_ = std::move(f);
#endif
        if (!dec_) {
            char cc[5] = {(char)(comp & 0xff), (char)((comp >> 8) & 0xff), (char)((comp >> 16) & 0xff), (char)(comp >> 24), 0};
            error = std::string("unsupported video codec ") + cc;
            return false;
        }
    }
    (void)handler;
    dec_->w = w_;
    dec_->h = h_;
    cur_.assign((size_t)w_ * h_, 0);
    next_ = 0;
    return true;
}

bool Movie::frame(int i, std::vector<uint32_t>& out) {
    if (!dec_ || i < 0 || i >= (int)frames_.size()) return false;
    while (next_ <= i) {
        const Chunk& c = frames_[next_];
        if (c.size > 0) dec_->decode(data_.data() + c.off, c.size, cur_);
        next_++;
    }
    out = cur_;
    return true;
}

}  // namespace rm95
