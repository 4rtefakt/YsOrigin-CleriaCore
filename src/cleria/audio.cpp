#include "cleria/audio.hpp"
#include "cleria/vfs.hpp"   // mods: asset opens go through vfs::resolve

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// The decoder is compiled once as its own translation unit; here we only want
// its prototypes.
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

namespace cleria {
namespace fs = std::filesystem;

namespace {
// The engine has no total: each record owns `dup` DirectSound buffers (FUN_005d7f50). This bound
// only keeps a runaway caller from growing the list without end.
constexpr int kMaxVoices = 128;

SDL_AudioStream* as(void* p) { return static_cast<SDL_AudioStream*>(p); }
SDL_Mutex* mx(void* p) { return static_cast<SDL_Mutex*>(p); }
stb_vorbis* vb(void* p) { return static_cast<stb_vorbis*>(p); }

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
    // Tear the device down first: after this the audio thread is gone, so the
    // vorbis handle can be closed without racing mix(). After SDL_Quit (cleria-view quits SDL
    // before its Audio goes out of scope) SDL has already destroyed the stream: destroying it
    // again freed it twice and corrupted the heap on every exit with sound on.
    if (stream_ && SDL_WasInit(SDL_INIT_AUDIO)) SDL_DestroyAudioStream(as(stream_));
    if (bgm_) stb_vorbis_close(vb(bgm_));
    if (lock_) SDL_DestroyMutex(mx(lock_));
}

bool Audio::init(const fs::path& asset_root, bool device) {
    root_ = asset_root;
    defs_ = EffectDefs::load(asset_root);
    if (defs_.size() == 0) {
        std::fprintf(stderr, "audio: no EFFECTDEF.TXT under %s — silent\n",
                     asset_root.string().c_str());
        return false;
    }
    if (!device) {
        lock_ = SDL_CreateMutex();
        voices_.reserve(kMaxVoices);
        return true;
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "audio: SDL_InitSubSystem: %s\n", SDL_GetError());
        return false;
    }
    // 44.1 kHz stereo: the effect WAVs are 44.1 kHz *mono* and get upmixed when
    // decoded, but every music track is stereo, and downmixing those would be the
    // worse trade. SDL converts on to whatever the device actually wants.
    lock_ = SDL_CreateMutex();
    voices_.reserve(kMaxVoices);
    if (!open_stream(0)) {
        std::fprintf(stderr, "audio: SDL_OpenAudioDeviceStream: %s\n", SDL_GetError());
        SDL_DestroyMutex(mx(lock_));
        lock_ = nullptr;
        return false;
    }
    std::printf("audio: %zu effect defs, device open @ %d Hz\n", defs_.size(), rate_);
    return true;
}

std::vector<std::string> Audio::devices() {
    std::vector<std::string> out;
    int n = 0;
    if (SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&n)) {
        for (int i = 0; i < n; ++i) {
            const char* nm = SDL_GetAudioDeviceName(ids[i]);
            out.push_back(nm ? nm : "Device");
        }
        SDL_free(ids);
    }
    return out;
}

bool Audio::open_stream(int index) {
    SDL_AudioDeviceID id = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    if (index > 0) {
        int n = 0;
        SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&n);
        if (ids && index <= n) id = ids[index - 1];
        else index = 0;                                  // not there (unplugged): the default
        SDL_free(ids);
    }
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = channels_;
    spec.freq = rate_;
    stream_ = SDL_OpenAudioDeviceStream(id, &spec, feed_cb, this);
    if (!stream_) return false;
    device_ = index;
    SDL_ResumeAudioStreamDevice(as(stream_));
    return true;
}

bool Audio::set_device(int index) {
    if (!stream_) return false;                          // no device mode (CLERIA_MUTE), or init failed
    if (index < 0 || index > int(devices().size())) index = 0;   // a saved device that is gone: the default
    if (index == device_) return true;
    SDL_DestroyAudioStream(as(stream_));                 // the audio thread is gone after this
    stream_ = nullptr;
    if (open_stream(index)) return device_ == index;
    std::fprintf(stderr, "audio: device %d: %s\n", index, SDL_GetError());
    open_stream(0);
    return false;
}

const Audio::Clip* Audio::clip(int id) {
    auto it = clips_.find(id);
    if (it != clips_.end()) return it->second.get();

    // Cache the miss too (as null) so a missing WAV isn't retried every step.
    clips_.emplace(id, nullptr);
    const fs::path p = id >= kFileBase ? (std::size_t(id - kFileBase) < files_.size() ? files_[std::size_t(id - kFileBase)] : fs::path())
                                       : vfs::resolve(defs_.wav_path(root_, id));
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

void Audio::start(int id, const EffectDef* def, const Clip* c, float volume, float level, float gl,
                  float gr) {
    Voice nv;
    nv.clip = c; nv.volume = volume; nv.gl = gl; nv.gr = gr; nv.id = id;
    nv.loop = def && def->loop;
    nv.level = nv.target = level;
    const int dup = def ? def->dup : 0;
    int same = 0;
    Voice* pending = nullptr;   // requested since the mixer last ran: not yet "playing" to GetStatus
    Voice* oldest = nullptr;
    for (Voice& v : voices_) {
        if (v.id != id || !v.clip) continue;
        ++same;
        if (v.age == 0 && !pending) pending = &v;
        if (!oldest || v.age > oldest->age) oldest = &v;
    }
    // dup 1, playing and mid-ramp: the ramp is retargeted to the new volume (reached on the next
    // 16 ms tick) and the stop is cancelled; the buffer is not restarted [EXPORT 005d87a0.c:28-37].
    if (dup == 1 && same == 1 && oldest->age > 0 && oldest->level != oldest->target) {
        oldest->stop = false;
        oldest->target = level;
        oldest->step = (level - oldest->level) / (0.016f * float(rate_));
        return;
    }
    if (pending) { *pending = nv; return; }          // the first non-playing buffer is the same one
    if (dup <= 0 || same < dup) {
        if (voices_.size() < kMaxVoices) voices_.push_back(nv);
        return;
    }
    // Every buffer playing: stop and reuse the one with the largest age, if that is >= `cancel` ms;
    // records with `nega` (rec+8 & 4) never do [EXPORT 005d87a0.c:55-75].
    if (def->nega || oldest->age * 1000 < std::uint64_t(def->cancel) * std::uint64_t(rate_)) return;
    *oldest = nv;
}

bool Audio::play_file(const fs::path& wav, float volume) {
    if (!lock_) return false;
    std::size_t i = 0;
    while (i < files_.size() && files_[i] != wav) ++i;
    if (i == files_.size()) {
        if (files_.size() >= 512) return false;             // [H] files a run may hold
        files_.push_back(wav);
    }
    const int id = kFileBase + int(i);
    const Clip* c = clip(id);
    if (!c) return false;
    if (trace_) std::printf("  [se] play file %s\n", wav.filename().string().c_str());
    SDL_LockMutex(mx(lock_));
    start(id, nullptr, c, volume, 1.0f, 1.0f, 1.0f);
    SDL_UnlockMutex(mx(lock_));
    return true;
}

void Audio::play(int id, float volume, float level) {
    if (!lock_ || id < 0) return;
    if (trace_) std::printf("  [se] play %d level %.3f\n", id, level);
    const Clip* c = clip(id);
    if (!c) return;
    SDL_LockMutex(mx(lock_));
    start(id, defs_.find(id), c, volume, level, 1.0f, 1.0f);
    SDL_UnlockMutex(mx(lock_));
}

void Audio::play_at(int id, float pan, float dist, float volume) {
    if (!lock_ || id < 0) return;
    const EffectDef* def = defs_.find(id);
    // A record without the 3-D flag has no 3-D buffer (rec+0 = 0x80: no DSBCAPS_CTRL3D), and FUN_005d87a0
    // sets the position only on a buffer that has one (+4 != 0) [EXPORT 005d87a0.c:79-80]: FUN_0043a080
    // on such a record is a plain 2-D play, whatever the position.
    const bool flat = def && !def->is3d;
    const float att = def && !flat ? distance_gain(def->mindist, def->maxdist, dist) : 1.0f;
    if (att <= 0.0f) return;                                   // muted at maxdist
    const Clip* c = clip(id);
    if (!c) return;
    pan = std::clamp(pan, -1.0f, 1.0f);
    const float th = (pan + 1.0f) * 0.25f * 3.14159265f;       // equal-power
    SDL_LockMutex(mx(lock_));
    start(id, def, c, volume * att, 1.0f, flat ? 1.0f : std::cos(th), flat ? 1.0f : std::sin(th));   // command 5: volume 0x400
    SDL_UnlockMutex(mx(lock_));
}

// FUN_005d8970: target, step = (target - cur) * 16 / ms per 16 ms tick (ms 0 -> 1), i.e. a linear
// ramp; the stop flag +0x5d is written on the playing buffers. Call with lock_ held.
void Audio::ramp(int id, float level, int ms, bool stop) {
    const float frames = std::max(1.0f, float(ms) * float(rate_) / 1000.0f);
    for (Voice& v : voices_) {
        if (v.id != id || !v.clip) continue;
        v.target = level;
        v.step = (level - v.level) / frames;
        v.stop = stop;
    }
}

void Audio::stop_se(int id, int ms) {
    if (!lock_) return;
    if (trace_) std::printf("  [se] stop %d over %d ms\n", id, ms);
    SDL_LockMutex(mx(lock_));
    ramp(id, 0.0f, ms, true);
    SDL_UnlockMutex(mx(lock_));
}

void Audio::set_se_volume(int id, float level, int ms) {
    if (!lock_) return;
    if (trace_) std::printf("  [se] volume %d -> %.3f over %d ms\n", id, level, ms);
    SDL_LockMutex(mx(lock_));
    ramp(id, clamp01(level), ms, false);
    SDL_UnlockMutex(mx(lock_));
}

void Audio::stop_all(bool force) {
    if (!lock_) return;
    SDL_LockMutex(mx(lock_));
    for (std::size_t i = 0; i < voices_.size(); ++i) {
        const int id = voices_[i].id;
        const EffectDef* def = defs_.find(id);
        if (force || !def || !def->manual) ramp(id, 0.0f, 0x5dc, true);
    }
    SDL_UnlockMutex(mx(lock_));
}

int Audio::voices_of(int id) const {
    if (!lock_) return 0;
    SDL_LockMutex(mx(lock_));
    int n = 0;
    for (const Voice& v : voices_) n += (v.id == id && v.clip);
    SDL_UnlockMutex(mx(lock_));
    return n;
}

std::vector<int> Audio::looping() const {
    std::vector<int> ids;
    if (!lock_) return ids;
    SDL_LockMutex(mx(lock_));
    for (const Voice& v : voices_)
        if (v.clip && v.loop && std::find(ids.begin(), ids.end(), v.id) == ids.end()) ids.push_back(v.id);
    SDL_UnlockMutex(mx(lock_));
    std::sort(ids.begin(), ids.end());
    return ids;
}

void Audio::advance(int frames) {
    if (!lock_ || stream_ || frames <= 0) return;
    std::vector<std::int16_t> buf(std::size_t(frames) * std::size_t(channels_));
    mix(buf.data(), frames);
}

bool Audio::play_bgm(const fs::path& ogg) {
    if (!stream_) return false;
    if (ogg.empty()) {
        stop_bgm();
        return false;
    }
    const std::string name = ogg.filename().string();
    // already streaming this track — leave it alone (unless it is fading out: then restart it)
    if (name == bgm_name_ && bgm_ && !bgm_fade_ms_) return true;
    stop_bgm();

    int err = 0;
    stb_vorbis* v = stb_vorbis_open_filename(vfs::resolve(ogg).string().c_str(), &err, nullptr);
    if (!v) {
        std::fprintf(stderr, "audio: stb_vorbis_open(%s) failed (error %d)\n",
                     ogg.string().c_str(), err);
        return false;
    }
    const stb_vorbis_info info = stb_vorbis_get_info(v);
    if (int(info.sample_rate) != rate_)
        std::fprintf(stderr, "audio: %s is %u Hz but the device is %d Hz — it will play at the "
                             "wrong pitch (no resampler yet)\n",
                     name.c_str(), info.sample_rate, rate_);

    // LOOPSTART / LOOPLENGTH Vorbis comments, in samples. 29 of the 40 tracks
    // carry them; the rest loop whole (the track loop flag, audio.hpp).
    std::uint32_t ls = 0, ll = 0;
    const stb_vorbis_comment vc = stb_vorbis_get_comment(v);
    for (int i = 0; i < vc.comment_list_length; ++i) {
        const char* c = vc.comment_list[i];
        if (!c) continue;
        if (SDL_strncasecmp(c, "LOOPSTART=", 10) == 0)
            ls = std::uint32_t(std::strtoul(c + 10, nullptr, 10));
        else if (SDL_strncasecmp(c, "LOOPLENGTH=", 11) == 0)
            ll = std::uint32_t(std::strtoul(c + 11, nullptr, 10));
    }

    // Debug hook: start at an arbitrary sample so the loop wrap can be reached in
    // seconds instead of minutes.
    std::uint64_t start = 0;
    if (const char* sk = std::getenv("CLERIA_BGM_SEEK")) {
        start = std::strtoull(sk, nullptr, 10);
        stb_vorbis_seek(v, unsigned(start));
    }

    SDL_LockMutex(mx(lock_));
    bgm_ = v;
    bgm_name_ = name;
    bgm_pos_ = start;
    bgm_loop_start_ = ls;
    bgm_loop_end_ = ll ? ls + ll : 0;
    bgm_loops_ = 0;
    SDL_UnlockMutex(mx(lock_));
    const unsigned len = stb_vorbis_stream_length_in_samples(v);
    if (ll)
        std::printf("audio: BGM %s — loop [%u..%u] of %u samples (%.1fs)\n", name.c_str(), ls,
                    ls + ll, len, double(len) / double(info.sample_rate));
    else
        std::printf("audio: BGM %s — no loop tags, loops whole, %u samples (%.1fs)\n", name.c_str(), len,
                    double(len) / double(info.sample_rate));
    return true;
}

void Audio::stop_bgm() {
    if (!stream_) return;
    SDL_LockMutex(mx(lock_));
    stb_vorbis* v = vb(bgm_);
    bgm_ = nullptr;
    bgm_name_.clear();
    bgm_pos_ = 0;
    bgm_loop_start_ = bgm_loop_end_ = 0;
    bgm_fade_ms_ = bgm_fade_done_ = 0;
    bgm_fade_from_ = bgm_fade_cur_ = 1.0f;
    SDL_UnlockMutex(mx(lock_));
    // Closed outside the lock: the mixer can no longer reach it.
    if (v) stb_vorbis_close(v);
}

bool Audio::bgm_streaming() const {
    if (!stream_) return false;
    SDL_LockMutex(mx(lock_));
    const bool on = bgm_ != nullptr;
    SDL_UnlockMutex(mx(lock_));
    return on;
}

void Audio::fade_out_bgm(int ms) {
    if (!stream_) return;
    if (ms <= 0) {                     // command 3 with no duration sets 0 at once; the stop follows
        stop_bgm();
        return;
    }
    SDL_LockMutex(mx(lock_));
    if (bgm_) {
        bgm_fade_from_ = bgm_fade_cur_;   // FUN_005cd455: from = the current volume (+0x44)
        bgm_fade_ms_ = std::uint64_t(ms);
        bgm_fade_done_ = 0;
    }
    SDL_UnlockMutex(mx(lock_));
}

void Audio::mix(std::int16_t* out, int frames) {
    const int n = frames * channels_;
    std::memset(out, 0, std::size_t(n) * sizeof(std::int16_t));
    SDL_LockMutex(mx(lock_));

    // Music bed first; effects add on top of it below.
    if (bgm_) {
        static thread_local std::vector<std::int16_t> bb;
        bb.resize(std::size_t(n));
        int filled = 0;
        // Bounded: one loop wrap per callback is all that can really happen, but
        // never spin here if a seek fails — silence beats hanging the device.
        for (int pass = 0; pass < 4 && filled < frames; ++pass) {
            int want = frames - filled;
            // Never decode past the loop end: what follows it is the track's
            // fade-out tail, which must not be heard on the way round.
            if (bgm_loop_end_)
                want = int(std::min<std::uint64_t>(std::uint64_t(want),
                                                   bgm_loop_end_ > bgm_pos_
                                                       ? bgm_loop_end_ - bgm_pos_
                                                       : 0));
            int got = 0;
            if (want > 0)
                got = stb_vorbis_get_samples_short_interleaved(
                    vb(bgm_), channels_, bb.data() + std::size_t(filled) * channels_,
                    want * channels_);
            if (got > 0) {
                filled += got;
                bgm_pos_ += std::uint64_t(got);
            }
            if (got >= want && want > 0) continue;  // buffer satisfied
            if (!bgm_loop_end_ && got <= 0 && bgm_pos_ == bgm_loop_start_) break;   // an empty track
            stb_vorbis_seek(vb(bgm_), bgm_loop_start_);     // the loop end, or an untagged track's end
            bgm_pos_ = bgm_loop_start_;
            ++bgm_loops_;
        }
        const int got_n = filled * channels_;
        float g = bgm_volume_;
        if (bgm_fade_ms_) {
            // The ramp at this block's start: from + (0 - from) * elapsed / duration.
            const std::uint64_t total = bgm_fade_ms_ * std::uint64_t(rate_) / 1000;
            const float t = total ? std::min(1.0f, float(bgm_fade_done_) / float(total)) : 1.0f;
            bgm_fade_cur_ = bgm_fade_from_ * (1.0f - t);
            g = log_gain(bgm_slider_ * bgm_fade_cur_);
            bgm_fade_done_ += std::uint64_t(frames);
        }
        if (!bgm_on_) g = 0.0f;                             // PlayBgm off
        for (int k = 0; k < got_n; ++k) out[k] = std::int16_t(bb[k] * g);
        // The delayed command 2: the stop, once the fade's time has run out. Only the decoder is
        // dropped here; bgm_name_ belongs to the game thread (play_bgm restarts a faded track).
        if (bgm_fade_ms_ && bgm_fade_done_ >= bgm_fade_ms_ * std::uint64_t(rate_) / 1000) {
            stb_vorbis_close(vb(bgm_));
            bgm_ = nullptr;
        }
    }

    for (Voice& v : voices_) {
        if (!v.clip) continue;
        const std::size_t len = v.clip->samples.size();
        float g = v.volume * log_gain(master_ * v.level);
        for (int k = 0; k < n; ++k) {
            if (v.pos >= len) {                               // the loop flag: round again
                if (!v.loop) break;
                v.pos = 0;
            }
            if (v.level != v.target && (k & 1) == 0) {        // FUN_005d8ab0: one step per stereo frame
                const bool up = v.target > v.level;
                v.level += v.step;
                if (v.step == 0.0f || (up ? v.level >= v.target : v.level <= v.target)) v.level = v.target;
                g = v.volume * log_gain(master_ * v.level);
            }
            // interleaved stereo: even = L, odd = R (pan gains are 1/1 for 2D plays)
            const float pg = !sfx_on_ ? 0.0f : (k & 1) ? v.gr : v.gl;   // PlayEffect off: mixed silent
            const int mixed = out[k] + int(v.clip->samples[v.pos] * g * pg);
            out[k] = std::int16_t(std::clamp(mixed, -32768, 32767));
            ++v.pos;
        }
        v.age += std::uint64_t(frames);
        if (v.stop && v.level == v.target) v.clip = nullptr;       // ramp done: Stop(), dropped below
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [](const Voice& v) {
                                     return !v.clip || (!v.loop && v.pos >= v.clip->samples.size());
                                 }),
                  voices_.end());
    SDL_UnlockMutex(mx(lock_));
}


}  // namespace cleria
