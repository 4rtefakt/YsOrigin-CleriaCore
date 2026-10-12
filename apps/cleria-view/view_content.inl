// cleria-view — mod content (phase 2a): the save sidecar, the event bus, the grant filter and the
// content/*.ini patches wired into the live game (build/shell_re/CONTENT.md). Textual part of
// main.cpp: #included after view_mods.inl; ONE translation unit, not standalone.
//
// None of this is retail. With no content mod active: the grant filter passes every query (it only
// reports events), no item patch is installed, no pickup is placed, no sidecar file is written —
// the game, its saves and its frames are what they were before this file existed.
//
// The hook lines in view_game.inl (each a single call into this file):
//   render_window start   content_startup               after mods_startup: merge content/, patch items
//   title Load            content_title_load            the chosen slot's sidecar, applied at session start
//   session start         content_session_start         NewGame (fresh) or Load (title) -> the sidecar
//   spawn_enemies end     content_on_room               RoomEnter, the room's chests, mod pickups
//   csMgr built           content_bind / install        the grant filter on the live VM, the callbacks
//   per tick              content_tick                  queued events, the g_flags diff (FlagChanged)
//   [death]               content_on_death              PlayerDeath
//   the book's use        content_item_used             ItemUsed (the Panacea)
//   [duel] lost           content_duel_lost             a losable duel's KO: its 156 = 0 is not a win
//   do_save / book load / delete   content_save / content_load / content_delete   the sidecar file
//   pickup touch          content_touch_new / content_taken   new-item pickups, collected flags
//   script loads          content_script                text.ini on the string pool
//   drive commands        content_drive                 cstate / give= / csave= / cload=
#include <deque>

#include "cleria/content.hpp"
#include "cleria/mod_events.hpp"
#include "cleria/sidecar.hpp"

struct ContentRt {
    content::Content c;
    bool on = false;                        // some active mod has content (c non-empty)
    std::vector<std::string> stamp;         // "id version" of the mods that contributed content
    ItemTable items;                        // the patched INVINFO (names for the treasure box)
    Sidecar sc;                             // this game's sidecar (saved beside the slot)
    std::string room, prev_room;            // upper-case
    // the current room's chests: script instance name (lower) -> {box flag, object}
    std::map<std::string, std::pair<int, std::string>> chests;
    events::FlagWatch watch;
    std::vector<events::Event> queued;      // emitted at the next content_tick
    fs::path title_load;                    // a title Load's slot file (read at session start)
    // bound to the live game (content_bind)
    std::int32_t* flags = nullptr;
    std::string toast, toast_head;          // a rule's text, shown by content_hud
    int toast_t = 0;                        // ticks left
    std::function<void(int)> add_sp, heal_hp;
    std::function<void(int)> save_slot, load_slot;
    std::deque<std::string> log;            // the last events, for Mods > Content additions
    int rules_sub = 0;
    bool duel_lost = false;                 // FUN_0046f810's losable branch cleared 156 since the last tick
    std::string fight_room;                 // the room in which 156 last went 0 -> 1 (content_boss_watch)
    std::string defeated_room;              // ... and the floor-boss flags already raised in this room (one event a boss:
    unsigned defeated_mask = 0;             // Hugo's Pictimos sets 224 twice, issue #91)
    int room_age = 0;                       // ticks since the last room entry (cleria.game.save's gate)
};
static ContentRt g_content;

static void content_log(const std::string& s) {
    std::printf("  [content] %s\n", s.c_str());
    g_content.log.push_back(s);
    while (g_content.log.size() > 12) g_content.log.pop_front();
}

static std::string content_hex(int v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%X", v);
    return b;
}

static void content_item_patch(ItemTable& t) { content::patch_items(g_content.c, t); }

static void content_emit(events::Event e) {
    if (e.room.empty()) e.room = g_content.room;
    events::bus().emit(e);
}

// Grant an item by the inventory's rules (retail ids: the count cell, gold -> SP; new ids: by Kind,
// kept in the sidecar) and report it. `source` names who gave it ("rule", "chest", "pickup", "ap").
static void content_give(int id, int count, const std::string& source, const std::string& script = {}) {
    ContentRt& r = g_content;
    if (id < 0 || count < 1) return;
    if (id < content::kRetailItems) {
        const Item* it = r.items.get(id);
        if (id >= 0x78 && id <= 0x7f) {                     // the gold denominations: SP (FUN_00585ff0)
            if (r.add_sp && it) r.add_sp(int(it->magnitude) * count);
        } else if (r.flags) {
            r.flags[id] = content::add_count(r.flags[id], count);
        }
    } else if (id >= content::kNewItemFirst && id <= content::kNewItemLast) {
        const auto d = r.c.items.find(id);
        const std::string kind = d == r.c.items.end() ? std::string() : d->second.kind;
        const int amount = d == r.c.items.end() ? 0 : d->second.amount;
        if (kind == "heal") { if (r.heal_hp) r.heal_hp(amount * count); }
        else if (kind == "gold") { if (r.add_sp) r.add_sp(amount * count); }
        else {
            r.sc.add_item(id, count);
            if (kind == "flag" && d != r.c.items.end() && !d->second.sets_flag.empty()) {
                const std::optional<int> fi = content::parse_int(d->second.sets_flag);
                if (fi && *fi >= 0 && *fi < 0x200) { if (r.flags) r.flags[*fi] = 1; }
                else r.sc.set_flag(d->second.sets_flag, 1);
            }
        }
    } else {
        return;
    }
    content_log("item " + content_hex(id) + " x" + std::to_string(count) + " (" + r.items.name(id) + ") from " + source);
    events::Event e;
    e.kind = events::Kind::ItemObtained;
    e.item = id;
    e.count = count;
    e.source = source;
    e.script = script;
    content_emit(e);
}

// The rules: one bus subscriber that runs every content rule whose trigger matches.
static void content_rules_on(const events::Event& e) {
    ContentRt& r = g_content;
    for (const auto& [name, rule] : r.c.rules) {
        if (!content::rule_fires(rule, e, r.sc)) continue;
        content_log("rule " + name + " fires on " + events::kind_name(e.kind));
        content::RuleEnv env;
        env.flags = r.flags;
        env.sc = &r.sc;
        env.give = [](int item, int n) { content_give(item, n, "rule"); };
        env.message = [&](const std::string& m) {
            r.toast = m;
            r.toast_head = rule.mod;
            for (const mods::Mod& md : g_mods_ui.list)
                if (md.m.id == rule.mod) r.toast_head = md.label();
            r.toast_t = 300;                               // [H] 5 s
            content_log("text \"" + m + "\"");
        };
        content::run_rule(rule, env);
    }
}

// After mods_startup: every Active mod's content/ in load order.
static void content_startup(const fs::path& asset_root) {
    ContentRt& r = g_content;
    set_item_patch(nullptr);
    if (r.rules_sub) { events::bus().unsubscribe(r.rules_sub); r.rules_sub = 0; }
    r.c = {};
    r.stamp.clear();
    for (mods::Mod& m : g_mods_ui.list) {
        if (m.state != mods::State::Active) continue;
        const fs::path dir = m.root / "content";
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        if (m.m.api < 2) {
            m.notes.push_back("content/ ignored: it needs ApiVersion=2");
            continue;
        }
        const std::size_t e0 = r.c.errors.size();
        const int n = content::merge_dir(r.c, m.m.id, dir);
        r.stamp.push_back(m.m.id + " " + m.m.version);
        std::printf("[content] %s: %d file(s)\n", m.label().c_str(), n);
        for (std::size_t i = e0; i < r.c.errors.size(); ++i) m.notes.push_back("content error: " + r.c.errors[i].text);
    }
    for (const content::Note& n : r.c.notes)
        for (mods::Mod& m : g_mods_ui.list)
            if (m.m.id == n.mod) m.notes.push_back("content: " + n.text);
    for (const content::Note& e : r.c.errors) std::printf("[content]   %s: %s\n", e.mod.c_str(), e.text.c_str());
    for (const content::Note& n : r.c.notes) std::printf("[content]   %s: %s\n", n.mod.c_str(), n.text.c_str());
    r.on = !r.c.empty();
    if (r.on) set_item_patch(&content_item_patch);          // every ItemTable::load from here on
    r.items = ItemTable::load(asset_root / "MISC" / "INVINFO.DAT");   // names (patched when on)
    if (!r.on) return;
    if (!r.c.rules.empty()) r.rules_sub = events::bus().subscribe(content_rules_on);
    std::printf("[content] %zu item(s), %zu chest(s), %zu pickup(s), %zu rule(s), %zu text script(s)\n",
                r.c.items.size(), r.c.chests.size(), r.c.pickups.size(), r.c.rules.size(), r.c.text.size());
}

// text.ini on a script being loaded for the live VM (`p` = the path the loader built).
static std::shared_ptr<Xso> content_script(const fs::path& p) {
    auto x = std::make_shared<Xso>(Xso::parse(read_bytes(p)));
    if (g_content.on && !g_content.c.text.empty()) {
        const std::string vn = vfs::virtual_name(p);
        if (const int n = content::patch_script(g_content.c, vn, *x))
            content_log("text: " + std::to_string(n) + " string(s) of " + vn);
    }
    return x;
}

// The title's Load: the slot whose sidecar the session starts with.
static void content_title_load(const fs::path& slot_file) { g_content.title_load = slot_file; }

// A game session starts (FUN_0041fd10 ran; a title Load's flags are applied).
static void content_session_start(std::int32_t* flags, int character, bool loaded) {
    ContentRt& r = g_content;
    r.flags = flags;
    r.sc.clear();
    r.watch.primed = false;
    r.queued.clear();
    events::Event e;
    if (loaded && !r.title_load.empty()) {
        r.sc = read_sidecar(r.title_load);
        e.kind = events::Kind::Load;
    } else {
        e.kind = events::Kind::NewGame;
        e.character = character;
    }
    r.title_load.clear();
    r.queued.push_back(e);
}

// A NewGame / Load is still queued (emitted at the next content_tick): the sidecar was just replaced,
// and a layer that keeps per-save state must not write it yet (the Lua API's cleria.save.ready()).
static bool content_session_pending() {
    for (const events::Event& e : g_content.queued)
        if (e.kind == events::Kind::NewGame || e.kind == events::Kind::Load) return true;
    return false;
}

// Every room entry (the boot room, doors, script warps, loads): the event, the chests, the pickups.
static void content_on_room(const Room& room, std::vector<Pickup>& pickups, bool water) {
    ContentRt& r = g_content;
    std::string up = room.name;
    for (char& ch : up) ch = char(std::toupper((unsigned char)ch));
    r.prev_room = r.room;
    r.room = up;
    r.room_age = 0;
    r.chests.clear();
    for (const Interactable& it : room.mesh.interactables) {
        if (it.opened_flag < 0) continue;
        std::string n = it.script_name.empty() ? fs::path(it.script).stem().string() : it.script_name;
        for (char& ch : n) ch = char(std::tolower((unsigned char)ch));
        r.chests[n] = {it.opened_flag, it.name};
    }
    events::Event e;
    e.kind = events::Kind::RoomEnter;
    e.prev_room = r.prev_room;
    content_emit(e);
    if (!r.on) return;
    for (const auto& [id, pd] : r.c.pickups) {
        if (pd.room != up || r.sc.flag(pd.flag) != 0) continue;
        // [H] a resting, collectable, never-expiring pickup (flags 4 | 8 | 0x10) built without the spawner's
        // RNG draws, so a mod pickup does not shift the room's random stream.
        Pickup p;
        p.id = pd.item;
        p.flags = 0x1c;
        p.pos = p.prev = p.base = {pd.pos[0], pd.pos[1], pd.pos[2]};
        p.life = 990.0f;
        p.half = 0.9f * 0.315f;
        p.resting = true;
        p.in_water = water;
        p.content = int(std::distance(r.c.pickups.begin(), r.c.pickups.find(id)));
        pickups.push_back(p);
        content_log("pickup " + id + " (" + r.items.name(pd.item) + ") placed in " + up);
    }
}

static const content::PickupDef* content_pickup_def(int idx) {
    if (idx < 0 || idx >= int(g_content.c.pickups.size())) return nullptr;
    return &std::next(g_content.c.pickups.begin(), idx)->second;
}

// The touch of a pickup holding a NEW item (id >= 0x100): no g_flags cell, the content grant.
static bool content_touch_new(const Pickup& pk) {
    const content::PickupDef* pd = content_pickup_def(pk.content);
    content_give(pk.id, pd ? pd->count : 1, "pickup");
    return true;
}
// Any pickup taken: a mod pickup is marked collected; every pickup is reported.
static void content_taken(const Pickup& pk) {
    if (const content::PickupDef* pd = content_pickup_def(pk.content)) {
        g_content.sc.set_flag(pd->flag, 1);
        content_log("pickup " + pd->id + " collected");
        if (pk.id < content::kRetailItems && pd->count > 1 && g_content.flags)   // the touch gave one
            g_content.flags[pk.id] = content::add_count(g_content.flags[pk.id], pd->count - 1);
    }
    if (pk.id >= content::kNewItemFirst) return;            // reported by content_give
    events::Event e;
    e.kind = events::Kind::ItemObtained;
    e.item = pk.id;
    e.count = 1;
    e.source = "pickup";
    content_emit(e);
}

// More grant filters for other CleriaCore layers (the Lua mods): consulted in order BEFORE
// chests.ini; the first decision that is not Pass is the one taken (and that layer grants / reports
// what it substitutes itself, e.g. with content_give). `room` is the current room, upper-case.
using ContentFilter = std::function<GrantDecision(const GrantQuery&, const std::string& room)>;
static std::vector<ContentFilter> g_content_filters;
[[maybe_unused]] static void content_add_filter(ContentFilter f) { g_content_filters.push_back(std::move(f)); }

// The grant filter on the live VM (xso.hpp GrantQuery). Reports chests opened and items obtained;
// applies the added filters, then chests.ini when content is on.
static GrantDecision content_filter(const GrantQuery& q) {
    ContentRt& r = g_content;
    GrantDecision d;
    std::vector<content::Obtained> got;
    bool other = false;
    for (const ContentFilter& f : g_content_filters) {
        d = f(q, r.room);
        if (d.action != GrantDecision::Action::Pass) { other = true; break; }
    }
    if (!other && r.on) d = content::decide(r.c, q, r.room, &r.items, &got);
    if (q.kind == GrantQuery::Kind::Store) {
        const auto ch = r.chests.find(q.script);
        if (ch != r.chests.end() && ch->second.first == q.index && q.old_value == 0 && q.value != 0) {
            events::Event e;
            e.kind = events::Kind::ChestOpened;
            e.index = q.index;
            e.script = q.script;
            e.object = ch->second.second;
            std::string sn = q.script;
            for (char& c : sn) c = char(std::toupper((unsigned char)c));
            content_log("chest " + r.room + "/" + sn + " opened (flag " + std::to_string(q.index) + ")");
            content_emit(e);
        }
        // A locked door opened with its key or medallion: the room's `UseKey` script stores the door's flag
        // (then the key's cell = 0) [DATA: the twelve UseKey.XSO, S_1005 315 .. S_6053 472].
        std::string usekey = q.script;                      // "UseKey" as its starter spells it
        for (char& c : usekey) c = char(std::tolower((unsigned char)c));
        if (d.action == GrantDecision::Action::Pass && usekey == "usekey" && q.index >= content::kRetailItems &&
            q.old_value == 0 && q.value != 0) {
            events::Event e;
            e.kind = events::Kind::DoorOpened;
            e.index = q.index;
            e.script = usekey;
            content_log("door_opened " + r.room + " flag " + std::to_string(q.index));
            content_emit(e);
        }
        if (d.action == GrantDecision::Action::Pass && q.index >= 0 && q.index < content::kRetailItems &&
            q.value > std::max(q.old_value, 0)) {
            events::Event e;
            e.kind = events::Kind::ItemObtained;
            e.item = q.index;
            e.count = q.value - std::max(q.old_value, 0);
            e.source = r.chests.count(q.script) ? "chest" : "script";
            e.script = q.script;
            content_emit(e);
        }
        if (d.action != GrantDecision::Action::Pass) {
            const content::ChestDef* cd = content::find_chest(r.c, r.room, q.script, q.index);
            content_log("chest " + (cd ? cd->key : r.room + "/" + q.script) + ": grant of " + content_hex(q.index) +
                        (d.action == GrantDecision::Action::Suppress ? " suppressed" : " redirected"));
        }
        for (const content::Obtained& o : got) {
            if (o.apply) {
                content_give(o.item, o.count, "chest", q.script);
            } else {
                content_log("item " + content_hex(o.item) + " x" + std::to_string(o.count) + " (" + r.items.name(o.item) +
                            ") from chest");
                events::Event e;
                e.kind = events::Kind::ItemObtained;
                e.item = o.item;
                e.count = o.count;
                e.source = "chest";
                e.script = q.script;
                content_emit(e);
            }
        }
    } else if (d.action == GrantDecision::Action::Replace) {
        content_log(std::string(q.kind == GrantQuery::Kind::Icon ? "icon " : "treasure box ") + content_hex(q.index) +
                    " -> " + content_hex(d.index) + (q.kind == GrantQuery::Kind::Window ? " \"" + d.text + "\"" : ""));
    }
    return d;
}

// The blessing shop's filters (xso.hpp ShopQuery; the Lua mods' cleria.shop.add_filter): consulted in order,
// the first decision that is not Pass is the one taken. With none the VM keeps no shop filter at all.
using ShopFilter = std::function<GrantDecision(const ShopQuery&, const std::string& room)>;
static std::vector<ShopFilter> g_shop_filters;
[[maybe_unused]] static void content_add_shop_filter(ShopFilter f) { g_shop_filters.push_back(std::move(f)); }

// ... and BlessingBought, raised when a purchase reaches its effect (op 0xAF), whatever the filters said.
static GrantDecision content_shop_filter(const ShopQuery& q) {
    GrantDecision d;
    for (const ShopFilter& f : g_shop_filters) {
        d = f(q, g_content.room);
        if (d.action != GrantDecision::Action::Pass) break;
    }
    if (q.kind == ShopQuery::Kind::Buy) {
        const bool off = d.action == GrantDecision::Action::Suppress;
        content_log("blessing_bought " + std::to_string(q.index) + " for " + std::to_string(q.value) + " SP" +
                    (off ? " (effect suppressed)" : d.action == GrantDecision::Action::Replace
                                                         ? " (effect -> " + std::to_string(d.index) + ")" : ""));
        events::Event e;
        e.kind = events::Kind::BlessingBought;
        e.index = q.index;
        e.new_value = q.value;
        e.old_value = off ? 1 : 0;
        e.script = q.script;
        content_emit(e);
    }
    return d;
}

static void content_install(XsoVm& vm) {
    vm.grant_filter = content_filter;
    vm.shop_filter = content_shop_filter;
}

// BossDefeated, from the flags the tick diff is about to report (so: never on a load, a Retry or a new
// game, which re-prime the diff). Two rules, both read off the shipped scripts and the boss modules:
//  - a floor boss: its flag 220..227 goes 0 -> 1 (boss01..08 / M870 write it on the death frame; Toal's
//    S_6082 `BATTLE_THOR` stores 225 itself; Darm's later 227 = 2 is not a second defeat);
//  - a duel / mid-boss: g_flags[156] (DAT_0076bb8c, "a boss fight runs") goes 1 -> 0. Every such fight's
//    script sets 156 = 1 and then waits on the opponent (Wait_FlagEqualInt(156, 0), or a kill count and its
//    own store); the duel brains clear it on their defeat frame (m810/820/830/840/850/860, m360, m460).
//    Not in the eight floor-boss rooms (their scripts clear 156 between phases and after the flag), not
//    when the clear was the player's KO in a losable duel (duel::losable_death, flag 133), and not in
//    another room than the fight's (S_7080's 156 is cleared after the ChangeScene to S_7000).
static void content_boss_watch(const std::int32_t* flags) {
    ContentRt& r = g_content;
    const bool lost = r.duel_lost;
    r.duel_lost = false;
    if (!r.watch.primed) { r.fight_room = r.room; return; }
    if (r.watch.prev[156] == 0 && flags[156] == 1) r.fight_room = r.room;
    static const char* const kFlagged[] = {"S_1099", "S_2099", "S_3099", "S_4099", "S_5099", "S_6082", "S_7080", "S_7099"};
    auto raise = [&](int flag) {
        content_log("boss_defeated " + r.room + (flag >= 0 ? " flag " + std::to_string(flag) : ""));
        events::Event e;
        e.kind = events::Kind::BossDefeated;
        e.index = flag;
        content_emit(e);
    };
    // One event a boss. On Hugo's route Pictimos is killed twice: Battle_Hugo_1 clears 224 for the second round
    // and the brain sets it again. The retail Archipelago mod reads the flags as state (>= 1, hook_ap.cpp
    // goal_bosses_done), which is true from the first kill on; so the first rise in a room is the defeat.
    if (r.defeated_room != r.room) { r.defeated_room = r.room; r.defeated_mask = 0; }
    for (int f = 220; f <= 227; ++f)
        if (r.watch.prev[std::size_t(f)] == 0 && flags[f] == 1 && !(r.defeated_mask & (1u << (f - 220)))) {
            r.defeated_mask |= 1u << (f - 220);
            raise(f);
        }
    if (r.watch.prev[156] == 1 && flags[156] == 0 && !lost && r.fight_room == r.room &&
        std::find(std::begin(kFlagged), std::end(kFlagged), r.room) == std::end(kFlagged))
        raise(-1);
}

// Once a tick, after the scripts' grants reached the game flags.
static void content_tick(const std::int32_t* flags) {
    ContentRt& r = g_content;
    if (!r.queued.empty()) {
        std::vector<events::Event> q;
        q.swap(r.queued);
        for (const events::Event& e : q) {
            content_log(std::string("event ") + events::kind_name(e.kind));
            content_emit(e);
        }
    }
    ++r.room_age;
    content_boss_watch(flags);
    r.watch.diff(flags, events::bus(), r.room);
    if (r.toast_t > 0) --r.toast_t;
}

// Shell::hud layer "content": a rule's text as a notice card, bottom centre, fading out.
static void content_hud(ImDrawList* dl, float w, float h) {
    const ContentRt& r = g_content;
    if (r.toast_t <= 0 || r.toast.empty()) return;
    const float fade = std::clamp(float(r.toast_t) / 30.0f, 0.0f, 1.0f);
    auto A = [&](ImU32 c) { return (c & 0x00FFFFFFu) | (ImU32(float((c >> 24) & 0xFF) * fade) << 24); };
    const float S = ui::S, hpx = 13.5f * S, bpx = 17.0f * S;
    const std::string head = ui::upper(r.toast_head.c_str());
    const ImVec2 ts = ui::text_size(ui::F.body, bpx, r.toast.c_str());
    const float bw = std::max(340.0f * S, ts.x + 40.0f * S), bh = 76.0f * S;
    const ImVec2 a{(w - bw) * 0.5f, h - 190.0f * S}, b{a.x + bw, a.y + bh};
    dl->AddRectFilled(a, b, A(IM_COL32(26, 24, 16, 235)), 6.0f * S);
    dl->AddRect(a, b, A(ui::kPanelEdge), 6.0f * S, 0, 1.2f * S);
    ui::text(dl, ui::F.cond, hpx, {a.x + 20.0f * S, a.y + 15.0f * S}, A(ui::kSection), head.c_str());
    ui::text(dl, ui::F.body, bpx, {a.x + 20.0f * S, a.y + 40.0f * S}, A(ui::kDesc), r.toast.c_str());
}

// The player's KO ended a losable duel (view_game.inl, duel::losable_death): it cleared 156 without a win.
static void content_duel_lost() { g_content.duel_lost = true; }

// The book used an item up (view_game.inl, BookEvents::use_item: the Panacea, FUN_00443390).
static void content_item_used(int id) {
    content_log("item_used " + content_hex(id) + " (" + g_content.items.name(id) + ")");
    events::Event e;
    e.kind = events::Kind::ItemUsed;
    e.item = id;
    content_emit(e);
}

static void content_on_death() {
    events::Event e;
    e.kind = events::Kind::PlayerDeath;
    content_emit(e);
}

// After a slot's retail record was written.
static void content_save(const fs::path& slot_file, int slot) {
    ContentRt& r = g_content;
    r.sc.mods = r.stamp;
    write_sidecar(slot_file, r.sc);
    if (!r.sc.empty()) content_log("sidecar " + sidecar_path(slot_file).filename().string() + " written");
    events::Event e;
    e.kind = events::Kind::Save;
    e.slot = slot;
    content_emit(e);
}

// Before a slot's record is applied in game (the book's Load): the sidecar now, the Load event at the
// next tick (the flag diff re-primes on the loaded flags).
static void content_load(const fs::path& slot_file, int slot) {
    ContentRt& r = g_content;
    r.sc = read_sidecar(slot_file);
    std::string now, was;
    for (const std::string& s : r.stamp) now += (now.empty() ? "" : ", ") + s;
    for (const std::string& s : r.sc.mods) was += (was.empty() ? "" : ", ") + s;
    if (!r.sc.empty() && was != now) content_log("save made with content mods [" + was + "], active now [" + now + "]");
    r.watch.primed = false;
    events::Event e;
    e.kind = events::Kind::Load;
    e.slot = slot;
    r.queued.push_back(e);
}

// A Retry restores the continue snapshot (view_game.inl ContSnap): the sidecar taken with it comes back
// with the flags, and the mods get the Load a slot load raises (AP_FULLGAME.md B1).
static void content_retry(const Sidecar& snap) {
    ContentRt& r = g_content;
    r.sc = snap;
    r.watch.primed = false;
    events::Event e;
    e.kind = events::Kind::Load;
    r.queued.push_back(e);
}

static void content_delete(const fs::path& slot_file) { remove_sidecar(slot_file); }

// The save profile (CleriaCore's, not retail's; LUA_API.md §6.1): "" = the save folder itself (CLERIA_SAVE_DIR,
// default ./saves), else its profiles/<name>/. Set by cleria.save.set_profile or CLERIA_SAVE_PROFILE; lasts for
// the process. The folder is made by the first save (do_save's create_directories).
static std::string g_save_profile;
static fs::path save_folder() {
    const char* e = std::getenv("CLERIA_SAVE_DIR");
    return save_profile_dir(e ? e : "saves", g_save_profile);
}
// The clear data `record.ta2` (src/cleria/records.hpp; build/mode_re/CLEAR_DATA.md): which characters cleared
// the game, the cleared tiers, the Bonus Shop's purchases. It is per INSTALL, not per save: retail keeps it beside
// the save slots (FUN_005ce950's folder), so here it is <CLERIA_SAVE_DIR, default ./saves>/record.ta2 — the save
// folder itself, never a profile's folder. Read once at boot (FUN_00442260); written by the clear (EventXX(4))
// and at a clean shutdown (FUN_0040dcc0). [not modelled] the other six record files; the achievement repair of
// the reader (CLEAR_DATA.md §3.4: nothing without Steam / GOG). A retail record.ta2 copied there is read as is.
static records::ClearData g_clear;
static bool g_clear_dirty = false;
static fs::path clear_data_path() {
    const char* e = std::getenv("CLERIA_SAVE_DIR");
    return fs::path(e ? e : "saves") / "record.ta2";
}
// The Arena SP wallet 0x74c128, `record.ar2` beside it (FUN_00438b10 at boot, before record.ta2 [EXPORT
// 00442260.c:119-123]; FUN_00438de0 after each Bonus Shop purchase and by op 0x18F ArenaSave).
static std::uint32_t g_arena_sp = 0;
static bool arena_sp_save() {
    std::error_code ec;
    fs::create_directories(clear_data_path().parent_path(), ec);
    const std::uint32_t seed = std::uint32_t(std::chrono::steady_clock::now().time_since_epoch().count());
    return records::save_arena_sp(clear_data_path().parent_path() / "record.ar2", g_arena_sp, records::make_key(seed));
}
// The best-time tables (CLEAR_DATA.md §4), beside record.ta2: [0] the normal table, [1] the "e" one (a run played
// with the character's second version, FUN_00583960). Read at boot (FUN_00435950, FUN_00437ca0 — each then zeroes
// the marks: FUN_005832e0 with character 0's 14 rows, FUN_00583340); written at the end of a run and by the stage
// select's Cancel (FUN_00436060 / FUN_004383e0).
static records::RecordTable g_rec_ta[2] = {records::RecordTable(records::RecordTable::kTimeAttackRows),
                                           records::RecordTable(records::RecordTable::kTimeAttackRows)};
static records::RecordTable g_rec_ar[2] = {records::RecordTable(records::RecordTable::kArenaRows),
                                           records::RecordTable(records::RecordTable::kArenaRows)};
static int record_times(const records::RecordTable& t) {
    int n = 0;
    for (std::size_t e = 0; e < t.bytes.size(); e += 12) n += (t.bytes[e] | t.bytes[e + 1] | t.bytes[e + 2] | t.bytes[e + 3]) != 0;
    return n;
}
static void record_tables_load() {
    static const char* const kNames[4] = {"record.ta", "record.tae", "record.ar", "record.are"};
    for (int i = 0; i < 4; ++i) {
        records::RecordTable& t = (i < 2 ? g_rec_ta : g_rec_ar)[i & 1];
        t = records::RecordTable(t.rows);
        if (auto f = records::load_table(clear_data_path().parent_path() / kNames[i], t.rows)) {
            t = std::move(*f);
            if (const int n = record_times(t)) std::printf("[clear] %s: %d time(s)\n", kNames[i], n);
        }
        t.clear_marks(i < 2 ? 14 : 7);
    }
}
static bool record_tables_save(bool arena) {
    std::error_code ec;
    const fs::path dir = clear_data_path().parent_path();
    fs::create_directories(dir, ec);
    const std::uint32_t seed = std::uint32_t(std::chrono::steady_clock::now().time_since_epoch().count());
    const records::RecordTable* t = arena ? g_rec_ar : g_rec_ta;
    const bool a = records::save_table(dir / (arena ? "record.ar" : "record.ta"), t[0], records::make_key(seed));
    const bool b = records::save_table(dir / (arena ? "record.are" : "record.tae"), t[1], records::make_key(seed + 1));
    return a && b;
}
// The bonus modes' front end (BONUS_MODES.md §2.1, §3.1), kept across the title and the game: the mode byte
// 0x74e25e (0 story, 1 Time Attack, 2 Arena), the title's state 0x74e25a (0 the list, 2 the stage select is wanted
// / up, 3 a run was started: the title reopens the stage select), the stage picked 0x74ab9f, and what Character
// Select and Difficulty chose (0x76bb74 — 0 = Adol, the boot value —, the type, 0x74ac3c / 0x74e685).
struct RunState {
    int mode = 0, front = 0, pick = 0;
    int character = 0, type = 0, difficulty = 0;
    bool easy = false;
    int column() const { return easy ? difficulty : difficulty + 1; }   // [BYTES 0x57e92e..0x57e947]: 0 Easy .. 3 Nightmare
    bool second() const { return type == 1; }                           // FUN_00583960
};
static RunState g_run;
static void clear_data_load() {
    record_tables_load();
    g_clear = records::ClearData{};
    g_clear_dirty = false;
    g_arena_sp = records::load_arena_sp(clear_data_path().parent_path() / "record.ar2").value_or(0);
    if (g_arena_sp) std::printf("[clear] record.ar2: Arena SP %u\n", g_arena_sp);
    std::error_code ec;
    if (!fs::exists(clear_data_path(), ec)) return;
    if (const auto d = records::load_clear(clear_data_path())) {
        g_clear = *d;
        std::printf("[clear] record.ta2: Yunica %02x Hugo %02x Toal %02x, %d cleared tier(s)\n", g_clear.clear[0], g_clear.clear[1],
                    g_clear.clear[2], g_clear.tier_count());
        int bought = 0;
        for (std::size_t i = 0; i < g_clear.shop.size(); ++i) bought |= (g_clear.shop[i] != 0) << i;
        if (bought) std::printf("[clear] record.ta2: Bonus Shop purchases 0x%03x\n", bought);
    } else {
        std::printf("[clear] record.ta2: wrong length or CRC, ignored (the defaults stay)\n");   // [EXPORT 00436790.c:153-165]
    }
}
// The cleared-tier bytes as they lie from 0x74ab88 (the rows at +0, +4, +12, +16, +20: CLEAR_DATA.md §3.1), for the
// book's boss notes, whose stat columns open per cleared difficulty (FUN_005bff40 [EXPORT 005bff40.c:308-318]).
static const std::uint8_t* clear_tier_bytes() {
    static std::uint8_t b[24];
    static constexpr int kRow[5] = {0, 4, 12, 16, 20};
    std::memset(b, 0, sizeof b);
    for (std::size_t t = 0; t < 5; ++t) for (std::size_t c = 0; c < 3; ++c) b[std::size_t(kRow[t]) + c] = g_clear.tier[t][c];
    return b;
}
// [approx] the key and the two noise words come from the clock, not from the engine's xorshift (retail draws
// three numbers of the game's stream here; CleriaCore leaves that stream alone).
static bool clear_data_save() {
    std::error_code ec;
    fs::create_directories(clear_data_path().parent_path(), ec);
    const std::uint32_t seed = std::uint32_t(std::chrono::steady_clock::now().time_since_epoch().count());
    const bool ok = records::save_clear(clear_data_path(), g_clear, records::make_key(seed));
    if (ok) g_clear_dirty = false;
    return ok;
}
// The clean shutdown's write (FUN_0040dcc0 [EXPORT 0040dcc0.c:78]). Retail rewrites the file every time; here
// only when it is missing or its content changed (the bytes would differ by the key alone).
static void clear_data_shutdown() {
    std::error_code ec;
    if (g_clear_dirty || !fs::exists(clear_data_path(), ec)) clear_data_save();
}
// The config tool's Clear Time Attack Records / Clear Game Progress (buttons 4501 / 4502 [BYTES 0x41d230.. /
// 0x41cf17..]): the record files deleted from the save folder -- Time Attack: record.ta, .tae, .ar, .are; game
// progress: record.ta2, .ar2, .tp. Live: the clear data in memory goes back to a fresh install's, so the title's
// gates close at once. Returns the files deleted. [not modelled] retail also deletes the Steam / Galaxy cloud copies.
static int clear_records(bool progress) {
    static const char* const kTa[] = {"record.ta", "record.tae", "record.ar", "record.are"};
    static const char* const kProgress[] = {"record.ta2", "record.ar2", "record.tp"};
    int n = 0;
    std::error_code ec;
    const char* const* files = progress ? kProgress : kTa;
    for (int i = 0; i < (progress ? 3 : 4); ++i) n += fs::remove(clear_data_path().parent_path() / files[i], ec) ? 1 : 0;
    if (progress) { g_clear = records::ClearData{}; g_clear_dirty = false; g_arena_sp = 0; }
    else record_tables_load();                          // the tables in memory are the empty ones again
    if (progress) std::printf("[clear] game progress cleared: %d file(s) deleted\n", n);
    else std::printf("[clear] Time Attack records cleared: %d file(s) deleted, %d time(s) left\n", n,
                     record_times(g_rec_ta[0]) + record_times(g_rec_ta[1]) + record_times(g_rec_ar[0]) + record_times(g_rec_ar[1]));
    return n;
}
// False (and no change) for a name save_profile_name refuses; "" returns to the normal folder.
static bool save_set_profile(const std::string& raw) {
    const std::string n = save_profile_name(raw);
    if (n.empty() && !raw.empty()) return false;
    if (n == g_save_profile) return true;
    g_save_profile = n;
    std::error_code ec;
    if (n.empty()) std::printf("[save] profile cleared\n");
    else std::printf("[save] profile \"%s\" -> %s\n", n.c_str(), fs::absolute(save_folder(), ec).string().c_str());
    return true;
}

// CLERIA_DRIVE: cstate | give=<id>[,<n>] | csave=<slot> | cload=<slot> | cprofile
static bool content_drive(const std::string& cmd) {
    ContentRt& r = g_content;
    if (cmd == "cprofile") {                       // the profile, and the slot files in the normal folder / in the active one
        const char* e = std::getenv("CLERIA_SAVE_DIR");
        auto count = [](const fs::path& d) {
            int n = 0;
            std::error_code ec;
            for (fs::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec))
                n += it->path().extension() == ".bin" || it->path().extension() == ".cleria";
            return n;
        };
        std::printf("  [drive] cprofile \"%s\" normal %d active %d\n", g_save_profile.c_str(), count(e ? e : "saves"),
                    count(save_folder()));
        return true;
    }
    if (cmd == "cstate") {
        std::string items, flags;
        for (const auto& [id, n] : r.sc.items) items += " " + content_hex(id) + "=" + std::to_string(n);
        for (const auto& [k, v] : r.sc.flags) flags += " " + k + "=" + std::to_string(v);
        std::printf("  [drive] cstate room %s content %d items{%s } cflags{%s }\n", r.room.c_str(), int(r.on),
                    items.c_str(), flags.c_str());
        return true;
    }
    if (cmd.rfind("give=", 0) == 0) {
        int id = 0, n = 1;
        if (std::sscanf(cmd.c_str() + 5, "%i,%d", &id, &n) >= 1) content_give(id, n, "drive");
        return true;
    }
    if (cmd.rfind("csave=", 0) == 0) {
        if (r.save_slot) r.save_slot(std::atoi(cmd.c_str() + 6));
        return true;
    }
    if (cmd.rfind("cload=", 0) == 0) {
        if (r.load_slot) r.load_slot(std::atoi(cmd.c_str() + 6));
        return true;
    }
    return false;
}

// The Mods tab's "Content additions" row (view_mods.inl): how many active mods add content.
static std::string content_summary() {
    const ContentRt& r = g_content;
    if (!r.on || r.stamp.empty()) return "None";
    return std::to_string(r.stamp.size()) + (r.stamp.size() == 1 ? " mod" : " mods");
}

// Mods > Content additions (a sub-page of the Mods tab, view_mods.inl; once its own F1 tab): what the
// content mods add, this save's CleriaCore state, the events.
static void content_page(ui::List& L) {
    ContentRt& r = g_content;
    ui::section(L, "Mod content");
    if (!r.on) {
        ui::info(L, "Status", "No content mod active", ui::kMuted,
                 "A mod adds or changes game content with a content/ folder of INI files (items, chests, pickups, "
                 "rules, text). Turn one on in the Mods tab and restart. Not a retail feature.");
    } else {
        std::string names;
        for (const mods::Mod& m : g_mods_ui.list)
            for (const std::string& s : r.stamp)
                if (s.rfind(m.m.id + " ", 0) == 0) names += (names.empty() ? "" : ", ") + m.label() + " " + m.m.version;
        const std::string from_desc =
            "The active mods that add or change game content, in load order (a later mod wins an entry two mods "
            "set; the Mods tab lists those notes). Saves made with them keep CleriaCore's extra state in a "
            ".cleria file beside the save; the save itself stays a retail save. Not a retail feature.";
        ui::info(L, "From", names.c_str(), ui::kGood, from_desc.c_str());
        int n_new = 0;
        for (const auto& [id, d] : r.c.items) n_new += id >= content::kNewItemFirst;
        auto count_row = [&](const char* label, std::size_t n, const std::string& extra, const char* desc) {
            const std::string v = std::to_string(n) + extra;
            ui::info(L, label, v.c_str(), ui::kValue, desc);
        };
        count_row("Items", r.c.items.size(), n_new ? " (" + std::to_string(n_new) + " new)" : std::string(),
                  "Item names, descriptions and icons changed (content/items.ini), and new items.");
        count_row("Chests", r.c.chests.size(), "", "Chests that give something else (content/chests.ini).");
        count_row("Pickups", r.c.pickups.size(), "", "Items placed in rooms, each collected once per save (content/pickups.ini).");
        count_row("Rules", r.c.rules.size(), "", "Things that happen on a game event: a flag, a room, an item (content/rules.ini).");
        count_row("Scripts", r.c.text.size(), "", "Event scripts whose text is changed (content/text.ini).");
        if (!r.c.errors.empty())
            ui::info(L, "Problems", (std::to_string(r.c.errors.size()) + " (see the Mods tab)").c_str(), ui::kWarn);
    }
    ui::section(L, "Mod items held");
    int shown = 0;
    for (const auto& [id, n] : r.sc.items) {
        if (n < 1) continue;
        ui::info(L, r.items.name(id).c_str(), ("x " + std::to_string(n)).c_str(), ui::kValue,
                 r.items.get(id) ? r.items.get(id)->description.c_str() : nullptr);
        ++shown;
    }
    if (!shown) ui::info(L, "None", "", ui::kMuted);
    if (!r.log.empty()) {
        ui::section(L, "Recent events");
        int k = 0;
        for (auto it = r.log.rbegin(); it != r.log.rend() && k < 6; ++it, ++k)
            ui::info(L, "", it->c_str(), ui::kMuted, it->c_str());
    }
}
