// music/bgmdef.txt — BGM id -> track name, and locating the music folder itself.
//
// The .ogg tracks are loose files in the game install (`release\music\`) and are
// NOT part of the extracted `assets/` tree, so the folder has to be found rather
// than derived from asset_root alone — same spirit as CLERIA_FONT_FILE.
//
// The table is a two-column text file ("<id>\t<name>", ';' comments the rest of
// the line). The exe picks `bgmdef2.txt` for Japanese (its id 19 is YSO_019a) and
// `bgmdef.txt` otherwise; the two files differ at exactly that one line.
// See `build/audio_re/BGM.md` §1.
#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>

namespace cleria {

class BgmDefs {
  public:
    // Locates the music folder and parses its bgmdef. Search order:
    //   $CLERIA_MUSIC_DIR, <asset_root>/music, <asset_root>/../music,
    //   <asset_root>/../release/music
    bool load(const std::filesystem::path& asset_root, bool japanese = false);

    bool ready() const { return !names_.empty(); }
    const std::filesystem::path& dir() const { return dir_; }

    // "YSO_004" for id 4, or "" if the id isn't in the table (id 0 = stop).
    std::string name(int id) const;
    // Full path to the .ogg, or empty if the id is unknown or the file is absent.
    std::filesystem::path track(int id) const;

  private:
    std::filesystem::path dir_;
    std::unordered_map<int, std::string> names_;
};

}  // namespace cleria
