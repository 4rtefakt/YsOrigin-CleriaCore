// EFFECTDEF.TXT — the sound-effect table: id -> WAV name + playback parameters.
//
// A faithful port of the game's own parser (FUN_00439ac0, main/game/g_sound.cpp).
// It is a hand-rolled strtok tokenizer, and its quirks are load-bearing:
//
//   * delimiters are " \t\r:/.,-"  — note '\n' is NOT one, so a token can begin
//     with (or contain) a newline, and that is how records are separated
//   * '.' and '-' being delimiters is why mindist/maxdist can never be fractional
//     or negative: atof() only ever sees integer text
//   * a token starting with ';' comments out the rest of the LINE
//
// Everything after the ';' on a data line (the "s/m", the status kanji, the
// WAV_* symbolic name, the Japanese description) is build-time documentation the
// game never reads.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace cleria {

struct EffectDef {
    int id = 0;
    std::string name;      // e.g. "foot\\f_stone0" — relative to WAVE/EFFECT, no extension
    bool is3d = false;
    bool loop = false;
    int dup = 0;           // max simultaneous voices
    int cancel = 0;
    float mindist = 0, maxdist = 0;
};

class EffectDefs {
  public:
    // Parse the file's bytes. The file is Shift-JIS, but every field the game
    // reads is ASCII, so the bytes are consumed as-is (the multi-byte comment
    // text is skipped, never decoded).
    static EffectDefs parse(const std::string& text);
    static EffectDefs load(const std::filesystem::path& asset_root);

    const EffectDef* find(int id) const;
    std::size_t size() const { return by_id_.size(); }
    const std::unordered_map<int, EffectDef>& all() const { return by_id_; }

    // Absolute path of a def's WAV under `asset_root`, or "" if unknown.
    std::filesystem::path wav_path(const std::filesystem::path& asset_root, int id) const;

  private:
    std::unordered_map<int, EffectDef> by_id_;
};

}  // namespace cleria
