#include "cleria/audio.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace cleria {
namespace fs = std::filesystem;

namespace {
constexpr int kMaxVoices = 24;  // well past what footsteps need; cheap insurance

SDL_AudioStream* as(void* p) { return static_cast<SDL_AudioStream*>(p); }
SDL_Mutex* mx(void* p) { return static_cast<SDL_Mutex*>(p); }

// SDL's audio thread pulls through here.
void SDLCALL feed_cb(void* userdata, SDL_AudioStream* stream, int additional, int) {
    if (additional <= 0) return;
    Audio* a = static_cast<Audio*>(userdata);
    const int frames = additional / int(sizeof(std::int16_t)) / a->channels();
    if (frames <= 0) return;
    static thread_local std::vector<std::int16_t> buf;
    buf.resize(std::size_t(frames) * a->channels());
    a->mix(buf.data(), frames);
    SDL_PutAudioStreamData(stream, buf.data(), int(buf.size() * sizeof(std::int16_t)));
}
}  // namespace

Audio::~Audio() {
    if (stream_) SDL_DestroyAudioStream(as(stream_));
    if (lock_) SDL_DestroyMutex(mx(lock_));
}

bool Audio::init(const fs::path& asset_root) {
    root_ = asset_root;
    defs_ = EffectDefs::load(asset_root);
    if (defs_.size() == 0) {
        std::fprintf(stderr, "audio: no EFFECTDEF.TXT under %s — silent\n",
                     asset_root.string().c_str());
        return false;
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "audio: SDL_InitSubSystem: %s\n", SDL_GetError());
        return false;
    }
    // The game's effect WAVs are 44.1 kHz mono 16-bit; ask for that and let SDL
    // convert to whatever the device wants.
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = channels_;
    spec.freq = rate_;
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed_cb, this);
    if (!stream_) {
        std::fprintf(stderr, "audio: SDL_OpenAudioDeviceStream: %s\n", SDL_GetError());
        return false;
    }
    lock_ = SDL_CreateMutex();
    voices_.reserve(kMaxVoices);
    SDL_ResumeAudioStreamDevice(as(stream_));
    std::printf("audio: %zu effect defs, device open @ %d Hz\n", defs_.size(), rate_);
    return true;
}

const Audio::Clip* Audio::clip(int id) {
    auto it = clips_.find(id);
    if (it != clips_.end()) return it->second.get();

    // Cache the miss too (as null) so a missing WAV isn't retried every step.
    clips_.emplace(id, nullptr);
    const fs::path p = defs_.wav_path(root_, id);
    if (p.empty() || !fs::exists(p)) return nullptr;

    SDL_AudioSpec ws{};
    Uint8* buf = nullptr;
    Uint32 len = 0;
    if (!SDL_LoadWAV(p.string().c_str(), &ws, &buf, &len)) {
        std::fprintf(stderr, "audio: SDL_LoadWAV(%s): %s\n", p.string().c_str(), SDL_GetError());
        return nullptr;
    }
    // Convert whatever the file is to the mixer's format up front, so the audio
    // thread only ever adds int16s.
    Uint8* conv = nullptr;
    int conv_len = 0;
    SDL_AudioSpec want{};
    want.format = SDL_AUDIO_S16LE;
    want.channels = channels_;
    want.freq = rate_;
    bool ok = true;
    if (ws.format != want.format || ws.channels != want.channels || ws.freq != want.freq) {
        ok = SDL_ConvertAudioSamples(&ws, buf, int(len), &want, &conv, &conv_len);
        if (!ok)
            std::fprintf(stderr, "audio: convert(%s): %s\n", p.string().c_str(), SDL_GetError());
    }
    auto c = std::make_unique<Clip>();
    c->channels = channels_;
    const Uint8* src = conv ? conv : buf;
    const int src_len = conv ? conv_len : int(len);
    if (ok && src && src_len > 0) {
        c->samples.resize(std::size_t(src_len) / sizeof(std::int16_t));
        std::memcpy(c->samples.data(), src, c->samples.size() * sizeof(std::int16_t));
    }
    if (conv) SDL_free(conv);
    SDL_free(buf);

    Clip* raw = c.get();
    clips_[id] = std::move(c);
    return raw->samples.empty() ? nullptr : raw;
}

void Audio::play(int id, float volume) {
    if (!stream_ || id < 0) return;
    const Clip* c = clip(id);
    if (!c) return;
    SDL_LockMutex(mx(lock_));
    if (voices_.size() < kMaxVoices) voices_.push_back({c, 0, volume});
    SDL_UnlockMutex(mx(lock_));
}

void Audio::mix(std::int16_t* out, int frames) {
    const int n = frames * channels_;
    std::memset(out, 0, std::size_t(n) * sizeof(std::int16_t));
    SDL_LockMutex(mx(lock_));
    for (Voice& v : voices_) {
        if (!v.clip) continue;
        const std::size_t avail = v.clip->samples.size() - v.pos;
        const std::size_t take = std::min<std::size_t>(avail, std::size_t(n));
        const float g = v.volume * master_;
        for (std::size_t k = 0; k < take; ++k) {
            const int mixed = out[k] + int(v.clip->samples[v.pos + k] * g);
            out[k] = std::int16_t(std::clamp(mixed, -32768, 32767));
        }
        v.pos += take;
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [](const Voice& v) {
                                     return !v.clip || v.pos >= v.clip->samples.size();
                                 }),
                  voices_.end());
    SDL_UnlockMutex(mx(lock_));
}


}  // namespace cleria
