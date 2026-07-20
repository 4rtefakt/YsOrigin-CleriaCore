// MAP/SCENELIST.SL — the per-scene table: room path, display name, BGM id.
//
// The exe loads the zlib sidecar `data\map\scenelist.sl.z` in FUN_00435680
// (g_scn_data.cpp) into a flat array indexed by scene ordinal, base
// DAT_0074c124 / count DAT_0074e230, stride 0x1c8. Only the fields CleriaCore
// consumes are modelled here; the full record is documented in
// `build/audio_re/BGM.md` §2.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cleria {

struct SceneRec {
    std::uint16_t index = 0;   // +0x00  scene ordinal (== record position)
    std::uint8_t reverb = 0;   // +0x02  room acoustic preset — no consumer yet
    std::uint8_t bgm = 0;      // +0x03  bgmdef id. 0 means STOP, *not* "inherit".
    std::string path;          // +0x04  e.g. "s_10\s_1001\"
    std::string display;       // +0xc4  e.g. "2F Path 1"
};

class SceneList {
  public:
    // Reads <asset_root>/MAP/SCENELIST.SL. False if missing or malformed.
    bool load(const std::filesystem::path& asset_root);

    // Look up by room folder name ("S_1001"), case-insensitively: the extract
    // upper-cases directory names while the table stores them lower-case.
    const SceneRec* find_room(std::string_view room) const;

    std::size_t size() const { return recs_.size(); }
    const std::vector<SceneRec>& recs() const { return recs_; }

  private:
    std::vector<SceneRec> recs_;
};

}  // namespace cleria
