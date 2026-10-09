#include "cleria/dds.hpp"
#include "cleria/vfs.hpp"   // mods: asset opens go through vfs::resolve

#include <algorithm>
#include <cstring>
#include <fstream>

namespace cleria {
namespace {

std::uint32_t rd32(const std::uint8_t* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

}  // namespace

// The next level of an RGBA8 image: a 2 x 2 box, the colour weighted by alpha (a cutout's transparent
// texels, whatever their colour, do not darken its edge).
static std::vector<std::uint8_t> half_rgba(const std::vector<std::uint8_t>& s, std::uint32_t w, std::uint32_t h) {
    const std::uint32_t nw = std::max(1u, w / 2), nh = std::max(1u, h / 2);
    std::vector<std::uint8_t> d(std::size_t(nw) * nh * 4);
    for (std::uint32_t y = 0; y < nh; ++y)
        for (std::uint32_t x = 0; x < nw; ++x) {
            std::uint32_t c[3] = {0, 0, 0}, p[3] = {0, 0, 0}, a = 0;
            for (int k = 0; k < 4; ++k) {
                const std::uint8_t* t = &s[(std::size_t(std::min(h - 1, y * 2 + (k >> 1))) * w +
                                            std::min(w - 1, x * 2 + (k & 1))) * 4];
                for (int i = 0; i < 3; ++i) { c[i] += std::uint32_t(t[i]) * t[3]; p[i] += t[i]; }
                a += t[3];
            }
            std::uint8_t* o = &d[(std::size_t(y) * nw + x) * 4];
            for (int i = 0; i < 3; ++i) o[i] = std::uint8_t(a ? (c[i] + a / 2) / a : (p[i] + 2) / 4);
            o[3] = std::uint8_t((a + 2) / 4);
        }
    return d;
}

Dds load_dds(const std::filesystem::path& path, bool mips) {
    Dds out;
    std::ifstream f(vfs::resolve(path), std::ios::binary);
    if (!f) return out;
    std::vector<std::uint8_t> b{std::istreambuf_iterator<char>(f),
                                std::istreambuf_iterator<char>()};
    if (b.size() < 128 || std::memcmp(b.data(), "DDS ", 4) != 0) return out;

    // DDS_HEADER: height @12, width @16; PIXELFORMAT flags @80, fourCC @84,
    // RGB bit count @88, R/G/B/A masks @92..104.
    const std::uint32_t height = rd32(&b[12]);
    const std::uint32_t width = rd32(&b[16]);
    const std::uint32_t pf_flags = rd32(&b[80]);
    const std::uint8_t* fourcc = &b[84];
    const std::size_t data_off = 128;  // no DX10 extended header handled
    // The file's own mip chain: DDSD_MIPMAPCOUNT (0x20000) in the flags @8, the count @28.
    const std::uint32_t file_levels = mips && (rd32(&b[8]) & 0x20000) ? std::max(1u, rd32(&b[28])) : 1;

    out.width = width;
    out.height = height;

    auto block_bytes = [&](std::size_t blk) {
        const std::size_t bw = (width + 3) / 4, bh = (height + 3) / 4;
        return bw * bh * blk;
    };
    // The block-compressed levels after mip 0 (which ends at `off`), as far as the file holds them.
    auto block_mips = [&](std::size_t off, std::size_t blk) {
        std::uint32_t w = width, h = height;
        for (std::uint32_t l = 1; l < file_levels && (w > 1 || h > 1); ++l) {
            w = std::max(1u, w / 2); h = std::max(1u, h / 2);
            const std::size_t n = std::size_t((w + 3) / 4) * ((h + 3) / 4) * blk;
            if (off + n > b.size()) break;
            out.mips.emplace_back(b.begin() + off, b.begin() + off + n);
            off += n;
        }
    };

    if (pf_flags & 0x4 /*DDPF_FOURCC*/) {
        if (std::memcmp(fourcc, "DXT1", 4) == 0) {
            out.format = Dds::BC1;
            const std::size_t n = block_bytes(8);
            if (data_off + n > b.size()) return {};
            out.data.assign(b.begin() + data_off, b.begin() + data_off + n);
            block_mips(data_off + n, 8);
        } else if (std::memcmp(fourcc, "DXT3", 4) == 0 ||
                   std::memcmp(fourcc, "DXT5", 4) == 0) {
            // DXT3 -> BC2 is exact. DXT5 (BC3) is NOT BC2: they share the colour
            // block but not the alpha block, so this would give it garbage alpha —
            // which feeds straight into the alpha test. It is dead code in practice:
            // the shipped corpus has ZERO DXT5 (3819 DXT1, 296 DXT3, 3966 RGB16,
            // 847 RGB32, 50 RGB24). Decode BC3 properly before relying on it.
            out.format = Dds::BC2;
            const std::size_t n = block_bytes(16);
            if (data_off + n > b.size()) return {};
            out.data.assign(b.begin() + data_off, b.begin() + data_off + n);
            block_mips(data_off + n, 16);
        }
        return out;
    }

    // Uncompressed. Decode any 16- or 32-bit layout from its channel masks and
    // expand to RGBA8. Covers RGB565, A1R5G5B5, A4R4G4B4, X8R8G8B8, A8R8G8B8.
    if (pf_flags & 0x40 /*DDPF_RGB*/) {
        const std::uint32_t bits = rd32(&b[88]);
        // 24-bit matters: 50 files use it, including every SKYBOX/B_*/H/BACK*.DDS.
        // Rejecting it silently left five rooms (S_1100/1101/1102, S_2100/2101)
        // drawing their sky as untextured white.
        if (bits != 16 && bits != 24 && bits != 32) return out;
        const std::uint32_t rmask = rd32(&b[92]);
        const std::uint32_t gmask = rd32(&b[96]);
        const std::uint32_t bmask = rd32(&b[100]);
        // Only honor alpha if DDPF_ALPHAPIXELS (0x1) is set; else X8R8G8B8 etc.
        // would feed garbage alpha into the alpha test and vanish.
        const std::uint32_t amask = (pf_flags & 0x1) ? rd32(&b[104]) : 0;
        const std::size_t bpp = bits / 8;
        const std::size_t px = std::size_t(width) * height;
        if (data_off + px * bpp > b.size()) return {};

        // Extract one channel, scaled to 8 bits. mask==0 => 255 (opaque).
        auto chan = [](std::uint32_t v, std::uint32_t mask) -> std::uint8_t {
            if (!mask) return 255;
            std::uint32_t m = mask; int shift = 0;
            while (!(m & 1)) { m >>= 1; ++shift; }
            int nbits = 0; std::uint32_t mm = m;
            while (mm & 1) { mm >>= 1; ++nbits; }
            const std::uint32_t c = (v >> shift) & ((1u << nbits) - 1);
            return std::uint8_t((c * 255u) / ((1u << nbits) - 1));
        };

        auto decode = [&](std::size_t off, std::size_t n, std::vector<std::uint8_t>& dst) {
            dst.resize(n * 4);
            for (std::size_t i = 0; i < n; ++i) {
                const std::uint8_t* s = &b[off + i * bpp];
                std::uint32_t v = s[0] | (std::uint32_t(s[1]) << 8);
                if (bpp >= 3) v |= std::uint32_t(s[2]) << 16;
                if (bpp == 4) v |= std::uint32_t(s[3]) << 24;
                std::uint8_t* d = &dst[i * 4];
                d[0] = chan(v, rmask);
                d[1] = chan(v, gmask);
                d[2] = chan(v, bmask);
                d[3] = chan(v, amask);
            }
        };
        decode(data_off, px, out.data);
        out.format = Dds::RGBA8;
        if (mips) {
            std::size_t off = data_off + px * bpp;
            std::uint32_t w = width, h = height;
            for (std::uint32_t l = 1; w > 1 || h > 1; ++l) {
                const std::vector<std::uint8_t>& prev = out.mips.empty() ? out.data : out.mips.back();
                const std::uint32_t pw = w, ph = h;
                w = std::max(1u, w / 2); h = std::max(1u, h / 2);
                const std::size_t n = std::size_t(w) * h;
                if (l < file_levels && off + n * bpp <= b.size()) {
                    out.mips.emplace_back();
                    decode(off, n, out.mips.back());
                    off += n * bpp;
                } else {
                    out.mips.push_back(half_rgba(prev, pw, ph));
                }
            }
        }
    }
    return out;
}

}  // namespace cleria
