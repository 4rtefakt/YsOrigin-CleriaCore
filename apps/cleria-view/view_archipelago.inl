// cleria-view — the Archipelago client in the live game (build/shell_re/AP_GAME.md). Textual part of
// main.cpp: #included after view_video.inl; ONE translation unit, not standalone.
//
// CleriaCore's own layer, not retail. The protocol is src/cleria/ap (Session / Client), the Ys Origin
// rules are src/cleria/ap/ys_game (Logic); this file binds them to the game: the session each frame,
// the grant filter (vanilla suppression + the relabelled treasure box), detection from the event bus,
// receiving items per tick, DeathLink, the goal, the save block in the content sidecar, the F1
// "Archipelago" tab, the Shell::hud overlays (status, item feed, tracker, the fog trap) and state.json.
// With no slot set nothing here opens a socket or changes a frame: every hook returns at once.
//
// The hook lines in view_game.inl (each a single call into this file):
//   render_window start   ap_startup              after content_startup: table, settings, uuid, filter, auto-connect
//   the shell's pages     ap_shell                the Archipelago tab, the hud layers, the speedrun provider
//   csMgr built           ap_bind                 the live game (GameIo) and the actions it needs
//   icons loaded          ap_seed_icons           the foreign-item icon in the two icon caches
//   each frame            ap_frame                Session::update and its events
//   each tick             ap_tick                 after content_tick: checks, grants, DeathLink, goal, traps
//   award_exp             ap_exp_factor           the seed's EXP multiplier
//   drive commands        ap_drive                apconnect / apsay= / apstate / apedit= / aptype= / apenter ...
#include <deque>
#include <random>
#include <sstream>

#include "cleria/ap/session.hpp"
#include "cleria/ap/ys_game.hpp"
#include "cleria/speedrun.hpp"

// The foreign-item icon's item id in the icon caches: outside 0..0x1FF, so it is never a g_flags cell
// nor a content mod's new item (0x100..0x1FF). [H]
constexpr int kApIcon = 0x2A0;

struct ApFeed {
    ap::Line line;
    double t = 0;                          // seconds (SDL clock) when it arrived
};

// What the view binds (ap_bind): the GameIo pointers plus the actions only the game loop can do.
struct ApBind {
    std::int32_t* flags = nullptr;
    std::int32_t* item_arr = nullptr;
    std::uint8_t* warp_on = nullptr;
    const int* character = nullptr;        // g_flags[150] as the view keeps it
    std::function<void(int)> add_sp, set_weapon_tier, set_level, warp;
    std::function<int()> level, weapon_tier;
    std::function<void()> refresh, kill;
    std::function<bool()> frozen, alive;
};

struct ApRt {
    bool started = false;
    std::optional<ap::ys::Table> table;
    std::unique_ptr<ap::FileDataPackageStore> store;
    std::unique_ptr<ap::Session> session;
    ap::ys::Logic logic;
    ap::ys::GameIo io;
    ApBind b;
    fs::path dir;                          // <settings folder>/archipelago ("" in a memory-only run)
    std::map<std::string, std::int64_t> chest_loc;   // "S_1001/S_BOX01" -> location id (the table's chests)
    // the game session
    bool in_game = false;                  // a NewGame / Load event was seen (the sidecar is current)
    bool adopted = false;                  // the save's AP state belongs to the connected room
    std::string mismatch;                  // why the save and the room do not match
    std::string room;
    int live_from = 0;                     // received() index from which items arrived live (traps fire)
    bool await_login_items = false;        // the ReceivedItems right after Connected sets live_from
    // events from the VM filter
    std::string fired_script;              // the script whose store last found a location (a non-chest box)
    std::int64_t fired_loc = 0;
    std::uint64_t ticks = 0, fired_tick = 0;
    // DeathLink
    bool death_pending = false;
    std::string death_from, death_cause;
    double death_at = -100;                // the last death sent or received (s), the 6-s debounce [MOD hook_ap.cpp:1906]
    bool death_ours = false;               // the next PlayerDeath was a received DeathLink: do not echo it
    std::uint64_t death_ours_tick = 0;
    // traps (CleriaCore ticks)
    int fog_t = 0, butter_t = 0, butter_tier = 0;
    int chaos_pending = 0, leech_pending = 0;
    std::mt19937 rng{0xA9u};               // [H] the Chaos Warp pick: its own stream, never the game's RNG
    // UI / output
    std::deque<ApFeed> feed;               // the overlay's lines (newest last)
    std::deque<std::string> log;           // the tab's recent events
    bool state_dirty = true;
    double state_at = -10;
    bool icons_ok = false;
};
static ApRt g_ap;

static double ap_now() { return double(SDL_GetTicksNS()) / 1.0e9; }

static void ap_log(const std::string& s) {
    std::printf("  [ap] %s\n", s.c_str());
    g_ap.log.push_back(s);
    while (g_ap.log.size() > 14) g_ap.log.pop_front();
    g_ap.state_dirty = true;
}

// A local line in the feed (one span, a colour).
static void ap_feed_text(const std::string& s, std::uint32_t rgb = 0xFFFFFF, bool colored = false) {
    ApFeed f;
    ap::Span sp;
    sp.text = s;
    sp.colored = colored;
    sp.rgb = rgb;
    f.line.spans.push_back(sp);
    f.t = ap_now();
    g_ap.feed.push_back(std::move(f));
    while (g_ap.feed.size() > 40) g_ap.feed.pop_front();
}

static apcfg::Settings& ap_cfg() { return settings().s.archipelago; }
static bool ap_connected() { return g_ap.session && g_ap.session->status() == ap::Session::Status::Connected; }
// The AP rules apply: a configured Logic (from the room or the save's cached slot_data) on a game whose
// save belongs to that seed.
static bool ap_active() { return g_ap.in_game && g_ap.adopted && g_ap.logic.configured(); }

// The content sidecar holds the save's AP block; a NewGame / Load still queued means it was just
// replaced and Logic::st is stale until that event arrives.
static bool ap_save_pending() {
    for (const events::Event& e : g_content.queued)
        if (e.kind == events::Kind::NewGame || e.kind == events::Kind::Load) return true;
    return false;
}
static void ap_store_state() {
    if (!g_ap.in_game || ap_save_pending()) return;
    g_content.sc.ap = g_ap.logic.st.to_kv();
    g_ap.state_dirty = true;
}

static std::string ap_upper(std::string s) {
    for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// ---- slot_data cache: an AP save can be played offline (checks kept, vanilla items still swallowed) --
static fs::path ap_slot_cache(const std::string& seed, int slot) {
    if (g_ap.dir.empty() || seed.empty()) return {};
    std::string s = seed;
    for (char& c : s) if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
    return g_ap.dir / ("slot_" + s + "_" + std::to_string(slot) + ".json");
}

// ---- checks -----------------------------------------------------------------------------------------
static void ap_report(const std::vector<std::int64_t>& ids, const char* how) {
    if (ids.empty()) return;
    for (std::int64_t id : ids) {
        const ap::ys::Reg* r = g_ap.logic.reg(id);
        ap_log(std::string("check ") + std::to_string(id) + " " + (r ? r->name : std::string("?")) + " (" + how + ")");
    }
    if (g_ap.session) g_ap.session->client().check_locations(ids);
    ap_store_state();
}

// ---- the save <-> the room ----------------------------------------------------------------------------
// Bring the save's AP block and the room together: stamp an unstamped save, refuse a save of another
// seed / slot (the session is closed so nothing is sent from it), configure from the cached slot_data
// when offline.
static void ap_sync_save() {
    ApRt& a = g_ap;
    if (!a.in_game) return;
    ap::ys::SaveState& st = a.logic.st;
    a.mismatch.clear();
    bool online = ap_connected();
    if (online) {
        ap::Client& c = a.session->client();
        if (st.empty()) {
            st.seed = c.seed_name();
            st.slot = c.slot();
            st.slot_name = c.player_name(c.slot());
            ap_log("save stamped: seed " + st.seed + " slot " + std::to_string(st.slot) + " (" + st.slot_name + ")");
        } else if (st.seed != c.seed_name() || st.slot != c.slot()) {
            a.mismatch = "this save belongs to seed " + st.seed + " slot " + std::to_string(st.slot) + "; the room is seed " +
                         c.seed_name() + " slot " + std::to_string(c.slot());
            ap_log("seed mismatch: " + a.mismatch + " -- disconnected");
            ap_feed_text("Archipelago: this save belongs to another seed. Disconnected.", 0xFA8072, true);
            a.session->disconnect();
            online = false;
        }
        if (online) {
            a.adopted = true;
            if (!st.checks.empty()) c.check_locations(std::vector<std::int64_t>(st.checks.begin(), st.checks.end()));
            if (!st.started && a.io.flags) ap_log("slot loadout: " + a.logic.start_loadout(a.io));   // start_items ...
            ap_report(a.logic.on_scene(ap::ys::scene_number(a.room)), "room");   // the room the game is in now
            const int want = a.logic.options().character;                         // slot_data 0..2 = g_flags[150] 1..3
            const int playing = a.b.character ? *a.b.character : 0;
            if (want >= 0 && playing >= 1 && want + 1 != playing) {
                static const char* kName[3] = {"Yunica", "Hugo", "Toal"};
                const std::string w = std::string("this seed is for ") + kName[std::clamp(want, 0, 2)] + "; the game is " +
                                      kName[std::clamp(playing - 1, 0, 2)] + "'s";
                ap_log("warning: " + w);
                ap_feed_text("Archipelago: " + w + ".", 0xE6B040, true);
            }
        }
    }
    if (!online) a.adopted = false;
    if (!online && !st.empty()) {
        // offline: the rules from the cached slot_data of this seed, so vanilla items stay swallowed
        const fs::path p = ap_slot_cache(st.seed, st.slot);
        std::error_code ec;
        if (!p.empty() && fs::exists(p, ec)) {
            std::ifstream f(p, std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            json::ParseOptions po;
            po.allow_nan = true;
            if (auto v = json::parse(ss.str(), nullptr, po)) {
                const ap::ys::SaveState keep = st;
                a.logic.configure(*v, *a.table);
                a.logic.st = keep;
                a.adopted = true;
                ap_log("offline: rules of seed " + st.seed + " from " + p.filename().string());
            }
        }
    }
    ap_store_state();
}

// NewGame / Load (the content sidecar was just set): this save's AP block.
static void ap_on_session(const events::Event& e) {
    ApRt& a = g_ap;
    a.in_game = true;
    const ap::ys::SaveState st = ap::ys::SaveState::from_kv(g_content.sc.ap);
    a.logic.st = st;
    a.logic.expected_hi = 0;
    a.death_pending = false;
    a.fog_t = a.butter_t = a.chaos_pending = a.leech_pending = 0;
    if (!st.empty() || (a.session && a.session->status() != ap::Session::Status::Idle))   // quiet when AP is unused
        ap_log(std::string(e.kind == events::Kind::Load ? "loaded save" : "new game") +
               (st.empty() ? ": no AP block" : ": seed " + st.seed + " applied " + std::to_string(st.applied) + " checks " +
                                                   std::to_string(st.checks.size())));
    ap_sync_save();
}

// ---- the connection ---------------------------------------------------------------------------------
static ap::Config ap_config() {
    const apcfg::Settings& c = ap_cfg();
    ap::Config cfg;
    cfg.name = c.slot;
    cfg.password = c.password;
    cfg.uuid = c.uuid;
    if (c.death_link) cfg.tags.push_back("DeathLink");
    if (g_ap.in_game && !g_ap.logic.st.seed.empty()) cfg.expected_seed = g_ap.logic.st.seed;   // the save's seed guard
    return cfg;
}

static void ap_connect() {
    ApRt& a = g_ap;
    if (!a.session) return;
    const apcfg::Settings& c = ap_cfg();
    if (c.slot.empty() || c.server.empty()) { ap_log("connect: no server / slot set"); return; }
    if (c.uuid.empty()) {                                   // one per install, made at the first connect (ARCHIPELAGO.md §7.3.6)
        ap_cfg().uuid = ap::make_uuid();
        settings().touch();
        settings().save();
    }
    a.mismatch.clear();
    ap_log("connecting to " + c.server + " as " + c.slot);   // never the password
    a.session->connect(c.server, ap_config());
}

static void ap_disconnect() {
    if (!g_ap.session) return;
    g_ap.session->disconnect();
    g_ap.adopted = false;
    ap_log("disconnected");
    ap_sync_save();                        // offline rules from the cache
}

static bool ap_death_link_on() {
    return ap_cfg().death_link || (g_ap.logic.configured() && g_ap.logic.options().death_link);
}

static void ap_on_connected() {
    ApRt& a = g_ap;
    ap::Client& c = a.session->client();
    const ap::ys::SaveState keep = a.logic.st;
    a.logic.configure(c.slot_data(), *a.table);
    a.logic.st = keep;
    ap_log("connected as " + c.player_name(c.slot()) + " (slot " + std::to_string(c.slot()) + ") seed " + c.seed_name() +
           ", " + std::to_string(a.logic.regs().size()) + " locations");
    for (const std::string& u : a.logic.options().unsupported) ap_log("not supported: " + u);
    // the slot_data for offline play of this seed's saves
    if (const fs::path p = ap_slot_cache(c.seed_name(), c.slot()); !p.empty()) {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
        std::ofstream f(p, std::ios::binary);
        f << json::write(c.slot_data());
    }
    // what every location holds, for the treasure box [MOD hook_ap.cpp:1754]
    std::vector<std::int64_t> ids;
    for (const ap::ys::Reg& r : a.logic.regs()) ids.push_back(r.id);
    c.scout_locations(ids);
    if (ap_death_link_on() && !c.has_tag("DeathLink")) c.set_tags({"DeathLink"});
    ap_sync_save();
}

static void ap_handle(const ap::Event& e) {
    ApRt& a = g_ap;
    using K = ap::Event::Kind;
    switch (e.kind) {
        case K::Connected: a.await_login_items = true; ap_on_connected(); break;
        case K::SeedMismatch:
            a.mismatch = "the room is seed " + e.text + "; this save belongs to seed " + a.logic.st.seed;
            ap_log("seed mismatch: " + a.mismatch);
            ap_feed_text("Archipelago: this save belongs to another seed.", 0xFA8072, true);
            break;
        case K::Refused: {
            std::string why;
            for (const std::string& er : e.errors) why += (why.empty() ? "" : ", ") + ap::describe_refusal(er);
            ap_log("refused: " + why);
            break;
        }
        case K::ItemsReset:
            // the login list arrived before this session: a trap in it already happened [H]. A later index-0
            // packet (the first item of an empty list, a resync) keeps the mark.
            if (a.await_login_items) a.live_from = int(a.session->client().received().size());
            a.await_login_items = false;
            ap_log("received list: " + std::to_string(a.live_from) + " item(s)");
            break;
        case K::ItemsReceived: ap_log("received " + std::to_string(e.items.size()) + " item(s) at #" + std::to_string(e.index)); break;
        case K::LocationInfo: ap_log("scouted " + std::to_string(e.items.size()) + " location(s)"); break;
        case K::Print: {
            ApFeed f;
            f.line = e.line;
            f.t = ap_now();
            std::printf("  [ap] print %s: %s\n", e.line.type.c_str(), e.line.plain().c_str());
            a.feed.push_back(std::move(f));
            while (a.feed.size() > 40) a.feed.pop_front();
            break;
        }
        case K::DeathLink:
            if (!ap_death_link_on()) break;
            a.death_pending = true;
            a.death_from = e.source;
            a.death_cause = e.text;
            ap_log("deathlink from " + e.source + (e.text.empty() ? "" : ": " + e.text));
            break;
        case K::InvalidPacket: ap_log("server: invalid packet: " + e.text); break;
        case K::ProtocolError: ap_log("protocol error: " + e.text); break;
        default: break;
    }
    a.state_dirty = true;
}

// ---- the grant filter (the VM's item stores, the get-item icon, the treasure box) ------------------
static std::string ap_rgb_tag(std::uint32_t rgb) {
    char b[24];
    std::snprintf(b, sizeof b, "<color:0xff%06x>", rgb & 0xFFFFFFu);
    return b;
}
static std::uint32_t ap_class_rgb(int flags) {   // the AP text colours (ARCHIPELAGO.md §3.5)
    if (flags & ap::kTrap) return 0xFA8072;
    if (flags & ap::kProgression) return 0xAF99EF;
    if (flags & ap::kUseful) return 0x6D8BE8;
    return 0x00EEEE;
}

// The icon of a scouted item: our own item's INVINFO icon when it has one, else the AP icon.
static int ap_icon_of(const ap::NetworkItem& it) {
    ApRt& a = g_ap;
    if (!a.session || it.player != a.session->client().slot()) return kApIcon;
    const ap::ys::Item* ti = a.table->item(it.item);
    if (!ti) return kApIcon;
    using IK = ap::ys::ItemKind;
    switch (ti->kind) {
        case IK::Item: case IK::CleriaOre: return ti->flag >= 0 && ti->flag < 0x80 ? ti->flag : kApIcon;
        case IK::Sp: return 0x78;                                        // [H] a gold piece
        case IK::ProgressiveSkill: return ti->artifact >= 0 && ti->artifact < 0x80 ? ti->artifact : kApIcon;
        case IK::ProgressiveGear: {
            const char* ch = g_ap.io.character == 2 ? "hugo" : g_ap.io.character == 3 ? "toal" : "yunica";
            const auto l = ti->ladder.find(ch);
            if (l != ti->ladder.end() && a.io.flags)
                for (int c : l->second) if (c >= 0 && c < 0x80 && a.io.flags[c] < 1) return c;
            return kApIcon;
        }
        default: return kApIcon;
    }
}

// The location whose box this is: a chest by its key, else the location the same script just found.
static std::int64_t ap_box_location(const GrantQuery& q, const std::string& room) {
    ApRt& a = g_ap;
    if (!q.script.empty()) {
        const auto it = a.chest_loc.find(room + "/" + ap_upper(q.script));
        if (it != a.chest_loc.end() && a.logic.reg(it->second)) return it->second;
        if (q.script == a.fired_script && a.ticks - a.fired_tick < 600) return a.fired_loc;   // [H] 10 s
    }
    return 0;
}

static GrantDecision ap_filter(const GrantQuery& q, const std::string& room) {
    ApRt& a = g_ap;
    GrantDecision d;
    if (!ap_active()) return d;
    const std::string key = room + "/" + ap_upper(q.script);
    // A chest that is not an AP location (S_2005/S_BOX03) keeps its vanilla item (ARCHIPELAGO.md §5.4).
    const bool chest = g_content.chests.count(q.script) != 0;
    const bool ap_chest = chest && a.chest_loc.count(key) && a.logic.reg(a.chest_loc.at(key));
    if (q.kind == GrantQuery::Kind::Store) {
        if ((!chest || ap_chest) && a.logic.suppress_store(q.index, q.old_value, q.value)) {
            char b[96];
            std::snprintf(b, sizeof b, "suppressed g_flags[0x%X] %d -> %d (%s)", q.index, int(q.old_value), int(q.value), key.c_str());
            ap_log(b);
            d.action = GrantDecision::Action::Suppress;
            return d;
        }
        const std::vector<std::int64_t> f = a.logic.on_store(q.index, q.old_value, q.value);
        if (!f.empty()) {
            a.fired_script = q.script;
            a.fired_loc = f.back();
            a.fired_tick = a.ticks;
            ap_report(f, "store");
        }
        return d;
    }
    if (q.kind == GrantQuery::Kind::Icon && a.logic.suppress_give(q.index) && (!chest || ap_chest)) {
        d.action = GrantDecision::Action::Suppress;
        return d;
    }
    const std::int64_t loc = ap_box_location(q, room);
    if (!loc) return d;
    const auto& sc = a.session->client().scouted();
    const auto s = sc.find(loc);
    d.action = GrantDecision::Action::Replace;
    if (s == sc.end()) {                       // not scouted (offline): what it is shows at the server
        d.index = kApIcon;
        d.text = "Found an\\n" + ap_rgb_tag(0xF2EA8C) + "Archipelago item<color:>.";
    } else {
        const ap::NetworkItem& it = s->second;
        ap::Client& c = a.session->client();
        const std::string name = c.item_name(it.item, it.player);
        d.index = ap_icon_of(it);
        if (it.player == c.slot())
            d.text = content::treasure_text(q.text, name, 1);
        else
            d.text = "Sent\\n" + ap_rgb_tag(ap_class_rgb(it.flags)) + name + "<color:> to " + c.player_name(it.player) + ".";
    }
    if (q.kind == GrantQuery::Kind::Window) {
        std::string plain = d.text;
        for (std::size_t p; (p = plain.find("<color:")) != std::string::npos;) plain.erase(p, plain.find('>', p) - p + 1);
        ap_log("box " + key + ": " + plain);
    }
    return d;
}

// ---- receiving, per tick ----------------------------------------------------------------------------
static void ap_run_trap(const std::string& t) {
    ApRt& a = g_ap;
    // [MOD hook_ap.cpp:658-673, 2686-2726]: EXP Leech -1 level, Chaos Warp a random unlocked statue,
    // Butterfingers the weapon at tier 0 for 8 s, Blinding Fog a haze for 30 s.
    if (t == "EXP Leech") ++a.leech_pending;
    else if (t == "Chaos Warp") ++a.chaos_pending;
    else if (t == "Butterfingers") {
        if (a.butter_t <= 0 && a.b.weapon_tier) a.butter_tier = a.b.weapon_tier();
        a.butter_t = 8 * 60;
        if (a.b.set_weapon_tier) a.b.set_weapon_tier(0);
    } else if (t == "Blinding Fog") a.fog_t = 30 * 60;
    ap_feed_text("Trap: " + t, 0xFA8072, true);
}

static void ap_grant_pending() {
    ApRt& a = g_ap;
    ap::Client& c = a.session->client();
    if (!c.items_in_sync()) return;
    const auto& rec = c.received();
    ap::ys::SaveState& st = a.logic.st;
    bool any = false;
    while (st.applied < int(rec.size())) {
        const ap::NetworkItem& it = rec[std::size_t(st.applied)];
        const int index = st.applied;
        const ap::ys::Logic::Grant g = a.logic.grant(it.item, it.flags, a.io);
        ++st.applied;
        any = true;
        ap_log("grant #" + std::to_string(index) + " " + g.text + " (from " + c.player_name(it.player) + ")");
        if (!g.trap.empty()) {
            if (index < a.live_from) ap_log("trap " + g.trap + " arrived while away: skipped");   // [H] like the mod's replay rule
            else ap_run_trap(g.trap);
        }
        const ap::ys::Item* ti = a.table->item(it.item);
        if (ti && ti->kind == ap::ys::ItemKind::Item && ti->flag >= 0) {
            events::Event ev;
            ev.kind = events::Kind::ItemObtained;
            ev.item = ti->flag;
            ev.count = 1;
            ev.source = "ap";
            content_emit(ev);
        }
    }
    if (any) ap_store_state();
}

// ---- hooks ------------------------------------------------------------------------------------------
static void ap_on_room(const events::Event& e) {
    ApRt& a = g_ap;
    a.room = e.room;
    if (!ap_active()) return;
    ap_report(a.logic.on_scene(ap::ys::scene_number(e.room)), "room");
}

static void ap_on_death(const events::Event&) {
    ApRt& a = g_ap;
    const bool ours = a.death_ours && a.ticks - a.death_ours_tick < 120;
    a.death_ours = false;
    if (ours) return;                                       // a received DeathLink is not sent back
    if (!ap_connected() || !ap_death_link_on() || !a.adopted) return;
    const double now = ap_now();
    if (now - a.death_at < 6.0) return;                     // the mod's debounce [MOD hook_ap.cpp:1906]
    a.death_at = now;
    ap::Client& c = a.session->client();
    const double unix_now = double(std::time(nullptr));
    c.send_death(unix_now, c.player_name(c.slot()) + " ran out of HP");
    ap_log("deathlink sent");
}

static void ap_startup() {
    ApRt& a = g_ap;
    if (a.started) return;
    a.started = true;
    std::string err;
    a.table = ap::ys::load(res_file("ap/ys_origin.json").string(), &err);
    if (!a.table) { std::printf("[ap] no location table (%s): Archipelago off\n", err.c_str()); return; }
    for (const ap::ys::Location& l : a.table->locations)
        if (!l.script.empty() || l.box_flag >= 0) a.chest_loc[ap_upper(l.key)] = l.id;
    if (!settings().memory_only() && !settings().path().empty()) a.dir = settings().path().parent_path() / "archipelago";
    a.store = std::make_unique<ap::FileDataPackageStore>(a.dir.empty() ? std::string() : (a.dir / "datapackage").string());
    a.session = std::make_unique<ap::Session>(a.dir.empty() ? nullptr : a.store.get());
    content_add_filter(ap_filter);
    events::bus().subscribe(events::Kind::RoomEnter, ap_on_room);
    events::bus().subscribe(events::Kind::NewGame, ap_on_session);
    events::bus().subscribe(events::Kind::Load, ap_on_session);
    events::bus().subscribe(events::Kind::PlayerDeath, ap_on_death);
    speedrun::set_provider("archipelago", [](std::vector<std::string>& out) {   // a session is a modification
        if ((g_ap.session && g_ap.session->status() != ap::Session::Status::Idle) || !g_ap.logic.st.empty())
            out.push_back("Archipelago session");
    });
    if (ap_cfg().auto_connect && !ap_cfg().slot.empty()) ap_connect();
}

static void ap_bind(ApBind b) {
    ApRt& a = g_ap;
    a.b = std::move(b);
    a.io.flags = a.b.flags;
    a.io.item_arr = a.b.item_arr;
    a.io.warp_on = a.b.warp_on;
    a.io.add_sp = a.b.add_sp;
    a.io.set_weapon_tier = a.b.set_weapon_tier;
    a.io.level = a.b.level;
    a.io.set_level = a.b.set_level;
    a.io.refresh = a.b.refresh;
}

// The foreign-item icon in both icon caches (the rising chest sprite, the treasure box). A mod may
// replace it at IMAGE/AP_ITEM.DDS; the built-in one is res/ap/AP_ITEM.DDS (tools/make_ap_icon.py).
static void ap_seed_icons(Gpu& g, const fs::path& asset_root, std::unordered_map<int, SpriteTex>& sprite_cache,
                          std::unordered_map<int, SpriteTex>& book_cache, const std::function<SpriteTex(const fs::path&)>& book_load) {
    fs::path p = vfs::resolve(asset_root / "IMAGE" / "AP_ITEM.DDS");
    std::error_code ec;
    if (!fs::exists(p, ec)) p = res_file("ap/AP_ITEM.DDS");
    Dds d = load_dds(p);
    if (!d.ok()) { std::printf("[ap] no AP_ITEM.DDS\n"); return; }
    sprite_cache[kApIcon] = upload_rgba(g, d.width, d.height, d.data.data());
    book_cache[kApIcon] = book_load(p);
    g_ap.icons_ok = true;
}

static float ap_exp_factor(int level) { return ap_active() ? g_ap.logic.exp_factor(level) : 1.0f; }

static void ap_frame() {
    ApRt& a = g_ap;
    if (!a.session) return;
    a.session->update(ap_now());
    for (const ap::Event& e : a.session->take_events()) ap_handle(e);
}

static void ap_write_state();

static void ap_tick() {
    ApRt& a = g_ap;
    ++a.ticks;
    if (a.fog_t > 0) --a.fog_t;
    if (!a.session || !a.in_game || ap_save_pending()) { ap_write_state(); return; }
    a.io.character = a.b.character ? *a.b.character : 1;
    const bool frozen = a.b.frozen && a.b.frozen();
    if (a.butter_t > 0 && --a.butter_t == 0 && a.b.set_weapon_tier) a.b.set_weapon_tier(a.butter_tier);
    if (ap_active()) {
        ap_report(a.logic.sweep(a.io), "sweep");             // flags set behind the VM (a safety net)
        if (ap_connected() && !frozen) ap_grant_pending();   // never into a cutscene (ARCHIPELAGO.md §7.2)
        if (a.butter_t <= 0) a.logic.enforce(a.io);
        if (!frozen) {
            if (const int lv = a.b.level ? a.logic.level_floor(a.b.level()) : 0; lv > 0 && a.b.set_level) {
                a.b.set_level(lv);
                ap_log("level floor: Lv " + std::to_string(lv));
            }
            if (a.leech_pending > 0 && a.b.level && a.b.set_level) {
                --a.leech_pending;
                if (a.b.level() > 1) a.b.set_level(a.b.level() - 1);
            }
            if (a.chaos_pending > 0 && a.b.warp && a.io.warp_on) {
                --a.chaos_pending;
                std::vector<int> on;
                for (int i = 0; i < 22; ++i) if (a.io.warp_on[i]) on.push_back(i);
                if (!on.empty()) {
                    const int w = on[std::uniform_int_distribution<int>(0, int(on.size()) - 1)(a.rng)];
                    ap_log("chaos warp to S_" + std::to_string(ap::ys::scene_of_warp_index(w)));
                    a.b.warp(w);
                }
            }
        }
        // goal (ARCHIPELAGO.md §7.6)
        if (!a.logic.st.goal_sent && a.logic.goal_reached(a.io.flags, a.logic.scene)) {
            a.logic.st.goal_sent = true;
            a.session->client().set_status(ap::ClientStatus::Goal);
            ap_log("goal reached: StatusUpdate(Goal)");
            ap_feed_text("Goal complete!", 0x00FF7F, true);
            ap_store_state();
        }
        // DeathLink in: on the next tick in play, not in a cutscene (ARCHIPELAGO.md §7.7)
        // [H] not in a story duel either (g_flags[133]: a death there only ends the fight)
        if (a.death_pending && !frozen && a.io.flags[133] == 0 && a.b.alive && a.b.alive()) {
            a.death_pending = false;
            a.death_ours = true;
            a.death_ours_tick = a.ticks;
            a.death_at = ap_now();
            ap_feed_text("DeathLink: " + (a.death_cause.empty() ? a.death_from + " died" : a.death_cause), 0xFA8072, true);
            if (a.b.kill) a.b.kill();
        }
    }
    ap_write_state();
}

// ---- state.json for external trackers / stream overlays (MODS.md §8.4) -------------------------------
static void ap_write_state() {
    ApRt& a = g_ap;
    if (!a.state_dirty || a.dir.empty() || !ap_cfg().export_state || !a.session) return;
    const double now = ap_now();
    if (now - a.state_at < 1.0) return;                     // [H] at most once a second
    a.state_at = now;
    a.state_dirty = false;
    const ap::Client& c = a.session->client();
    json::Value s;
    s["format"] = 1;
    s["game"] = "Ys Origin";
    s["status"] = a.session->status_text(now);
    s["connected"] = ap_connected();
    s["seed"] = a.logic.st.seed.empty() ? c.seed_name() : a.logic.st.seed;
    s["slot"] = a.logic.st.slot ? a.logic.st.slot : c.slot();
    s["slot_name"] = ap_cfg().slot;
    s["room"] = a.room;
    s["scene"] = a.logic.scene;
    s["goal"] = a.logic.st.goal_sent;
    s["death_link"] = ap_death_link_on();
    s["applied"] = a.logic.st.applied;
    json::Value rec = json::Value::array();
    int i = 0;
    for (const ap::NetworkItem& it : c.received()) {
        json::Value r;
        r["index"] = i++;
        r["item"] = it.item;
        r["name"] = c.item_name(it.item, c.slot());
        r["from"] = c.player_name(it.player);
        r["flags"] = it.flags;
        rec.push_back(std::move(r));
    }
    s["received"] = std::move(rec);
    json::Value chk = json::Value::array(), miss = json::Value::array();
    for (std::int64_t id : a.logic.st.checks) chk.push_back(id);
    for (const ap::ys::Reg& r : a.logic.regs())
        if (!a.logic.st.checks.count(r.id) && !c.checked_locations().count(r.id)) miss.push_back(r.id);
    s["checked"] = std::move(chk);
    s["missing"] = std::move(miss);
    std::error_code ec;
    fs::create_directories(a.dir, ec);
    json::WriteOptions wo;
    wo.indent = 1;
    std::ofstream f(a.dir / "state.json", std::ios::binary);
    f << json::write(s, wo);
}

// ---- drawing ------------------------------------------------------------------------------------------
static ImU32 ap_col(std::uint32_t rgb, float alpha) {
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, int(255.0f * std::clamp(alpha, 0.0f, 1.0f)));
}

// The logo of tools/make_ap_icon.py, live (the same geometry): a gold-rimmed night-blue disc, five
// coloured islands linked to a pale centre.
static void ap_draw_logo(ImDrawList* dl, ImVec2 c, float size, float alpha = 1.0f) {
    const float u = size;
    dl->AddCircleFilled(c, 0.47f * u, ap_col(0xD6B03E, alpha), 40);
    dl->AddCircleFilled(c, (0.47f - 0.045f) * u, ap_col(0x1B2240, alpha), 40);
    static const std::uint32_t isl[5] = {0xAF99EF, 0x6D8BE8, 0xFA8072, 0x00EEEE, 0x00FF7F};
    for (int k = 0; k < 5; ++k) {
        const float ang = (-90.0f + 72.0f * float(k)) * 3.14159265f / 180.0f;
        const ImVec2 p{c.x + 0.265f * u * std::cos(ang), c.y + 0.265f * u * std::sin(ang)};
        dl->AddLine(c, p, ap_col(0xC4CCE8, alpha), std::max(1.0f, 0.035f * u));
        dl->AddCircleFilled(p, 0.1f * u, ap_col(0x14182C, alpha), 20);
        dl->AddCircleFilled(p, 0.08f * u, ap_col(isl[k], alpha), 20);
    }
    dl->AddCircleFilled(c, 0.075f * u, ap_col(0xF4F0E6, alpha), 20);
}

// Lay out a Line's spans in a box of width `maxw` from `p` (word wrap); returns the height used.
static float ap_draw_line(ImDrawList* dl, const ap::Line& ln, ImVec2 p, float maxw, float px, float alpha, bool draw) {
    float x = 0, y = 0;
    const float lh = px * 1.25f;
    for (const ap::Span& sp : ln.spans) {
        const ImU32 col = ap_col(sp.colored ? sp.rgb : 0xEAE6DA, alpha);
        ImFont* f = sp.kind == ap::Span::Kind::Player || sp.kind == ap::Span::Kind::Item || sp.bold ? ui::F.medium : ui::F.body;
        std::size_t i = 0;
        while (i < sp.text.size()) {
            std::size_t j = sp.text.find(' ', i);
            j = j == std::string::npos ? sp.text.size() : j + 1;
            const std::string w = sp.text.substr(i, j - i);
            const float ww = ui::text_size(f, px, w.c_str()).x;
            if (x > 0 && x + ww > maxw) { x = 0; y += lh; }
            if (draw) ui::text(dl, f, px, {p.x + x, p.y + y}, col, w.c_str());
            x += ww;
            i = j;
        }
    }
    return y + lh;
}

// Shell::hud "ap.feed": the last lines (items sent / received, chat, DeathLink), fading out. [H] 10 s, 6 lines.
static void ap_hud_feed(ImDrawList* dl, float w, float h) {
    ApRt& a = g_ap;
    if (!ap_cfg().show_feed || a.feed.empty()) return;
    const double now = ap_now();
    const float S = ui::S, px = 16.0f * S, bw = 430.0f * S, pad = 10.0f * S;
    float y = h * 0.24f;
    int shown = 0;
    for (auto it = a.feed.rbegin(); it != a.feed.rend() && shown < 6; ++it) {
        const double age = now - it->t;
        if (age > 10.0) break;
        ++shown;
    }
    if (!shown) return;
    for (std::size_t k = a.feed.size() - std::size_t(shown); k < a.feed.size(); ++k) {
        const ApFeed& f = a.feed[k];
        const float alpha = std::clamp(float(10.0 - (now - f.t)), 0.0f, 1.0f);
        const float th = ap_draw_line(dl, f.line, {0, 0}, bw - 2 * pad - 28.0f * S, px, alpha, false);
        const ImVec2 b0{w - bw - 18.0f * S, y}, b1{w - 18.0f * S, y + th + 2 * pad * 0.7f};
        dl->AddRectFilled(b0, b1, IM_COL32(16, 15, 11, int(200 * alpha)), 5.0f * S);
        dl->AddRect(b0, b1, IM_COL32(156, 132, 66, int(150 * alpha)), 5.0f * S, 0, 1.0f * S);
        ap_draw_logo(dl, {b0.x + pad + 10.0f * S, b0.y + pad * 0.7f + px * 0.6f}, 20.0f * S, alpha);
        ap_draw_line(dl, f.line, {b0.x + pad + 26.0f * S, b0.y + pad * 0.7f}, bw - 2 * pad - 28.0f * S, px, alpha, true);
        y = b1.y + 6.0f * S;
    }
}

// Shell::hud "ap.status": the connection dot + line, top left.
static void ap_hud_status(ImDrawList* dl, float w, float h) {
    (void)w; (void)h;
    ApRt& a = g_ap;
    if (!ap_cfg().show_status || !a.session) return;
    using St = ap::Session::Status;
    const St s = a.session->status();
    if (s == St::Idle && a.logic.st.empty()) return;
    const float S = ui::S, px = 14.5f * S;
    std::string t = "AP  " + (a.mismatch.empty() ? a.session->status_text(ap_now()) : a.mismatch);
    if (s == St::Connected && a.logic.configured())
        t += "  \xE2\x80\xA2  " + std::to_string(a.logic.st.checks.size()) + "/" + std::to_string(a.logic.regs().size()) + " checks";
    const ImU32 dot = s == St::Connected ? (a.mismatch.empty() ? ui::kGood : ui::kWarn)
                    : (s == St::Refused || s == St::Disconnected) ? IM_COL32(230, 96, 80, 255) : ui::kWarn;
    const ImVec2 ts = ui::text_size(ui::F.body, px, t.c_str());
    const ImVec2 b0{14.0f * S, 12.0f * S}, b1{b0.x + ts.x + 34.0f * S, b0.y + 26.0f * S};   // top left: the game's HUD is below
    dl->AddRectFilled(b0, b1, IM_COL32(16, 15, 11, 170), 4.0f * S);
    dl->AddCircleFilled({b0.x + 12.0f * S, (b0.y + b1.y) * 0.5f}, 4.5f * S, dot);
    ui::text(dl, ui::F.body, px, {b0.x + 24.0f * S, (b0.y + b1.y) * 0.5f - px * 0.58f}, IM_COL32(234, 230, 218, 230), t.c_str());
}

// Shell::hud "ap.tracker": checked / total per zone of the active locations, left middle.
static void ap_hud_tracker(ImDrawList* dl, float w, float h) {
    (void)w;
    ApRt& a = g_ap;
    if (!ap_cfg().show_tracker || !a.logic.configured()) return;
    std::map<std::string, std::pair<int, int>> zones;   // zone -> checked, total
    std::map<std::string, int> lowest;                  // zone -> its lowest tower floor (the tower order)
    for (const ap::ys::Reg& r : a.logic.regs()) {
        const ap::ys::Location* l = a.table->location(r.id);
        const std::string z = l && !l->zone.empty() ? l->zone : "Other";
        const int fl = l ? std::atoi(l->floor.c_str()) : 0;
        auto lo = lowest.emplace(z, 999).first;
        if (fl > 0) lo->second = std::min(lo->second, fl);
        auto& c = zones[z];
        ++c.second;
        c.first += a.logic.st.checks.count(r.id) ? 1 : 0;
    }
    std::vector<std::string> order;
    for (const auto& [z, n] : zones) order.push_back(z);
    std::stable_sort(order.begin(), order.end(), [&](const std::string& x, const std::string& y) { return lowest[x] < lowest[y]; });
    const float S = ui::S, px = 14.0f * S, lh = 19.0f * S;
    const ImVec2 b0{14.0f * S, h * 0.30f}, b1{b0.x + 250.0f * S, b0.y + 34.0f * S + lh * float(order.size())};
    dl->AddRectFilled(b0, b1, IM_COL32(16, 15, 11, 185), 5.0f * S);
    dl->AddRect(b0, b1, IM_COL32(156, 132, 66, 150), 5.0f * S, 0, 1.0f * S);
    ui::text(dl, ui::F.cond, 13.5f * S, {b0.x + 12.0f * S, b0.y + 8.0f * S}, ui::kSection, "ARCHIPELAGO CHECKS");
    float y = b0.y + 30.0f * S;
    for (const std::string& z : order) {
        const auto [got, all] = zones[z];
        ui::text(dl, ui::F.body, px, {b0.x + 12.0f * S, y}, got == all ? ui::kGood : ui::kValue, z.c_str());
        const std::string n = std::to_string(got) + " / " + std::to_string(all);
        const float nw = ui::text_size(ui::F.body, px, n.c_str()).x;
        ui::text(dl, ui::F.body, px, {b1.x - 12.0f * S - nw, y}, got == all ? ui::kGood : ui::kValue, n.c_str());
        y += lh;
    }
}

// Shell::hud "ap.fog": the Blinding Fog trap, a grey haze fading in and out.
static void ap_hud_fog(ImDrawList* dl, float w, float h) {
    const int t = g_ap.fog_t;
    if (t <= 0) return;
    const float k = std::min({1.0f, float(t) / 60.0f, float(30 * 60 - t) / 60.0f});
    dl->AddRectFilled({0, 0}, {w, h}, IM_COL32(196, 200, 206, int(205.0f * k)));
}

// ---- the F1 "Archipelago" tab ---------------------------------------------------------------------
static void ap_page(ui::List& L, const std::function<void()>& persist) {
    ApRt& a = g_ap;
    apcfg::Settings& c = ap_cfg();
    if (!a.table) { ui::info(L, "Status", "res/ap/ys_origin.json is missing", ui::kWarn); return; }
    using St = ap::Session::Status;
    const St s = a.session->status();
    ui::section(L, "Connection");
    {
        const std::string st = a.mismatch.empty() ? a.session->status_text(ap_now()) : a.mismatch;
        ui::info(L, "Status", st.c_str(), s == St::Connected && a.mismatch.empty() ? ui::kGood
                                         : s == St::Idle ? ui::kMuted : ui::kWarn,
                 "The connection to the Archipelago server. Archipelago is CleriaCore's multiworld randomizer client "
                 "for the Ys Origin apworld. Not a retail feature.");
    }
    const bool editable = s == St::Idle || s == St::Refused || s == St::Disconnected;
    if (ui::text_field(L, "Server", &c.server,
                       "The room's address, e.g. archipelago.gg:38281 (the port is on the room page). ws:// or wss:// may "
                       "be given; without one the secure connection is tried first."))
        persist();
    if (ui::text_field(L, "Slot name", &c.slot, "Your player (slot) name in the multiworld, as in your YAML.")) persist();
    if (ui::text_field(L, "Password", &c.password, "The room password, if it has one. Stored in cleria.ini; never shown or logged.",
                       /*secret*/ true, "(none)"))
        persist();
    if (editable) {
        if (ui::button(L, "Connect", "Connect to the room with the server, slot name and password above.", !c.slot.empty()))
            ap_connect();
    } else if (ui::button(L, "Disconnect", "Close the connection. Checks made meanwhile are kept in the save and sent on the "
                                           "next connect.")) {
        ap_disconnect();
    }
    if (ui::toggle(L, "Connect at start", &c.auto_connect, "Connect to this room as soon as the game starts.")) persist();
    if (ui::toggle(L, "DeathLink", &c.death_link,
                   "Your deaths kill the other DeathLink players, and theirs kill you (not in a story duel). A seed made "
                   "with DeathLink turns it on too. Not a retail option.")) {
        persist();
        if (ap_connected()) a.session->client().set_tags(ap_death_link_on() ? std::vector<std::string>{"DeathLink"}
                                                                           : std::vector<std::string>{});
    }
    ui::section(L, "This game");
    const ap::ys::SaveState& st = a.logic.st;
    const ap::Client& cl = a.session->client();
    ui::info(L, "Seed", st.seed.empty() ? (ap_connected() ? cl.seed_name().c_str() : "-") : st.seed.c_str(), ui::kValue,
             "The seed this save belongs to. A save is stamped the first time it is played connected; loading it into "
             "another room's session is refused.");
    {
        const std::string v = std::to_string(st.applied) + " / " + std::to_string(cl.received().size());
        ui::info(L, "Items received", v.c_str(), ui::kValue,
                 "Items granted into this save / items the server has sent this slot. The count is saved with the game, "
                 "so a reload never grants an item twice.");
    }
    {
        const std::string v = std::to_string(st.checks.size()) + " / " + std::to_string(a.logic.regs().size());
        ui::info(L, "Locations checked", v.c_str(), ui::kValue, "Locations this save has found, of the seed's active ones.");
    }
    ui::info(L, "Goal", st.goal_sent ? "Complete" : "Not yet", st.goal_sent ? ui::kGood : ui::kMuted,
             a.logic.options().goal == 1 ? "Defeat every floor boss and the final boss." : "Defeat the final boss.");
    for (const std::string& u : a.logic.options().unsupported)
        ui::info(L, "Not supported", u.c_str(), ui::kWarn, "An option of this seed CleriaCore does not do yet (AP_GAME.md).");
    ui::section(L, "Overlays");
    if (ui::toggle(L, "Connection line", &c.show_status, "The connection status, top left.")) persist();
    if (ui::toggle(L, "Item feed", &c.show_feed, "Items sent and received, chat and DeathLinks, top right.")) persist();
    if (ui::toggle(L, "Tracker", &c.show_tracker, "Checked / total locations per area, left.")) persist();
    if (ui::toggle(L, "Tracker file", &c.export_state,
                   "Keeps archipelago/state.json (beside cleria.ini) up to date for external trackers and stream overlays."))
        persist();
    if (!a.log.empty()) {
        ui::section(L, "Recent events");
        int k = 0;
        for (auto it = a.log.rbegin(); it != a.log.rend() && k < 6; ++it, ++k) ui::info(L, "", it->c_str(), ui::kMuted, it->c_str());
    }
}

static void ap_shell(Shell& shell, const std::function<void()>& persist) {
    shell.panel.add("Archipelago", [persist](ui::List& L) { ap_page(L, persist); });
    shell.hud["ap.feed"] = ap_hud_feed;
    shell.hud["0ap.fog"] = ap_hud_fog;          // sorts first: the haze lies under every overlay
    shell.hud["ap.status"] = ap_hud_status;
    shell.hud["ap.tracker"] = ap_hud_tracker;
}

// ---- CLERIA_DRIVE ----------------------------------------------------------------------------------
//   apconnect[=address[,slot[,password]]]   connect (the settings' values by default)
//   apdisconnect | apsay=<text> | apstate | apfeed=<text>
//   apedit=<field label> | aptype=<text> | apenter      the tab's text fields
//   apflag=<i>                               print g_flags[i]
//   apopt=Key=value                          ShowFeed / ShowTracker / ShowStatus / DeathLink
static bool ap_drive(const std::string& cmd) {
    ApRt& a = g_ap;
    if (cmd.rfind("ap", 0) != 0 || !a.session) return false;
    if (cmd == "apconnect" || cmd.rfind("apconnect=", 0) == 0) {
        if (cmd.size() > 10) {
            std::stringstream ss(cmd.substr(10));
            std::string part;
            int i = 0;
            while (std::getline(ss, part, ',')) {
                if (i == 0) ap_cfg().server = part;
                if (i == 1) ap_cfg().slot = part;
                if (i == 2) ap_cfg().password = part;
                ++i;
            }
        }
        ap_connect();
        return true;
    }
    if (cmd == "apdisconnect") { ap_disconnect(); return true; }
    if (cmd.rfind("apsay=", 0) == 0) { a.session->client().say(cmd.substr(6)); return true; }
    if (cmd.rfind("apfeed=", 0) == 0) { ap_feed_text(cmd.substr(7)); return true; }
    if (cmd.rfind("apedit=", 0) == 0) { ui::drive_text_activate = cmd.substr(7); return true; }
    if (cmd.rfind("aptype=", 0) == 0) { ui::drive_text_input += cmd.substr(7); return true; }
    if (cmd == "apenter") { ui::drive_text_commit = true; return true; }
    if (cmd.rfind("apflag=", 0) == 0) {                 // apflag=<i>: print g_flags[i]
        const int i = int(std::strtol(cmd.c_str() + 7, nullptr, 0));
        if (a.io.flags && i >= 0 && i < 0x200) std::printf("  [ap] flag 0x%X = %d\n", i, int(a.io.flags[i]));
        return true;
    }
    if (cmd.rfind("apopt=", 0) == 0) {
        const std::string kv = cmd.substr(6);
        const std::size_t eq = kv.find('=');
        const bool on = eq != std::string::npos && kv.substr(eq + 1) != "0";
        const std::string k = kv.substr(0, eq);
        if (k == "ShowFeed") ap_cfg().show_feed = on;
        else if (k == "ShowTracker") ap_cfg().show_tracker = on;
        else if (k == "ShowStatus") ap_cfg().show_status = on;
        else if (k == "DeathLink") ap_cfg().death_link = on;
        return true;
    }
    if (cmd == "apstate") {
        const ap::Client& c = a.session->client();
        const ap::ys::SaveState& st = a.logic.st;
        std::printf("  [ap] state '%s' seed %s slot %d applied %d/%zu checks %zu goal %d active %d deathlink %d\n",
                    a.session->status_text(ap_now()).c_str(), st.seed.c_str(), st.slot, st.applied, c.received().size(),
                    st.checks.size(), int(st.goal_sent), int(ap_active()), int(ap_death_link_on()));
        return true;
    }
    return false;
}
