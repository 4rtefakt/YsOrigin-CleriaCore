// Speedrun mode: the timer, the auto-split observer, the splits file (build/shell_re/SPEEDRUN.md).
#include "cleria/speedrun.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>

#include "cleria/livesplit.hpp"
#include "cleria/settings.hpp"   // the ini dialect (Ini) for the splits file

namespace cleria::speedrun {

// ---- active modifications --------------------------------------------------------------------------
namespace {
std::mutex g_mu;
std::map<std::string, Provider>& providers() {
    static std::map<std::string, Provider> m;
    return m;
}
std::vector<std::string>& notes() {
    static std::vector<std::string> v;
    return v;
}
void add_unique(std::vector<std::string>& v, const std::string& s) {
    if (!s.empty() && std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
}
}  // namespace

void set_provider(const std::string& key, Provider p) {
    std::lock_guard<std::mutex> lk(g_mu);
    providers()[key] = std::move(p);
}
void clear_provider(const std::string& key) {
    std::lock_guard<std::mutex> lk(g_mu);
    providers().erase(key);
}
std::vector<std::string> active_modifications() {
    std::map<std::string, Provider> copy;
    { std::lock_guard<std::mutex> lk(g_mu); copy = providers(); }
    std::vector<std::string> out, all;
    for (auto& [k, p] : copy) {
        out.clear();
        if (p) p(out);
        for (const std::string& s : out) add_unique(all, s);
    }
    return all;
}
void note_modification(const std::string& what) {
    std::lock_guard<std::mutex> lk(g_mu);
    add_unique(notes(), what);
}
std::vector<std::string> take_notes() {
    std::lock_guard<std::mutex> lk(g_mu);
    std::vector<std::string> v;
    v.swap(notes());
    return v;
}

// ---- times -------------------------------------------------------------------------------------
std::string format_time(double ms) {
    const bool neg = ms < 0;
    const std::uint64_t cs = std::uint64_t(std::floor(std::fabs(ms) / 10.0));   // centiseconds, truncated
    const std::uint64_t s = cs / 100, m = s / 60, h = m / 60;
    char b[48];
    if (h > 0) std::snprintf(b, sizeof b, "%s%llu:%02llu:%02llu.%02llu", neg ? "-" : "", (unsigned long long)h,
                             (unsigned long long)(m % 60), (unsigned long long)(s % 60), (unsigned long long)(cs % 100));
    else std::snprintf(b, sizeof b, "%s%llu:%02llu.%02llu", neg ? "-" : "", (unsigned long long)m,
                       (unsigned long long)(s % 60), (unsigned long long)(cs % 100));
    return b;
}

std::string format_play_time(std::uint64_t ms) {
    // FUN_005b3e10 / 0x69a344 "%d:%02d:%02d:%03d": h = ms / 3600000 capped at 9999 (SAVELOAD.md).
    const std::uint64_t h = std::min<std::uint64_t>(ms / 3600000u, 9999u);
    char b[48];
    std::snprintf(b, sizeof b, "%d:%02d:%02d:%03d", int(h), int(ms / 60000u % 60u), int(ms / 1000u % 60u),
                  int(ms % 1000u));
    return b;
}

std::string format_delta(double ms) {
    const char sign = ms < 0 ? '-' : '+';
    const double a = std::fabs(ms);
    char b[48];
    if (a < 60000.0) {
        std::snprintf(b, sizeof b, "%c%.1f", sign, std::floor(a / 100.0) / 10.0);
        return b;
    }
    const std::uint64_t s = std::uint64_t(a / 1000.0), m = s / 60, h = m / 60;
    if (h > 0) std::snprintf(b, sizeof b, "%c%llu:%02llu:%02llu", sign, (unsigned long long)h,
                             (unsigned long long)(m % 60), (unsigned long long)(s % 60));
    else std::snprintf(b, sizeof b, "%c%llu:%02llu", sign, (unsigned long long)m, (unsigned long long)(s % 60));
    return b;
}

// ---- events ------------------------------------------------------------------------------------
namespace {
std::string upper(std::string_view s) {
    std::string u(s);
    for (char& c : u) c = char(std::toupper(static_cast<unsigned char>(c)));
    return u;
}
std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}
bool room_eq(std::string_view pat, std::string_view room) {
    return pat == "*" || upper(pat) == upper(room);
}
bool parse_int(std::string_view s, int& v) {
    if (s.empty()) return false;
    char* e = nullptr;
    const std::string t(s);
    const long x = std::strtol(t.c_str(), &e, 10);
    if (!e || *e != '\0') return false;
    v = int(x);
    return true;
}
// "kind:arg" -> kind, arg (both trimmed, kind lower-cased).
void split_spec(std::string_view spec, std::string& kind, std::string& arg) {
    const std::string s = trim(spec);
    const std::size_t c = s.find(':');
    kind = c == std::string::npos ? s : s.substr(0, c);
    for (char& ch : kind) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    arg = c == std::string::npos ? std::string() : trim(std::string_view(s).substr(c + 1));
}
}  // namespace

bool valid_spec(std::string_view spec) {
    std::string k, a;
    split_spec(spec, k, a);
    int n = 0;
    if (k == "manual") return a.empty();
    if (k == "enter" || k == "leave") return !a.empty();
    if (k == "room") {
        const std::size_t g = a.find('>');
        return g != std::string::npos && g > 0 && g + 1 < a.size();
    }
    if (k == "boss") {
        if (a.empty()) return false;
        const std::size_t h = a.find('#');
        return h == std::string::npos || (h > 0 && parse_int(std::string_view(a).substr(h + 1), n) && n >= 1);
    }
    if (k == "item") return parse_int(a, n) && n >= 0 && n < 0x200;
    if (k == "flag") {
        const std::size_t e = a.find('=');
        int v = 0;
        return e != std::string::npos && parse_int(std::string_view(a).substr(0, e), n) && n >= 0 && n < 0x200 &&
               parse_int(std::string_view(a).substr(e + 1), v);
    }
    return false;
}

bool matches(std::string_view spec, const Event& e) {
    if (!valid_spec(spec)) return false;
    std::string k, a;
    split_spec(spec, k, a);
    if (k == "enter") return e.kind == Event::Room && room_eq(a, e.to);
    if (k == "leave") return e.kind == Event::Room && room_eq(a, e.from);
    if (k == "room") {
        const std::size_t g = a.find('>');
        return e.kind == Event::Room && room_eq(trim(a.substr(0, g)), e.from) && room_eq(trim(a.substr(g + 1)), e.to);
    }
    if (k == "boss") {
        if (e.kind != Event::Boss) return false;
        const std::size_t h = a.find('#');
        int n = 0;
        if (h != std::string::npos) parse_int(std::string_view(a).substr(h + 1), n);
        return room_eq(trim(a.substr(0, h)), e.room) && (n == 0 || e.count == n);
    }
    if (k == "item") {
        int id = -1;
        parse_int(a, id);
        return e.kind == Event::Flag && e.flag == id && e.old_v < 1 && e.new_v >= 1;
    }
    if (k == "flag") {
        const std::size_t q = a.find('=');
        int id = -1, v = 0;
        parse_int(std::string_view(a).substr(0, q), id);
        parse_int(std::string_view(a).substr(q + 1), v);
        return e.kind == Event::Flag && e.flag == id && e.new_v == v && e.old_v != v;
    }
    return false;
}

std::string describe(const Event& e) {
    char b[160];
    switch (e.kind) {
        case Event::Room: std::snprintf(b, sizeof b, "room %s > %s", e.from.c_str(), e.to.c_str()); break;
        case Event::Boss: std::snprintf(b, sizeof b, "boss gauge empty in %s (#%d)", e.room.c_str(), e.count); break;
        case Event::Flag: std::snprintf(b, sizeof b, "flag %d: %d -> %d", e.flag, e.old_v, e.new_v); break;
    }
    return b;
}

void Observer::step(const GameView& v, std::vector<Event>& out) {
    const bool room_changed = have_ && room_ != v.room;
    if (room_changed) {
        Event e;
        e.kind = Event::Room;
        e.from = room_;
        e.to = std::string(v.room);
        out.push_back(e);
        boss_count_ = 0;
        death_latch_ = false;
    }
    if (!v.player_alive) death_latch_ = true;
    // A lost duel (FUN_0046f810 with g_flags[133]): 156 = 130 = 0 and the gauge withdrawn, HP 1.
    const bool duel_lost = v.flags && v.flags[133] != 0 && v.flags[156] == 0 && !flags_.empty() && flags_[156] != 0;
    if (have_ && !room_changed && gauge_ && hp_ > 0.0f) {
        const bool emptied = v.gauge_visible ? v.gauge_hp <= 0.0f : true;
        if (emptied && !death_latch_ && !duel_lost && v.player_alive) {
            Event e;
            e.kind = Event::Boss;
            e.room = std::string(v.room);
            e.count = ++boss_count_;
            out.push_back(e);
        }
    }
    // The withdrawal that follows a death (the retry's re-init) is swallowed; then the latch opens.
    if (death_latch_ && v.player_alive && !v.gauge_visible) death_latch_ = false;
    if (v.flags) {
        if (flags_.size() == 0x200) {
            int changed = 0;
            for (int i = 0; i < 0x200; ++i) changed += flags_[std::size_t(i)] != v.flags[i];
            if (changed <= kBulkFlags)
                for (int i = 0; i < 0x200; ++i)
                    if (flags_[std::size_t(i)] != v.flags[i]) {
                        Event e;
                        e.kind = Event::Flag;
                        e.flag = i;
                        e.old_v = flags_[std::size_t(i)];
                        e.new_v = v.flags[i];
                        out.push_back(e);
                    }
        }
        flags_.assign(v.flags, v.flags + 0x200);
    }
    have_ = true;
    room_ = std::string(v.room);
    gauge_ = v.gauge_visible;
    hp_ = v.gauge_hp;
}

// ---- the splits file ---------------------------------------------------------------------------
namespace {
double get_ms(const Ini& d, const std::string& sec, const char* key) {
    const auto f = d.get_float(sec, key);
    return f ? double(*f) : -1.0;
}
// A string value with or without its quotes (the file is meant to be hand-edited too).
std::string get_str(const Ini& d, const std::string& sec, const char* key, const char* def) {
    const auto r = d.raw(sec, key);
    if (!r) return def;
    std::string v = *r;
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
    return v;
}
std::string ms_str(double ms) {
    if (ms < 0) return "-1";
    char b[32];
    std::snprintf(b, sizeof b, "%.0f", ms);
    return b;
}
}  // namespace

std::string Route::text() const {
    Ini d;
    d.set_string("run", "Game", "Ys Origin");
    d.set_string("run", "Category", category);
    d.set_int("run", "Attempts", attempts);
    d.set_int("run", "Completed", completed);
    for (std::size_t i = 0; i < segs.size(); ++i) {
        const std::string sec = "split." + std::to_string(i);
        const Segment& s = segs[i];
        d.set_string(sec, "Name", s.name);
        d.set_string(sec, "Event", s.event);
        d.set_raw(sec, "PbRta", ms_str(s.pb_rta));
        d.set_raw(sec, "PbIgt", ms_str(s.pb_igt));
        d.set_raw(sec, "GoldRta", ms_str(s.gold_rta));
        d.set_raw(sec, "GoldIgt", ms_str(s.gold_igt));
    }
    std::string t =
        "; CleriaCore speedrun splits (build/shell_re/SPEEDRUN.md). Times in ms, -1 = none.\r\n"
        "; Event: enter:ROOM  leave:ROOM  room:FROM>TO  boss:ROOM[#n]  item:ID  flag:ID=VALUE  manual\r\n";
    return t + d.text();
}

bool Route::parse(std::string_view text) {
    Ini d;
    d.parse(text);
    if (!d.has("run", "Category")) return false;
    Route r;
    r.category = get_str(d, "run", "Category", "");
    r.attempts = d.get_int("run", "Attempts").value_or(0);
    r.completed = d.get_int("run", "Completed").value_or(0);
    for (int i = 0;; ++i) {
        const std::string sec = "split." + std::to_string(i);
        if (!d.has(sec, "Name")) break;
        Segment s;
        s.name = get_str(d, sec, "Name", "");
        s.event = get_str(d, sec, "Event", "manual");
        s.pb_rta = get_ms(d, sec, "PbRta");
        s.pb_igt = get_ms(d, sec, "PbIgt");
        s.gold_rta = get_ms(d, sec, "GoldRta");
        s.gold_igt = get_ms(d, sec, "GoldIgt");
        r.segs.push_back(std::move(s));
    }
    if (r.segs.empty()) return false;
    *this = std::move(r);
    return true;
}

bool Route::load(const std::filesystem::path& p) {
    Ini probe;
    if (!probe.load(p)) return false;
    return parse(probe.text());
}

bool Route::save(const std::filesystem::path& p) const {
    std::error_code ec;
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    std::FILE* f = nullptr;
#ifdef _WIN32
    if (_wfopen_s(&f, p.c_str(), L"wb") != 0) f = nullptr;
#else
    f = std::fopen(p.c_str(), "wb");
#endif
    if (!f) return false;
    const std::string t = text();
    const bool ok = std::fwrite(t.data(), 1, t.size(), f) == t.size();
    std::fclose(f);
    return ok;
}

std::string category_name(int character, bool extra) {
    static const char* const kNames[] = {"Yunica", "Yunica", "Hugo", "Toal"};
    return std::string("Any% (") + kNames[std::clamp(character, 0, 3)] + (extra ? " EX)" : ")");
}
std::string category_key(int character, bool extra) {
    static const char* const kKeys[] = {"yunica", "yunica", "hugo", "toal"};
    return std::string(kKeys[std::clamp(character, 0, 3)]) + (extra ? "_ex" : "");
}

Route default_route(int character, bool extra) {
    // From the community ASL split points (na2na-p/FalcomSpeedrunASL, Ys/Origin; SPEEDRUN.md §3),
    // their room ordinals read through MAP/SCENELIST.SL. [H] the selection and the names.
    Route r;
    r.category = category_name(character, extra);
    auto add = [&](const char* n, const char* e) { Segment s; s.name = n; s.event = e; r.segs.push_back(s); };
    add("Velagunder", "boss:S_1099");                        // 45  "5F Velagunder"
    if (character != 3) add("Hugo", "boss:S_2080");          // 67  SPBossHP ("Hugo1" in ToalEasy.asl)
    add("Nygtilger", "boss:S_2099");                         // 69  FloorBossHP
    add("Gelaldy", "boss:S_3099");                           // 93  FloorBossHP
    add("Khonsclard", "room:S_4099>S_4097");                 // 123 -> 122
    add("Pictimos", "room:S_5099>S_5097");                   // 147 -> 146
    add("Yog & Om", "room:S_6082>S_6050");                   // 166 -> 161
    if (character == 3) add("Darm", "boss:S_7099");          // 176 "Other Dimension Darm"
    else add("Dalles", "boss:S_7080");                       // 175 "Dalles Sky"
    return r;
}

// ---- the run -----------------------------------------------------------------------------------
void Run::start(double now_ms) {
    times_.clear();
    old_golds_.clear();
    mods_used_.clear();
    igt_ticks_ = 0;
    t0_ = now_ms;
    t_end_ = 0;
    new_pb_ = false;
    ++route.attempts;
    base_ = route;
    phase_ = Phase::Running;
}

double Run::rta(double now_ms) const {
    if (phase_ == Phase::Idle) return 0;
    return std::max(0.0, (phase_ == Phase::Finished ? t_end_ : now_ms) - t0_);
}

bool Run::split(double now_ms) {
    if (!running() || index() >= count()) return false;
    const int i = index();
    SplitTime t;
    t.rta = now_ms - t0_;
    t.igt = igt();
    Segment& s = route.segs[std::size_t(i)];
    old_golds_.push_back({s.gold_rta, s.gold_igt});
    // A gold needs the previous split's time (a skipped one leaves the segment unknown).
    const bool prev_ok = i == 0 || times_[std::size_t(i - 1)].rta >= 0;
    if (prev_ok) {
        const double seg_rta = t.rta - (i == 0 ? 0.0 : times_[std::size_t(i - 1)].rta);
        const double seg_igt = t.igt - (i == 0 ? 0.0 : times_[std::size_t(i - 1)].igt);
        const bool g_rta = s.gold_rta < 0 || seg_rta < s.gold_rta;
        const bool g_igt = s.gold_igt < 0 || seg_igt < s.gold_igt;
        if (g_rta) s.gold_rta = seg_rta;
        if (g_igt) s.gold_igt = seg_igt;
        t.gold = timing == Timing::Rta ? g_rta : g_igt;
    }
    times_.push_back(t);
    if (index() == count()) { finish(now_ms); return true; }
    return false;
}

bool Run::skip(double) {
    if (!running() || index() >= count() - 1) return false;   // the last split cannot be skipped
    const Segment& s = route.segs[std::size_t(index())];
    old_golds_.push_back({s.gold_rta, s.gold_igt});
    times_.push_back(SplitTime{});
    return true;
}

bool Run::undo() {
    if (phase_ == Phase::Idle || times_.empty()) return false;
    if (phase_ == Phase::Finished) {         // un-finish: drop what the finish wrote
        const int a = route.attempts;
        route = pb_backup_;
        route.attempts = a;
        phase_ = Phase::Running;
        new_pb_ = false;
    }
    const std::size_t i = times_.size() - 1;
    route.segs[i].gold_rta = old_golds_.back().first;
    route.segs[i].gold_igt = old_golds_.back().second;
    old_golds_.pop_back();
    times_.pop_back();
    return true;
}

void Run::finish(double now_ms) {
    t_end_ = now_ms;
    phase_ = Phase::Finished;
    pb_backup_ = route;
    // the golds are already in pb_backup_ (split() wrote them); undo restores the ones before
    ++route.completed;
    const double fin = split_time(count() - 1);
    const double pb = timing == Timing::Rta ? route.segs.back().pb_rta : route.segs.back().pb_igt;
    if (fin >= 0 && (pb < 0 || fin < pb)) {
        for (std::size_t i = 0; i < route.segs.size(); ++i) {
            route.segs[i].pb_rta = times_[i].rta;
            route.segs[i].pb_igt = times_[i].igt;
        }
        new_pb_ = true;
    }
}

void Run::reset() {
    phase_ = Phase::Idle;
    times_.clear();
    old_golds_.clear();
    mods_used_.clear();
    igt_ticks_ = 0;
    new_pb_ = false;
}

void Run::record_mods(const std::vector<std::string>& active) {
    if (phase_ != Phase::Running) return;
    for (const std::string& s : active) add_unique(mods_used_, s);
}

double Run::split_time(int i) const {
    if (i < 0 || i >= int(times_.size())) return -1;
    return timing == Timing::Rta ? times_[std::size_t(i)].rta : times_[std::size_t(i)].igt;
}

double Run::comparison(int i, Compare c) const {
    if (i < 0 || i >= count()) return -1;
    const Route& r = phase_ == Phase::Idle || base_.segs.size() != route.segs.size() ? route : base_;
    if (c == Compare::PersonalBest) {
        const Segment& s = r.segs[std::size_t(i)];
        return timing == Timing::Rta ? s.pb_rta : s.pb_igt;
    }
    double sum = 0;
    for (int k = 0; k <= i; ++k) {
        const Segment& s = r.segs[std::size_t(k)];
        const double g = timing == Timing::Rta ? s.gold_rta : s.gold_igt;
        if (g < 0) return -1;
        sum += g;
    }
    return sum;
}

double Run::delta(int i, Compare c) const {
    const double t = split_time(i), cmp = comparison(i, c);
    if (t < 0 || cmp < 0) return kNone;
    return t - cmp;
}

double Run::sum_of_best() const {                // the golds as they are now
    double sum = 0;
    for (const Segment& s : route.segs) {
        const double g = timing == Timing::Rta ? s.gold_rta : s.gold_igt;
        if (g < 0) return -1;
        sum += g;
    }
    return route.segs.empty() ? -1 : sum;
}

// ---- the session ---------------------------------------------------------------------------------
std::filesystem::path Session::route_path() const {
    if (dir.empty()) return {};
    return dir / (category_key(character_, extra_) + ".splits");
}

bool Session::reload() {
    if (run.phase() != Run::Phase::Idle) return false;
    const Timing t = run.timing;
    run = Run{};
    run.timing = t;
    loaded_ = true;
    const std::filesystem::path p = route_path();
    if (!p.empty() && run.route.load(p)) return true;
    run.route = default_route(character_, extra_);
    if (!p.empty()) run.route.save(p);       // a file to edit from the first run on
    return false;
}

void Session::select(int character, bool extra) {
    if (run.phase() != Run::Phase::Idle) return;
    if (loaded_ && character == character_ && extra == extra_) return;
    character_ = character;
    extra_ = extra;
    reload();
}

void Session::save() const {
    const std::filesystem::path p = route_path();
    if (!p.empty()) run.route.save(p);
}

void Session::new_game(int character, bool extra, double now_ms) {
    if (!cfg.enabled || !cfg.auto_start) return;
    if (run.phase() != Run::Phase::Idle) reset(now_ms);   // a new game ends the run that was going
    select(character, extra);
    start(now_ms);
}

void Session::start(double now_ms) {
    if (!cfg.enabled) return;
    if (!loaded_) reload();
    if (run.phase() != Run::Phase::Idle) reset(now_ms);
    run.timing = cfg.timing;
    run.start(now_ms);
    take_notes();                            // what happened before the run is not the run's
    run.record_mods(active_modifications());
    save();                                  // the attempt count
    ls("reset");
    ls("starttimer");
    ls("pausegametime");                     // LiveSplit's game time = exactly the IGT we send
    ls(ls_setgametime(0));
    ls_ticks_ = 0;
    say("Run started: " + run.route.category, now_ms);
}

void Session::tick(const GameView& v, double now_ms) {
    if (!cfg.enabled) return;
    run.tick();
    ev_.clear();
    obs_.step(v, ev_);
    if (run.running() && cfg.auto_split)
        for (const Event& e : ev_) {
            if (!run.running() || run.index() >= run.count()) break;
            if (matches(run.route.segs[std::size_t(run.index())].event, e)) {
                std::printf("  [speedrun] auto-split %d '%s' on %s (IGT step %llu)\n", run.index(),
                            run.route.segs[std::size_t(run.index())].name.c_str(), describe(e).c_str(),
                            (unsigned long long)run.igt_ticks());
                split(now_ms);
            }
        }
    std::vector<std::string> m = active_modifications();
    for (std::string& n : take_notes()) m.push_back(std::move(n));
    run.record_mods(m);
}

void Session::frame(double now_ms, std::string_view room) {
    if (cfg.enabled && run.running() && run.igt_ticks() != ls_ticks_) {
        ls_ticks_ = run.igt_ticks();
        ls(ls_setgametime(run.igt()));
    }
    publish(run, now_ms, room);
}

bool Session::split(double now_ms) {
    if (!run.running()) return false;
    const int i = run.index();
    ls(ls_setgametime(run.igt()));           // the split's IGT, before LiveSplit records it
    ls_ticks_ = run.igt_ticks();
    const bool fin = run.split(now_ms);
    ls("split");
    const double d = run.delta(i, cfg.compare);
    std::string msg = run.route.segs[std::size_t(i)].name + "  " + format_time(run.split_time(i));
    if (d != Run::kNone) msg += "  " + format_delta(d);
    if (run.times()[std::size_t(i)].gold) msg += "  (gold)";
    if (fin) msg = (run.new_pb() ? "Finished - new personal best! " : "Finished. ") + format_time(run.split_time(i));
    save();                                  // golds (and a finish's PB) survive a quit or a crash
    say(msg, now_ms);
    return true;
}

bool Session::skip(double now_ms) {
    if (!run.skip(now_ms)) return false;
    ls("skipsplit");
    say("Skipped " + run.route.segs[std::size_t(run.index() - 1)].name, now_ms);
    return true;
}

bool Session::undo(double now_ms) {
    const bool was_finished = run.phase() == Run::Phase::Finished;
    if (!run.undo()) return false;
    ls("unsplit");
    if (was_finished) save();
    say("Undid " + run.route.segs[std::size_t(run.index())].name, now_ms);
    return true;
}

void Session::reset(double now_ms) {
    if (run.phase() == Run::Phase::Idle) return;
    run.reset();
    save();                                  // the golds of this attempt
    ls("reset");
    say("Run reset", now_ms);
}

void Session::set_enabled(bool on, double now_ms) {
    if (cfg.enabled && !on) reset(now_ms);
    cfg.enabled = on;
    if (on && !loaded_) reload();
}

std::vector<std::string> Session::modifications(std::vector<bool>* active_now) const {
    std::vector<std::string> now = active_modifications(), all = now;
    for (const std::string& s : run.mods_used()) add_unique(all, s);
    if (active_now) {
        active_now->clear();
        for (const std::string& s : all) active_now->push_back(std::find(now.begin(), now.end(), s) != now.end());
    }
    return all;
}

// ---- the autosplitter block ---------------------------------------------------------------------
// The offsets SPEEDRUN.md §6 documents for ASL authors.
static_assert(offsetof(AslBlock, version) == 16 && offsetof(AslBlock, phase) == 20 &&
              offsetof(AslBlock, split_index) == 24 && offsetof(AslBlock, split_count) == 28 &&
              offsetof(AslBlock, igt_ticks) == 32 && offsetof(AslBlock, igt_ms) == 40 &&
              offsetof(AslBlock, rta_ms) == 48 && offsetof(AslBlock, split_seq) == 56 &&
              offsetof(AslBlock, modified) == 60 && offsetof(AslBlock, room) == 64 && sizeof(AslBlock) == 80,
              "the ASL block layout is a published interface");
namespace {
// Constant-initialised, so the magic lives only here in .data (no second copy for a scan to hit).
volatile AslBlock g_asl = {{'C', 'l', 'e', 'r', 'i', 'a', 'S', 'p', 'e', 'e', 'd', 'r', 'u', 'n', '0', '1'},
                           1u, 0u, 0, 0u, 0u, 0.0, 0.0, 0u, 0u, {}};
std::uint32_t g_last_index = 0, g_last_phase = 0;
}  // namespace

void publish(const Run& run, double now_ms, std::string_view room) {
    const std::uint32_t ph = std::uint32_t(run.phase());
    const std::uint32_t ix = std::uint32_t(run.index());
    if (ph != g_last_phase || ix != g_last_index) g_asl.split_seq = g_asl.split_seq + 1u;
    g_last_phase = ph;
    g_last_index = ix;
    g_asl.phase = ph;
    g_asl.split_index = run.index();
    g_asl.split_count = std::uint32_t(run.count());
    g_asl.igt_ticks = run.igt_ticks();
    g_asl.igt_ms = run.igt();
    g_asl.rta_ms = run.rta(now_ms);
    g_asl.modified = std::uint32_t(run.mods_used().size());
    for (std::size_t i = 0; i < sizeof g_asl.room; ++i) g_asl.room[i] = i < room.size() && i + 1 < sizeof g_asl.room ? room[i] : '\0';
}

const volatile AslBlock& asl_block() { return g_asl; }

}  // namespace cleria::speedrun
