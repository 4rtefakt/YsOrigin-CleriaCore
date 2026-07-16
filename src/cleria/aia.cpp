#include "cleria/aia.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace cleria {
namespace {

std::uint16_t rd16(const std::uint8_t* p) {
    std::uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}
std::uint32_t rd32(const std::uint8_t* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

}  // namespace

bool Aia::load(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    data.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    if (data.size() < 0x28 || std::memcmp(data.data(), "AIA\0", 4) != 0) { data.clear(); return false; }
    version = rd16(&data[4]);
    if (version != 0x150) { data.clear(); return false; }  // only the shipping version
    nPal = rd32(&data[8]);
    nImg = rd32(&data[12]);
    nReg = rd32(&data[16]);
    canvasW = rd16(&data[0x14]);
    canvasH = rd16(&data[0x16]);

    imgtab = 0x28;
    regtab = imgtab + std::size_t(nImg) * 16;
    palbase = regtab + std::size_t(nReg) * 32;
    pxbase = palbase + std::size_t(nPal) * 256 * 4;
    if (pxbase > data.size()) { data.clear(); return false; }

    images.resize(nImg);
    for (std::uint32_t i = 0; i < nImg; ++i) {
        const std::uint8_t* o = &data[imgtab + std::size_t(i) * 16];
        images[i] = {rd32(o), rd32(o + 4), rd32(o + 8), rd32(o + 12)};
    }

    // Precompute distinct pixel-data offsets so frame_len is a binary search.
    sorted_offsets.reserve(nReg);
    for (std::uint32_t r = 0; r < nReg; ++r)
        sorted_offsets.push_back(rd32(&data[regtab + std::size_t(r) * 32]));
    std::sort(sorted_offsets.begin(), sorted_offsets.end());
    sorted_offsets.erase(std::unique(sorted_offsets.begin(), sorted_offsets.end()),
                         sorted_offsets.end());
    return true;
}

// Compressed length of the blob at imageOfs = distance to the next distinct offset.
std::uint32_t Aia::frame_len(std::uint32_t imageOfs) const {
    auto it = std::lower_bound(sorted_offsets.begin(), sorted_offsets.end(), imageOfs);
    if (it == sorted_offsets.end()) return 0;
    auto nxt = it + 1;
    const std::uint32_t end = (nxt != sorted_offsets.end())
                                  ? *nxt
                                  : std::uint32_t(data.size() - pxbase);
    return end - imageOfs;
}

bool Aia::decode_region(std::uint32_t region, AiaFrame& out) const {
    if (region >= nReg) return false;
    const std::uint8_t* rec = &data[regtab + std::size_t(region) * 32];
    const std::uint32_t imageOfs = rd32(rec);
    const std::uint16_t palIndex = rd16(rec + 4);
    const std::uint16_t x0 = rd16(rec + 0x18), y0 = rd16(rec + 0x1A);
    const std::uint16_t x1 = rd16(rec + 0x1C), y1 = rd16(rec + 0x1E);
    if (x1 <= x0 || y1 <= y0) return false;
    const std::uint32_t W = x1 - x0, H = y1 - y0;

    out.w = W; out.h = H; out.x0 = x0; out.y0 = y0;
    std::memcpy(out.bill, rec + 8, sizeof out.bill);
    out.rgba.assign(std::size_t(W) * H * 4, 0);  // zero = transparent

    // Palette: 256 entries of B,G,R,pad; the loader forces alpha opaque.
    const std::uint8_t* pal = &data[palbase + std::size_t(palIndex) * 1024];
    auto put = [&](std::size_t px, std::uint8_t idx, std::uint8_t a) {
        std::uint8_t* d = &out.rgba[px * 4];
        d[0] = pal[idx * 4 + 2];  // R
        d[1] = pal[idx * 4 + 1];  // G
        d[2] = pal[idx * 4 + 0];  // B
        d[3] = a;
    };

    const std::size_t src0 = pxbase + imageOfs;
    const std::size_t end = src0 + frame_len(imageOfs);
    std::size_t src = src0;
    std::uint32_t x = 0, y = 0;
    // Big-endian control word per span; rows fill left->right, wrap at width.
    while (y < H && src + 1 < end) {
        const std::uint8_t b0 = data[src], b1 = data[src + 1];
        const std::uint32_t word = (std::uint32_t(b0) << 8) | b1;
        src += 2;
        if (b0 & 0x80) {  // LITERAL: n opaque palette indices
            std::uint32_t n = word & 0xFFF;
            while (n-- && y < H && src < end) {
                put(std::size_t(y) * W + x, data[src++], 255);
                if (++x >= W) { x = 0; ++y; }
            }
        } else if (b0 & 0x40) {  // ALPHA: n (alpha, index) pairs
            std::uint32_t n = word & 0xFFF;
            while (n-- && y < H && src + 1 < end) {
                const std::uint8_t a = data[src], idx = data[src + 1];
                src += 2;
                put(std::size_t(y) * W + x, idx, a);
                if (++x >= W) { x = 0; ++y; }
            }
        } else {  // SKIP: n transparent pixels, no data
            std::uint32_t n = word;
            while (n-- && y < H) {
                if (++x >= W) { x = 0; ++y; }
            }
        }
    }
    return true;
}

bool Aia::decode_frame(std::uint32_t image, std::uint32_t dir,
                       std::uint32_t frame, AiaFrame& out) const {
    const std::uint32_t region = frame_region(image, dir, frame);
    return region != kNoRegion && decode_region(region, out);
}

// Body/2nd-layer pairs, hence the *2. kNoRegion on a bad index.
std::uint32_t Aia::frame_region(std::uint32_t image, std::uint32_t dir,
                                std::uint32_t frame) const {
    if (image >= nImg) return kNoRegion;
    const AiaImage& im = images[image];
    if (dir >= im.nDir || frame >= im.nFrame) return kNoRegion;
    return im.first + (im.nFrame * dir + frame) * 2;
}

bool Aia::has_layer(std::uint32_t body_region) const {
    if (body_region + 1 >= nReg) return false;
    // Bit 0 of the BODY's flags — not bit 7 of the slot.
    return (data[regtab + std::size_t(body_region) * 32 + 6] & 0x01) != 0;
}

bool Aia::decode_layer(std::uint32_t body_region, AiaFrame& out) const {
    if (!has_layer(body_region)) return false;
    return decode_region(body_region + 1, out);
}

bool Aia::decode_frame_layer(std::uint32_t image, std::uint32_t dir,
                             std::uint32_t frame, AiaFrame& out) const {
    const std::uint32_t region = frame_region(image, dir, frame);
    return region != kNoRegion && decode_layer(region, out);
}

}  // namespace cleria
