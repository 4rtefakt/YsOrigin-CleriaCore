// cleria-view — CLERIA_PROF=1: where a tick and a frame spend their wall time (a debug aid; off = one branch per mark).
// prof("name") closes the running section and opens `name`; the time between two marks belongs to the first one.
// prof_report(tick) prints, every 60 ticks, each section's mean ms per frame over the window (largest first), the
// frames in the window, and the slowest single frame with its largest section; prof_count("name", n) adds a pool
// size to the same line. A frame over 25 ms is printed at once with its sections ("[hitch]").
#include <chrono>

struct Prof {
    struct Sec { const char* name; double win = 0, frame = 0, peak = 0; };
    std::vector<Sec> secs;
    std::vector<std::pair<const char*, long long>> counts;
    bool on = std::getenv("CLERIA_PROF") != nullptr;
    int cur = -1, frames = 0;
    std::chrono::steady_clock::time_point t0{};
    double worst = 0; std::string worst_what;
    int find(const char* n) {
        for (std::size_t i = 0; i < secs.size(); ++i) if (secs[i].name == n || std::strcmp(secs[i].name, n) == 0) return int(i);
        secs.push_back({n});
        return int(secs.size()) - 1;
    }
    void mark(const char* n) {
        const auto t = std::chrono::steady_clock::now();
        if (cur >= 0) secs[std::size_t(cur)].frame += std::chrono::duration<double, std::milli>(t - t0).count();
        cur = n ? find(n) : -1;
        t0 = t;
    }
    void count(const char* n, long long v) {
        for (auto& c : counts) if (std::strcmp(c.first, n) == 0) { c.second = v; return; }
        counts.push_back({n, v});
    }
    void end_frame(unsigned long long tick) {
        mark(nullptr);
        double tot = 0; const Sec* big = nullptr;
        for (Sec& s : secs) { tot += s.frame; if (!big || s.frame > big->frame) big = &s; }
        if (tot > 25.0 && frames > 0) {
            std::printf("  [hitch] t%llu frame %.1f ms:", tick, tot);
            for (const Sec& s : secs) if (s.frame >= 1.0) std::printf(" %s %.1f", s.name, s.frame);
            std::printf("\n");
        }
        if (tot > worst) { worst = tot; worst_what = big ? big->name : ""; }
        for (Sec& s : secs) { s.win += s.frame; s.peak = std::max(s.peak, s.frame); s.frame = 0; }
        ++frames;
    }
    void report(unsigned long long tick) {
        std::vector<const Sec*> o;
        double tot = 0;
        for (const Sec& s : secs) { o.push_back(&s); tot += s.win; }
        std::sort(o.begin(), o.end(), [](const Sec* a, const Sec* b) { return a->win > b->win; });
        std::printf("  [prof] t%llu %d frames, %.2f ms/frame (worst %.1f in %s):", tick, frames, tot / std::max(1, frames),
                    worst, worst_what.c_str());
        for (const Sec* s : o) if (s->win / std::max(1, frames) >= 0.05) std::printf(" %s %.2f", s->name, s->win / std::max(1, frames));
        std::printf(" | peaks:");
        for (const Sec* s : o) if (s->peak >= 3.0) std::printf(" %s %.1f", s->name, s->peak);
        std::printf(" |");
        for (const auto& c : counts) std::printf(" %s %lld", c.first, c.second);
        std::printf("\n");
        for (Sec& s : secs) s.win = s.peak = 0;
        frames = 0; worst = 0;
    }
};
static Prof g_prof;
static inline void prof(const char* name) { if (g_prof.on) g_prof.mark(name); }
// A first-use load inside a frame (a texture, a model): "[load] what name: ms" when it took a millisecond or more.
static inline double prof_ms() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static inline void prof_load(const char* what, const std::string& name, double t0) {
    if (g_prof.on && prof_ms() - t0 >= 1.0) std::printf("  [load] %s %s: %.1f ms\n", what, name.c_str(), prof_ms() - t0);
}
static inline void prof_count(const char* name, long long v) { if (g_prof.on) g_prof.count(name, v); }
