// EF2 — the effect format, and the two registries that resolve an effect id to files.
//
// The container, each emitter's scale/lifetime and frame table (texture id + UV rect per
// frame), plus the raw B1 / B2 records and the A array, which the particle simulation
// (ef2_sim.hpp, build/ai_spec/EF2SIM.md) reads field by field. See build/ef2_re/EF2_FORMAT.md
// for the container and build/effect_re/EFFECTS.md for the id catalogue.
//
// Two registries, both {u32 header_zero; u32 count} then 196-byte records of
// {u32 stored_id; char name[192]}:
//   EFTLIST.DAT/2/3  effect id -> "pc\pc02\PC2_DEF_ATTACK.eft"   (640 ids, flat)
//   EFTTEX.DAT       texture id -> "YsF_used\adol_g\aef_4_02.dds" (229)
// EFTTEX's stored ids are 1-BASED, so tex_id N is record N-1 — an off-by-one that
// silently resolves to a plausible-but-wrong texture if you get it backwards.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cleria {

// effect id -> the .EF2 on disk.
class EftList {
  public:
    bool load(const std::filesystem::path& asset_root);
    std::size_t size() const { return names_.size(); }
    // Logical name as stored, e.g. "pc\pc02\PC2_DEF_ATTACK.eft" ("" if unknown).
    const std::string& name(int id) const;
    // The file itself: EFFECT/EFT/PC/PC02/PC2_DEF_ATTACK.EF2, or empty if absent.
    std::filesystem::path path(const std::filesystem::path& asset_root, int id) const;

  private:
    std::vector<std::string> names_;  // flat, indexed by effect id
};

// texture id -> the .DDS on disk.
class EftTex {
  public:
    bool load(const std::filesystem::path& asset_root);
    std::size_t size() const { return names_.size(); }
    // Textures ship in H and L quality tiers, as a directory inserted before the
    // filename; H is preferred and L is the fallback.
    std::filesystem::path path(const std::filesystem::path& asset_root, int tex_id) const;

  private:
    std::vector<std::string> names_;  // indexed by stored id (index 0 unused)
};

struct Ef2Frame {
    std::uint32_t tex_id = 0;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
};

struct Ef2Emitter {
    std::uint32_t life = 0;              // B1 +0x24, in ticks [H-medium]
    float sx = 1, sy = 1, sz = 1;        // B1 +0x28
    std::vector<Ef2Frame> frames;        // B2 +0x48 count, +0x4c ids, +0x8c uv rects
    // Orientation MODE + blend, from the B2 header (build/effect_re/EF2_RENDER.md §A). `ring`
    // (B2+0x00 bit 0x8) means this emitter is NOT a billboard but a procedurally-generated FLAT
    // horizontal annulus in the world XZ plane (the kekkai ward): `ring_segs` segments, radii
    // `ring_inner..ring_outer`, at Y = `ring_height`. `additive` (B2+0x08 bit 0x80) selects the
    // additive glow blend vs alpha.
    bool ring = false;
    int ring_segs = 0;
    float ring_outer = 0, ring_inner = 0, ring_height = 0;
    bool additive = false;
    // B2+0x00 bit 0x100: the quad is CAMERA-FACING. Clear = a world-fixed quad, which is
    // how MAP_KEKKAI's two BARIARING01 emitters (7 and 8 — the ward's magenta ring) are
    // authored, against bit 0x100 being set on its camera-facing glow emitters.
    bool face_cam = false;
    // B2+0x44 — the emitter's ORIENTATION mode, and the field that actually separates a
    // world-oriented quad from a camera-facing one (B2+0x00 bit 0x100 does not):
    //   0 = camera-facing billboard   1 = world-oriented (lies in the world plane)   2 = ?
    // MAP_KEKKAI: 1 on exactly its cylinder + its two BARIARING01 rings, 0 on every upright
    // glow. SAVE_MEGAMIZOU01: 0 on all its glows (one 2). PC2_DEF_ATTACK: 1 on its bolts.
    std::uint32_t orient = 0;
    // True when this emitter's quad lies FLAT in the world XZ plane rather than facing the
    // camera. There is no per-emitter flag for it: MAP_KEKKAI's flat ground pool (em3) and
    // SAVE_MEGAMIZOU01's upright glow (em1) have byte-identical B1/B2 apart from texture,
    // UVs, scale and height. What separates them is that a flat quad needs a SECOND in-plane
    // extent, so its author sets sz; an upright XY quad only uses sx/sy and leaves sz at the
    // default 1.0. Checked against both retail frames:
    //   MAP_KEKKAI       sz != 1 on em1,2,3,4,6,7,8 -> ground pool + both rings lie flat;
    //                    em5 (sz 1) stays upright as the column.
    //   SAVE_MEGAMIZOU01 sz == 1 on em0,1,2,3 -> wings, envelope and column upright;
    //                    em4 (sz 3.5, height -0.2) lies flat as the floor pool.
    // A heuristic on authored data, NOT a decoded flag. [H-med]
    bool flat = false;
    // B1+0x48 local offset triple. Its THIRD component is a world-height offset: it is the
    // only field separating MAP_KEKKAI's two otherwise byte-identical ring emitters (0.65
    // vs 0.40 — the ward's two concentric bands, with its cylinder at 0.50), and in
    // SAVE_MEGAMIZOU01 it puts the statue's wing glow at 1.5 and its floor pool at -0.2.
    // Whether the triple is (x,y,z) in a rotated local space was NOT traced — only that
    // off[2] behaves as height, across three independent effects. [H-high]
    float off[3] = {0, 0, 0};
    bool textured() const { return !frames.empty(); }
    // The raw records (0xf0 / 0x924 bytes) as the engine's loader leaves them in memory: the
    // tracks with constant values collapsed to one key (EF2SIM.md §3.5.2).
    std::vector<std::uint8_t> b1, b2;
};

// An A-array entry: a root emitter spawned at effect creation, and its start delay.
struct Ef2Root { int emitter = 0; std::uint32_t delay = 0; };

class Ef2 {
  public:
    static Ef2 load(const std::filesystem::path& file);
    bool ok() const { return ok_; }
    const std::vector<Ef2Emitter>& emitters() const { return emitters_; }
    const std::vector<Ef2Root>& roots() const { return roots_; }
    // The emitter a simple consumer should draw: the first textured one, since
    // untextured emitters carry no quad at all (frame_count 0).
    const Ef2Emitter* primary() const;
    // Longest emitter lifetime — the engine keeps a projectile alive exactly as long
    // as its effect lives, so this is a bullet's lifetime too.
    std::uint32_t life() const;

  private:
    bool ok_ = false;
    std::vector<Ef2Emitter> emitters_;
    std::vector<Ef2Root> roots_;
};

}  // namespace cleria
