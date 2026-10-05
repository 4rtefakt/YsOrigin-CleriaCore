// XSO event-script VM. Container "XSR\0"; header 0x24 bytes, +0x1C = code length
// in 32-bit words, code at +0x24. Instruction encoding and control flow verified
// against the game's interpreter (FUN_004472e0 / FUN_00566350). See
// docs/formats/ and tools/xso_dis.py (the validated static reference).
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace cleria {

struct XsoInstr {
    std::uint32_t pc = 0;
    std::uint32_t word = 0;
    std::uint32_t cls = 0;          // word >> 24
    std::uint32_t sub = 0;          // class 2 only: (word >> 12) & 0xFFF
    std::vector<std::int32_t> ops;  // class 2 operands (int32 immediates)
    std::uint32_t length = 1;       // in words
    bool unknown = false;           // unrecognised class -> stop
};

struct Grant {
    std::string op;          // "set","add","sub","mul","copy","and","or","xor","zero"
    std::int32_t index = 0;  // g_flags index (< 0x200)
    std::int32_t value = 0;  // immediate, or 0 for copy/set_reg (see has_value)
    bool has_value = true;
};

class Xso {
  public:
    // Parse an XSR/XSO blob. Throws std::runtime_error on a bad container.
    static Xso parse(std::span<const std::uint8_t> bytes);

    // Linear disassembly (static walk; stops on an unknown class).
    std::vector<XsoInstr> disasm() const;

    // Every g_flags store (matches tools/xso_dis.py grants()).
    std::vector<Grant> grants() const;

    std::uint32_t code_words() const { return std::uint32_t(code_.size()); }
    const std::vector<std::int32_t>& words() const { return code_; }

    // String-pool entry (object/state names). "" if out of range. Header dword
    // 0x20 = count; a count-length offset table follows the code; then the pool.
    std::string string(int idx) const {
        return (idx >= 0 && idx < int(strings_.size())) ? strings_[idx] : std::string();
    }
    int string_count() const { return int(strings_.size()); }

  private:
    std::vector<std::int32_t> code_;
    std::vector<std::string> strings_;
};

// The world a script spawns actors into and queries. ** Kept to primitive types on
// purpose: ** it lets XsoVm drive live actors without xso.cpp taking a dependency on
// actor.hpp / world.hpp / the stat tables, which is what keeps the VM headlessly
// testable — the same isolation that let every other module here be validated alone.
// ActorWorld (or a thin adapter) implements it. A null host makes the actor ops
// no-ops, which is exactly how every existing XSO test already runs.
struct XsoHost {
    virtual ~XsoHost() = default;

    // op 0x12c, the spawn op. Operand layout is [C-DIS] and corpus-validated over
    // 1198 sites (build/entity_re/ENTITY.md): (entity_id, cast_type, x, y, z,
    // yaw_deg, group). id 0 => auto-assign; group 0 => untagged, only 1..4 honoured.
    // x/y/z/yaw arrive as floats — the VM bit-casts the raw operand words.
    virtual void spawn(int entity_id, int cast_type, float x, float y, float z,
                       float yaw_deg, int group) = 0;

    // op 0x13f: is the entity with this id alive? The CONDITION is [C-DIS] —
    // `find(op0) != null && hp >= 1.0`. Absent id => false.
    virtual bool is_alive(int entity_id) const = 0;
    // op 0x10e EnclosePlayer: the arena rectangle {xmin, zmin, xmax, zmax}.
    virtual void enclose(float /*xmin*/, float /*zmin*/, float /*xmax*/, float /*zmax*/) {}

    // op 0x13d Chr_IsInEventBox(entityId, boxName): is the actor (998=self, 13=player, else
    // by id) inside the named oriented box placed in the map's .SEV scene-event table? An
    // OBB containment test (build/event_re/OP_13D_REGION.md). This is the region-talk trigger
    // predicate — a Stop*_Roi_* controller polls it before firing a TalkCxxx. A null/absent
    // host answers false, so nothing triggers until the .SEV boxes are wired.
    virtual bool chr_in_event_box(int /*entityId*/, const std::string& /*boxName*/) const { return false; }

    // Cutscene actor control (Chr family, EVENTVM_HANDLERS.md §4). The player (id
    // 13/999) is special-cased by the view (its position is not a world actor).
    virtual void chr_pos(int /*id*/, float /*x*/, float /*y*/, float /*z*/) {}   // 0x140 teleport
    virtual void chr_dir(int /*id*/, float /*deg*/) {}                           // 0x133 facing
    // 0x143 Chr_MoveTo(id, x, z, arriveRadius, speedScale) (FUN_00570ad0, audit event_vm #32):
    // walk until within `radius` (+0x364 = max(0.025, r)^2), at the actor's base walk speed
    // times `speed_scale` (+0x368).
    virtual void chr_moveto(int /*id*/, float /*x*/, float /*z*/, float /*radius*/, float /*speed_scale*/) {}
    virtual bool chr_moving(int /*id*/) const { return false; }                  // 0x100 Wait_ChrMoving
    virtual void chr_alpha(int /*id*/, float /*end_alpha*/) {}                    // 0x154 Chr_Alpha (end)
    virtual void chr_lookat(int /*id*/, int /*target*/) {}                        // 0x134/0x136 face actor
    virtual void chr_lookat_pos(int /*id*/, float /*x*/, float /*z*/) {}          // 0x137 face point
    virtual void chr_animate(int /*id*/, int /*motion*/) {}                        // 0x151 pin/clear pose
    // Standing bust portraits (0xe3 Portrait_Open / 0xe4 Portrait_Close / 0xe5 Portrait_Move):
    // a GoXx type-6 sprite loaded from IMAGE/<name>.dds, positioned in the 2D overlay. `name`
    // is the DDS stem (e.g. "t_840a", "v_003a"); (x,y) is the authored 2D anchor.
    virtual void portrait_open(const std::string& /*name*/, float /*x*/, float /*y*/) {}
    virtual void portrait_close(const std::string& /*name*/) {}
    virtual void portrait_move(const std::string& /*name*/, float /*x*/, float /*y*/) {}

    // op 0x12d Chr_Delete(id): remove the actor with this instance id. Cutscene NPCs
    // spawn (0x12c), talk, then delete themselves; enemies use it too. Null host / an
    // absent id is a no-op.
    virtual void despawn(int /*entity_id*/) {}

    // op 0x131 Chr_IsSeedLiving(group): "is any live entity tagged with group-N still
    // in the registry?" (FUN_00570640, EVENTVM.md §6). Maps arg 1..4 -> bit (n<<8) at
    // entity+0x130; scans the live registry for a match. The barrier-fight poll: a
    // KEKKAIZAKO script loops on it and grants its progress flag when it goes false.
    // Always called with arg 1 in the shipped corpus. A null/absent group => false.
    virtual bool any_seed_alive(int /*group*/) const { return false; }

    // Position lookup behind the region-query ops 0x139/0x13a/0x13b (Chr_IsInSphere/
    // Cylinder/Box). The workers (FUN_00570c10/c80/cf0) resolve the actor (998=self,
    // else FUN_00434d50 by id) and read its position (+0x50/+0x54/+0x58); the GEOMETRY
    // lives in the VM (operand-supplied), only the position comes from the host.
    // Returns false when the id resolves to no actor — the engine's null path.
    virtual bool actor_pos(int /*entityId*/, float /*out*/[3]) const { return false; }

    // op 0x8c Flag_IsEquip(itemId): acc = FUN_00443ce0(itemId), "is this item currently
    // equipped" (EVENTVM_HANDLERS_2.md). CleriaCore has no runtime equipment model yet,
    // so the default answers false (nothing equipped) rather than guessing.
    virtual bool is_equipped(int /*itemId*/) const { return false; }

    // op 0xc1 IsFix(): acc = (fix counter 0x75ad90 + pause counter 0x75ad94) != 0 —
    // "is any script holding the world fixed/paused?" (FUN_0040b2c0,
    // EVENTVM_HANDLERS_5.md §1). The ScriptManager's ref-counted Fix state answers it.
    virtual bool is_fix() const { return false; }

    // op 0xc2 IsUnderWaterWorld(): acc = (int @0x76a544 == 2) — the world-mode global
    // (EVENTVM_HANDLERS_5.md). CleriaCore has no underwater-world mode yet; default false.
    virtual bool is_underwater_world() const { return false; }

    // op 0x13e Chr_IsThisChr(id): acc = (resolve(id) == this script's bound actor
    // GoScript+0x240), FUN_005706c0 [C-DIS]. CleriaCore scripts don't model a bound
    // actor yet, so the default answers false.
    virtual bool chr_is_this(int /*entityId*/) const { return false; }

    // op 0x11a Eft_LocatePosAddStr(id, x,y,z, a, b, name, c): place a named effect at a
    // world position, persistent until killed. [C-DIS] signature ifffffsf. The barrier
    // ward is `Eft_LocatePosAddStr(210 "map_kekkai", pos, .., "eft_barrier", ..)` — id
    // 210 resolves through eftlist to IVENT/MAP_KEKKAI.EF2. a/b/c are placement params
    // (rotation/scale) passed through for the host to interpret.
    virtual void locate_effect(int /*id*/, float /*x*/, float /*y*/, float /*z*/,
                               float /*a*/, float /*b*/, const std::string& /*name*/) {}
    // op 0x119 Eft_Kill(name): remove the named located effect. The barrier break
    // (KEKKAIBREAK) calls `Eft_Kill("eft_barrier")`, so the ward vanishes on clear.
    virtual void kill_effect(const std::string& /*name*/) {}

    // --- cutscene condition-wait predicates (CUTSCENE_COROUTINE.md §4b) --------------
    // A condition-wait op re-tests its predicate every frame (the VM rewinds the PC to
    // the op until it passes). The host answers each; the DEFAULTS all return true so a
    // null host / headless run never blocks (and run-to-completion never calls these).
    virtual bool wait_prompt() { return true; }              // 0xf3: advance button pressed?
    virtual bool wait_fader() { return true; }               // 0xf1: fade finished?
    virtual bool wait_window(bool /*wantOpen*/) { return true; } // 0xf5/0xf6: msg window open/closed?
    virtual bool wait_sound(int /*channel*/) { return true; }    // 0xf7: SE on channel done?
    virtual bool wait_script_done(const std::string& /*name*/) { return true; } // 0xf4: child ended?

    // --- dialogue / caption ops (CUTSCENE_DIALOGUE.md) --------------------------------
    // op 0xd3 Window_Msg(speakerId, voiceParam, name, text): open/update the message box
    // with a line. `speaker` picks the face/voice (player id remaps by char); `name`/`text`
    // are the string operands ("" when NULL). FUN_00573e60 defaults an EMPTY NAME to the
    // speaker's cast record name (+0x10) -- the host does that (it owns the cast table;
    // audit event_vm #26). The box stays up until 0xd2; Wait_Prompt gates on advance.
    virtual void window_msg(int /*speaker*/, int /*voice*/, const std::string& /*name*/,
                            const std::string& /*text*/) {}
    virtual void window_close() {}                                // op 0xd2 Window_Close
    // op 0xe2 Caption(name): the area-name card is PRE-RENDERED art (data/image/<name>.dds).
    virtual void caption(const std::string& /*name*/) {}
    // op 0xd5 Window_Treasure(x, y, item, s): the "Acquired X" box (FUN_005781f0 draws a
    // DIALOG0 9-slice at screen (x,y) with the item icon; x=512 => centred). Its TEXT is the
    // op3 string verbatim (e.g. "Acquired<newline><color:..>Ring Mail<color:>."), and it writes
    // no inventory: the grant is the script's own Flag_SetInt(item, 1) (audit event_vm
    // #27/#28). The script parks on the following Wait_Prompt, so the player dismisses it.
    virtual void acquire_item(float /*x*/, float /*y*/, int /*item_id*/, const std::string& /*text*/) {}

    // --- cutscene camera ops (CUTSCENE_CAMERA.md) ------------------------------------
    // op 0xc3 Scroll_MoveTo(x,y,z, frames, priority): slide the camera look-at to a world
    // point over `frames` (linear); op 0xc4 Scroll_Return(frames): slide it back to the player.
    virtual void scroll_moveto(float /*x*/, float /*y*/, float /*z*/, int /*frames*/) {}
    virtual void scroll_return(int /*frames*/) {}
    // op 0xcf Shake(ampX, ampY, ampZ, frames): a decaying screen shake — for `frames` frames
    // the camera basis is jittered by a fresh random per-axis offset in [-amp,+amp]. The three
    // floats are per-axis amplitudes, the int a frame duration (CUTSCENE_CAMERA.md §6).
    virtual void shake(float /*ampX*/, float /*ampY*/, float /*ampZ*/, int /*frames*/) {}
    // Screen FADER (EVENTVM_HANDLERS.md §4, fader obj 0x769f18): a fullscreen colored
    // overlay whose alpha ramps `current`→`target` at `1/frames` per frame. 0xc8 sets
    // current instantly, 0xc9 the target, 0xca the RGB(A), 0xcc the frame count.
    // Wait_Fader (0xf1) blocks until current==target. (Used for cut-to-black scene
    // transitions and the crossfades in the S_1000 talks.)
    virtual void fader_current(float /*alpha01*/) {}
    virtual void fader_target(float /*alpha01*/) {}
    virtual void fader_color(int /*rgb*/, int /*a*/) {}
    virtual void fader_time(int /*frames*/) {}
    // Camera_Set (0xc5) forces an SCM region index; Camera_Return (0xc6) reverts to
    // the position pick (CUTSCENE_CAMERA.md §4). Frames = blend, ignored for now.
    virtual void camera_set(int /*region*/, int /*frames*/) {}
    virtual void camera_return(int /*frames*/) {}

    // --- scene flow (build/event_re/SCENEFLOW.md) -------------------------------------
    // op 0x97 ChangeScene(name, entry): FUN_00430ef0(sceneId, force 0) — a request for the
    // START OF THE NEXT FRAME (refused while one is pending); `entry` is ignored. The engine
    // then freezes every script (the caller finishes its current burst) and tears the room
    // down. `name` is a scene path ("s_10\s_1009\" or "demo3\").
    virtual void change_scene(const std::string& /*name*/) {}
    // op 0xcd CrossFade(start, rate, name, entry): freeze this frame's image and dissolve it
    // into the live view, opacity `start` then += `rate` per step. A non-NULL name (never in
    // the corpus) would also change scene.
    virtual void crossfade(float /*start*/, float /*rate*/) {}
    // op 0x94 SetHealSpot(x, y, z): a heal-spot object for this room (the statue heal).
    virtual void heal_spot(float /*x*/, float /*y*/, float /*z*/) {}
    // op 0xa7 SetWarpEnableIndex(i): unlock goddess-statue warp destination i (saved).
    virtual void warp_enable(int /*index*/) {}
    // op 0xa6 GoWarpPoint(): ChangeScene to warp table[flag 137] (the Wing).
    virtual void go_warp_point() {}

    // op 0xa0, the BULK SCT SPAWN (build/roomload_re/ROOMLOAD.md §3). The engine's
    // handler FUN_004341a0 loads the room's .SCT (named by the operand) and spawns
    // every placement through the CASTINFO factory. We pre-load the host with the
    // room's placements, so the operand (the SCT basename) is ignored here. This runs
    // exactly once per room script, before any 0x12c. Default: no host placements set,
    // so nothing spawns.
    virtual void spawn_placements() {}
};

// Dynamic executor: runs control flow (jumps, accumulator conditionals) and the
// flag-store / give-item sub-ops. Unimplemented sub-ops are skipped by length
// (safe — the length is encoded), so a script always runs to its end op.
struct XsoVm {
    // Optional: set to drive live actors. Null => the actor ops (0x12c/0x13f) are
    // inert and the VM behaves exactly as it did before actors existed.
    XsoHost* host = nullptr;

    // The GoScript variable block +0x244..+0x2b7 that `$I0/$I1`, `$F0/$F1`, `$S0/$S1` operands
    // read (audit event_vm #11): filled by StartScript's extra operands or by the map object
    // whose script this is (its SOB name and i0/i1/f0; sob.hpp). Per running script.
    struct Vars {
        std::int32_t i[2] = {0, 0};
        float f[2] = {0.0f, 0.0f};
        std::string s[2];
    };
    Vars vars;

    // StartScript sink (op 0xb4 StartScript / 0xb8 StartScript2, CUTSCENE_COROUTINE.md §5).
    // A running COROUTINE hands the resolved child-script NAME here so a ScriptManager can
    // spawn it as a concurrent GoScript, with the operands after the name marshalled into the
    // child's variable block (FUN_00434970). Null (the default) => the op is inert, which is
    // exactly the run-to-completion behaviour for static grant extraction, so `run()` and
    // `--grants` never try to load or recurse into children. Only fired in coroutine mode.
    std::function<void(const std::string& name, const Vars& child)> on_start_script;

    // Fix(slot) (0xb6) / Unfix (0xb7) sinks (FUN_0056e8d0, audit event_vm Part C): a script
    // holds at most one fix. Fix(slot) releases its previous one, then counts slot
    // (0x75ad90[slot]); Unfix releases it. Window_Msg / Window_Treasure / Window_Menu fix
    // slot 0 implicitly when the script holds none (`on_window_fix`). Coroutine-only; null
    // => inert (run-to-completion).
    std::function<void(int slot)> on_fix;
    std::function<void()> on_unfix, on_window_fix;
    std::int32_t g_flags[0x200] = {0};
    // `itemArr` u32[54] @0x76a654 (EQUIP.md): per-item ownership/availability, 0 at new
    // game (the memset covers it), written by the scene dispatcher FUN_00431840. Read by
    // IsXXX (0x9a) op0==7/8 through FUN_00420210. Lives in the save block like g_flags.
    std::int32_t item_arr[54] = {0};
    std::unordered_map<int, std::int32_t> locals;  // indices >= 0x200
    std::int32_t acc = 0;
    // outputs collected during a run:
    std::vector<Grant> grants;
    // 0x116 Effect_GetItem icon ids: a VISUAL item sprite only, never a grant (the grant is
    // the script's Flag_SetInt(item, 1); audit event_vm #28).
    std::vector<int> items;
    std::vector<int> acquires;  // "Acquired X" box item ids (sub-op 0xD5, operand 2)
    // 0x169 Mesh_Delete (FUN_00572a30: +0x11 |= 2, +0x60 &= ~0x80, a destroy request; audit
    // event_vm #29): object names this script removed. (It was long misread as "enable".)
    std::vector<std::string> obj_deleted;
    // Deprecated duplicate of obj_deleted under its old (wrong) name, kept so tools that still
    // print it (apps/cleria-xso) build. Do not read it for semantics.
    std::vector<std::string> obj_enable;
    std::vector<std::string> obj_opened;  // 0x175: object names given a state (legacy view)
    // 0x175 in full: object name -> the YMO animation CLIP it was posed to ("OPEN" for a
    // looted chest, "UP"/"STOP" for S_1004's stepping stones, "TURN_ON"/"OFF" for its switch).
    std::unordered_map<std::string, std::string> obj_state;
    // 0x173 Obj_SetPos: object name -> world position it was moved to.
    struct ObjPos { float x, y, z; };
    std::unordered_map<std::string, ObjPos> obj_place;
    // 0x188 Obj_BindShadow: shadow object name -> the object it follows. A shadow must
    // inherit its owner's visibility and placement, or hiding/moving a stepping stone
    // leaves its drop shadow behind as a black quad on the floor.
    std::unordered_map<std::string, std::string> obj_shadow_of;
    // 0x16B Mesh_SetVisible: name -> shown (+0x298 bit 0x20 = hidden). Several rooms place
    // multiple variants of one object at the same spot and let a script pick.
    std::unordered_map<std::string, bool> obj_visible;
    // 0x16C Mesh_EnableCollision: name -> collidable (+0x298 bit 0x40 = no collision).
    std::unordered_map<std::string, bool> obj_collide;

    std::int32_t& flag(int idx) { return idx < 0x200 ? g_flags[idx] : locals[idx]; }

    // The comparison dispatcher behind sub-ops 0x61/0x62/0x63 (FUN_00570240).
    // `sel` is the operator, and its encoding is exactly (jump class - 4) — the same
    // ordering as the class 5..0xA conditional jumps.
    static std::int32_t compare(std::int32_t a, std::int32_t sel, std::int32_t b) {
        switch (sel) {
            case 0:  return 0;
            case 1:  return a < b;
            case 2:  return a == b;
            case 3:  return a <= b;
            case 4:  return a > b;
            case 5:  return a != b;
            case 6:  return a >= b;
            default: return 1;          // 7+
        }
    }

    // xorshift128, as the game implements it (FUN_00439120). Sub-op 0x81 rolls a
    // percentage from it, so scripts using it are non-deterministic by design.
    std::uint32_t rs[4] = {123456789u, 362436069u, 521288629u, 916191069u};   // .data 0x74622c (read)
    std::uint32_t rng_next() {
        const std::uint32_t t = rs[0] ^ (rs[0] << 11);
        rs[0] = rs[1]; rs[1] = rs[2]; rs[2] = rs[3];
        rs[3] = (rs[3] ^ (rs[3] >> 19)) ^ (t ^ (t >> 8));
        return rs[3];
    }

    // Clear per-run state (accumulator, locals, outputs) but keep g_flags, so one
    // VM can persist game state across many script runs.
    void reset_run() {
        acc = 0; locals.clear(); grants.clear(); items.clear(); obj_enable.clear();
        acquires.clear(); obj_deleted.clear(); obj_opened.clear(); obj_visible.clear(); obj_collide.clear();
        obj_state.clear(); obj_place.clear(); obj_shadow_of.clear();
    }

    // NEW GAME flag initialization — faithful port of FUN_0041fd10 (the exe's own
    // main\game\g_flags.cpp), per build/event_re/NEWGAME_FLAGS.md [C-DIS]:
    //   - the whole array is wiped (the engine memsets the 0x1fc8 save block),
    //   - flags 0..127 are the owned-item/inventory array, filled with -1 (= not owned),
    //   - a fixed header set is written, then four per-character stores: the two
    //     starting items {char-1, char*6} and the header pair 151/152 mirroring them.
    // `character` is g_flags[150] (1 Yunica / 2 Hugo / 3 Toal; 0 = the exe's unused
    // Adol row — same pattern, kept for fidelity). Flags 225/232/234/235/236/237 stay 0:
    // the intro XSO sets the S_1000 routing flag at runtime, never this init.
    // `time_attack` mirrors the DAT_0074e25e∈{1,2} gate that sets flag 193.
    void new_game(int character, bool time_attack = false) {
        for (int i = 0;   i < 0x200; ++i) g_flags[i] = 0;    // the memset
        for (int i = 0;   i < 0x80;  ++i) g_flags[i] = -1;   // inventory: not owned
        g_flags[119] = 1;                                    // always-owned base item
        g_flags[149] = 1;                                    // character-block header
        g_flags[150] = character;                            // preserved across the wipe
        g_flags[153] = -1;                                   // header sentinel
        g_flags[154] = 1;
        g_flags[155] = 1;
        g_flags[193] = time_attack ? 1 : 0;
        g_flags[228] = 16;
        const int item_a = character - 1 < 0 ? 3 : character - 1;   // char 0 -> {3,24}
        const int item_b = character == 0 ? 24 : character * 6;
        g_flags[item_a] = 1;                                 // starting equipment
        g_flags[item_b] = 1;
        g_flags[151] = item_a;
        g_flags[152] = item_b;
    }

    // Execute from word `entry`; bounded by max_steps. Returns steps executed. This is
    // the RUN-TO-COMPLETION path (static grant extraction): Wait_* ops are inert no-ops.
    int run(const Xso& script, std::uint32_t entry = 0, int max_steps = 2'000'000);

    // ## Coroutine (resumable) execution — the runtime model for cutscenes/events.
    // CUTSCENE_COROUTINE.md: FUN_00566350 ticks a GoScript once per frame — if a wait
    // counter is armed it counts down, else it BURSTS instructions until a Wait_* op
    // parks the burst or the script ends. Counter waits (Wait_Time/Interval) arm a frame
    // countdown; condition waits (Wait_Prompt/Fader/Window/Sound/Script/FlagEq) rewind
    // the PC to the same op and re-test each frame (no stored condition). One coroutine
    // per XsoVm for now (concurrent StartScript children come later); acc/locals/g_flags
    // are shared with the VM, so a cutscene reads/writes the same game flags.
    enum class Tick { Ended, Waiting };
    const Xso* co_script = nullptr;   // bound script (caller keeps it alive); null = inactive
    std::uint32_t co_pc = 0;          // saved PC (engine GoScript+0x1e4)
    std::int32_t co_wait = 0;         // frame countdown (engine +0x1e8)

    // Begin a resumable run of `s` from `entry`. Shares g_flags; resets acc/pc/wait.
    void co_begin(const Xso& s, std::uint32_t entry = 0) {
        co_script = &s; co_pc = entry; co_wait = 0; acc = 0;
    }
    bool co_active() const { return co_script != nullptr; }
    // Run one frame: count down a wait, else burst until a Wait_* parks or the script ends.
    Tick tick();

  private:
    // One instruction at code word `pc` (updated in place). `coroutine` enables the
    // Wait_* ops (park the burst); false = run-to-completion (waits are no-ops). Shared
    // by run() and tick() so the opcode semantics live in exactly one place.
    enum class Step { Continue, End, Unknown, Park };
    Step step_one(const Xso& script, std::uint32_t& pc, bool coroutine);
    // Typed operand reads (FUN_00566190 int / FUN_00566250 float / FUN_00566100 string): the
    // 2-bit type per operand in the descriptor words; a mismatched type reads 0 / "" and a
    // `$`-string reads this script's variable block. `old` = the op's pc.
    std::uint32_t op_type(const Xso& s, std::uint32_t old, std::uint32_t i) const;
    std::int32_t op_int(const Xso& s, std::uint32_t old, std::uint32_t i) const;
    float op_float(const Xso& s, std::uint32_t old, std::uint32_t i) const;
    std::string op_str(const Xso& s, std::uint32_t old, std::uint32_t i) const;
    // A condition-wait predicate (0xf1/0xf3/0xf4/0xf5/0xf6/0xf7/0xf8): true => proceed.
    bool eval_wait(std::uint32_t sub, const Xso& script, std::uint32_t old,
                   std::uint32_t argoff, std::uint32_t nargs);
};

// Resolves a StartScript child NAME to a parsed script. The host owns filesystem access
// and decompression (the VM/manager stay headless and testable); the manager keeps the
// returned script alive for the child's lifetime. Return null if the target doesn't exist
// (the engine throws "StartScript target doesn't exist"; we skip the spawn instead).
struct ScriptLoader {
    virtual ~ScriptLoader() = default;
    virtual std::shared_ptr<const Xso> load_script(const std::string& name) = 0;
};

// The concurrent event-script runtime (CUTSCENE_COROUTINE.md §2/§5): the engine's
// `DAT_0073014c` live-GoScript array and its per-frame driver `FUN_004281c0`. Owns the
// live set of script instances, ticks each once per frame, spawns StartScript children
// (which tick the same frame), and reaps finished scripts. Every instance SHARES one flag
// array (the game state) held by the embedded scratch `vm`; each instance carries its own
// PC / wait counter / accumulator / locals, loaded into the scratch VM around each tick.
struct ScriptManager {
    XsoVm vm;                          // shared g_flags + host; scratch coroutine registers
    ScriptLoader* loader = nullptr;    // resolves child names -> scripts (host-side FS)
    bool paused = false;               // the global freeze byte DAT_0074e686 (§2/§3 pause gate)

    // One live GoScript. Resume state (pc/wait/acc/locals) is the coroutine; the shared
    // g_flags live in `vm`, so every instance reads/writes the same game flags.
    struct Inst {
        std::shared_ptr<const Xso> script;
        std::string name;
        std::uint32_t pc = 0;
        std::int32_t wait = 0;
        std::int32_t acc = 0;
        std::unordered_map<int, std::int32_t> locals;
        bool finished = false;
        int fix_slot = -1;                 // +0x230: the fix slot this script holds (-1 none)
        XsoVm::Vars vars;                  // +0x244..: $I/$F/$S
    };
    std::vector<Inst> live;
    struct Spawn { std::string name; XsoVm::Vars vars; };
    std::vector<Spawn> spawn_queue;         // children queued by StartScript during a tick
    std::size_t peak_live = 0;              // max instances resident in one frame (pre-reap)
    int fix_counts[2] = {0, 0};             // 0x75ad90 / 0x75ad94: the two fix slots

    ScriptManager() {
        vm.on_start_script = [this](const std::string& n, const XsoVm::Vars& v) {
            spawn_queue.push_back({n, v});
        };
        vm.on_fix = [this](int slot) { if (cur_inst_) fix(*cur_inst_, slot); };
        vm.on_unfix = [this] { if (cur_inst_) unfix(*cur_inst_); };
        vm.on_window_fix = [this] { if (cur_inst_ && cur_inst_->fix_slot < 0) fix(*cur_inst_, 0); };
    }

    // The world is frozen while any live script holds a fix (IsFix = the two counters'
    // sum, FUN_0040b2c0; CUTSCENE_CAMERA.md §7).
    bool world_frozen() const { return fix_counts[0] + fix_counts[1] > 0; }

    // Register a root (or already-loaded child) instance and tick it once this frame,
    // matching StartScript's immediate first tick (§5a). Returns the live-index.
    std::size_t start_root(std::shared_ptr<const Xso> s, std::string name);
    // Spawn a child by NAME via the loader (the StartScript path). Dedup: refuse if a live
    // instance with the same name already exists (single-instance; §5a). Ticks the child
    // once immediately. Returns false if deduped or the loader can't find it.
    bool start(const std::string& name, const XsoVm::Vars& vars = {});
    // One engine frame: tick every live script once, drain StartScript spawns (children
    // tick this frame too), then reap finished scripts. A no-op while paused.
    void tick_all();
    bool any_live() const;

    // Cutscene SKIP support: zero every live script's pending Wait_Time countdown so the
    // next tick_all() bursts straight through the delays. Side effects still execute in
    // order — only the waiting collapses. Caller loops tick_all() until world_frozen()
    // clears. (The per-instance `wait` is the saved co_wait; see tick_inst.)
    void skip_waits() { for (Inst& in : live) if (!in.finished) in.wait = 0; }

  private:
    void tick_inst(Inst& in);          // swap regs into `vm`, run one frame, save back
    void fix(Inst& in, int slot);      // FUN_0056e8d0: release the held slot, count `slot`
    void unfix(Inst& in);
    Inst* cur_inst_ = nullptr;         // the instance being ticked (for Fix/Unfix attribution)
};

}  // namespace cleria
