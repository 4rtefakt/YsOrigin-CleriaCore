// AIA — Ys Origin animated sprite container (gfx/ani_dat.cpp, version 0x0150).
//
// A palette-indexed, run-length-encoded sprite sheet: the 2D characters and
// enemies of the game's 2.5D world (3D rooms, sprite actors). Each file holds a
// set of animations; each animation is nDir facings x nFrame frames; each frame
// is a tight RGBA bbox decoded on demand from the RLE pixel stream.
//
// Reverse-engineered from yso_win.exe: loader FUN_00444a50, 32-bit RLE decoder
// FUN_00445220. See docs/formats/AIA.md.
#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

namespace cleria {

// One decoded frame: a tight RGBA8 bitmap plus where it sits on the canvas.
struct AiaFrame {
    std::uint32_t w = 0, h = 0;         // tight bbox dimensions
    std::vector<std::uint8_t> rgba;     // w*h*4, RGBA8, straight alpha
    std::uint16_t x0 = 0, y0 = 0;       // bbox origin within the canvas
    float bill[4] = {0, 0, 0, 0};       // billboard placement (scale, ox, oy, oz)
};

struct AiaImage {  // one animation
    std::uint32_t tag = 0, first = 0, nDir = 0, nFrame = 0;
};

// Composite `layer` over `body` in CANVAS space, growing body's bbox to the union
// of the two. Both frames carry their own canvas origin (x0,y0) — that is what
// aligns them; their tight bboxes generally differ. Straight alpha, layer on top.
void composite_over(AiaFrame& body, const AiaFrame& layer);

// A loaded AIA file. Keeps the raw bytes and decodes frames on demand.
struct Aia {
    std::uint16_t version = 0;
    std::uint32_t nPal = 0, nImg = 0, nReg = 0;
    std::uint16_t canvasW = 0, canvasH = 0;
    std::vector<AiaImage> images;

    bool load(const std::filesystem::path& path);
    bool ok() const { return version == 0x150 && !data.empty(); }

    // Decode a body region (0..nReg) to RGBA8. Returns false on a bad index.
    bool decode_region(std::uint32_t region, AiaFrame& out) const;
    // Resolve image i's (dir,frame) to its body region and decode it.
    bool decode_frame(std::uint32_t image, std::uint32_t dir,
                      std::uint32_t frame, AiaFrame& out) const;
    std::uint32_t region_count() const { return nReg; }

    // Frames are region PAIRS: an even body and an odd second-layer slot. The layer
    // is real only when BIT 0 OF THE BODY'S FLAGS is set — despite appearances it is
    // not bit 7; that 0x80 belongs to the empty placeholder (a 0x0 bbox at the canvas
    // corner). The layer is the same artwork in the NEXT palette.
    //
    // *** HOW THE ENGINE USES THE LAYER IS [UNKNOWN]. DO NOT ASSUME IT IS AN OVERLAY. ***
    // An earlier revision of this comment called it "an aura — C_115 draws Toal's
    // demon body in palette 0 and its aura in palette 1". That is REFUTED: dump
    // C_115 region 0 and 1 and look. Both have the IDENTICAL bbox (73x99 @102,13)
    // and near-identical coverage (3408 vs 3406 non-transparent pixels) — the layer
    // is not a sparse aura, it is the WHOLE SILHOUETTE flat-shaded in purple.
    // Compositing it over the body with straight alpha therefore erases the body
    // completely, which is obviously not what the game shows.
    //
    // The likelier reading — untested — is a STATE variant rather than an overlay:
    // a Boost/hit flash the engine swaps in. C_115 (Toal's Boost form) and the
    // enemies M_340/M_370/M_540 are 100% layered, while C_104's layers sit only on
    // two special animations (tags 408, 419) and none of his locomotion. Settle it
    // before drawing anything.
    //
    // `composite_over` and `decode_frame_composited` below implement the straight-
    // alpha overlay reading. They are kept because they make the refutation
    // reproducible, but the renderer deliberately does NOT use them.
    //
    // 5906 of 60844 slots are real, across 28 files. C_115 and M_340/M_370 use one on
    // every frame; C_102 uses none — so a renderer that ignores layers looks correct
    // until it loads a gameplay or Boost set.
    bool has_layer(std::uint32_t body_region) const;
    // Decode the second layer of `body_region` (i.e. body_region + 1).
    bool decode_layer(std::uint32_t body_region, AiaFrame& out) const;
    // The layer of image i's (dir,frame), if it has one.
    // Decode (dir,frame) and, when the body flags a second layer, composite it over
    // the body — the one call a renderer wants. Equivalent to decode_frame() for the
    // ~90% of frames with no layer.
    bool decode_frame_composited(std::uint32_t image, std::uint32_t dir,
                                 std::uint32_t frame, AiaFrame& out) const;

    bool decode_frame_layer(std::uint32_t image, std::uint32_t dir,
                            std::uint32_t frame, AiaFrame& out) const;

    static constexpr std::uint32_t kNoRegion = 0xFFFFFFFFu;
    // The body region for image i's (dir, frame). kNoRegion on a bad index.
    std::uint32_t frame_region(std::uint32_t image, std::uint32_t dir,
                               std::uint32_t frame) const;

  private:
    std::vector<std::uint8_t> data;
    std::size_t imgtab = 0, regtab = 0, palbase = 0, pxbase = 0;
    std::vector<std::uint32_t> sorted_offsets;  // distinct imageOfs, ascending

    std::uint32_t frame_len(std::uint32_t imageOfs) const;
};

}  // namespace cleria
