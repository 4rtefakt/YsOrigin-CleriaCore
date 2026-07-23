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

    // op 0x13d Chr_IsInEventBox(entityId, boxName): is the actor (998=self, 13=player, else
    // by id) inside the named oriented box placed in the map's .SEV scene-event table? An
    // OBB containment test (build/event_re/OP_13D_REGION.md). This is the region-talk trigger
    // predicate — a Stop*_Roi_* controller polls it before firing a TalkCxxx. A null/absent
    // host answers false, so nothing triggers until the .SEV boxes are wired.
    virtual bool chr_in_event_box(int /*entityId*/, const std::string& /*boxName*/) const { return false; }

    // op 0x131 Chr_IsSeedLiving(group): "is any live entity tagged with group-N still
    // in the registry?" (FUN_00570640, EVENTVM.md §6). Maps arg 1..4 -> bit (n<<8) at
    // entity+0x130; scans the live registry for a match. The barrier-fight poll: a
    // KEKKAIZAKO script loops on it and grants its progress flag when it goes false.
    // Always called with arg 1 in the shipped corpus. A null/absent group => false.
    virtual bool any_seed_alive(int /*group*/) const { return false; }

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
    // op 0xd3 Window_Msg(speakerId, voiceParam, nameStrIdx, textStrIdx): open/update the
    // message box with a line. `speaker` picks the face/voice (player id remaps by char);
    // `name`/`text` are the resolved pool strings ("" if the index was < 0). The box
    // stays up until 0xd2. The next Wait_Prompt gates on advance + text fully revealed.
    virtual void window_msg(int /*speaker*/, int /*voice*/, const std::string& /*name*/,
                            const std::string& /*text*/) {}
    virtual void window_close() {}                                // op 0xd2 Window_Close
    // op 0xe2 Caption(name): the area-name card is PRE-RENDERED art (data/image/<name>.dds).
    virtual void caption(const std::string& /*name*/) {}

    // --- cutscene camera ops (CUTSCENE_CAMERA.md) ------------------------------------
    // op 0xc3 Scroll_MoveTo(x,y,z, frames, priority): slide the camera look-at to a world
    // point over `frames` (linear); op 0xc4 Scroll_Return(frames): slide it back to the player.
    virtual void scroll_moveto(float /*x*/, float /*y*/, float /*z*/, int /*frames*/) {}
    virtual void scroll_return(int /*frames*/) {}
    // op 0xcf Shake(ampX, ampY, ampZ, frames): a decaying screen shake — for `frames` frames
    // the camera basis is jittered by a fresh random per-axis offset in [-amp,+amp]. The three
    // floats are per-axis amplitudes, the int a frame duration (CUTSCENE_CAMERA.md §6).
    virtual void shake(float /*ampX*/, float /*ampY*/, float /*ampZ*/, int /*frames*/) {}

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

    // StartScript sink (op 0xb4 StartScript / 0xb8 StartScript2, CUTSCENE_COROUTINE.md §5).
    // A running COROUTINE hands the resolved child-script NAME here so a ScriptManager can
    // spawn it as a concurrent GoScript. Null (the default) => the op is inert — which is
    // exactly the run-to-completion behaviour for static grant extraction, so `run()` and
    // `--grants` never try to load or recurse into children. Only fired in coroutine mode.
    std::function<void(const std::string& name)> on_start_script;

    // Fix (0xb6) / Unfix (0xb7) sinks (CUTSCENE_CAMERA.md §7): a running COROUTINE that Fixes
    // freezes the world (ref-counted); Unfix (and script teardown) releases. The ScriptManager
    // counts these to drive `world_frozen()`. Coroutine-only; null => inert (run-to-completion).
    std::function<void()> on_fix, on_unfix;
    std::int32_t g_flags[0x200] = {0};
    std::unordered_map<int, std::int32_t> locals;  // indices >= 0x200
    std::int32_t acc = 0;
    // outputs collected during a run:
    std::vector<Grant> grants;
    std::vector<int> items;     // give-item ids (sub-op 0x116)
    std::vector<int> acquires;  // "Acquired X" box item ids (sub-op 0xD5, operand 2)
    std::vector<std::string> obj_enable;  // 0x169: object names shown/spawned
    std::vector<std::string> obj_opened;  // 0x175: object names set to opened state
    // 0x16B/0x16C: explicit object visibility, name -> shown. Several rooms place
    // multiple variants of one object at the same spot and let a script pick.
    std::unordered_map<std::string, bool> obj_visible;

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
    std::uint32_t rs[4] = {123456789u, 362436069u, 521288629u, 88675123u};
    std::uint32_t rng_next() {
        const std::uint32_t t = rs[0] ^ (rs[0] << 11);
        rs[0] = rs[1]; rs[1] = rs[2]; rs[2] = rs[3];
        rs[3] = (rs[3] ^ (rs[3] >> 19)) ^ (t ^ (t >> 8));
        return rs[3];
    }

    // Clear per-run state (accumulator, locals, outputs) but keep g_flags, so one
    // VM can persist game state across many script runs.
    void reset_run() {
        acc = 0; locals.clear(); grants.clear(); items.clear();
        acquires.clear(); obj_enable.clear(); obj_opened.clear(); obj_visible.clear();
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
        int fix_held = 0;                  // net Fix(0xb6) this script holds (released on teardown)
    };
    std::vector<Inst> live;
    std::vector<std::string> spawn_queue;   // names queued by StartScript during a tick
    std::size_t peak_live = 0;              // max instances resident in one frame (pre-reap)
    int fix_count = 0;                      // global Fix ref-count -> world_frozen()

    ScriptManager() {
        vm.on_start_script = [this](const std::string& n) { spawn_queue.push_back(n); };
        // Fix/Unfix adjust the current instance's held count and the global ref-count.
        vm.on_fix = [this] { if (cur_inst_) { ++cur_inst_->fix_held; ++fix_count; } };
        vm.on_unfix = [this] {
            if (cur_inst_ && cur_inst_->fix_held > 0) { --cur_inst_->fix_held; if (fix_count > 0) --fix_count; }
        };
    }

    // The world is frozen while any live script holds a Fix (CUTSCENE_CAMERA.md §7).
    bool world_frozen() const { return fix_count > 0; }

    // Register a root (or already-loaded child) instance and tick it once this frame,
    // matching StartScript's immediate first tick (§5a). Returns the live-index.
    std::size_t start_root(std::shared_ptr<const Xso> s, std::string name);
    // Spawn a child by NAME via the loader (the StartScript path). Dedup: refuse if a live
    // instance with the same name already exists (single-instance; §5a). Ticks the child
    // once immediately. Returns false if deduped or the loader can't find it.
    bool start(const std::string& name);
    // One engine frame: tick every live script once, drain StartScript spawns (children
    // tick this frame too), then reap finished scripts. A no-op while paused.
    void tick_all();
    bool any_live() const;

  private:
    void tick_inst(Inst& in);          // swap regs into `vm`, run one frame, save back
    Inst* cur_inst_ = nullptr;         // the instance being ticked (for Fix/Unfix attribution)
};

}  // namespace cleria
