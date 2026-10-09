// Differential harness for the EFFECTDEF parser: dump every id -> name the C++
// parser produces, so it can be diffed against tools/effectdef_parse.py's CSV.
// The two are independent ports of the same hand-rolled tokenizer; if they agree
// across the whole file, the quirks (';' comments, '\n'-bearing tokens, '.'/'-'
// as delimiters) are ported right.
//
//   cleria-audiotest <asset_root> [--surfaces]
//   cleria-audiotest <asset_root> --walk <room.SOB>
//   cleria-audiotest <asset_root> --bgm [ROOM [seconds]]
//   cleria-audiotest <asset_root> --fade ROOM ms     (SDL_AUDIO_DRIVER=dummy for silence)
//   cleria-audiotest <asset_root> --voices           (the SE voices: loop, dup pick, stops; no device)
//
// `--walk` is the end-to-end footstep test with no GPU and no audio device: it
// walks the player across the room's collision hulls from the SCT spawn and
// resolves each step the way the engine does — collision material name ->
// surface id -> EFFECTDEF id -> WAV name.
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include <SDL3/SDL.h>

#include "cleria/audio.hpp"
#include "cleria/bgmdef.hpp"
#include "cleria/effectdef.hpp"
#include "cleria/scenelist.hpp"
#include "cleria/physics.hpp"
#include "cleria/scene.hpp"
#include "cleria/sct.hpp"
#include "cleria/surface.hpp"

namespace fs = std::filesystem;
using namespace cleria;

static std::string read_all(const fs::path& p) {
    FILE* f = std::fopen(p.string().c_str(), "rb");
    if (!f) return {};
    std::string s;
    char buf[65536];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

static int walk_room(const fs::path& root, const fs::path& sob, const EffectDefs& defs) {
    const RoomMesh coll = build_collision(root, sob);
    if (coll.indices.empty()) {
        std::printf("%s: no collision twins\n", sob.stem().string().c_str());
        return 1;
    }
    fs::path sct_p = sob;
    sct_p.replace_extension(".SCT");
    Vec3 feet{0, 0, 0};
    if (fs::exists(sct_p)) {
        const std::string bytes = read_all(sct_p);
        const Sct sct = Sct::parse(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
        for (const CastPlacement& c : sct.casts)
            if (c.name == "CAST_ADOL") { feet = c.position; break; }
    }
    std::printf("%s: %zu coll tris, spawn (%.1f, %.1f, %.1f)\n", sob.stem().string().c_str(),
                coll.indices.size() / 3, feet.x, feet.y, feet.z);

    // Walk outward along +X and -Z, sampling where the floor actually is.
    const float kStepDist = 1.6f;
    unsigned step_n = 0;
    std::map<std::string, int> heard;
    for (const Vec3 dir : {Vec3{1, 0, 0}, Vec3{-1, 0, 0}, Vec3{0, 0, 1}, Vec3{0, 0, -1}}) {
        for (float d = 0; d < 40.0f; d += kStepDist) {
            const float x = feet.x + dir.x * d, z = feet.z + dir.z * d;
            int tri = -1;
            const float y = floor_at(coll, x, z, feet.y + 2.0f, &tri);
            if (y == kNoFloor) continue;
            const std::string_view surf = surface_of(coll, tri);
            const int idx = surface_index_or_default(surf);
            const int id = footstep_sound(idx, step_n++);
            const EffectDef* def = id >= 0 ? defs.find(id) : nullptr;
            const std::string key = std::string(surf) +
                                    (surface_index(surf) < 0 ? " (->stone) -> " : " -> ") +
                                    (def ? def->name : "(silent)");
            ++heard[key];
        }
    }
    for (const auto& [k, n] : heard) std::printf("   %4dx  %s\n", n, k.c_str());
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: cleria-audiotest <asset_root> [--surfaces | --walk <room.SOB>]\n");
        return 2;
    }
    const cleria::EffectDefs defs = cleria::EffectDefs::load(argv[1]);
    if (defs.size() == 0) {
        std::fprintf(stderr, "no EFFECTDEF.TXT under %s\n", argv[1]);
        return 1;
    }

    if (argc >= 4 && std::strcmp(argv[2], "--walk") == 0) return walk_room(argv[1], argv[3], defs);

    // Open a real device and play one sound, so the SDL path is exercised outside
    // the interactive loop: cleria-audiotest <root> --sound <id> [seconds]
    if (argc >= 4 && std::strcmp(argv[2], "--sound") == 0) {
        Audio a;
        if (!a.init(argv[1])) return 1;
        const int id = std::atoi(argv[3]);
        const EffectDef* d = defs.find(id);
        const fs::path w = defs.wav_path(argv[1], id);
        std::printf("playing id %d (%s)\n  wav: %s  exists=%d\n", id, d ? d->name.c_str() : "?",
                    w.string().c_str(), int(!w.empty() && fs::exists(w)));
        if (!w.empty() && !fs::exists(w)) {
            std::fprintf(stderr, "  -> WAV missing; play() would be a silent no-op\n");
            return 1;
        }
        a.play(id);
        SDL_Delay(argc >= 5 ? Uint32(std::atof(argv[4]) * 1000) : 1200);
        return 0;
    }

    // Per-room music. With no ROOM this dumps the whole SCENELIST -> bgmdef
    // mapping (headless — it checks the two tables against each other and against
    // what is on disk); with one it opens a device and actually streams the track.
    //   cleria-audiotest <root> --bgm [ROOM [seconds]]
    if (argc >= 3 && std::strcmp(argv[2], "--bgm") == 0) {
        SceneList scenes;
        BgmDefs bgm;
        if (!scenes.load(argv[1])) {
            std::fprintf(stderr, "no MAP/SCENELIST.SL under %s\n", argv[1]);
            return 1;
        }
        const bool have_music = bgm.load(argv[1]);
        std::printf("%zu scenes; music dir: %s\n", scenes.size(),
                    have_music ? bgm.dir().string().c_str()
                               : "(not found -- set CLERIA_MUSIC_DIR)");
        if (argc < 4) {
            for (const SceneRec& s : scenes.recs()) {
                if (s.path.empty()) continue;
                std::printf("  %-3u %-26s bgm=%-3u %-10s %s\n", s.index, s.path.c_str(), s.bgm,
                            s.bgm ? bgm.name(s.bgm).c_str() : "(stop)", s.display.c_str());
            }
            return 0;
        }
        const SceneRec* s = scenes.find_room(argv[3]);
        if (!s) {
            std::fprintf(stderr, "no scene record for room %s\n", argv[3]);
            return 1;
        }
        const fs::path t = bgm.track(s->bgm);
        std::printf("%s (%s): bgm id %u -> %s\n", argv[3], s->display.c_str(), s->bgm,
                    t.empty() ? "(none)" : t.string().c_str());
        if (t.empty()) return s->bgm == 0 ? 0 : 1;  // id 0 legitimately means silence
        Audio a;
        if (!a.init(argv[1])) return 1;
        if (!a.play_bgm(t)) return 1;
        SDL_Delay(argc >= 5 ? Uint32(std::atof(argv[4]) * 1000) : 5000);
        std::printf("looped %u time(s)\n", a.bgm_loops());
        return 0;
    }

    // Bgm_Stop(ms): the room's track plays 0.5 s, fades over `ms`, and must still stream at
    // half the fade and be stopped once its time has run out.
    if (argc >= 5 && std::strcmp(argv[2], "--fade") == 0) {
        SceneList scenes;
        BgmDefs bgm;
        const SceneRec* s = scenes.load(argv[1]) ? scenes.find_room(argv[3]) : nullptr;
        const fs::path t = s && bgm.load(argv[1]) ? bgm.track(s->bgm) : fs::path();
        if (t.empty()) { std::printf("fade: SKIP (no track for %s)\n", argv[3]); return 0; }
        Audio a;
        if (!a.init(argv[1]) || !a.play_bgm(t)) return 1;
        const int ms = std::atoi(argv[4]);
        SDL_Delay(500);
        a.fade_out_bgm(ms);
        SDL_Delay(Uint32(ms / 2));
        const bool mid = a.bgm_streaming() && a.bgm_fading();
        SDL_Delay(Uint32(ms / 2 + 300));
        const bool after = a.bgm_streaming();
        const bool restart = a.play_bgm(t) && a.bgm_streaming() && !a.bgm_fading();
        std::printf("fade %d ms: streaming at half %s, after %s, same-track restart %s\n", ms,
                    mid ? "yes" : "no", after ? "yes" : "no", restart ? "yes" : "no");
        const bool ok = mid && !after && restart;
        std::printf("fade: %s\n", ok ? "OK" : "FAIL");
        return ok ? 0 : 1;
    }

    // The SE voice rules of FUN_005d87a0 / FUN_005d8970 / FUN_005d8ab0 / FUN_005ce430, on a device-less
    // mixer run by hand. 604 env/v_wind: loop, dup 1, manual (12.1 s). 609 env/v_earth: loop, dup 1
    // (2.2 s). 214 boss/b_02_se04: loop, dup 3 (5.7 s). 0 sys/s_ok: a one-shot (1.0 s).
    if (argc >= 3 && std::strcmp(argv[2], "--voices") == 0) {
        Audio a;
        if (!a.init(argv[1], /*device*/false)) return 1;
        int fails = 0;
        auto check = [&](const char* what, bool ok) {
            std::printf("voices: %-58s %s\n", what, ok ? "ok" : "FAIL");
            fails += !ok;
        };
        auto run = [&](double s) { for (int n = int(s * 44100.0); n > 0; n -= 735) a.advance(735); };
        for (int id : {604, 609, 214, 0})
            if (!a.defs().find(id)) { std::printf("voices: SKIP (no record %d)\n", id); return 0; }
        a.play(0); a.play(609);
        run(0.5);
        check("both play", a.voices_of(0) == 1 && a.voices_of(609) == 1);
        run(5.0);
        check("the one-shot ended, the loop wrapped (2.2 s clip, 5.5 s)", a.voices_of(0) == 0 && a.voices_of(609) == 1);
        check("looping() lists it", a.looping() == std::vector<int>{609});
        a.play(609);
        check("dup 1, older than 200 ms: restarted, still one voice", a.voices_of(609) == 1);
        a.stop_se(609, 1000);
        run(0.5);
        check("Se_Stop(1000): still fading at 0.5 s", a.voices_of(609) == 1);
        a.play(609);                                   // dup 1 + mid-ramp: the fade is cancelled
        run(2.0);
        check("a play during the fade cancels the stop", a.voices_of(609) == 1);
        a.stop_se(609, 1000);
        run(1.1);
        check("Se_Stop(1000): stopped after 1.1 s", a.voices_of(609) == 0);
        a.play(609);
        run(0.1);
        a.set_se_volume(609, 0.0f, 100);
        run(1.0);
        check("Se_Volume to 0 does not stop the voice", a.voices_of(609) == 1);
        a.stop_se(609, 0);
        run(0.1);
        check("Se_Stop(0) stops at once", a.voices_of(609) == 0);
        // the dup pick
        a.play(214); a.play(214);
        check("two requests before the mixer runs take the same buffer", a.voices_of(214) == 1);
        run(0.05); a.play(214); run(0.05); a.play(214); run(0.05);
        check("dup 3: three voices", a.voices_of(214) == 3);
        a.play(214);
        check("all playing, none 200 ms old: the new play is dropped", a.voices_of(214) == 3);
        run(0.3);
        a.play(214);
        check("all playing, the oldest restarted: still three", a.voices_of(214) == 3);
        // the scene entry's stop-all
        a.play(604); a.play(609);
        run(0.1);
        a.stop_all(false);
        run(1.6);
        check("stop_all(0): 609 and 214 stopped after 1.5 s", a.voices_of(609) == 0 && a.voices_of(214) == 0);
        check("stop_all(0): the manual bed 604 plays on", a.voices_of(604) == 1);
        a.stop_all(true);
        run(1.6);
        check("stop_all(1): 604 stopped too", a.voices_of(604) == 0 && a.looping().empty());
        std::printf("voices: %s\n", fails ? "FAIL" : "OK");
        return fails ? 1 : 0;
    }

    if (argc >= 3 && std::strcmp(argv[2], "--surfaces") == 0) {
        // Resolve every surface through the table the way the exe does, and show
        // the four variants each one cycles through.
        for (int i = 0; i < cleria::kSurfaceCount; ++i) {
            const cleria::SurfaceType& s = cleria::kSurfaceTable[i];
            std::printf("%2d %-10s base=%-3u walk=%-3u run=%-3u jit=%u mask=%u ", i, s.name,
                        s.base_sound_id, s.walk_ticks, s.run_ticks, s.jitter, s.variant_mask);
            if (s.base_sound_id == 0) {
                std::printf("(silent)\n");
                continue;
            }
            for (unsigned n = 0; n < 4; ++n) {
                const int id = cleria::footstep_sound(i, n);
                const cleria::EffectDef* d = defs.find(id);
                std::printf("%s%s", n ? " " : "", d ? d->name.c_str() : "?");
            }
            std::printf("\n");
        }
        return 0;
    }

    std::map<int, std::string> sorted;
    for (const auto& [id, d] : defs.all()) sorted[id] = d.name;
    std::printf("id,filename\n");
    for (const auto& [id, name] : sorted) std::printf("%d,%s\n", id, name.c_str());
    return 0;
}
