// Game-side logic of the retail probe: stage a room load through the engine's own
// door path, set the test's state at the load boundary, and the small helpers.
//
// How a test is staged (see game.h for the evidence behind every address):
//   1. The panel click writes the cells the door trigger FUN_0042cf00 writes when
//      the player walks into an exit: fade target 1.0, the pending scene, the
//      0x194-byte arrival descriptor. No game function is called.
//   2. The engine's fade machine (FUN_004106e0) fades to black over 20 steps and
//      commits the room change itself; next frame it deletes the room's objects
//      and calls the scene loader FUN_00431260.
//   3. Our hook on FUN_00431260 runs first, at the one moment nothing of the old
//      or the new room exists — the same boundary at which a save is loaded — and
//      writes the test's flags / items / gear / difficulty / config bytes there.
//   4. The loader spawns the player at the descriptor's position (FUN_00434020)
//      and runs the room's own root script. About 20 frames later the level, HP,
//      MP and Boost gauge are set with the player alive.
#include <windows.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "MinHook.h"
#include "game.h"
#include "probe.h"

using namespace ga;
void mh_ensure_init();
uintptr_t ga::g_delta = 0;

namespace probe {

bool g_invincible = false, g_hp_lock = false, g_allow_save = false, g_allow_char_switch = false;

static Status g_st;
static char g_msg[320];

// The staged test, between the click and the end of the room load.
static Test g_pending;
static std::vector<std::pair<std::string, int>> g_pending_cfg;
static bool g_armed = false;       // waiting for the loader hook
static int g_armed_scene = 0;
static int g_armed_frames = 0;
static bool g_post = false;        // waiting for the player to exist in the new room
static int g_post_scene = 0, g_post_frames = 0;

static bool g_saved = false;
static int g_saved_scene = 0;
static float g_saved_pos[3], g_saved_heading;

static void setmsg(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_msg, sizeof(g_msg), fmt, ap);
    va_end(ap);
}

// ---- guarded execution: an access violation becomes a log line, not a crash ----
static int fault_filter(unsigned code) {
    return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}
static bool guarded(bool (*fn)(), const char* what) {
    __try {
        return fn();
    } __except (fault_filter(GetExceptionCode())) {
        g_st.faulted = true;
        g_armed = g_post = false;
        plog("FAULT in %s: access violation caught; game writes are now off until restart", what);
        setmsg("A fault was caught in '%s'. Game writes are off until the game is restarted.", what);
        return false;
    }
}

// ---- small readers -------------------------------------------------------------
static unsigned char* player() { return at<unsigned char*>(kPlayer); }

// "s_10\s_1004\" -> "S_1004" into out. False when the record is empty.
static bool scene_leaf(int id, char* out, size_t n) {
    out[0] = 0;
    unsigned char* tab = at<unsigned char*>(kSceneTable);
    unsigned count = at<unsigned>(kSceneCount);
    if (!tab || id <= 0 || (unsigned)id >= count || count > 4096) return false;
    const char* dir = (const char*)tab + (size_t)id * kSceneStride + 4;
    size_t len = strnlen(dir, 0x100);
    while (len && (dir[len - 1] == '\\' || dir[len - 1] == '/')) len--;
    size_t start = len;
    while (start && dir[start - 1] != '\\' && dir[start - 1] != '/') start--;
    size_t m = len - start;
    if (!m || m >= n) return false;
    for (size_t i = 0; i < m; i++) out[i] = (char)toupper((unsigned char)dir[start + i]);
    out[m] = 0;
    return true;
}
// Room name ("S_1004", any case) -> scene id; 0 when the scene list has no such room.
static int find_scene(const char* room) {
    unsigned count = at<unsigned>(kSceneCount);
    if (!at<unsigned char*>(kSceneTable) || count > 4096) return 0;
    char leaf[64];
    for (unsigned i = 1; i < count; i++)
        if (scene_leaf((int)i, leaf, sizeof(leaf)) && _stricmp(leaf, room) == 0) return (int)i;
    return 0;
}

static int read_difficulty() {
    if (flag(kFlagVeryEasy) != 0) return 0;
    if (flag(kFlagEasy) != 0) return 1;
    int d = flag(kFlagDiff);
    return d == 1 ? 3 : d == 2 ? 4 : 2;
}

static bool refresh_impl() {
    Status& s = g_st;
    unsigned char* p = player();
    s.in_room = p != nullptr;
    s.scene_id = at<int>(kCurScene);
    s.room_number = at<int>(kSceneNumber);
    if (!s.in_room || !scene_leaf(s.scene_id, s.room, sizeof(s.room))) s.room[0] = 0;
    if (p) {
        memcpy(s.pos, p + kActPos, 12);
        s.heading = *(float*)(p + kActHeading);
    }
    s.character = flag(kFlagChar);
    s.difficulty = read_difficulty();
    s.level = at<int>(kLevel);
    s.sp = at<int>(kSp);
    s.hp = at<float>(kHp);
    s.max_hp = at<float>(kMaxHp);
    s.mp = at<float>(kMp);
    s.max_mp = at<float>(kMaxMp);
    s.boost = at<float>(kBoost);
    s.weapon_tier = flag(kFlagWeaponTier);
    s.cfg_shadows = at<unsigned char>(kCfgShadows);
    s.cfg_water = at<unsigned char>(kCfgWater);
    s.cfg_glare = at<unsigned char>(kCfgGlare);
    s.cfg_other = at<unsigned char>(kCfgOtherSimpl);
    s.cfg_caustics = at<unsigned char>(kCfgCaustics);
    s.busy = g_armed || g_post;
    return true;
}

const Status& status() { return g_st; }

const char* why_not() {
    if (!g_st.supported) return "This exe is not the build the mod was made for (Steam v1.1.1.0): the panel is read-only.";
    if (g_st.faulted) return "A fault was caught earlier: game writes are off until the game is restarted.";
    if (g_armed || g_post) return "A room load is in progress.";
    if (!at<unsigned char*>(kPlayer)) return "Not in a room yet: load a save first.";
    if (at<unsigned char>(kBonusMode) != 0) return "Time Attack / Arena is running: use a story save.";
    if (at<unsigned char>(kGameOver) != 0) return "Game over screen.";
    if (at<int>(kFixA) + at<int>(kFixB) != 0) return "A cutscene or message box is running: wait until you can move.";
    if (at<unsigned char>(kMenuFreeze) != 0) return "The game is frozen by a menu: close it first.";
    if (at<int>(kNextScene) != 0 || at<int>(kPendScene) != 0 || (at<int>(kSceneCmd) & 3) != 0)
        return "A room change is already under way.";
    if (!(at<float>(kHp) >= 1.0f)) return "The player is dead.";
    return nullptr;
}

const char* test_blocker(const Test& t) {
    if (t.character >= 0 && t.character != g_st.character && !g_allow_char_switch) {
        static char buf[160];
        snprintf(buf, sizeof(buf), "Needs a %s save (this one is %s). Load one, or tick the experimental character switch in Helpers.",
                 char_name(t.character), char_name(g_st.character));
        return buf;
    }
    return nullptr;
}

// ---- config levels -------------------------------------------------------------
static uintptr_t cfg_addr(const std::string& n) {
    if (n == "shadows") return kCfgShadows;
    if (n == "reflections" || n == "water") return kCfgWater;
    if (n == "glare") return kCfgGlare;
    if (n == "other") return kCfgOtherSimpl;
    if (n == "caustics") return kCfgCaustics;
    return 0;
}
static int cfg_boot(const std::string& n) {
    if (n == "shadows") return g_st.boot_shadows;
    if (n == "reflections" || n == "water") return g_st.boot_water;
    if (n == "glare") return g_st.boot_glare;
    return 99;   // on/off bytes: no resource depends on them at start-up (as far as read)
}
const char* config_note(const std::string& name, int value) {
    if (!cfg_addr(name)) return "unknown config name";
    if (value > cfg_boot(name))
        return "needs a restart: the game started with a lower level, so set this one in config.exe and restart";
    if (name == "shadows")
        return "applied at the room load (Toal's trail buffer is sized at spawn; a boss's shadow target keeps its start-up size). For a sure comparison set it in config.exe and restart";
    if (name == "other")
        return "applied at the room load (read when the room's objects spawn) - not tried in the game";
    if (name == "caustics") return "applied at the room load; 1 = caustics on";
    return "applied at the room load; render targets keep their start-up size. For a sure comparison set it in config.exe and restart";
}
static void apply_config(const std::string& name, int value) {
    uintptr_t a = cfg_addr(name);
    if (!a) { plog("  config: unknown name '%s'", name.c_str()); return; }
    if (value < 0 || value > 2 || value > cfg_boot(name)) {
        plog("  config: %s=%d NOT applied (needs a restart; the game started with %d)", name.c_str(), value, cfg_boot(name));
        return;
    }
    unsigned char byte = (unsigned char)value;
    if (name == "caustics") byte = value ? 0 : 1;            // the ini key is "disable caustics"
    if (name == "other" && value > 1) byte = 1;
    plog("  config: %s = %d (byte 0x%X: %u -> %u)", name.c_str(), value, (unsigned)a, at<unsigned char>(a), byte);
    at<unsigned char>(a) = byte;
}

// ---- step 3: the state written at the load boundary (from the loader hook) --------
static bool reserved_flag(int i) {
    return i == kFlagEasy || i == kFlagDiff || i == kFlagVeryEasy || i == kFlagChar ||
           i == kFlagWeaponId || i == kFlagArmorId || i == kFlagBootsId || i == kFlagWeaponTier;
}
static bool preload_impl() {
    const Test& t = g_pending;
    plog("load boundary: applying '%s' (scene %d)", t.id.c_str(), at<int>(kNextScene));
    flag(kFlagBossFight) = 0;      // never arrive with the doors shut; a boss room's own script sets it again
    if (t.character >= 0 && t.character != flag(kFlagChar)) {
        if (g_allow_char_switch) {
            // What the new-game init FUN_0041fd10 writes for that character.
            static const int weapon[4] = {3, 0, 1, 2}, armor[4] = {0x18, 6, 0xC, 0x12};
            plog("  character %d -> %d (EXPERIMENTAL switch)", flag(kFlagChar), t.character);
            flag(kFlagChar) = t.character;
            flag(kFlagWeaponId) = weapon[t.character];
            flag(weapon[t.character]) = 1;
            flag(kFlagArmorId) = armor[t.character];
            flag(armor[t.character]) = 1;
            flag(kFlagBootsId) = -1;
        } else {
            plog("  character mismatch left alone (switch not allowed)");
        }
    }
    if (t.difficulty >= 0 && t.difficulty != read_difficulty()) {
        static const int idx[5] = {0, 0, 0, 1, 2}, easy[5] = {1, 1, 0, 0, 0}, very[5] = {1, 0, 0, 0, 0};
        plog("  difficulty -> %s", diff_name(t.difficulty));
        flag(kFlagDiff) = idx[t.difficulty];
        flag(kFlagEasy) = easy[t.difficulty];
        flag(kFlagVeryEasy) = very[t.difficulty];
    }
    for (size_t i = 0; i < t.flags.size(); i++) {
        int k = t.flags[i].first, v = t.flags[i].second;
        if (reserved_flag(k)) { plog("  flag %d is set through its own key, skipped", k); continue; }
        plog("  flag %d: %d -> %d", k, flag(k), v);
        flag(k) = v;
    }
    for (size_t i = 0; i < t.items.size(); i++) {
        plog("  item %d: %d -> %d", t.items[i].first, flag(t.items[i].first), t.items[i].second);
        flag(t.items[i].first) = t.items[i].second;
    }
    for (size_t i = 0; i < t.gear.size(); i++) {
        plog("  gear cell %d -> %d", t.gear[i].first, t.gear[i].second);
        at<int>(kGearCells + (uintptr_t)t.gear[i].first * 4) = t.gear[i].second;
    }
    for (int s = 0; s < 3; s++) {
        if (t.skill_lv[s] < 0) continue;
        plog("  skill %d: bracelet owned, level %d", s, t.skill_lv[s]);
        flag(116 + s) = 1;
        flag(kFlagSkillLv + s) = t.skill_lv[s];
    }
    if (t.equip_skill >= -1) {
        at<int>(kEquipSkill) = t.equip_skill < 0 ? -1 : 0x74 + t.equip_skill;
        plog("  equipped skill -> %d", at<int>(kEquipSkill));
    }
    if (t.weapon_tier >= 0) {
        plog("  weapon tier -> %d", t.weapon_tier);
        for (int c = 0; c < 4; c++) at<int>(kWeaponCells + (uintptr_t)c * 8) = t.weapon_tier;
    }
    if (t.boost == 1) flag(kFlagBoostOn) = 1;
    if (t.sp >= 0) at<int>(kSp) = t.sp;
    for (size_t i = 0; i < t.config.size(); i++) apply_config(t.config[i].first, t.config[i].second);
    for (size_t i = 0; i < g_pending_cfg.size(); i++) apply_config(g_pending_cfg[i].first, g_pending_cfg[i].second);
    at<int>(kRecalc) |= 0x75;      // what the spawn sets anyway; the recompute runs with the new player
    return true;
}

// Called by the naked stub in front of FUN_00431260.
static void __cdecl on_scene_load() {
    if (!g_armed) {
        // The game's own Load: the whole save block was just replaced by a save
        // file, so nothing of a test state is left in it.
        if (g_st.dirty && _stricmp((const char*)va(kArriveLabel), "load") == 0) {
            g_st.dirty = false;
            g_post = false;
            plog("a save was loaded by the game: the state is the save's own again, saving is no longer blocked");
        }
        return;
    }
    g_armed = false;
    if (at<int>(kNextScene) != g_armed_scene) {
        plog("load boundary: the game is loading scene %d, not the staged %d - nothing applied",
             at<int>(kNextScene), g_armed_scene);
        return;
    }
    if (guarded(preload_impl, "load boundary")) {
        g_post = true;
        g_post_scene = g_armed_scene;
        g_post_frames = 0;
    }
}

static void* o_SceneLoad = nullptr;
__declspec(naked) static void hk_SceneLoad() {
    __asm {
        pushad
        pushfd
        call on_scene_load
        popfd
        popad
        jmp dword ptr [o_SceneLoad]
    }
}

// ---- the save writer: never write a save while a test state is loaded --------------
static int __cdecl save_should_block() {
    if (!g_st.dirty || g_allow_save) return 0;
    g_st.saves_blocked++;
    plog("SAVE BLOCKED: the game asked to write a save while a probe state is loaded (tick 'Allow saving' in Helpers to permit it)");
    return 1;
}
static void* o_SaveWrite = nullptr;
__declspec(naked) static void hk_SaveWrite() {
    __asm {
        pushad
        pushfd
        call save_should_block
        test eax, eax
        jnz blocked
        popfd
        popad
        jmp dword ptr [o_SaveWrite]
    blocked:
        popfd
        popad
        xor eax, eax
        ret
    }
}

// ---- step 1: stage the room change the way a door does ------------------------------
static int g_stage_scene;
static bool g_stage_has_pos;
static float g_stage_pos[3], g_stage_facing_rad;

static bool stage_impl() {
    unsigned char desc[kDescSize];
    memset(desc, 0, sizeof(desc));
    if (g_stage_has_pos) {
        memcpy(desc, g_stage_pos, 12);
        memcpy(desc + 0xC, &g_stage_facing_rad, 4);
        // A non-empty label makes the spawn use the position (FUN_00434020). Doors
        // put the destination's room tag there.
        scene_leaf(g_stage_scene, (char*)desc + 0x14, 0x40);
        if (!desc[0x14]) strcpy((char*)desc + 0x14, "PROBE");
    }
    float speed = at<float>(kFadeSpeed);
    if (!(speed > 0.0f) || speed > 1.0f) at<float>(kFadeSpeed) = 0.05f;   // a script may have left it at 0
    at<float>(kFadeTarget) = 1.0f;
    at<int>(kPendScene) = g_stage_scene;
    at<float>(kFadePrev) = 0.0f;
    at<float>(kFadeCur) = 0.0f;
    at<int>(kFadeHold) = 0;
    memcpy((void*)va(kPendDesc), desc, kDescSize);
    at<int>(kNextScene) = g_stage_scene;
    if (unsigned char* p = player()) {
        unsigned& fr = *(unsigned*)(p + kActIframes);
        if (fr < 0xFF) fr = 0xFF;
    }
    g_armed = true;
    g_armed_scene = g_stage_scene;
    g_armed_frames = 0;
    g_st.dirty = true;
    return true;
}

static float facing_to_rad(float deg) {      // FUN_00443a80: 90 - deg, wrapped, in radians
    if (deg == -1.0f || deg == 360.0f) return 0.0f;
    float d = fmodf(90.0f - deg, 360.0f);
    if (d < 0) d += 360.0f;
    return d * 0.01745329238f;
}

bool go(const Test& t, const std::vector<std::pair<std::string, int>>* cfg_extra, std::string& msg) {
    if (const char* r = why_not()) { msg = r; return false; }
    if (const char* r = test_blocker(t)) { msg = r; return false; }
    int scene = t.room.empty() ? at<int>(kCurScene) : find_scene(t.room.c_str());
    if (scene <= 0) { msg = "Room '" + t.room + "' is not in the game's scene list."; return false; }
    g_pending = t;
    g_pending_cfg.clear();
    if (cfg_extra) g_pending_cfg = *cfg_extra;
    g_stage_scene = scene;
    g_stage_has_pos = t.has_pos;
    memcpy(g_stage_pos, t.pos, sizeof(g_stage_pos));
    g_stage_facing_rad = t.has_facing ? facing_to_rad(t.facing_deg) : 0.0f;
    plog("GO test '%s': room %s (scene %d)%s", t.id.c_str(), t.room.empty() ? "(current)" : t.room.c_str(), scene,
         t.has_pos ? " at a set position" : " at the room's default spawn");
    if (!guarded(stage_impl, "stage")) { msg = g_msg; return false; }
    msg = "Loading " + (t.room.empty() ? std::string("the current room") : t.room) + "...";
    return true;
}

bool go_room(const char* room, bool has_pos, const float* pos, std::string& msg) {
    if (const char* r = why_not()) { msg = r; return false; }
    int scene = find_scene(room);
    if (scene <= 0) { msg = std::string("Room '") + room + "' is not in the game's scene list."; return false; }
    g_pending = Test();
    g_pending.id = std::string("goto ") + room;
    g_pending.hp_full = g_pending.mp_full = false;
    g_pending_cfg.clear();
    g_stage_scene = scene;
    g_stage_has_pos = has_pos;
    if (has_pos) memcpy(g_stage_pos, pos, sizeof(g_stage_pos));
    g_stage_facing_rad = 0.0f;
    plog("GO room %s (scene %d)", room, scene);
    if (!guarded(stage_impl, "stage")) { msg = g_msg; return false; }
    msg = std::string("Loading ") + room + "...";
    return true;
}

// ---- step 4: with the player alive in the new room -----------------------------------
static void heal_now() {
    at<float>(kHp) = at<float>(kMaxHp);
    if (unsigned char* p = player()) *(float*)(p + kActHp) = *(float*)(p + kActMaxHp);
}
static bool post_impl() {
    const Test& t = g_pending;
    if (t.level > 0 && t.level != at<int>(kLevel)) {
        plog("  level %d -> %d (FUN_004200f0)", at<int>(kLevel), t.level);
        ((void(__fastcall*)(unsigned))va(kFnSetLevel))((unsigned)t.level);
    }
    if (t.hp_full) heal_now();
    if (t.mp_full) at<float>(kMp) = at<float>(kMaxMp);
    if (t.boost >= 0) at<float>(kBoost) = t.boost ? 30000.0f : 0.0f;
    unsigned char* p = player();
    float* q = (float*)(p + kActPos);
    plog("arrived: room scene %d, player at %.2f, %.2f, %.2f; level %d, HP %.0f/%.0f, weapon tier %d, character %d, difficulty %s",
         at<int>(kCurScene), q[0], q[1], q[2], at<int>(kLevel), at<float>(kHp), at<float>(kMaxHp),
         flag(kFlagWeaponTier), flag(kFlagChar), diff_name(read_difficulty()));
    return true;
}

static bool frame_impl() {
    refresh_impl();
    // The levels the game started with (the ini is parsed before the first frame):
    // render targets and buffers were created for these, so a test never goes above.
    static bool boot_read = false;
    if (!boot_read) {
        boot_read = true;
        g_st.boot_shadows = g_st.cfg_shadows;
        g_st.boot_water = g_st.cfg_water;
        g_st.boot_glare = g_st.cfg_glare;
        plog("start-up config: shadows %u reflections %u glare %u other %u", g_st.cfg_shadows, g_st.cfg_water, g_st.cfg_glare, g_st.cfg_other);
    }
    if (g_armed && ++g_armed_frames == 900)
        plog("note: the staged room load has not started after 900 frames (is the game paused or unfocused?)");
    if (g_post) {
        unsigned char* p = player();
        if (p && (at<int>(kSceneCmd) & 3) == 0 && at<int>(kCurScene) == g_post_scene) {
            if (++g_post_frames >= 20) {
                g_post = false;
                post_impl();
            }
        } else if (++g_post_frames > 1800) {
            g_post = false;
            plog("note: the staged room never finished loading; post-load settings dropped");
        }
    }
    if (unsigned char* p = player()) {
        if (g_invincible) {
            unsigned& fr = *(unsigned*)(p + kActIframes);
            if (fr < 120) fr = 120;
        }
        if (g_hp_lock && at<unsigned char>(kGameOver) == 0) heal_now();
    }
    return true;
}
void frame() {
    // Nothing of the game is even read unless the exe was verified (install()).
    if (!g_st.supported || g_st.faulted) return;
    guarded(frame_impl, "frame");
}

// ---- helpers ---------------------------------------------------------------------------
static const char* helper_gate() {
    if (!g_st.supported) return "Read-only: unsupported exe build.";
    if (g_st.faulted) return "Game writes are off (a fault was caught).";
    if (!at<unsigned char*>(kPlayer)) return "Not in a room.";
    return nullptr;
}
static bool heal_impl() { heal_now(); plog("helper: full heal (%.0f)", at<float>(kMaxHp)); setmsg("HP restored."); return true; }
static bool boost_impl() {
    if (flag(kFlagBoostOn) == 0) { flag(kFlagBoostOn) = 1; plog("helper: flag 187 (Boost unlocked) 0 -> 1"); }
    at<float>(kBoost) = 30000.0f;
    plog("helper: Boost gauge full");
    setmsg("Boost gauge filled.");
    return true;
}
static bool mp_impl() { at<float>(kMp) = at<float>(kMaxMp); plog("helper: MP full"); setmsg("MP restored."); return true; }
static bool boss_impl() {
    int id = at<int>(kBossGaugeId);
    if (!id) { setmsg("No boss gauge is on screen."); return false; }
    unsigned char** pool = at<unsigned char**>(kActorPool);
    unsigned n = at<unsigned>(kActorCount);
    if (!pool || n > 4096) { setmsg("Actor pool not readable."); return false; }
    float shown = at<float>(kBossGaugeCur);
    for (unsigned i = 0; i < n; i++) {
        unsigned char* a = pool[i];
        if (!a || *(int*)(a + kActId) != id) continue;
        float hp = *(float*)(a + kActHp);
        if (!(fabsf(hp - shown) <= 1.0f)) {
            setmsg("Refused: the gauge shows %.0f but that actor holds %.0f - not touching it.", shown, hp);
            plog("helper: boss HP refused (gauge %.1f, actor %.1f)", shown, hp);
            return false;
        }
        *(float*)(a + kActHp) = 1.0f;
        plog("helper: boss actor %p HP %.0f -> 1", a, hp);
        setmsg("Boss HP %.0f -> 1. Hit it once.", hp);
        return true;
    }
    setmsg("The boss gauge's actor was not found.");
    return false;
}
static bool monsters_impl() {
    unsigned char** pool = at<unsigned char**>(kActorPool);
    unsigned n = at<unsigned>(kActorCount);
    if (!pool || n > 4096) { setmsg("Actor pool not readable."); return false; }
    unsigned char* me = player();
    int count = 0;
    for (unsigned i = 0; i < n; i++) {
        unsigned char* a = pool[i];
        if (!a || a == me || a[kActDead] != 0) continue;
        float hp = *(float*)(a + kActHp), mx = *(float*)(a + kActMaxHp);
        if (hp > 1.0f && hp < 1.0e7f && mx >= hp) { *(float*)(a + kActHp) = 1.0f; count++; }
    }
    plog("helper: %d actors set to 1 HP", count);
    setmsg("%d actors set to 1 HP.", count);
    return true;
}
static bool savepos_impl() {
    unsigned char* p = player();
    g_saved = true;
    g_saved_scene = at<int>(kCurScene);
    memcpy(g_saved_pos, p + kActPos, 12);
    g_saved_heading = *(float*)(p + kActHeading);
    plog("helper: position saved (scene %d: %.2f, %.2f, %.2f)", g_saved_scene, g_saved_pos[0], g_saved_pos[1], g_saved_pos[2]);
    setmsg("Position saved (%s: %.1f, %.1f, %.1f).", g_st.room, g_saved_pos[0], g_saved_pos[1], g_saved_pos[2]);
    return true;
}
static bool simple(bool (*fn)(), const char* what, std::string& msg) {
    if (const char* r = helper_gate()) { msg = r; return false; }
    g_st.dirty = true;
    bool ok = guarded(fn, what);
    msg = g_msg;
    return ok;
}
bool heal(std::string& msg) { return simple(heal_impl, "heal", msg); }
bool refill_boost(std::string& msg) { return simple(boost_impl, "boost", msg); }
bool refill_mp(std::string& msg) { return simple(mp_impl, "mp", msg); }
bool boss_hp_1(std::string& msg) { return simple(boss_impl, "boss hp", msg); }
bool monsters_hp_1(std::string& msg) { return simple(monsters_impl, "monsters hp", msg); }
bool save_pos(std::string& msg) {
    if (const char* r = helper_gate()) { msg = r; return false; }
    bool ok = guarded(savepos_impl, "save position");
    msg = g_msg;
    return ok;
}
bool restore_pos(std::string& msg) {
    if (!g_saved) { msg = "No position saved yet."; return false; }
    if (const char* r = why_not()) { msg = r; return false; }
    g_pending = Test();
    g_pending.id = "restore position";
    g_pending.hp_full = g_pending.mp_full = false;
    g_pending_cfg.clear();
    g_stage_scene = g_saved_scene;
    g_stage_has_pos = true;
    memcpy(g_stage_pos, g_saved_pos, sizeof(g_stage_pos));
    g_stage_facing_rad = g_saved_heading;
    plog("GO saved position (scene %d)", g_saved_scene);
    if (!guarded(stage_impl, "stage")) { msg = g_msg; return false; }
    msg = "Reloading the room at the saved position...";
    return true;
}
int get_flag(int idx) { return (idx >= 0 && idx <= 511 && g_st.supported) ? flag(idx) : 0; }
bool set_flag(int idx, int value, std::string& msg) {
    if (const char* r = helper_gate()) { msg = r; return false; }
    if (idx < 0 || idx > 511 || reserved_flag(idx)) { msg = "That flag is not writable from here."; return false; }
    plog("helper: flag %d: %d -> %d (live)", idx, flag(idx), value);
    flag(idx) = value;
    if (idx < 128) at<int>(kRecalc) |= 0x75;
    g_st.dirty = true;
    msg = "Flag written.";
    return true;
}
bool run_helper(const std::string& name, std::string& msg) {
    if (name == "boss_hp_1") return boss_hp_1(msg);
    if (name == "monsters_hp_1") return monsters_hp_1(msg);
    if (name == "heal") return heal(msg);
    if (name == "boost") return refill_boost(msg);
    if (name == "mp") return refill_mp(msg);
    if (name == "invincible") { g_invincible = !g_invincible; msg = g_invincible ? "Invincible ON." : "Invincible OFF."; g_st.dirty = true; return true; }
    if (name == "hp_lock") { g_hp_lock = !g_hp_lock; msg = g_hp_lock ? "HP lock ON." : "HP lock OFF."; g_st.dirty = true; return true; }
    msg = "Unknown helper '" + name + "'.";
    return false;
}

// ---- install -----------------------------------------------------------------------------
static bool bytes_match(uintptr_t a, const unsigned char* want, size_t n) {
    MEMORY_BASIC_INFORMATION mbi;
    a = va(a);
    if (!VirtualQuery((void*)a, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return false;
    return memcmp((void*)a, want, n) == 0;
}
void install() {
    static const unsigned char b_load[]  = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x81, 0xec, 0xe8, 0x01, 0x00, 0x00};
    static const unsigned char b_save[]  = {0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x78, 0x4b, 0x67, 0x00, 0x64, 0xa1};
    static const unsigned char b_level[] = {0xf3, 0x0f, 0x10, 0x1d, 0x04, 0xe7, 0x74, 0x00, 0x8b, 0xd1, 0xf3, 0x0f};
    static const unsigned char b_look[]  = {0x55, 0x8b, 0xec, 0x51, 0x53, 0x56, 0x57, 0x8b, 0xf1, 0x8d, 0x51, 0x01};
    bool ok = bytes_match(kFnSceneLoad, b_load, sizeof(b_load)) && bytes_match(kFnSaveWrite, b_save, sizeof(b_save)) &&
              bytes_match(kFnSetLevel, b_level, sizeof(b_level)) && bytes_match(0x004358B0, b_look, sizeof(b_look));
    g_st.supported = ok;
    if (!ok) {
        plog("install: the exe's code does not match yso_win.exe v1.1.1.0 (Steam) - no game hook installed, panel is read-only");
        return;
    }
    mh_ensure_init();
    MH_STATUS a = MH_CreateHook((void*)va(kFnSceneLoad), (void*)&hk_SceneLoad, &o_SceneLoad);
    MH_STATUS b = MH_CreateHook((void*)va(kFnSaveWrite), (void*)&hk_SaveWrite, &o_SaveWrite);
    if (a == MH_OK) a = MH_EnableHook((void*)va(kFnSceneLoad));
    if (b == MH_OK) b = MH_EnableHook((void*)va(kFnSaveWrite));
    plog("install: exe verified; scene-loader hook=%d save-writer hook=%d (0 = ok)", (int)a, (int)b);
    if (a != MH_OK) {
        g_st.supported = false;
        plog("install: without the scene-loader hook nothing can be staged - panel is read-only");
    }
}

}  // namespace probe
