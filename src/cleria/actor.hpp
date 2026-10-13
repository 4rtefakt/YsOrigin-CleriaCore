// The Actor — a live entity in a room. **Our struct, not the game's.**
//
// ## This is a reimplementation, so the layout is ours [design]
//
// build/entity_re/ENTITY.md gives the engine's real offsets (`GoActor`: +0x94 max HP,
// +0x98 HP, +0x130 the group bits, +0x2b4 a marker colour, and a total size in the
// 0x9xx range). **We do not reproduce that layout.** Mirroring it would import the
// engine's accidents — padding, dead fields, MSVC's choices — into code that has no
// reason to carry them, and this project is a clean-room reimplementation, not a
// decompilation.
//
// So: fields are named for MEANING, and each cites the offset it was derived from.
// ** The offsets below are PROVENANCE, not layout. ** Nothing here is at +0x94.
//
// What we DO reproduce exactly is observable BEHAVIOUR, quirks included (see the
// group note on `tag`).
//
// ## A field that is absent is a field we have not validated
//
// Everything here traces to a read instruction. The engine's actor carries a great
// deal more that we have not established, and it stays out: an empty slot is honest,
// and a guessed one is the failure mode this codebase exists to avoid.
#pragma once
#include <cstdint>
#include <string>

#include "cleria/chr_state.hpp"
#include "cleria/combat.hpp"  // owns kActorDead and the damage path
#include "cleria/enemy_ai.hpp"
#include "cleria/enemy_mover.hpp"
#include "cleria/hit_volume.hpp"
#include "cleria/mathx.hpp"
#include "cleria/physics_player.hpp"

namespace cleria {

// The cast id of CAST_ADOL — the player. The damage path's own test is `cast != 13`
// (actor+0x124), and it forks the whole formula on it (see combat.hpp).
inline constexpr int kPlayerCast = 13;

// The KIND WORD — actor+0x60, a u32. ** It is not CASTINFO+0x6c. ** The factory
// (FUN_00459410) TRANSFORMS the cast's class into it [C-DIS]:
//
//     CASTINFO+0x6c   2  ->  0x81
//                     4  ->  0x84
//                  0x10  ->  0x88     (0x10 == monster, per cast.hpp)
//
// i.e. 0x80 | a one-bit class code. ** Bit 0x8 means ENEMY, ** and it gates group
// tagging at 0x5704f7 (`TEST byte ptr [ESI+0x60],0x8` — the low byte of this u32).
//
// ** Storing the raw CASTINFO value here and testing `& 0x8` would be silently,
// totally wrong: ** none of {0, 2, 4, 0x10} has bit 0x8 set, so the enemy gate would
// be false for every actor in the game and nothing would ever tag. The transform is
// where the bit comes from. This header first said "class byte, set from CASTINFO+0x6c"
// and that sentence was the whole bug.
//
// [UNKNOWN] what casts with CASTINFO+0x6c == 0 become; ENTITY.md observed the domain
// as {2, 4, 16} and cast.hpp records a 0 class, so one of the two is incomplete.
// +0x64 is written alongside (0x3c/0x3f/0x2f) and means [UNKNOWN].
enum : std::uint32_t { kKindEnemy = 0x8 };
inline constexpr std::uint32_t kind_word(std::int32_t cast_class) {
    switch (cast_class) {
        case 2:    return 0x81;
        case 4:    return 0x84;
        case 0x10: return 0x88;
        default:   return 0;  // [UNKNOWN] — not observed; deliberately not invented
    }
}

// ## Death is TWO flags in two bytes, and only one of them drives removal [C-DIS]
//
// This was misunderstood, and the correction is load-bearing (build/actor_re/ACTOR.md
// §4, and independently build/combat_re/damage.json — two RE passes agreeing):
//
//   +0x3bc bit 0x10  the "DIED event marker". Written by the damage code on a lethal
//                    hit (and XOR-undone by the player's survival gate). ** Nothing
//                    reads it. ** Both passes ran a full-program operand scan and found
//                    no `TEST [x+0x3bc],0x10`; the deferral the project once pinned on
//                    it is NOT carried here. Owned by combat.hpp as kActorDead — do not
//                    redefine (this header did once, and only escaped a redefinition
//                    error because no TU then included both).
//
//   +0x11 bit 0x2    "DESTROY-REQUESTED", kDestroyRequested below. ** This is what
//                    actually removes the actor. ** Set by GoActor::Kill (FUN_004593c0)
//                    AFTER the AI has played out the death state, then consumed by the
//                    once-per-render-frame sweep (FUN_00428b10 -> the vtable dtor).
//
// The real lethal-hit chain: damage zeroes HP and enters MOTION STATE 5 (the dying
// anim) in the damage code itself; the actor's AI runs state 5 over the next ticks,
// awards EXP/drops, then calls Kill to set +0x11 bit 2; the frame-boundary sweep does
// the order-preserving removal. So HP<1 does NOT remove an actor — a request does, a
// frame later. (world.hpp models this; motion states are not RE'd yet, so our actor
// requests destruction directly on death rather than through a state machine.)
enum : std::uint8_t { kDestroyRequested = 0x2 };  // actor+0x11 bit 1

// Spawn groups, from the XSO spawn op 0x12c (FUN_00570400, ENTITY.md).
//
// ** The "groups" OVERLAP, and that is the engine's real behaviour. ** The setter
// writes `n << 8` into +0x130 and the reader (FUN_00570640) masks with `n << 8`, so
// this is a 3-BIT VALUE at bits 8..10 tested by bitwise-AND — not four independent
// bits. A group-3 entity therefore answers YES to a query for group 1 or group 2.
// The compiler expanded both sides as jump tables, which disguised the shift and had
// a prior pass reporting `3 -> 0x300` as "unexplained but real".
//
// In the shipped game this never bites: only group 1 is ever used (157 group-1 spawns
// across 33 XSO files, and no other). So it is documented and reproduced rather than
// designed around — `in_group()` masks exactly as the engine does, and if a room ever
// wants 3, it gets the game's answer instead of a tidier one.
struct SpawnTag {
    std::uint32_t bits = 0;  // engine: entity+0x130, bits 8..10

    // The op honours 1..4 only; 0 means untagged and anything else is ignored.
    void set(int group) {
        if (group >= 1 && group <= 4) bits = std::uint32_t(group) << 8;
    }
    bool tagged() const { return (bits & 0x700u) != 0; }  // the engine's own test
    bool in_group(int group) const {
        return group >= 1 && group <= 4 && (bits & (std::uint32_t(group) << 8)) != 0;
    }
};

struct Actor {
    // --- identity ---
    // engine: +0x128. ** PER-INSTANCE, and not the cast id ** — this is the key every
    // query op resolves through (FUN_00434d50 linear-scans for it). The DEFAULT is the
    // cast type, which makes the two look interchangeable across the corpus; they are
    // not. EVNT_3002_THOR.XSO spawns five instances of cast 35 as ids 167..171.
    int instance_id = 0;
    // CleriaCore's own (the mods' puppets, API 7; not retail): the sprite set drawn for this actor instead of its
    // cast's ("C_103" = Hugo's player sprites). Empty = the cast's own.
    std::string sprite_override;
    int cast_id = 0;      // engine: +0x124. Indexes CASTINFO; 13 is the player.
    int enemy_index = -1; // ours: enemies.hpp fold of cast_id, or -1 if not an enemy.
    std::uint32_t kind = 0;  // engine: +0x60, the kind word. See kind_word().
    SpawnTag tag;            // engine: +0x130

    // The engine takes the player's identity from CASTINFO+0x60 == 13 (i.e. the
    // CAST_ADOL record), NOT from the spawn op's operand [C-DIS].
    bool is_player() const { return cast_id == kPlayerCast; }
    bool is_enemy() const { return (kind & kKindEnemy) != 0; }

    // --- vitals ---
    // engine: +0x98 / +0x94, both float. The event VM's "is alive" (sub-op 0x13f) is
    // `hp >= 1.0` — NOT `> 0`. Kept as the engine's test rather than the obvious one.
    float hp = 0;
    float max_hp = 0;
    bool alive() const { return hp >= 1.0f; }

    // --- combat ---
    // engine: +0xa4. ** The level curve PLUS equipment ** — never the bare curve; see
    // combat.hpp, where feeding the curve alone drives the formula negative.
    float atk = 0;
    float def = 0;  // engine: +0xa8 — read from the TARGET and subtracted.
    float resist[4] = {0, 0, 0, 0};  // engine: +0xb4..+0xc0, (1 - r) per damage class (hit.hpp)
    // engine: +0xb0. The EXP this actor awards when killed:
    // FLOOR(K(ENEMYINFO+0xDC) * GAMEINFO's curve 4). Validated 67/67.
    float exp_reward = 0;
    int sp_reward = 0;   // engine: +0xc4, the SP a kill credits (FUN_004202e0; FUN_004fbb40 0x4fcd1d)

    // engine: +0x734, a DWORD (not a byte — +0x4bf, the footstep counter, is the byte
    // idiom and they are not the same). ** Gates damage FOR THE PLAYER ONLY: ** the
    // engine's test is `cast != 13 || i_frames == 0`, so every non-player cast is
    // always hittable. Enemy hit-spam is vanilla. Grant with grant_iframes() in
    // combat.hpp — granting is a max(), never a store.
    std::uint32_t i_frames = 0;

    // engine: +0x3bc, the per-tick EVENT word. kActorDead (bit 0x10) is the "died
    // marker" written on a lethal hit -- see the death note above: it is written but
    // NOT read, so `dead()` is our own convenience, not a query the engine performs.
    std::uint32_t flags = 0;
    bool dead() const { return (flags & kActorDead) != 0; }

    // engine: +0x11, the pooled-object header flags byte. kDestroyRequested (bit 0x2)
    // is the flag the sweep actually consumes to remove an actor. Distinct byte from
    // `flags` above on purpose -- the engine keeps the removal request (+0x11) and the
    // death event (+0x3bc) in different bytes, and conflating them is the mistake the
    // death note documents.
    std::uint8_t header_flags = 0;
    bool destroy_requested() const { return (header_flags & kDestroyRequested) != 0; }

    // --- placement and motion ---
    Vec3 pos{};
    float yaw = 0;         // radians — the actor's heading (engine +0x314). Used by the
                           // chase mover; the billboard sprite still faces the camera.
    float floor_y = 0;     // engine: +0x318 — the floor under this actor
    VerticalState vert;    // engine: +0x330, the signed timer (physics_player.hpp)

    // Chase speed, world units per tick — the enemy's ENEMYINFO base (enemies.hpp).
    // 0 for the player and for casts with no enemy record.
    float move_speed = 0;  // engine: actor+0x30c

    // Cutscene walk-to (op 0x143 Chr_MoveTo -> +0x358 move block): the actor walks
    // toward `cmove_tgt` at `cmove_spd` units/frame until within reach, then stops.
    // `cmoving` is what Wait_ChrMoving (0x100) polls. Advanced by ActorWorld each tick.
    Vec3 cmove_tgt{};
    float cmove_spd = 0;
    float speed30c = 0;          // +0x30c from the factory (CASTINFO+0x48); a script walk's base
    float cmove_r2 = 0;          // the squared arrival radius (+0x364)
    bool cmoving = false;

    // Op 0x151 Chr_Animate(id, motion) (FUN_00571000): +0x994 = motion, the ASO SECTION to play
    // (a dialogue gesture: C_250/C_280 carry section 400, C_270 319); -1 = +0x994 = +0x998, back
    // to the mover's locomotion section. It also clears the Chr_Pattern pin. CHR_OPS.md §4.
    int anim_motion = -1;
    // Op 0x152 Chr_Pattern (FUN_005712e0, CHR_OPS.md §4): the pinned AIA tag/frame; freezes the ASO.
    PatternPin pattern;
    // +0x97c: the index of the AIA file the sprite is decoded from (the anime_File numbering, aso_aia_name); -1 =
    // not set, the cast's own sheet. Written by the actor's ASO (a section start, anime_File: AsoVm::bind_file)
    // and by op 0x167 Chr_AniChange, which exists because a Chr_Pattern pin stops the ASO.
    // build/event_re/ANI_VARIANT.md.
    std::int32_t aia_file = -1;
    // The look block +0x308/+0x39a/+0x370/+0x350/+0x354/+0x360/+0x36c (ops 0x132..0x138, §6).
    // look.cmd is +0x308 and `yaw` is +0x314 (in the CleriaCore basis; chr_state.hpp).
    LookState look;
    // Op 0x162 Chr_Tremble (FUN_00571ce0, §7): the body sprite's square-wave jitter.
    Tremble tremble;
    // Op 0x159 Chr_FootSound (FUN_00571320): +0x4be. Factory: class 2 (the player) 1, else 0 (§3).
    bool footsteps = false;
    // Op 0x144 Chr_FlyTo's block (FUN_00570b30): the 3D target ORIGIN (+0x374..+0x37c), the
    // squared arrival radius +0x380 (0 = none) and the speed scale +0x368 (§8).
    Vec3 fly_tgt{};
    float fly_r2 = 0.0f, fly_spd = 0.0f;

    // Op 0x130 Chr_AttachScript (FUN_00570770): the talk script +0x630 the action probe starts
    // (FUN_00460470, build/event_re/BARRIER_OBJECTS.md §1b). Empty = not a probe candidate.
    std::string talk_script;
    // Op 0x154 Chr_Alpha's fade block (FUN_005715d0): ai.alpha (+0x4d8) steps by +0x4e0 toward
    // +0x4dc in the actor tick (FUN_0045c490 0x45d147). ActorWorld::tick_alpha_fades.
    float alpha_to = 1.0f, alpha_step = 0.0f;
    // The event-group glow (build/event_re/BARRIER_OBJECTS.md §1a): the spawn op's group writes the
    // marker ARGB +0x2b4 (group 1 = 0xFFE00000) and seeds the phase byte +0x2a7 from the game RNG;
    // the actor tick steps the phase; the colour builder adds RGB * A * |sin(2 pi phase / 255)|.
    std::uint32_t glow_marker = 0;   // +0x2b4 (0 = no glow)
    std::uint8_t glow_phase = 0;     // +0x2a7
    std::uint8_t dust_cd = 0;        // +0x4c2: ticks before the next foot dust (FUN_0045c490 0x45d228)
    int launch = 0;                  // +0x4d0: the knock-up / juggle counter (hit.hpp launch_on_hit)

    // Hidden outright (a removed corpse). Chr_Alpha fades ai.alpha instead (above).
    bool visible = true;

    // Transient chase-speed multiplier, driven by the enemy's animation script the way
    // anime_Movable gates the player: the brain sets it to 0 while an attack is committed
    // so the enemy plants for its swing instead of sliding through it. 1 = full chase.
    float move_scale = 1.0f;

    // Combat reaction, on being hit. The engine writes a knockback impulse into the
    // cosmetic offset +0x338/+0x340, and FUN_00478510 sets the hurt timer +0x238 = 1.65, which
    // the integrator's FUN_0045a3a0 runs down by 0.075 a tick; while it is non-zero the colour
    // builder FUN_00473c60 forces the sprite's SPECULAR to (0x6b, 0x37, 0x13) — a dim orange
    // add, not a white flash (hit_feedback.hpp). (+0x4e8 is not a hit flash: it is the
    // resist-all timer a kind-0xe homing orb sets, FUN_0059a6a0.)
    Vec3 knockback{};      // (legacy; enemies use mover.k1/k2 — the player keeps its own)
    MoverState mover;      // engine: +0x32c velocity, +0x338/+0x344 knock channels, +0xa3c disp
    float hit_flash = 0.0f;  // engine: +0x238, the hurt timer (1.65 -> 0 by 0.075 a tick)
    // +0x4ec, radians: XSO op 0x161 Chr_SlantFire writes it (FUN_00571ca0); the one reader found (the export
    // searched for the offset) is anime_GenerateEffect's attached branch, as the effect's pitch
    // [EXPORT 004496f0.c:106] (a caster aiming up or down in a scene).
    float slant = 0.0f;
    float scale = 1.0f;    // engine: +0x248, the model/sprite scale (M460 shrinks on a split)
    float height = 0.0f;   // engine: +0x324, collision height: the origin floats this far above pos
    bool flyer = false;    // engine: byte +0x398 (CASTINFO+0x44 > 0 or EI+0xb0 != 0): hovers via
                           // FUN_00476960, no gravity, never floor-snapped
    float spawn_y = 0.0f;  // engine: +0x31c, the authored placement y (the hover's phase-0/1 base)
    float radius = 0.675f; // engine: +0x328, collision radius (M460 rescales it)
    // The hurt body +0x60 (CASTINFO +0x30/+0x58/+0x5c/+0x54) and its re-hit bank +0x5e0.
    float hurt_radius = 0.4f, hurt_half_h = 0.0f, hurt_off = 0.0f;
    int hurt_shape = 0;
    RehitBank bank;
    std::uint32_t hit_serial = 0;   // +0x3c4: drawn at spawn; the player redraws per attack
    // The factory's flag words +0x12c / +0x130 (factory_flags.hpp, build/ai_spec/FACTORY.md).
    std::uint32_t flags12c = 0x00110010;
    int live_section = -1; // engine: +0x994, the live ASO section, mirrored by the host (M470 reads the other Levenuebas')
    std::uint32_t flags130 = 0;
    // The sprite's render-state word +0x294 bit 0x400 (D3DRS_ZENABLE, FUN_005eb5b0 bit 10): set by the
    // factory's 0x24541 (FUN_00459410), op 0x158 Chr_ZTest writes it. [not modelled] the sprite pass
    // always depth-tests.
    bool ztest = true;
    int aso_local15 = 0;   // engine: +0xa18 = ASO local int 15, applied when its script binds
    // engine: +0x504, a free-running per-actor tick counter: the actor tick FUN_0045c490 does
    // `inc [esi+0x504]` (0x45d32d) every tick (build/aitick_re/AITICK.md §1d). Read by M900's
    // ring formation off its owner (M810). The host advances it.
    std::uint32_t age_ticks = 0;
    bool no_min_damage = false;   // engine: byte +0xce — no min-1 chip (hit.hpp no_floor)
    bool guard399 = false;        // engine: byte +0x399 — guarding (hit.hpp guard_blocks); M810's barrier
    float tint_alpha = 1.0f;      // engine: the alpha byte of +0x290 (M460's crowding fade)
    std::uint32_t tint = 0xffffffffu;  // engine: +0x148/+0x278 colour words (M110's enrage), ARGB [INFERRED order]
    // Hitstun: ticks an enemy is stunned after the player lands a hit, during which its
    // in-progress attack is interrupted and it can't act. The DURATION is [TUNED], not
    // RE'd from the enemy hurt-handler (which lives in the untouched main/object gap); it
    // exists so a hit visibly stops an enemy's swing instead of the swing continuing.
    int hitstun = 0;

    // engine: +0x120 <- CASTINFO+0x64. [H-high] an AI HANDLER INDEX — genuinely
    // stored (0x4342c1 -> FUN_00459410), but no consumer INDEXING it has been read.
    // ** It is also a known decoy: ** it equals enemy_index + 3 across the M_ range,
    // which is why it once scored 78/80 as a fake ENEMYINFO index. Kept because the
    // engine keeps it; do not index anything with it.
    std::int32_t ai_index = 0;
    // The enemy AI block (+0x6f0..+0x8b0 plus heading/speed/opacity/hittable): part of the
    // actor, as in the engine, so a brain can read and write OTHER actors' state (M460 and
    // its bats). src/cleria/enemy_ai.hpp.
    EnemyAi ai;

    // --- ours, not the engine's ---
    // The level the stats were computed at. ** Whether the engine's actor retains its
    // level is [UNKNOWN] ** — the spawn path (FUN_004202e0) reads ENEMYINFO+0xe8, feeds
    // the GAMEINFO curve, and we have not traced it being kept afterwards. We keep it
    // because our own code wants to explain a stat block. Flagged so nobody later reads
    // this as an engine fact.
    int level = 0;
};

}  // namespace cleria
