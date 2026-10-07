// CleriaCore's Speedrun mode (build/shell_re/SPEEDRUN.md): a run timer (RTA + IGT), splits with
// auto-split events taken from the live game, PB / best-segment comparisons kept in a splits file,
// the list of every non-retail option in use (so a run can be verified), a LiveSplit Server
// mirror (livesplit.hpp) and a signature-tagged state block for LiveSplit ASL autosplitters.
//
// Not part of the faithful game: nothing here changes gameplay. The only retail value it reads is
// the play-time step (flag 199 = 0x76bc38, FUN_004281c0 0x428274), which IGT mirrors.
//
// Pure logic, no SDL / ImGui: the time is passed in (milliseconds on any monotonic clock), so the
// tests drive it with a fake clock (apps/cleria-speedruntest).
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace cleria::speedrun {

// ---- active modifications: the extension point -------------------------------------------------
// Every feature that changes the game away from retail (a cheat, an assist, a QoL option that saves
// time, a mod) registers a provider under its own key; the provider appends one short line per
// option it has switched on right now ("Infinite HP", "Game speed 2.00x"). Speedrun mode shows the
// union on screen and remembers, for the current run, everything that was ever active.
//   cleria::speedrun::set_provider("cheats", [&](std::vector<std::string>& out) {
//       if (cheat_hp) out.push_back("Infinite HP"); });
using Provider = std::function<void(std::vector<std::string>&)>;
void set_provider(const std::string& key, Provider p);   // replaces the key's provider
void clear_provider(const std::string& key);
std::vector<std::string> active_modifications();       // every provider, in key order
// A one-off use of something non-retail (a debug key, a debug warp): remembered by the run that is
// going (a run-less note is dropped).
void note_modification(const std::string& what);
std::vector<std::string> take_notes();                 // drains the pending notes

// ---- times -------------------------------------------------------------------------------------
constexpr int kTickHz = 60;                             // the retail logic step (ACTOR.md §3)
// The retail play-time readout: ms = steps * 1000 / 60, integer (FUN_0043a340's __aulldiv).
inline std::uint64_t ticks_to_ms(std::uint64_t ticks) { return ticks * 1000u / kTickHz; }
// "1:02:03.45" / "2:03.45" / "0:03.45" (centiseconds); negative values get a '-'.
std::string format_time(double ms);
// The retail Play Time string "%d:%02d:%02d:%03d" (0x69a344): "0:02:03:456", hours capped at 9999.
std::string format_play_time(std::uint64_t ms);
// A comparison delta: "+1.2" / "-12.3" under a minute, "+1:02" / "-1:02:03" above.
std::string format_delta(double ms);

// ---- events and the split conditions --------------------------------------------------------------
// One thing that happened in the game this tick.
struct Event {
    enum Kind { Room, Boss, Flag } kind = Room;
    std::string from, to;      // Room: the room left / entered (folder names, "S_1004")
    std::string room;          // Boss: the room it happened in
    int count = 0;             // Boss: the n-th depletion during this visit of the room (1-based)
    int flag = -1;             // Flag: g_flags index
    int old_v = 0, new_v = 0;  // Flag: before / after
};
// A split's condition (the splits file's Event= value). Rooms compare case-blind; `*` = any.
//   enter:S_1005          any transition into S_1005
//   leave:S_1099          any transition out of S_1099
//   room:S_4099>S_4097    exactly that transition
//   boss:S_1099           a boss HP gauge empties in S_1099 (boss:S_7080#2 = its 2nd during the visit)
//   item:58               inventory id 58 obtained (g_flags[58] goes from < 1 to >= 1)
//   flag:310=1            g_flags[310] becomes 1
//   manual                never auto-splits (the Split button / LiveSplit's hotkey)
bool matches(std::string_view spec, const Event& e);
bool valid_spec(std::string_view spec);
std::string describe(const Event& e);                  // for the log / the overlay

// What the observer needs from one logic tick of the game.
struct GameView {
    std::string_view room;           // the current room folder ("S_1004")
    bool gauge_visible = false;      // the boss HP gauge is up (Hud::boss_frac >= 0)
    float gauge_hp = 0;              // its current value (Hud::boss_cur)
    const std::int32_t* flags = nullptr;   // g_flags[0x200]
    bool player_alive = true;
};

// Turns successive GameViews into Events. Rules (SPEEDRUN.md §4):
//  - Room: the room name changed between two ticks.
//  - Boss: the gauge was up with HP > 0 and now shows HP <= 0, or was withdrawn in the same room
//    (Velagunder withdraws it at HP < 1, a world-actor boss when its owner dies). Not after a player
//    death (a retry withdraws it too) and not on a lost duel (g_flags[133] set, 156 cleared). [H]
//  - Flag: any g_flags change; a tick that changes more than kBulkFlags flags (a save loaded, a new
//    game) is not reported. [H]
class Observer {
  public:
    static constexpr int kBulkFlags = 16;    // [H]
    void reset() { *this = Observer{}; }
    void step(const GameView& v, std::vector<Event>& out);
  private:
    bool have_ = false;
    std::string room_;
    bool gauge_ = false;
    float hp_ = 0;
    bool death_latch_ = false;
    int boss_count_ = 0;                     // depletions during this room visit
    std::vector<std::int32_t> flags_;
};

// ---- the splits file ---------------------------------------------------------------------------
// One per category, under <settings folder>/speedrun/<key>.splits, in the ini dialect:
//   [run] Category= Attempts= Completed=
//   [split.N] Name= Event= PbRta= PbIgt= GoldRta= GoldIgt=   (ms; -1 = none)
struct Segment {
    std::string name, event;
    double pb_rta = -1, pb_igt = -1;         // cumulative split time of the PB run
    double gold_rta = -1, gold_igt = -1;     // the best this segment was ever done in
};
struct Route {
    std::string category;
    int attempts = 0, completed = 0;
    std::vector<Segment> segs;
    bool load(const std::filesystem::path& p);
    bool save(const std::filesystem::path& p) const;
    std::string text() const;
    bool parse(std::string_view text);
};
// The built-in routes, from the community's split points (SPEEDRUN.md §3): character 1 Yunica,
// 2 Hugo, 3 Toal. `extra` = the Extra-mode categories (same splits).
Route default_route(int character, bool extra);
std::string category_name(int character, bool extra);   // "Any% (Yunica)" / "Any% (Toal EX)"
std::string category_key(int character, bool extra);    // "yunica" / "toal_ex"

// ---- the run -----------------------------------------------------------------------------------
enum class Timing { Rta = 0, Igt = 1 };
enum class Compare { PersonalBest = 0, BestSegments = 1 };

struct SplitTime { double rta = -1, igt = -1; bool gold = false; };   // -1 = skipped

class Run {
  public:
    enum class Phase { Idle, Running, Finished };
    Route route;
    Timing timing = Timing::Rta;             // which time decides a PB and the deltas

    Phase phase() const { return phase_; }
    bool running() const { return phase_ == Phase::Running; }
    int index() const { return int(times_.size()); }           // the split being run
    int count() const { return int(route.segs.size()); }
    const std::vector<SplitTime>& times() const { return times_; }
    const std::vector<std::string>& mods_used() const { return mods_used_; }

    void start(double now_ms);               // also counts the attempt
    void tick() { if (running()) ++igt_ticks_; }   // one retail play-time step
    bool split(double now_ms);               // true when it finished the run
    bool skip(double now_ms);
    bool undo();
    void reset();                            // back to Idle (kept: golds, a finished run's PB)
    void record_mods(const std::vector<std::string>& active);   // the run remembers them

    double rta(double now_ms) const;
    std::uint64_t igt_ticks() const { return igt_ticks_; }
    double igt() const { return double(ticks_to_ms(igt_ticks_)); }
    double now_time(double now_ms) const { return timing == Timing::Rta ? rta(now_ms) : igt(); }

    // Split i's time in the chosen timing (-1 none): this run's, and the comparison's. A run compares
    // against the PB and golds as they were when it started (as LiveSplit does), not the ones it set.
    double split_time(int i) const;
    double comparison(int i, Compare c) const;
    // This run's delta at split i (done) against the comparison; nullopt-like -1e30 when none.
    static constexpr double kNone = -1e30;
    double delta(int i, Compare c) const;
    double sum_of_best() const;              // -1 when a gold is missing
    bool new_pb() const { return new_pb_; }  // the finished run beat the PB (already written)

  private:
    Phase phase_ = Phase::Idle;
    double t0_ = 0, t_end_ = 0;
    std::uint64_t igt_ticks_ = 0;
    std::vector<SplitTime> times_;
    std::vector<std::pair<double, double>> old_golds_;   // for undo
    std::vector<std::string> mods_used_;
    Route pb_backup_;                                    // the route before the finish wrote a PB
    Route base_;                                         // the route as the run started: the comparisons
    bool new_pb_ = false;
    void finish(double now_ms);
};

// ---- the session: what cleria-view drives ------------------------------------------------------
struct Config {
    bool enabled = false;
    bool auto_start = true;                  // start on the New Game difficulty pick (the community rule)
    bool auto_split = true;
    Timing timing = Timing::Rta;
    Compare compare = Compare::PersonalBest;
};

class Session {
  public:
    Config cfg;
    Run run;
    std::filesystem::path dir;               // where the .splits files live ("" = never saved)
    // The LiveSplit mirror: every command line for LiveSplit Server (null = none).
    std::function<void(std::string_view)> livesplit;
    std::string notice;                      // the last split / event, for the overlay
    double notice_ms = -1e30;

    // The category (and its splits file) of the next run; ignored while a run is going.
    void select(int character, bool extra);
    int character() const { return character_; }
    bool extra() const { return extra_; }
    std::filesystem::path route_path() const;
    bool reload();                           // re-read the splits file (idle only)

    void new_game(int character, bool extra, double now_ms);   // the start rule (auto_start)
    void start(double now_ms);
    void tick(const GameView& v, double now_ms);   // one retail logic step
    void frame(double now_ms, std::string_view room);   // every rendered frame
    bool split(double now_ms);
    bool skip(double now_ms);
    bool undo(double now_ms);
    void reset(double now_ms);
    void set_enabled(bool on, double now_ms);
    void save() const;
    // Everything non-retail to show: the active ones, then those used earlier in the run.
    std::vector<std::string> modifications(std::vector<bool>* active_now = nullptr) const;

  private:
    Observer obs_;
    std::vector<Event> ev_;
    int character_ = 1;
    bool extra_ = false;
    bool loaded_ = false;
    std::uint64_t ls_ticks_ = ~0ull;         // the IGT last sent to LiveSplit
    void ls(std::string_view c) { if (livesplit) livesplit(c); }
    void say(const std::string& s, double now_ms) { notice = s; notice_ms = now_ms; }
};

// ---- the autosplitter block ---------------------------------------------------------------------
// A fixed, signature-tagged struct in cleria-view's .data for LiveSplit ASL scripts (SPEEDRUN.md §6):
// scan the main module for the 16 magic bytes "CleriaSpeedrun01" and read the fields after them.
struct AslBlock {
    char magic[16];                 // "CleriaSpeedrun01" (no terminator)
    std::uint32_t version;          // 1
    std::uint32_t phase;            // 0 idle, 1 running, 2 finished
    std::int32_t split_index;       // the split being run
    std::uint32_t split_count;
    std::uint64_t igt_ticks;        // retail play-time steps (60 Hz) since the run started
    double igt_ms;                  // ticks * 1000 / 60
    double rta_ms;                  // CleriaCore's own real-time clock
    std::uint32_t split_seq;        // +1 on every split / skip / undo / reset / start
    std::uint32_t modified;         // number of non-retail options used during the run
    char room[16];                  // the current room folder, NUL-terminated
};
void publish(const Run& run, double now_ms, std::string_view room);
const volatile AslBlock& asl_block();

}  // namespace cleria::speedrun
