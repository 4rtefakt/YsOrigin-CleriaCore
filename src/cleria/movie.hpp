// The demo movies (the logo, the prologue, the opening, the tower inserts, the endings).
//
// build/event_re/MOVIES.md is the spec. In short [BYTES]: a `demoN\` scene builds a movie
// object whose Play (FUN_005773e0) opens one AVI and whose tick (FUN_00577750) ends it at the
// end of the stream or on a skip press; the end fades the screen to black and warps to a fixed
// next scene. The engine renders ONLY the AVI's video stream (IAMMultiMediaStream with a single
// MSPID_PrimaryVideo stream, OpenFile flags 0): the soundtrack is the demo scene's SCENELIST
// BGM (demo1 = 19, demo2 = 32, demo3 = 37, demo6 = 20), whose .ogg is the same length as the
// video. The PCM track inside the AVIs is never played.
//
// This header holds the engine's tables and laws as pure functions (tested by
// cleria-movietest), plus a small video decoder. The decoder is Windows Media Foundation
// (built into Windows: the AVI source + the MPEG-4 Part 2 decoder for the XviD stream, NV12 out,
// converted to BGRA here). Linux / macOS: FFmpeg (libavformat's AVI demuxer + libavcodec's MPEG-4
// Part 2 decoder, CMake CLERIA_FFMPEG); without it a stub (available() false) and the view ends each
// movie at once, like retail's open failure.
#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace cleria {

// ---- the engine's tables -----------------------------------------------------------------

inline constexpr int kMovieCount = 8;

// The file FUN_005773e0 opens for movie `idx` (0..7): `release\<name>`, from the pointer table
// 0x6a0650, which is used for EVERY language (the per-language tables 0x6a05b0..0x6a0630 only
// feed a read-ahead CreateFileW) [BYTES 0x5776a6..0x5776b3]. nullptr when out of range.
const char* movie_file(int idx);

// The file to look for, in order. Movie 0 is `en_pro.avi` in the exe, but the installed game
// ships `yso_pro.avi` and no `en_pro.avi`, so `yso_pro.avi` is tried second [H]: its 13080
// frames at 60 fps (218.0 s) match BGM 19 (YSO_019.ogg, 218.05 s), the demo1 soundtrack.
std::vector<const char*> movie_file_candidates(int idx);

// Where the movies are looked for: $CLERIA_MOVIE_DIR, <install>/release when asset_root is a
// data folder whose manifest names the install (src/cleria/gamedata), <asset_root>/movie (where
// the release\ AVIs were copied, like assets/music), then <asset_root>/../release. Empty if not found.
std::filesystem::path find_movie(const std::filesystem::path& asset_root, int idx);

// "demo3\" / "DEMO3" / "demo3" -> 3; anything else -> 0.
int demo_number(std::string_view scene);

// FUN_00431840's demo branch [EXPORT 00431840.c, strings BYTES]: demo N plays movie `movie`
// and then warps to `next` (a SCENELIST dir). `toal` = flag 150 == 3 (DAT_0076bb74 == 3);
// only demo1 and demo2 have a Toal arm.
struct DemoScene {
    int movie = -1;
    const char* next = nullptr;
};
std::optional<DemoScene> demo_scene(int n, bool toal);

// The skip gate of FUN_00577750 [EXPORT]. The movie can be skipped by a press of the input
// mask 0x1001 (DAT_00764750, the held mask) once those buttons have been seen released
// (obj+0x27a). demo6 (the logo) is always skippable; demo1..5 only once DAT_0074ab9b
// ("GameFirstPlay" in the [game] section of the system ini, set by FUN_0058f210) is set;
// demo7 also needs one of the clear bytes 0x74ab7b / 0x74ab7a; demo8 needs 0x74ab79.
struct MovieSeen {
    bool first_play = false;   // 0x74ab9b
    bool clear_79 = false, clear_7a = false, clear_7b = false;
};
bool movie_skippable(int demo, const MovieSeen& seen);

struct MovieSkip {
    bool armed = false;   // obj+0x27a
    // One tick. `held` = any of the skip buttons is held. True = finish the movie now.
    bool step(bool held, bool skippable) {
        if (!held) { armed = true; return false; }
        return armed && skippable;
    }
};

// The screen quad FUN_005779e0 draws the frame into [BYTES 0x577a50..0x577b84], for a
// `w` x `h` back buffer, in pixels (pre-transformed vertices). The video always spans the
// full width; its height is w/2 (the AVIs are 2:1) centred, unless the screen is at least
// 2:1, where the engine uses top 0 and a literal bottom of 320.0 (`0x6bd778`) — a retail quirk
// kept as is. The texture is a power-of-two surface sampled over [0, vw/2^k] x [0, vh/2^m];
// the view uploads the frame at its own size, which samples the same texels.
struct MovieQuad {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
MovieQuad movie_quad(std::uint32_t w, std::uint32_t h);

// The video's length from the AVI header (avih: microseconds per frame x total frames), for
// the stand-in when the decoder fails. nullopt if the file is not a readable AVI.
std::optional<double> avi_duration(const std::filesystem::path& file);

// The logo, the inserts and the endings tag their video chunks '00xx' (not '00dc'; they are
// Adobe exports) [DATA: movi + idx1 of yso_logo.avi / yso_ins01..03.dat / yso_ed01..02.dat].
// Retail's DirectShow AVI splitter takes any two-letter suffix; Media Foundation's AVI source
// rejects the file (0x8007007b). This renames them in place to '00dc' in the LIST movi chunks and
// the idx1 entries of an AVI image, and returns how many it renamed.
std::size_t avi_patch_chunk_ids(std::uint8_t* data, std::size_t n);

// NV12 -> B, G, R, 255 with the BT.601 video-range matrix. Retail decodes through its bundled
// xvid.ax (FUN_0044a4f0 adds the "XviD" filter by name), whose YV12 -> RGB converters are
// BT.601 [H-high: the codec's documented behaviour, not traced in the exe].
void nv12_to_bgra(const std::uint8_t* y_plane, const std::uint8_t* uv_plane, std::size_t pitch,
                  std::uint32_t w, std::uint32_t h, std::uint8_t* out);

// ---- the decoder --------------------------------------------------------------------------

class MovieDecoder {
  public:
    MovieDecoder();
    ~MovieDecoder();
    MovieDecoder(const MovieDecoder&) = delete;
    MovieDecoder& operator=(const MovieDecoder&) = delete;

    // False when this build has no decoder (off Windows without CLERIA_FFMPEG): the view then ends
    // every movie at once, as retail does when FUN_005773e0's open fails.
    static bool available();

    // False if the platform has no decoder, the file is missing or Media Foundation cannot
    // decode it. The audio streams are deselected (see the header comment).
    bool open(const std::filesystem::path& file);
    void close();
    bool is_open() const;

    std::uint32_t width() const { return w_; }
    std::uint32_t height() const { return h_; }
    double duration() const { return duration_; }   // seconds, 0 if unknown

    // Decodes up to the frame shown at `t` seconds from the start (frames are dropped, never
    // waited for). True when the frame in bgra() changed.
    bool advance(double t);
    // True once the last frame has been shown and the stream reported its end.
    bool ended() const { return ended_; }

    // The current frame: width() x height() pixels, B, G, R, A=255, top row first.
    const std::vector<std::uint8_t>& bgra() const { return frame_; }

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::uint32_t w_ = 0, h_ = 0;
    double duration_ = 0;
    bool ended_ = false;
    std::vector<std::uint8_t> frame_;
};

}  // namespace cleria
