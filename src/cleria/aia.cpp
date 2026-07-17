#include "cleria/aia.hpp"

#include <algorithm>
#include <cstdlib>
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

bool Aia::decode_frame_composited(std::uint32_t image, std::uint32_t dir,
                                  std::uint32_t frame, AiaFrame& out) const {
    if (!decode_frame(image, dir, frame, out)) return false;
    if (std::getenv("CLERIA_NOLAYER")) return true;  // A/B the composite
    AiaFrame layer;
    // Additive: [C-DIS] SRCALPHA/ONE. See aia.hpp's note on `has_layer`.
    if (decode_frame_layer(image, dir, frame, layer)) composite_add(out, layer);
    return true;
}

namespace {
// Shared canvas-space setup for the two composites. Returns the union rect and the
// body blitted into it; the caller supplies how the layer combines.
struct Canvas {
    std::vector<std::uint8_t> px;
    std::uint32_t x0 = 0, y0 = 0, w = 0, h = 0;
};

Canvas union_canvas(const AiaFrame& body, const AiaFrame& layer) {
    Canvas c;
    c.x0 = std::min<std::uint32_t>(body.x0, layer.x0);
    c.y0 = std::min<std::uint32_t>(body.y0, layer.y0);
    const std::uint32_t x1 = std::max<std::uint32_t>(body.x0 + body.w, layer.x0 + layer.w);
    const std::uint32_t y1 = std::max<std::uint32_t>(body.y0 + body.h, layer.y0 + layer.h);
    c.w = x1 - c.x0;
    c.h = y1 - c.y0;
    c.px.assign(std::size_t(c.w) * c.h * 4, 0);
    // The body goes down as a straight copy — it is the bottom of the stack.
    for (std::uint32_t y = 0; y < body.h; ++y) {
        for (std::uint32_t x = 0; x < body.w; ++x) {
            const std::size_t si = (std::size_t(y) * body.w + x) * 4;
            if (body.rgba[si + 3] == 0) continue;
            const std::size_t di =
                ((std::size_t(body.y0 - c.y0 + y)) * c.w + (body.x0 - c.x0 + x)) * 4;
            std::memcpy(&c.px[di], &body.rgba[si], 4);
        }
    }
    return c;
}

void finish(AiaFrame& body, Canvas& c) {
    body.rgba.swap(c.px);
    body.w = c.w;
    body.h = c.h;
    body.x0 = std::uint16_t(c.x0);
    body.y0 = std::uint16_t(c.y0);
}
}  // namespace

// [C-DIS] the engine's blend for the layer pass: SRCALPHA/ONE. Saturating add of the
// layer's colour modulated by its own alpha, destination alpha maxed. Pure black is
// the identity here, which is why ~22% of a layer's opaque pixels can be black
// without punching holes — the observation that predicted this before we found it.
void composite_add(AiaFrame& body, const AiaFrame& layer) {
    if (layer.w == 0 || layer.h == 0) return;
    if (body.w == 0 || body.h == 0) { body = layer; return; }
    Canvas c = union_canvas(body, layer);
    for (std::uint32_t y = 0; y < layer.h; ++y) {
        for (std::uint32_t x = 0; x < layer.w; ++x) {
            const std::size_t si = (std::size_t(y) * layer.w + x) * 4;
            const std::uint32_t sa = layer.rgba[si + 3];
            if (sa == 0) continue;
            const std::size_t di =
                ((std::size_t(layer.y0 - c.y0 + y)) * c.w + (layer.x0 - c.x0 + x)) * 4;
            for (int k = 0; k < 3; ++k) {
                // SRCALPHA/ONE — the layer is modulated by its own alpha, then added.
                const std::uint32_t add = layer.rgba[si + k] * sa / 255;
                c.px[di + k] = std::uint8_t(std::min<std::uint32_t>(255, c.px[di + k] + add));
            }
            c.px[di + 3] = std::uint8_t(std::max<std::uint32_t>(c.px[di + 3], sa));
        }
    }
    finish(body, c);
}

void composite_over(AiaFrame& body, const AiaFrame& layer) {
    if (layer.w == 0 || layer.h == 0) return;
    if (body.w == 0 || body.h == 0) { body = layer; return; }

    // Union of the two canvas-space rects.
    const std::uint32_t x0 = std::min<std::uint32_t>(body.x0, layer.x0);
    const std::uint32_t y0 = std::min<std::uint32_t>(body.y0, layer.y0);
    const std::uint32_t x1 = std::max<std::uint32_t>(body.x0 + body.w, layer.x0 + layer.w);
    const std::uint32_t y1 = std::max<std::uint32_t>(body.y0 + body.h, layer.y0 + layer.h);
    const std::uint32_t w = x1 - x0, h = y1 - y0;

    std::vector<std::uint8_t> out(std::size_t(w) * h * 4, 0);
    auto blit = [&](const AiaFrame& src, bool over) {
        for (std::uint32_t y = 0; y < src.h; ++y) {
            const std::size_t dy = std::size_t(src.y0 - y0 + y);
            for (std::uint32_t x = 0; x < src.w; ++x) {
                const std::size_t si = (std::size_t(y) * src.w + x) * 4;
                const std::size_t di = (dy * w + (src.x0 - x0 + x)) * 4;
                const std::uint32_t sa = src.rgba[si + 3];
                if (sa == 0) continue;
                if (!over || sa == 255) {
                    std::memcpy(&out[di], &src.rgba[si], 4);
                    continue;
                }
                // Straight-alpha "src over dst".
                const std::uint32_t da = out[di + 3];
                const std::uint32_t oa = sa + da * (255 - sa) / 255;
                for (int k = 0; k < 3; ++k) {
                    const std::uint32_t sc = src.rgba[si + k], dc = out[di + k];
                    out[di + k] = std::uint8_t(oa ? (sc * sa + dc * da * (255 - sa) / 255) / oa : 0);
                }
                out[di + 3] = std::uint8_t(oa);
            }
        }
    };
    blit(body, false);
    blit(layer, true);

    body.rgba.swap(out);
    body.w = w; body.h = h;
    body.x0 = std::uint16_t(x0); body.y0 = std::uint16_t(y0);
}

}  // namespace cleria
