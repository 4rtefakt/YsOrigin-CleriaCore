// A small sound-effect mixer + Ogg Vorbis music streamer over SDL3's audio
// device stream.
//
// This is deliberately NOT a reimplementation of the game's DirectSound8 layer:
// it plays a WAV by EFFECTDEF id at a volume, which is all the footstep path
// needs. 3D panning, the `dup` voice cap and the emitter/listener lerp the exe
// does in FUN_0045d7a0 are not modelled yet.
//
// The device is stereo because the music is: the effect WAVs are mono and get
// upmixed by SDL when they are decoded (see Audio::clip).
#pragma once
#include <cmath>
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
    // either fails; the engine keeps running silently in that case. `device` false = no
    // playback device (CLERIA_MUTE, the tests): the SE voices are still tracked, and the
    // caller runs the mixer's clock itself with advance().
    bool init(const std::filesystem::path& asset_root, bool device = true);
    bool ready() const { return stream_ != nullptr; }

    // Play the sound EFFECTDEF calls `id`, at `volume` (0..1, the port's own gain). Unknown or
    // missing ids are ignored. Safe to call every frame. `level` is the engine's volume argument
    // (FUN_0043a040's EDX / 0x400: Se_Play's second operand), the value Se_Volume / Se_Stop ramp.
    //
    // Every play goes through the engine's buffer pick, FUN_005d87a0 [EXPORT 005d87a0.c]: a record
    // has `dup` buffers; a free one is taken; with all of them playing, the one that started longest
    // ago is stopped and restarted if it is at least `cancel` ms old (200 in every record), else the
    // new play is dropped (`nega` records never steal). A dup-1 sound that is fading (Se_Stop) is not
    // restarted: its fade is cancelled and it plays on at the new volume. A record with the `loop`
    // flag loops until stopped (rec+8 & 1 -> the buffer's Play flags, FUN_005d8ab0).
    void play(int id, float volume = 1.0f, float level = 1.0f);

    // Positional play (AUDIO.md: the engine's SE path FUN_0045d7a0 plays at the
    // emitter's position; EFFECTDEF carries per-record `dup` = max simultaneous
    // voices and mindist/maxdist = 8/180 across the corpus). `pan` is listener-
    // relative -1..+1, `dist` in world units. Attenuation is LINEAR between
    // mindist/maxdist [HAND-TUNED: the engine's curve reader was never found];
    // equal-power pan [HAND-TUNED: the exact pan law is inside FUN_0043a080,
    // untraced]. The dup cap is the engine's (see play()).
    void play_at(int id, float pan, float dist, float volume = 1.0f);

    // Se_Stop (op 0xbf -> FUN_0043a1c0, audio command 6 -> FUN_005d8970(target 0, ms, stop 1)): every
    // voice of `id` ramps linearly to silence over `ms` (0 = at once) and is then stopped.
    void stop_se(int id, int ms);
    // Se_Volume (op 0xc0 -> FUN_0043a230, audio command 7 -> FUN_005d8970(target, ms, stop 0)
    // [BYTES 0x5cd533..0x5cd558]): every voice of `id` ramps to `level` (the operand / 0x400) over
    // `ms` and plays on.
    void set_se_volume(int id, float level, int ms);
    // FUN_005ce430(force) [EXPORT 005ce430.c]: every sound fades out over 0x5dc = 1500 ms and stops —
    // all of them when `force`, else all but the `manual` records (600..606, the env beds a script
    // stops itself). The scene entry FUN_00431840 calls it with 0 [BYTES 0x431ca6], a load with 1
    // [BYTES 0x43136a].
    void stop_all(bool force);
    // How many voices of `id` are live, and the ids of the looping voices (tests, CLERIA_SFXLOG).
    int voices_of(int id) const;
    std::vector<int> looping() const;
    // No device: run the mixer for `frames` output frames (44.1 kHz), discarding the output, so
    // voices end, fade and stop on the game's clock. A no-op with a device.
    void advance(int frames);

    // Master SFX gain (0..1), applied on top of each voice's own volume. Driven by the
    // Option screen's "SFX Volume" setting. Retail applies a DirectSound centibel (log/dB) taper
    // rather than a linear one (FUN_005ce820): gain_cB = 3322*log10(norm) -> amplitude =
    // pow(norm, 3322/2000) = pow(f, 33.22/20); silent below 0.002, full at/above 0.998. The taper
    // is taken of slider x the voice's level (FUN_005d8ab0: cur * DAT_007035cc).
    void set_master_volume(float v) { master_ = clamp01(v); }

    // ---- BGM: one streaming Ogg Vorbis track ---------------------------------
    // Starts `ogg`, or keeps the current track playing untouched if it is already
    // that file — the exe compares the file name the same way, so walking between
    // rooms that share a track must not restart it (BGM.md §3.1). An empty path
    // stops. Honours the LOOPSTART/LOOPLENGTH Vorbis comments; tracks without
    // them are one-shots and simply end.
    bool play_bgm(const std::filesystem::path& ogg);
    void stop_bgm();
    // Bgm_Stop(ms) (FUN_00432fc0 -> FUN_005cde10 [BYTES]): queues audio command 3, a LINEAR
    // ramp of the stream volume (0..0x400) from its current value to 0 over `ms`
    // (FUN_005ce630 / FUN_005ce660), and command 2, the stop, delayed by `ms`. Each step's
    // volume goes through the same centibel taper as the slider, on the product
    // slider x volume (FUN_005ce6c0: norm = DAT_007035bc * vol / 2^20). `ms` <= 0 stops at
    // once. play_bgm() of the same track during or after the fade restarts it (FUN_005cde80
    // cancels the queued commands and reopens the stream).
    void fade_out_bgm(int ms);
    bool bgm_fading() const { return bgm_fade_ms_ > 0; }
    // A decoder is live: false after stop_bgm(), or once a fade_out_bgm() has run its time.
    bool bgm_streaming() const;
    // File name of the streaming track ("" = none), e.g. "YSO_004.ogg".
    const std::string& bgm_name() const { return bgm_name_; }
    void set_bgm_volume(float v) { bgm_slider_ = clamp01(v); bgm_volume_ = log_gain(v); }   // same centibel taper as SFX
    // How many times the current track has wrapped at its loop point. Only of
    // interest to the test harness — a track normally loops after minutes, so
    // $CLERIA_BGM_SEEK exists to start near the loop end and exercise the wrap.
    std::uint32_t bgm_loops() const { return bgm_loops_; }

    const EffectDefs& defs() const { return defs_; }

    // Called from SDL's audio thread. Public only so the C callback can reach it;
    // not part of the intended interface.
    void mix(std::int16_t* out, int frames);
    int channels() const { return channels_; }

  private:
    static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    // DirectSound centibel volume taper (FUN_005ce820): perceptually-logarithmic slider->amplitude.
    static float log_gain(float v) {
        v = clamp01(v);
        if (v < 0.002f) return 0.0f;
        if (v >= 0.998f) return 1.0f;
        return std::pow(v, 33.22f / 20.0f);
    }

    struct Clip {
        std::vector<std::int16_t> samples;  // interleaved, already at device format
        int channels = 1;
    };
    const Clip* clip(int id);

    struct Voice {
        const Clip* clip = nullptr;
        std::size_t pos = 0;
        float volume = 1.0f;         // the port's own gain (caller volume x distance attenuation)
        float gl = 1.0f, gr = 1.0f;  // per-channel pan gains (stereo interleave)
        int id = -1;
        bool loop = false;           // buffer +0x5c
        float level = 1.0f;          // buffer +0x48 / 0x400, the engine volume
        float target = 1.0f;         // +0x4c
        float step = 0.0f;           // level change per output frame (+0x50 is per 16 ms tick)
        bool stop = false;           // +0x5d: stopped once level reaches target
        std::uint64_t age = 0;       // output frames since it started (+0x54 is a timeGetTime stamp)
    };
    // FUN_005d87a0's buffer pick (see play()). Call with lock_ held.
    void start(int id, const EffectDef* def, const Clip* c, float volume, float level, float gl, float gr);
    void ramp(int id, float level, int ms, bool stop);

    // SDL types are kept out of this header so cleria-dump and friends need not
    // see SDL3; the .cpp casts them back.
    void* stream_ = nullptr;  // SDL_AudioStream*
    void* lock_ = nullptr;    // SDL_Mutex* — voices_ is touched from the audio thread; set = init() succeeded
    EffectDefs defs_;
    std::filesystem::path root_;
    std::unordered_map<int, std::unique_ptr<Clip>> clips_;  // id -> decoded, cached
    std::vector<Voice> voices_;
    int rate_ = 44100, channels_ = 2;
    float master_ = 1.0f;   // SFX slider 0..1 (Option screen), before the taper

    // BGM stream state. Decoded on the audio thread; every field below is
    // guarded by lock_.
    void* bgm_ = nullptr;             // stb_vorbis* — kept opaque like stream_
    std::string bgm_name_;            // "" when nothing is streaming
    std::uint64_t bgm_pos_ = 0;       // decode cursor, in frames
    std::uint32_t bgm_loop_start_ = 0;
    std::uint32_t bgm_loop_end_ = 0;  // 0 = no loop points -> one-shot
    std::uint32_t bgm_loops_ = 0;
    float bgm_volume_ = 1.0f;
    float bgm_slider_ = 1.0f;          // set_bgm_volume's raw value, for the fade's taper
    // fade_out_bgm: the ramp, timed in output frames (the engine times it with timeGetTime).
    std::uint64_t bgm_fade_ms_ = 0;    // 0 = no fade
    std::uint64_t bgm_fade_done_ = 0;  // frames mixed since the fade began
    float bgm_fade_from_ = 1.0f;       // the stream volume (/0x400) when it began
    float bgm_fade_cur_ = 1.0f;        // the stream volume now
};

}  // namespace cleria
