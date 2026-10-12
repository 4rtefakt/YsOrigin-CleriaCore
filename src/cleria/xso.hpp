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
    // CleriaCore mods (content text.ini, build/shell_re/CONTENT.md §6): replace a pool entry after
    // parse. Not retail. False when idx is out of range.
    bool set_string(int idx, std::string s) {
        if (idx < 0 || idx >= int(strings_.size())) return false;
        strings_[std::size_t(idx)] = std::move(s);
        return true;
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
// 0x187 Mesh_Locate's operands (XsoHost::mesh_locate).
struct ObjLocateArgs { std::string ymo, name; float x = 0, y = 0, z = 0, rx = 0, ry = 0, rz = 0; };
// op 0x111 FireBullet's operands as FUN_00571500 hands them to FUN_00593550: `f` is the stack
// block f0..f12 (speed, accel, gravity, power, knock, the int slots +0x158/+0x15c/+0x160 as raw
// bits, then +0x164..+0x174), i.e. bullet::block_spawn's 13 floats.
struct FireBulletOp {
    int actor = 0, kind = 0, effect = 0;
    float pos[3] = {0, 0, 0}, dir[3] = {0, 0, 0};
    float f[13] = {};
};

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
    virtual void chr_alpha(int /*id*/, float /*start*/, float /*end*/, int /*frames*/) {}   // 0x154 Chr_Alpha
    // The facing model (build/event_re/CHR_OPS.md §6): the ops write the look block, the actor
    // tick turns toward it at the turn rate.
    virtual void chr_lookat(int /*id*/, int /*target*/) {}                        // 0x136 track actor (0 = release)
    virtual void chr_dir_chr(int /*id*/, int /*target*/) {}                       // 0x134 one-shot face actor
    virtual void chr_lookat_pos(int /*id*/, float /*x*/, float /*z*/) {}          // 0x137 track point
    virtual void chr_look_dir(int /*id*/, float /*deg*/) {}                        // 0x138 fixed direction
    virtual void chr_turn_speed(int /*id*/, float /*mult*/) {}                     // 0x132 turn-rate factor
    virtual void chr_animate(int /*id*/, int /*motion*/) {}                        // 0x151 ASO motion; clears 0x152
    virtual void chr_pattern(int /*id*/, int /*tag*/, int /*frame*/) {}            // 0x152 sprite pin (§4)
    virtual void chr_run(int /*id*/, bool /*run*/) {}                              // 0x145 RunOrWalk (§5)
    virtual void chr_tremble(int /*id*/, int /*ticks*/, int /*period*/, float /*dx*/, float /*dy*/) {}   // 0x162 (§7)
    virtual void chr_foot_sound(int /*id*/, int /*on*/) {}                         // 0x159 (§3)
    virtual void chr_fly(int /*id*/, bool /*on*/) {}                               // 0x15d FlyOnOff (§8)
    virtual void chr_flyto(int /*id*/, float /*x*/, float /*y*/, float /*z*/, float /*radius*/, float /*scale*/) {}   // 0x144
    // Standing bust portraits (0xe3 Portrait_Open / 0xe4 Portrait_Close / 0xe5 Portrait_Move):
    // a GoXx type-6 sprite loaded from IMAGE/<name>.dds, positioned in the 2D overlay. `name`
    // is the DDS stem (e.g. "t_840a", "v_003a"); (x,y) is the authored 2D anchor.
    virtual void portrait_open(const std::string& /*name*/, float /*x*/, float /*y*/) {}
    virtual void portrait_close(const std::string& /*name*/) {}
    // 0xe5 Portrait_Move(s, x, y, frames, curve, ?) tweens (curve 0 smoothstep, 2 sine, else
    // linear); 0xe6 Color(s, four corner ARGBs); 0xe7 Priority(s, p); 0xe8 FadeSpeed(s, f)
    // (build/event_re/WNDDIALOG_DRAW.md §8).
    virtual void portrait_move(const std::string& /*name*/, float /*x*/, float /*y*/, int /*frames*/, int /*curve*/) {}
    virtual void portrait_color(const std::string& /*name*/, const std::uint32_t* /*corners4*/) {}
    virtual void portrait_priority(const std::string& /*name*/, int /*p*/) {}
    virtual void portrait_fade_speed(const std::string& /*name*/, float /*s*/) {}

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
    // 0x12F Chr_LocateDummy(id, x, y, z) (FUN_00570350): an actor of CASTINFO record 1
    // (CAST_C000: class 4, no sprite) at the point, created when `id` is not live; scripts
    // attach talk scripts to it (the locked doors, the Blue Moon plate).
    virtual void chr_locate_dummy(int /*id*/, float /*x*/, float /*y*/, float /*z*/) {}
    // 0x141 Chr_Jump(id, v) (FUN_005709d0): +0x330 = v, +0x54 += v, +0x388 = 0, +0x9d6 = 1.
    virtual void chr_jump(int /*id*/, float /*v*/) {}
    // 0x148 Chr_Emotion(id, kind, yoff) (FUN_005710d0, CHR_OPS.md §1): a bubble over the actor.
    virtual void chr_emotion(int /*id*/, int /*kind*/, float /*yoff*/) {}
    // 0x153 Chr_Shadow(id, on) (FUN_00570a80): +0x12c bit 1 = (on == 0), the floor shadow hidden.
    virtual void chr_shadow(int /*id*/, bool /*on*/) {}

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
    // op 0x118 Eft_LocateChr(eff, id, dist, deg, dy, scale, mode, name) (FUN_0056f600, build/event_re/
    // EVENTVM_HANDLERS_4.md §4.1): effect `eff` on actor `id` (998 = the script's own). mode 0 places it
    // once at the actor's origin + (cos, sin)(heading + deg) * dist, dy up; mode != 0 attaches it to the
    // actor's matrix at local (0, dy, -dist) turned by deg (FUN_00451c20, attach mode = `mode`).
    virtual void locate_effect_chr(int /*eff*/, int /*id*/, float /*dist*/, float /*deg*/, float /*dy*/,
                                   float /*scale*/, int /*mode*/, const std::string& /*name*/) {}
    // op 0x11c Eft_SetAlphaTarget(name, alpha, frames) (FUN_0056fba0): every live effect so named
    // (case-insensitive) fades its alpha (+0x1e0) to `alpha` at 1/frames a step (frames 0 = at once).
    virtual void effect_alpha_target(const std::string& /*name*/, float /*alpha*/, int /*frames*/) {}
    // op 0x10a GlareLevel(i): the bloom level byte 0x76c1c5 (EVENTVM_HANDLERS_4.md §2).
    virtual void glare_level(int /*level*/) {}
    // op 0x158 Chr_ZTest(id, on) (FUN_00571790): +0x294 bit 0x400, the actor's D3DRS_ZENABLE.
    virtual void chr_ztest(int /*id*/, bool /*on*/) {}
    // op 0xbf Se_Stop(id, ms) (FUN_0043a1c0): audio command 6, every voice of SE `id` fades to
    // silence over `ms` and stops.
    virtual void se_stop(int /*id*/, int /*ms*/) {}
    // op 0x112 WaterWallHitchk(x, y, z, radius, step) (handler 0x56ad63, build/ai_spec/BULLETS.md
    // §3.14): a GoXx type-0x16 falling probe (FUN_0058f9a0). The host spawns it with
    // bullet::Bullets::spawn_water_probe. S_2006's WATERWALL loops five of them, (34..38, 25, 97),
    // radius 0.8, step 0.4, between Wait_Time(6)s.
    virtual void water_wall_hitchk(float /*x*/, float /*y*/, float /*z*/, float /*radius*/, float /*step*/) {}
    // op 0x111 FireBullet (handler 0x56aaa5 -> FUN_00571500, build/event_re/DUELS.md §3): a GoXx
    // bullet from actor `actor` (0x3e6 already resolved to the script's own; 13 = the player)
    // through FUN_00593550 with the block below. Nothing happens when the actor does not exist.
    virtual void fire_bullet(const FireBulletOp& /*b*/) {}
    // The floor-3 weather (build/effect_re/WEATHER.md, src/cleria/weather.hpp). 0x125 FireDrop
    // [BYTES 0x56b771..0x56b80d] reads ONLY int operand 0 (the ember emitter's life, +0x12c;
    // +0x130 = 0); 0x122 FrameSmoke(i) [0x56b59c..0x56b663] the smoke emitter's life; 0x123
    // FireBlur(fff) [0x56b668..0x56b6c7] stores 0x76a558 / 0x76a55c / 0x76a560.
    virtual void fire_drop(int /*life*/) {}
    virtual void frame_smoke(int /*life*/) {}
    virtual void fire_blur(float /*level*/, float /*delta*/, float /*strength*/) {}

    // --- the floor 1..3 completion pass (build/event_re/FLOOR13_OPS.md) ----------------
    // 0x80 Reg_EntryLabel(s): acc = _stricmp(s, the string at 0x76a474) [EXPORT 00566350 case
    // 0x80]. 0x76a474 is the label of the last COMMITTED warp: the fade machine strcpy's the
    // descriptor label +0x14 to +0xd4 at 0x410907 (WARP.md §3/§4), i.e. the SEN record's name
    // (+0x00) of the door the player came through. A script ChangeScene leaves it unchanged.
    virtual std::string entry_label() const { return {}; }
    // 0x15C Chr_Damage(id, f atk, i b, i c) (FUN_00571b80 -> FUN_00571ae0): the script itself is
    // the attacker. When no fix is held, the actor's +0x60 & 0x80 is set, its HP >= 1.0, its
    // Untouchable byte +0x9d7 is clear and (not the player, or the player's i-frames +0x734 are
    // 0), the GoScript's +0xa4 = atk, +0xce = b, +0xcc = c, and the general hit applier
    // FUN_004fd350 runs with the angle = the actor's heading +0x314 + pi.
    virtual void chr_damage(int /*id*/, float /*atk*/, bool /*b*/, int /*c*/) {}
    // 0x14D Chr_CollisionVanish(id, on) (FUN_005712a0): +0x60 bit 0x80 = (on == 0).
    virtual void chr_collision_vanish(int /*id*/, bool /*on*/) {}
    // 0x163 Chr_NoForce(id, on) (FUN_00571d60): +0x130 bit 0x10000 = (on != 0); the 0x15e / 0x15f
    // pushes skip such an actor and the integrator FUN_0045a510 drops its vertical push.
    // 0x164 Chr_AniDisable(id, on) (FUN_00571db0): +0x130 bit 0x80000 = (on != 0); the NPC mover
    // FUN_0045f4d0 then stops picking its idle / walk / run motion from the table 0x69ba48.
    virtual void chr_no_force(int /*id*/, bool /*on*/) {}
    // 0x161 Chr_SlantFire(id, degrees) (FUN_00571ca0 [EXPORT 00571ca0.c:19]): +0x4ec = degrees x 0.0174533.
    virtual void chr_slant_fire(int /*id*/, float /*deg*/) {}
    virtual void chr_ani_disable(int /*id*/, bool /*on*/) {}
    // 0x165 Chr_Stop(id) (FUN_00571e00): the walk and fly targets = the position, the arrival
    // radii +0x364 / +0x380 and the speed +0x368 = 0, so Wait_ChrMoving passes.
    virtual void chr_stop(int /*id*/) {}
    virtual void chr_anichange(int /*id*/, int /*file*/) {}                        // 0x167 the sprite's AIA file
    // 0xD0 PadTilt(i): FUN_005d3500(i), the controller vibration pattern i.
    virtual void pad_tilt(int /*pattern*/) {}
    // 0xBD Se_Play3DPos(se, x, y, z): a 3D SE (EFFECTDEF flag 0x10) plays at the point
    // (FUN_0043a080), any other one 2D (FUN_0043a040). 0xBE Se_Play3DChr(se, id) (FUN_0043a150):
    // when actor `id` exists, a 3D SE follows it (FUN_0045d7a0), any other one plays 2D.
    virtual void se_play_pos(int /*se*/, float /*x*/, float /*y*/, float /*z*/) {}
    virtual void se_play_chr(int /*se*/, int /*id*/) {}
    // 0x11F Eft_SetAlphaCurrent(name, a) (FUN_0056fc70): every live effect so named: +0x1e0 = a.
    virtual void effect_alpha_current(const std::string& /*name*/, float /*a*/) {}
    // 0x16F Mesh_SetAlphaCurrent(name, a) (FUN_00572ba0): the GoMesh's +0x418 = a.
    virtual void mesh_alpha_current(const std::string& /*name*/, float /*a*/) {}
    // 0x17C Mesh_AttachEffect(mesh, frame, eff) (FUN_00572bd0): effect `eff` attached (+0x1a0,
    // mode +0x1d5 = 1) to the mesh's world matrix, or to its named frame when it has one.
    virtual void mesh_attach_effect(const std::string& /*mesh*/, const std::string& /*frame*/, int /*eff*/) {}
    // 0x95 SetTimeScale(f): the global 0x6d6078 the frame driver FUN_00410440 scales the sim
    // clock by (the room setup FUN_00431840 resets it to 1.0 at 0x431b62).
    virtual void set_time_scale(float /*s*/) {}
    // 0x7F SetWeaponLevel(a, b, c, d) [BYTES 0x568582..0x5685ee]: FUN_004201d0(k, op_k) for
    // k = 0..3, i.e. the weapon tier 0x76a634 + 8k = op_k, then the stat recompute.
    virtual void weapon_level(int /*character*/, int /*tier*/) {}
    // 0x82 SetLevel(n) [BYTES 0x5684cf..0x5684ef]: FUN_004200f0(n) — the level 0x76a760 = n, the EXP 0x76a748 = the
    // total that reaches n (0x76b910; 0x76c180 the level before), the stat recompute FUN_00420c40 — then HP
    // 0x76a730 = max HP 0x76a72c. Only @Flag_Inv and InitArena run it (the bonus modes' loadouts).
    virtual void set_level(int /*level*/) {}
    // 0x18E Arena(cast, level, count): `count` (0 = 8) monsters of CASTINFO row `cast` at `level` (0 = their own)
    // at random places of the arena (FUN_005722c0). 0x18F ArenaSave(): the SP held goes to the Arena SP wallet
    // (record.ar2), the stage's record, the run ends at the title (build/mode_re/ARENA.md §3.1-§3.2, §4).
    virtual void arena(int /*cast*/, int /*level*/, int /*count*/) {}
    virtual void arena_save() {}
    // 0x89(i, v) [BYTES 0x5688f1..0x56892d]: u32[0x76a628 + 4i] = v — slot 0 the equipped accessory, 1 the held
    // tool, 2 the bracelet 0x76a630.
    virtual void equip_slot(int /*slot*/, int /*item*/) {}
    // 0x15E ChrAllInEventBox_SetForce(box, fx, fy, fz, frames, grounded) (FUN_00571ef0) and 0x15F
    // ChrAll_SetPointForce(x, y, z, strength, frames, rmin, rmax, grounded) (FUN_005720b0): every
    // actor inside the named region box (FUN_0042dc20, the .SEV record) / at a distance in
    // [rmin, rmax] of the point, grounded (+0x388) when the flag is set and without NoForce
    // (+0x130 & 0x10000), gets the push +0x5d0..+0x5d8 and the counter +0x5dc = frames; the point
    // force is the unit vector to the point x strength. The integrator FUN_0045a510 adds its x/z
    // to the step while the counter is non-zero (a positive one counts down; HP >= 1 only).
    virtual void force_in_box(const std::string& /*box*/, float /*fx*/, float /*fy*/, float /*fz*/, int /*frames*/,
                              bool /*grounded_only*/) {}
    virtual void force_to_point(float /*x*/, float /*y*/, float /*z*/, float /*strength*/, int /*frames*/,
                                float /*rmin*/, float /*rmax*/, bool /*grounded_only*/) {}

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
    // The choice box (build/event_re/WNDDIALOG_DRAW.md §1, §5): 0xd4 Window_Menu(x, y, title,
    // (text, label)...), 0xd7 Menu_Init, 0xd8 Menu_Add(text, label), 0xd9 Menu_Select(title),
    // 0xda Menu_AssignCancel(label), 0xdb MoveCursorLastScript. A chosen record's label is a
    // script the host starts on the window's owner. 0xd6 SetMsgSpd: 16.16 glyphs per frame.
    virtual void window_menu(float /*x*/, float /*y*/, const std::string& /*title*/,
                             const std::vector<std::string>& /*texts*/, const std::vector<std::string>& /*labels*/) {}
    virtual void menu_init() {}
    virtual void menu_add(const std::string& /*text*/, const std::string& /*label*/) {}
    virtual void menu_select(const std::string& /*title*/) {}
    virtual void menu_assign_cancel(const std::string& /*label*/) {}
    virtual void menu_cursor_last() {}
    // 0xdc Menu_RenameLastScript(s) (FUN_00430940): copies s over the last chosen label 0x75ad98,
    // the record MoveCursorLastScript parks on (EVENTVM_HANDLERS_4 §5).
    virtual void menu_rename_last(const std::string& /*label*/) {}
    // 0xe0 SaveMenu_Enter(s) (FUN_00574eb0): builds the slot's area string (FUN_00574970), then
    // opens the book on its Save page (FUN_00443150, refused while HP < 1). s is walked, unused.
    virtual void save_menu_enter() {}
    // 0x84 SetGoldflagNowZenny(i): the live SP wallet 0x76a75c, which op0 0 copies into flag 216
    // and op0 != 0 restores from it (BLESSING_SHOP.md §0). 0xaf SetXXX(7 / 8) =
    // FUN_00420200(armor / boots id, 1): itemArr[id] = 1, then the stat recompute FUN_00420c40.
    virtual int gold() const { return 0; }
    virtual void set_gold(int /*v*/) {}
    virtual void item_arr_set(int /*idx*/, int /*v*/) {}
    virtual void set_msg_speed(int /*v*/) {}
    // 0x8b EventXX(n): sub-kind 10 unequips the accessory, 7 clears accessory / held / magic
    // (build/item_re/KEY_ITEMS.md §5).
    virtual void event_xx(int /*n*/) {}
    // 0x116 Effect_GetItem(item, flag, mesh): the display-only item icon that rises out of a
    // chest (FUN_00573210, build/item_re/PICKUPS.md §9). It grants nothing.
    virtual void effect_get_item(int /*item*/, int /*flag*/, const std::string& /*mesh*/) {}
    // 0x17e Mesh_DropItem(mesh, item, keep): a world pickup 0.5 above the mesh (FUN_00573150;
    // keep = never expires). The chance is the script's own 0x81 roll (PICKUPS.md §13.7).
    virtual void mesh_drop_item(const std::string& /*mesh*/, int /*item*/, int /*keep*/) {}
    // 0x130 Chr_AttachScript(id, script): the actor's talk script +0x630 (FUN_00570770).
    virtual void chr_attach_script(int /*id*/, const std::string& /*script*/) {}
    // 0x160 Chr_SetStatus(id, lv, hp, s1..s4, r1..r4) (FUN_00571bf0, EVENTVM_HANDLERS_3 §2):
    // f[0] = HP, f[1..4] = +0xa4..+0xb0, f[5..8] = the resists +0xb4..+0xc0.
    virtual void chr_set_status(int /*id*/, int /*lv*/, const float* /*f*/) {}
    // 0xBC Se_Play(se, vol): FUN_004396f0(se). 0xBB Bgm_Volume(volume, time): FUN_00432fe0,
    // the BGM stream's +0x10 / +0x14 (BARRIER_OBJECTS.md §1).
    virtual void se_play(int /*se*/) {}
    // ... with its second operand: a record that is not 3-D plays through FUN_0043a040(se, vol), and
    // vol is the buffer's volume, 0x400 = unity [BYTES 0x569a13, 0x43a06b..0x43a074, 0x5cd4c5..0x5cd4ea].
    virtual void se_play_vol(int se, int /*vol*/) { se_play(se); }
    // 0xC0 Se_Volume(se, vol, ms) [BYTES 0x569b26 -> FUN_0043a230(ecx = op0, edx = op1, push op2)]: audio
    // command 7, every voice of `se` ramps to vol / 0x400 over ms and plays on.
    virtual void se_volume(int /*se*/, int /*vol*/, int /*ms*/) {}
    virtual void bgm_volume(int /*volume*/, int /*time*/) {}
    // 0x8F Set_Skill(i): *0x76a630 = i, the equipped magic (EVENTVM_HANDLERS_2).
    virtual void set_skill(int /*i*/) {}
    // 0x187 Mesh_Locate (the spawn), 0x18A Mesh_Fall(name, e, h), 0x16E Mesh_SetAlphaTarget(name,
    // alpha, frames) (EVENTVM_HANDLERS_3 §3).
    virtual void mesh_locate(const ObjLocateArgs& /*a*/) {}
    // 0x186 Mesh_SetSphereCollision(name, r, ox, oy, oz) (FUN_005735b0): the GoMesh gets one
    // child sphere collider (+0x278 = 1, +0x298 |= 0x4000) with its stat block; the player's swing
    // hitting it runs the mesh's script (FLOOR3_ROUTE_AUDIT.md B1).
    virtual void mesh_sphere_collision(const std::string& /*name*/, float /*r*/, float /*ox*/, float /*oy*/,
                                       float /*oz*/) {}
    virtual void mesh_fall(const std::string& /*name*/, float /*e*/, float /*h*/) {}
    virtual void mesh_alpha_target(const std::string& /*name*/, float /*a*/, int /*frames*/) {}
    // 0x188 Mesh_DistanceToAlpha(a, b, f div, f k) (FUN_00573aa0) and 0x189
    // Mesh_SyncronizePos(a, b, x, y, z) (FUN_00573b80); meshobj.hpp has the formulas.
    virtual void mesh_distance_alpha(const std::string& /*a*/, const std::string& /*b*/, float /*div*/,
                                     float /*k*/) {}
    virtual void mesh_sync_pos(const std::string& /*a*/, const std::string& /*b*/, bool /*x*/, bool /*y*/,
                               bool /*z*/) {}
    // 0xA4 Scene_SetDefaultBgm(id), 0xB9 Bgm_Play(id; -1 = the default), 0xBA Bgm_Stop(time)
    // (EVENTVM_HANDLERS_2 §3).
    virtual void bgm_default(int /*id*/) {}
    virtual void bgm_play(int /*id*/) {}
    virtual void bgm_stop(int /*time*/) {}
    // 0xA2 Scene_SetClipPlain(f near, f far) [EXPORT FUN_004348a0, called at 0x569431]: the
    // projection's near / far (DAT_007b2f50 / DAT_007b2f54, and their scene copies 0x74c0fc /
    // 0x74c0f8). 13 sites, all (1, 4000) or (1, 7000), in the skybox rooms (S_x1xx).
    virtual void scene_clip(float /*near*/, float /*far*/) {}
    // 0x85 HealHp() (FUN_004fa0e0: HP to max), 0x14E Chr_Muteking(id, n) (player +0x734 =
    // max(+0x734, n)), 0x96 SaveContData() (the continue checkpoint, FUN_00443d70).
    virtual void heal_hp() {}
    virtual void give_o2() {}                                   // 0x86: the oxygen gauge back to full
    virtual void chr_muteking(int /*id*/, int /*frames*/) {}
    virtual void save_cont_data() {}
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
    // op 0xce ZoomBlur(s, r) [BYTES 0x569f43..0x569f81]: f32 0x76a550 = s, 0x76a554 = r, nothing else. The
    // strength s is stepped by r every update and is the blur's opacity (build/render_re/SCREEN_EFFECTS.md §7).
    virtual void zoom_blur(float /*s*/, float /*r*/) {}
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
    // 0xEB CopyCamera_Set(id) / 0xEC Camera_Change(dist, yaw deg, pitch deg, fov deg, frames)
    // (CAMERA_OPS.md §3): negative operands keep the current value (pitch: -1.1 < p < 0 keeps).
    virtual void camera_copy_set(int /*id*/) {}
    virtual void camera_change(float /*dist*/, float /*yaw*/, float /*pitch*/, float /*fov*/, int /*frames*/) {}

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
    // 0x92 SetManualEnableIndex: 0x76b8d8[i] = 1. 0x93 SetTopicKnownIndex: the collection
    // record 0x74ac40 + i*0x1c: seen = 1, count + 1 capped at 50 (EVENTVM_HANDLERS_2.md).
    virtual void manual_enable(int /*index*/) {}
    virtual void topic_known(int /*index*/) {}
    // op 0xa6 GoWarpPoint(): ChangeScene to warp table[flag 137] (the Wing).
    virtual void go_warp_point() {}

    // --- scripted map objects (build/event_re/WAITOPS.md §4; meshobj.hpp) ---------------
    virtual void mesh_move(const std::string& /*name*/, float /*x*/, float /*y*/, float /*z*/,
                           float /*radius*/, float /*speed*/, bool /*offset*/) {}   // 0x170 / 0x171
    virtual void mesh_set_motion(const std::string& /*name*/, const std::string& /*clip*/,
                                 bool /*blend*/, int /*start*/) {}                 // 0x175 (live)
    virtual void mesh_stop_motion(const std::string& /*name*/, int /*on*/) {}       // 0x16d
    virtual void mesh_sleep(const std::string& /*name*/, bool /*on*/) {}            // 0x16a
    virtual void mesh_set_position(const std::string& /*name*/, float /*x*/, float /*y*/, float /*z*/) {}  // 0x173 (live)
    virtual void mesh_reset_position(const std::string& /*name*/) {}                // 0x178
    virtual void mesh_set_rotation(const std::string& /*name*/, float /*x*/, float /*y*/, float /*z*/) {}  // 0x174 (deg)
    virtual void mesh_reset_rotation(const std::string& /*name*/) {}                // 0x179
    // 0x12a Eft_Die(id, i) [BYTES 0x56b8b3 -> FUN_0056fd10 -> FUN_00451730(op0); op1 is fetched, unused]:
    // every live effect of EFTLIST id op0 gets its kill flag (+0x1d4).
    virtual void eft_die(int /*id*/) {}
    // 0x11b EftAll_AddCameraOffset(i id, f off) [BYTES 0x56b3cf..0x56b407 -> FUN_0056fa00(op0), xmm2 = op1]:
    // every live effect of EFTLIST id op0 gets fx+0x204 = op1, so its draw pull toward the eye (fx+0x1f4)
    // becomes (fx+0x1f0 + op1) (ef2_sim.hpp depth_bias / bias_extra).
    virtual void effect_camera_offset(int /*id*/, float /*off*/) {}
    // 0x126 MirrorFloor(i) [BYTES 0x56b812..0x56b82d: byte 0x74c104 = (op0 != 0)]. Its reader is the actor draw
    // registration FUN_00473540 (a second, mirrored draw of each actor) [EXPORT map: the only other xref is
    // FUN_00431260's clear]. Render-only: no gameplay reads it.
    virtual void mirror_floor(bool /*on*/) {}
    // 0x127 SetDefaultCubeMapTexture(s) [BYTES 0x56b832..0x56b845 -> FUN_004442c0(name)]: the reflection cube map
    // (render-only).
    virtual void cube_map(const std::string& /*name*/) {}
    virtual void mesh_enable_event(const std::string& /*name*/, bool /*on*/) {}     // 0x176
    // 0x129 ChangeFog(i idx, f speed) [BYTES 0x56b86c -> FUN_0042e8f0; build/boss_re/ZAVA.md §2.1]: ignored when
    // idx >= the fog record count; else fogSpeed = speed (unless FLT_MAX), fogPrev = fogCur, fogCur = idx, fogT = 0
    // and the record is flagged script-selected (+5): it then holds while the player stands in no fog zone.
    virtual void change_fog(int /*idx*/, float /*speed*/) {}
    // 0x17a Mesh_IsMotionFinish(s mesh, i n) [BYTES 0x56d346 -> FUN_005730e0]: acc = the object exists, has a model
    // and its clip completion count (A+0x1c) >= n. No wait, no rewind.
    virtual bool mesh_is_motion_finish(const std::string& /*name*/, std::uint32_t /*n*/) const { return false; }
    // 0x182 Mesh_SetMaterialColor(s mesh, s material, i r, i g, i b) [BYTES 0x56d656 -> FUN_005737e0]: every
    // material of that name gets its instance colour multiplier (+0x14..+0x1c) = rgb / 255; alpha untouched.
    virtual void mesh_material_color(const std::string& /*name*/, const std::string& /*material*/, int /*r*/, int /*g*/,
                                     int /*b*/) {}
    // 0xaa Light_Directional(i argb, f x, f y, f z) [BYTES 0x5695c9..0x569603 -> FUN_00443ea0; build/boss_re/
    // DALLES.md §2.2]: the scene light's colour 0x76a570..78 = the argb's r, g, b bytes / 255 (alpha ignored) and
    // its direction 0x76a564..6c = (x, y, z) as given (not normalised).
    virtual void light_directional(std::uint32_t /*argb*/, float /*x*/, float /*y*/, float /*z*/) {}
    // 0x172 Mesh_Rotate(s mesh, f x, f y, f z, i frames) [BYTES 0x56d179..0x56d1bf -> FUN_00572e70; DALLES.md
    // §2.3]: the object's per-step rotation delta = deg x pi / 180 for `frames` steps (<= 0: forever).
    virtual void mesh_rotate(const std::string& /*name*/, float /*x*/, float /*y*/, float /*z*/, int /*frames*/) {}
    // 0x16B Mesh_SetVisible / 0x16C Mesh_EnableCollision, live (+0x298 bits 0x20 / 0x40).
    virtual void mesh_visible(const std::string& /*name*/, bool /*on*/) {}
    virtual void mesh_collision(const std::string& /*name*/, bool /*on*/) {}
    virtual void mesh_time_scale(const std::string& /*name*/, float /*s*/) {}       // 0x17b
    // The waits. A missing object returns `found` = false; the original then rewinds forever
    // (0x104 / 0x105) or proceeds (0x101). Defaults: found and done, so no host never blocks.
    virtual bool mesh_moving(const std::string& /*name*/, bool* found) const { *found = true; return false; }
    virtual bool mesh_motion_finished(const std::string& /*name*/, std::uint32_t /*n*/, bool* found) const {
        *found = true; return true;
    }
    // 0x101: is the actor grounded on / touching this mesh (its +0x540 / +0x52c contact)?
    virtual bool chr_on_mesh(int /*id*/, const std::string& /*name*/) const { return false; }

    // op 0xa0, the BULK SCT SPAWN (build/roomload_re/ROOMLOAD.md §3). The engine's
    // handler FUN_004341a0 loads the room's .SCT (named by the operand) and spawns
    // every placement through the CASTINFO factory. We pre-load the host with the
    // room's placements, so the operand (the SCT basename) is ignored here. This runs
    // exactly once per room script, before any 0x12c. Default: no host placements set,
    // so nothing spawns.
    virtual void spawn_placements() {}
};

// The grant filter (CleriaCore's own hook, NOT retail; build/shell_re/CONTENT.md §4): the twin of
// the retail Archipelago mod's splices on the VM's grant store (0x567D17 / 0x567DB3, hook_vm.cpp)
// and the get-item / treasure-box calls (0x573210 / 0x574410). Consulted, when set, at
//   Store   every g_flags write of the store family 0x64..0x77 (index < 0x200), AFTER the op
//           computed `value` and BEFORE it is kept / recorded as a Grant;
//   Icon    0x116 Effect_GetItem(item, flag, mesh), before the host spawns the rising icon;
//   Window  0xD5 Window_Treasure(x, y, item, text), before the host opens the "Acquired" box.
// With no filter (the default) every path runs exactly as before.
struct GrantQuery {
    enum class Kind { Store, Icon, Window };
    Kind kind = Kind::Store;
    int sub = 0;                         // the sub-op (0x64 set, 0x67 add, ... / 0x116 / 0xD5)
    int index = 0;                       // Store: the g_flags cell; Icon / Window: the item id
    std::int32_t old_value = 0;          // Store: the cell before the op
    std::int32_t value = 0;              // Store: the cell after the op (what would be kept)
    std::string text;                    // Window: the box text (operand 3)
    const std::int32_t* flags = nullptr; // the VM's g_flags (read-only)
    std::string script;                  // the running script instance's name ("s_box01", "@growmenu")
    std::string object;                  // its $S0: the map object that started it ("box_01"), or ""
    int owner = 0;                       // its bound actor (13 = the player)
};
struct GrantDecision {
    enum class Action { Pass, Suppress, Replace };
    Action action = Action::Pass;
    // Replace / Store: write `value` to cell `index` instead (an index outside 0..0x1FF writes no
    // cell: the filter handled the grant itself, e.g. a new item kept in the save sidecar).
    // Replace / Icon, Window: show item `index` (and, Window, `text`) instead.
    int index = 0;
    std::int32_t value = 0;
    std::string text;
};

// The blessing-shop filter (CleriaCore's own hook, NOT retail; build/shell_re/LUA_API.md §5.1): what a mod
// may answer for the goddess statues' @GrowMenu and its @GrowNN / @GrowAr / @GrowLe scripts. Consulted, when
// set, at
//   Price   a blessing's SP price as a script reads it: 0xDD Menu_AddSpShop's operand (the menu row), the
//           0x61 compare "flag 216 >= price" and the 0x69 deduction "flag 216 -= price" of @GrowNN, and
//           0xB0 SetRavalCostToFlag's cost (flag 218) of @GrowAr / @GrowLe. Replace: `value` = the price.
//   Row     a row added to the menu (0xD8 Menu_Add: a "[Done]" row, value -1; 0xDD: text + price, value =
//           the price after Price). Replace: `text` = the row's text.
//   Owned   0x9A IsXXX(index) inside an @Grow* script ("already applied" / which tier's row to show);
//           value = the game's answer 0 / 1. Replace: `value` = the answer.
//   Buy     0xAF SetXXX(index), the purchase's effect; value = the SP the script just deducted. Suppress:
//           the blessing bit / the Raval level is not written. Replace: `index` = the blessing applied.
// `index` is the script's own number: GROWnn's nn (0..6, 9..25), 7 = the armor, 8 = the leggings.
// With no filter (the default) every path runs exactly as before.
struct ShopQuery {
    enum class Kind { Price, Row, Owned, Buy };
    Kind kind = Kind::Price;
    int sub = 0;                         // the sub-op that asks (0xDD, 0x61, 0x69, 0xB0, 0xD8, 0x9A, 0xAF)
    int index = -1;
    std::int32_t value = 0;
    std::string text;                    // Row: the text as the script built it
    std::string script;                  // the running script instance's name ("@growmenu", "@grow03")
};
// The blessing an @Grow* script name stands for: "@Grow03" -> 3, "@GrowAr" -> 7, "@GrowLe" -> 8 (any case,
// with or without the '@'); -1 for anything else ("@GrowMenu", "@Grow_YesNo").
int blessing_index(const std::string& script);

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
    // The running script's bound actor (GoScript +0x240): what Chr_IsThisChr compares against
    // and what id 998 (0x3e6) resolves to. 13 = the player; 0 = none (a room root).
    int owner = 0;

    // StartScript sink (op 0xb4 StartScript / 0xb8 StartScript2, CUTSCENE_COROUTINE.md §5).
    // A running COROUTINE hands the resolved child-script NAME here so a ScriptManager can
    // spawn it as a concurrent GoScript, with the operands after the name marshalled into the
    // child's variable block (FUN_00434970). Null (the default) => the op is inert, which is
    // exactly the run-to-completion behaviour for static grant extraction, so `run()` and
    // `--grants` never try to load or recurse into children. Only fired in coroutine mode.
    // owner_id: StartScript's operand 1 (0 = none, 998 = the parent's owner, else an actor id).
    std::function<void(const std::string& name, const Vars& child, int owner_id)> on_start_script;

    // Fix(slot) (0xb6) / Unfix (0xb7) sinks (FUN_0056e8d0, audit event_vm Part C): a script
    // holds at most one fix. Fix(slot) releases its previous one, then counts slot
    // (0x75ad90[slot]); Unfix releases it. Window_Msg / Window_Treasure / Window_Menu fix
    // slot 0 implicitly when the script holds none (`on_window_fix`). Coroutine-only; null
    // => inert (run-to-completion).
    std::function<void(int slot)> on_fix;
    std::function<void()> on_unfix, on_window_fix;
    // 0xB5 AllowMultiInstance(i) sink: GoScript+0x238 = (i != 0) [EXPORT 00566350 case 0xb5].
    // StartScript's dedup (FUN_00434970) refuses a name only while a live instance of it has
    // +0x238 == 0, so a script that sets it lets another copy of itself start. Null => inert.
    std::function<void(bool)> on_allow_multi;
    // CleriaCore mods: the grant filter (GrantQuery above). Null = retail behaviour.
    std::function<GrantDecision(const GrantQuery&)> grant_filter;
    // CleriaCore mods: the blessing-shop filter (ShopQuery above). Null = retail behaviour.
    std::function<GrantDecision(const ShopQuery&)> shop_filter;
    std::int32_t shop_paid = 0;          // the SP the running @Grow* script deducted (its Buy query's value)
    // Coverage diagnostics (CleriaCore's own, not retail): called with the sub-op whenever a
    // class-2 call reaches step_one's fallthrough, i.e. a sub-op this VM neither models nor
    // knowingly ignores (it is then skipped by its encoded length). Null = silent. cleria-view
    // logs it under CLERIA_XSO_LOG=1; `cleria-xso --op-status` classifies every sub-op with it
    // (tools/xso_floor_ops.py, build/event_re/FLOOR13_OPS.md).
    std::function<void(std::uint32_t sub)> on_unhandled;
    // The running instance's name, set by ScriptManager around each tick ("" in a bare run()).
    std::string script_name;
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
    // 0x187 Mesh_Locate(ymo, name, x, y, z, rx, ry, rz, i) (FUN_00573930 -> the GoMesh factory
    // FUN_00501a30): a new named mesh. The boss rooms place their boss with it.
    struct ObjLocate { std::string ymo, name; float x, y, z, rx, ry, rz; };
    std::vector<ObjLocate> obj_locate;
    // 0x189 Mesh_SyncronizePos(a, b, x, y, z) (FUN_00573b80): a takes b's position on the
    // gated axes and b's hidden / no-collision bits (+0x298 & 0x60). Recorded for the load
    // pass (S_1004's STEPSHADOWUPDATE keeps each step_NN_shadow under its stepNN, so a stone
    // the root hid or moved takes its shadow with it).
    struct ObjSync { std::string from; bool x, y, z; };
    std::unordered_map<std::string, ObjSync> obj_sync;
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
    // The engine has ONE generator: FUN_00439120 advances 0x74622c..0x746238 for the 0x81 roll
    // (0x56849e) as for the brains, effects and pickups. A host VM that is not the game state binds
    // rs_ext to the game's words, so its rolls draw from (and advance) that one stream.
    std::uint32_t* rs_ext = nullptr;
    std::uint32_t rng_next() {
        std::uint32_t* s = rs_ext ? rs_ext : rs;
        const std::uint32_t t = s[0] ^ (s[0] << 11);
        s[0] = s[1]; s[1] = s[2]; s[2] = s[3];
        s[3] = (s[3] ^ (s[3] >> 19)) ^ (t ^ (t >> 8));
        return s[3];
    }

    // Clear per-run state (accumulator, locals, outputs) but keep g_flags, so one
    // VM can persist game state across many script runs.
    void reset_run() {
        acc = 0; locals.clear(); grants.clear(); items.clear(); obj_enable.clear();
        acquires.clear(); obj_deleted.clear(); obj_opened.clear(); obj_visible.clear(); obj_collide.clear();
        obj_state.clear(); obj_place.clear(); obj_sync.clear();
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
    // `skip_cycles` (a host-less VM only): a script that loops forever (238 of the 2225 map scripts: a mover's
    // `MoveTo; Wait` loop — Wait is inert here) is not stepped max_steps times. Once the whole state the script can
    // read (pc, acc, flags, items, locals, vars, the generator) is back where it was a loop ago, the whole loops
    // left are skipped and only the remainder is stepped: the state and every name -> value output end as they
    // would have. The list outputs (grants, items, obj_deleted, ...) hold fewer repeats of the same entries.
    int run(const Xso& script, std::uint32_t entry = 0, int max_steps = 2'000'000, bool skip_cycles = false);

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
    int burst_ops = 0;                // ops the last tick() ran (0: it only counted a Wait_Time down)

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
    // A GrantQuery with this VM's context (flags, script name, $S0, owner) filled in.
    GrantQuery filter_query(GrantQuery::Kind kind, int sub, int index) const;
    // shop_filter's answer for a price / an owned state: `value`, or the Replace decision's.
    std::int32_t shop_value(ShopQuery::Kind kind, int sub, int index, std::int32_t value) const;
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
        int owner = 0;                     // +0x240: the bound actor (13 = the player)
        bool multi = false;                // +0x238: AllowMultiInstance (0xb5)
        // A cutscene skip's bookkeeping (skip_tail_step; CleriaCore's own): the start order, and the tail's view
        // of this script — the pcs it has parked on, how long it has stood on one wait, whether it is left alone.
        std::uint64_t born = 0;
        std::vector<std::uint32_t> tail_pcs;
        int tail_stood = 0;
        bool tail_done = false;
    };
    std::vector<Inst> live;
    struct Spawn { std::string name; XsoVm::Vars vars; int owner = 0; };
    std::vector<Spawn> spawn_queue;         // children queued by StartScript during a tick
    std::size_t peak_live = 0;              // max instances resident in one frame (pre-reap)
    int fix_counts[2] = {0, 0};             // 0x75ad90 / 0x75ad94: the two fix slots
    // FUN_0056e8d0's writes to the player on a Fix (FIX_SLOTS.md §1.2 [EXPORT 0056e8d0.c:15-58]): called on every
    // Fix / implicit window Fix, `first` when that slot's count went 0 -> 1. The host zeroes the player's
    // velocity and push on `first`, and the charge state every time.
    std::function<void(int slot, bool first)> on_fix_taken;

    ScriptManager() {
        vm.on_start_script = [this](const std::string& n, const XsoVm::Vars& v, int oid) {
            // FUN_00570330: 998 -> the parent's +0x240; 999 / 13 -> the player; 0 -> no owner.
            const int parent = cur_inst_ ? cur_inst_->owner : 0;
            const int owner = oid == 998 ? parent : oid == 999 ? 13 : oid;
            spawn_queue.push_back({n, v, owner});
        };
        vm.on_fix = [this](int slot) { if (cur_inst_) fix(*cur_inst_, slot); };
        vm.on_unfix = [this] { if (cur_inst_) unfix(*cur_inst_); };
        vm.on_window_fix = [this] { if (cur_inst_ && cur_inst_->fix_slot < 0) fix(*cur_inst_, 0); };
        vm.on_allow_multi = [this](bool on) { if (cur_inst_) cur_inst_->multi = on; };
    }

    // The engine reads the two counters in three ways (build/mech_re/FIX_SLOTS.md §0, §1.4):
    //   fix_any        the SUM (FUN_0040b2c0, IsFix): the player's input and player-only bookkeeping — 27 readers;
    //   actors_stopped slot 1 ALONE, the script half of the actor-stop byte 0x74ab8b [EXPORT 004281c0.c:49-50]:
    //                  the actor handlers 3..93 (enemies, Hugo's eyes) and the EF2 particle hit test;
    //   (slot 0 alone: two input wrappers of the puppet handler, not used here).
    // So Fix(0) takes the controls away, Fix(1) also stops the monsters.
    bool fix_any() const { return fix_counts[0] + fix_counts[1] > 0; }
    bool actors_stopped() const { return fix_counts[1] > 0; }
    // The port's own "a Fix holds" test = the sum: its main loop still takes one frozen branch for both slots
    // (FIX_SLOTS.md §3.1 lists what that branch stops that slot 0 does not).
    bool world_frozen() const { return fix_any(); }

    // Register a root (or already-loaded child) instance and tick it once this frame,
    // matching StartScript's immediate first tick (§5a). Returns the live-index.
    std::size_t start_root(std::shared_ptr<const Xso> s, std::string name);
    // Spawn a child by NAME via the loader (the StartScript path). Dedup: refuse if a live
    // instance with the same name already exists that has not run AllowMultiInstance(1)
    // (FUN_00434970: +0x11 & 2 clear, _stricmp name, +0x238 == 0; §5a). Ticks the child
    // once immediately. Returns false if deduped or the loader can't find it.
    bool start(const std::string& name, const XsoVm::Vars& vars = {}, int owner = 0);
    // One engine frame: tick every live script once, drain StartScript spawns (children
    // tick this frame too), then reap finished scripts. A no-op while paused.
    void tick_all();
    bool any_live() const;

    // Cutscene SKIP support: zero every live script's pending Wait_Time countdown so the
    // next tick_all() bursts straight through the delays. Side effects still execute in
    // order — only the waiting collapses. Caller loops tick_all() until world_frozen()
    // clears. (The per-instance `wait` is the saved co_wait; see tick_inst.)
    void skip_waits() { for (Inst& in : live) if (!in.finished) in.wait = 0; }
    // A skip's tail (CleriaCore's own; the original has no skip): one step of the scripts the skipped scene
    // started — those born since the fix count last left 0 — that outlive its Fix: a child nobody waits for (an
    // actor's walk off the stage and its Chr_Delete). A played scene is long over when they end; a skipped one
    // must run them on, their Wait_Time collapsed. Each is run until it ends, takes a Fix, parks where it has
    // parked before (a loop: a controller, a spawner — left to real time after that one turn), stands on one
    // wait for `patience` steps (a wait on the fight's end), or counts a Wait_Time longer than `long_wait` (a
    // timer of the fight, not a scene's choreography). Returns whether any is still being run.
    bool skip_tail_step(int patience, int long_wait);

  private:
    void tick_inst(Inst& in);          // swap regs into `vm`, run one frame, save back
    void fix(Inst& in, int slot);      // FUN_0056e8d0: release the held slot, count `slot`
    void unfix(Inst& in);
    Inst* cur_inst_ = nullptr;         // the instance being ticked (for Fix/Unfix attribution)
    std::uint64_t serial_ = 0;         // the next Inst::born
    std::uint64_t scene_mark_ = 0;     // serial_ when the fix count last left 0: the scene's scripts are born >= it
};

}  // namespace cleria
