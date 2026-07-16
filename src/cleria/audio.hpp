// A small sound-effect mixer over SDL3's audio device stream.
//
// This is deliberately NOT a reimplementation of the game's DirectSound8 layer:
// it plays a WAV by EFFECTDEF id at a volume, which is all the footstep path
// needs. 3D panning, the `dup` voice cap and the emitter/listener lerp the exe
// does in FUN_0045d7a0 are not modelled yet.
#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "cleria/effectdef.hpp"

namespace cleria {

class Audio {
  public:
    Audio() = default;
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    // Opens the default playback device and parses EFFECTDEF. Returns false if
    // either fails; the engine keeps running silently in that case.
    bool init(const std::filesystem::path& asset_root);
    bool ready() const { return stream_ != nullptr; }

    // Play the sound EFFECTDEF calls `id`, at `volume` (0..1). Unknown or
    // missing ids are ignored. Safe to call every frame.
    void play(int id, float volume = 1.0f);

    const EffectDefs& defs() const { return defs_; }

    // Called from SDL's audio thread. Public only so the C callback can reach it;
    // not part of the intended interface.
    void mix(std::int16_t* out, int frames);
    int channels() const { return channels_; }

  private:
    struct Clip {
        std::vector<std::int16_t> samples;  // interleaved, already at device format
        int channels = 1;
    };
    const Clip* clip(int id);

    struct Voice {
        const Clip* clip = nullptr;
        std::size_t pos = 0;
        float volume = 1.0f;
    };

    // SDL types are kept out of this header so cleria-dump and friends need not
    // see SDL3; the .cpp casts them back.
    void* stream_ = nullptr;  // SDL_AudioStream*
    void* lock_ = nullptr;    // SDL_Mutex* — voices_ is touched from the audio thread
    EffectDefs defs_;
    std::filesystem::path root_;
    std::unordered_map<int, std::unique_ptr<Clip>> clips_;  // id -> decoded, cached
    std::vector<Voice> voices_;
    int rate_ = 44100, channels_ = 1;
};

}  // namespace cleria
