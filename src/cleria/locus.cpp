// GoLocusSword — see locus.hpp and build/ability_re/LOCUS.md.
#include "cleria/locus.hpp"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

namespace cleria {

namespace {

template <class T>
T rd(const std::vector<std::uint8_t>& b, std::size_t o) {
    T v{};
    std::memcpy(&v, b.data() + o, sizeof(T));
    return v;
}

}  // namespace

Sl2 Sl2::parse(const std::vector<std::uint8_t>& b) {
    Sl2 s;
    if (b.size() < 0x1c) return s;
    const std::int32_t nf = rd<std::int32_t>(b, 4), nv = rd<std::int32_t>(b, 8), nt = rd<std::int32_t>(b, 0xc);
    const std::int32_t nr = rd<std::int32_t>(b, 0x18);
    if (nf <= 0 || nv <= 0 || nt < 0 || nr <= 0 || nf > 4096 || nv > 4096 || nr > 4096) return s;
    // FUN_00453e90 reads nv*0x1c + nr*16 + nf*(nv*12+8) bytes after the 0x1c header (its buffer has
    // nt*6 more, filled later; the shipped files end right after the frames) [BYTES, DATA].
    const std::size_t stride = std::size_t(nv) * 12 + 8;
    const std::size_t rows_at = 0x1c + std::size_t(nv) * 0x1c;
    const std::size_t frames_at = rows_at + std::size_t(nr) * 16;
    if (b.size() < frames_at + std::size_t(nf) * stride) return s;
    s.nframes = nf; s.nverts = nv; s.ntris = nt;
    s.scale = rd<float>(b, 0x10);
    for (std::int32_t r = 0; r < nr; ++r) {
        const std::size_t o = rows_at + std::size_t(r) * 16;
        s.rows.push_back({rd<std::int32_t>(b, o), rd<std::int32_t>(b, o + 4), rd<float>(b, o + 8)});
    }
    for (std::int32_t f = 0; f < nf; ++f) {
        const std::size_t o = frames_at + std::size_t(f) * stride;
        s.advance.push_back(rd<float>(b, o));
        s.tip.push_back({rd<float>(b, o + 8), rd<float>(b, o + 12), rd<float>(b, o + 16)});
    }
    return s;
}

bool Sl2Set::load(const std::filesystem::path& asset_root) {
    bool all = true;
    for (int i = 0; i < 6; ++i) {
        std::ifstream f(asset_root / "EFFECT" / ("SWORD" + std::to_string(i) + ".SL2"), std::ios::binary);
        if (!f) { all = false; continue; }
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        sword[std::size_t(i)] = Sl2::parse(bytes);
        all = all && sword[std::size_t(i)].ok();
    }
    return all;
}

int locus_sword_index(int cast, int character, int flag149, bool adol_extra) {
    auto yunica = [&] { return flag149 != 0 ? 4 : 1; };          // 0x464b36: DAT_0076bb70 = g_flags[149]
    if (cast == 0xd) {
        switch (character) {                                      // 0x464a71: DAT_0076bb74, ja -> 0
            case 0: return adol_extra ? 5 : 0;                    // 0x464a8a: DAT_0074c0cc
            case 1: return yunica();                              // 0x464b2d
            case 2: return 2;                                     // 0x464b03
            case 3: return 3;                                     // 0x464b10
            default: return 0;
        }
    }
    if (cast == 0xa6) return yunica();                            // 0x464b26
    if (cast == 0xa8) return 3;                                   // 0x464b78
    return 0;
}

bool LocusSword::spawn(const Sl2& sl2, const LocusArgs& a, std::uint32_t token,
                       int flag215, int flag217, int character) {
    *this = LocusSword{};
    // 0x464a4f: row = arg[0] - 0x100, indexed unchecked into the row table [BYTES]; a row past
    // the table is refused here [H] (the shipped scripts never reach one with a valid file).
    const std::int32_t row = a.row - 0x100;
    if (!sl2.ok() || row < 0 || row >= std::int32_t(sl2.rows.size())) return false;
    const Sl2::Row& r = sl2.rows[std::size_t(row)];
    if (r.start < 0 || r.end - 1 >= sl2.nframes || r.start >= sl2.nframes) return false;   // [H] guard
    sl2_ = &sl2;
    frame_ = float(r.start);                                      // +0x128
    end_ = float(r.end) - 1.0f;                                   // +0x12c = end - 1.0 (0x6bd438)
    scale_ = r.scale;                                             // +0x140
    if (flag215 != 0) scale_ *= 1.5f;                             // 0x464c4f: DAT_0076bc78, x 0x6bd4d4
    else if ((flag217 & 0x2000) && character == 3) scale_ *= 1.4f;   // 0x464c62: bc80 & 0x2000, Toal, x 0x6bd4b4
    alpha_ = 1.0f;                                                // +0x138 = 1.0
    // FUN_0045e750(side, life, rehit, power, knock, 0.45, token, rtype 0, status 0, cls) [BYTES 0x464d21]:
    rehit_ = a.rehit != 0 ? a.rehit : 15;                         // 0x464d33: cmove 0xf
    life_ = a.life != 0 ? a.life : rehit_;                        // 0x464d54: cmovne arg[8]
    bonus_ = a.bonus;
    knock_ = a.knock;
    token_ = token;
    clank_lo_ = a.clank_lo;
    clank_hi_ = a.clank_hi;
    alive_ = true;
    return true;
}

bool LocusSword::tick(const Owner& owner) {
    armed_ = false;
    if (!alive_ || !sl2_) return false;
    const Sl2& s = *sl2_;
    auto fidx = [&](float f) {                                    // FUN_004217f0 = floor, cvttss2si
        int i = int(std::floor(f));
        return i < 0 ? 0 : (i >= s.nframes ? s.nframes - 1 : i);
    };
    // ---- logic step (0x500344..0x5003f4, DAT_0074c0f0 set) ----
    fi_start_ = int(std::floor(frame_));
    if (freeze_ != 0) --freeze_;                                  // +0x180: a landed hit holds the frame
    else frame_ += s.advance[std::size_t(fidx(frame_))];          // frame += frames[floor(frame)].advance
    if (fade_ != 0.0f) alpha_ -= fade_;
    if (frame_ >= end_) { alive_ = false; return false; }         // 0x50039e: +0x11 |= 2
    if (fade_ != 0.0f) {
        alpha_ -= fade_;                                          // the second subtraction (0x5003c2)
        if (0.0f >= alpha_) { alive_ = false; return false; }
    }
    if (owner.dead) fade_ = 0.15f;                                // 0x5003e1: owner+0x98 < 1.0 -> 0x3e19999a

    // ---- placement (0x5003fa..0x5004fe): P = lerp of vertex 0, then the row scale ----
    const int fi = fidx(frame_);
    const float t = frame_ - float(fi);
    Vec3 p = s.tip[std::size_t(fi)];
    if (t != 0.0f && fi + 1 < s.nframes) {
        const Vec3& q = s.tip[std::size_t(fi + 1)];
        p = {(q.x - p.x) * t + p.x, (q.y - p.y) * t + p.y, (q.z - p.z) * t + p.z};
    }
    p.x *= scale_;
    p.z *= scale_;
    p.y = (scale_ + 1.0f) * 0.5f * p.y;                           // y x (s + 1) / 2 (0x6bd304)

    // 0x500589: no hit life left -> no scan (the trail keeps drawing).
    if (life_ == 0) return true;

    // 0x500503..0x50064d: out = P x Scaling(sl2.scale) x RotationY(-facing - pi/2), D3DX row vectors.
    const float th = -owner.facing - 1.5707964f;                  // xorps 0x6bef00, subss 0x6bd4e4
    const float c = std::cos(th), sn = std::sin(th);
    const float g = s.scale;
    const Vec3 out{g * (p.x * c + p.z * sn), g * p.y, g * (-p.x * sn + p.z * c)};
    tip_ = out;

    // 0x500653..0x50080d: the three sphere centres on floor-point -> tip.
    const float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);   // |P| BEFORE the file scale
    const float inv = len != 0.0f ? 1.0f / len : 0.0f;
    const float rp = kRadius * inv;                               // +0xd8 x 1/|P|
    // +0x168 (never set on this path) would switch the factors to 0 / -2.05 [not modelled].
    const float f_body = 1.0f - 2.5f * rp;                        // 0x6bd624 -> +0x184
    const float f_tip = 1.0f - 0.2f * rp;                         // 0x6bd210 -> +0xd0
    const Vec3 fl{owner.origin.x, owner.origin.y - owner.floor_off, owner.origin.z};   // +0x50, y - +0x324
    const Vec3 a{fl.x + out.x * f_tip, fl.y + out.y * f_tip, fl.z + out.z * f_tip};
    const Vec3 b{fl.x + out.x * f_body, fl.y + out.y * f_body, fl.z + out.z * f_body};
    const Vec3 m{(b.x - a.x) * 0.5f + a.x, (b.y - a.y) * 0.5f + a.y, (b.z - a.z) * 0.5f + a.z};
    sph_[0] = {kRadius, a, 0.0f, 0};
    sph_[1] = {kRadius, b, 0.0f, 0};
    sph_[2] = {kRadius, m, 0.0f, 0};

    // FUN_004fed50 0x4fed67: the life is decremented BEFORE the test (L -> L tested ticks).
    if (life_ != -1) --life_;
    armed_ = true;
    return true;
}

}  // namespace cleria
