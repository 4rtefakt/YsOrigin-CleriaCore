// ASO — the animation-script VM (`XSR\0` container, `main/gfx/ani_script.cpp`).
//
// This is how the game actually animates: a per-actor bytecode script says
// `anime_Pattern(tag, frame)` then `anime_Wait(n)`, holding that frame for n ticks.
// One tick = one call of the interpreter (FUN_004472e0) = 1/60 s of scaled time.
//
// It matters because no constant can stand in for it: attacks run 1–4 ticks/frame
// while idle runs 10 — a 10x spread. Locomotion happens to be uniform per section
// (walk 6, run 4, idle 10), which is why a single fudged rate looked plausible.
//
// The script supplies only (file, animTag, frame). **`dir` is NOT in the script** —
// it comes from the actor's facing, which is what closes the AIA region formula
// `region = firstRegion + (nFrame*dir + frame)*2`.
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "cleria/hit_volume.hpp"

namespace cleria {

// `anime_File(id)` indexes the exe's AIA name table at `.data 0x68a800` (269
// entries) — `FUN_0040a460(buf, "data\\ani\\%s.aia", table[id])`. Engine data, so
// it lives in code, not assets.
//
// Only the entries the player scripts use are reproduced here; the full 269 are not
// dumped, and every M_*.ASO calls anime_File ZERO times (enemies just use their
// same-named .aia sidecar), so these nine are the ones that matter. Returns "" for
// anything else.
//
// This is what picks the sprite: in C_010 (Yunica), `Flag_IsEqualInt(161,1)` guards
// `anime_File(6)` = c_102 and falls through to `anime_File(15)` = c_111. So flag 161
// set => the cutscene set, clear => the gameplay set. The game chooses; we used to
// hardcode it.
const char* aso_aia_name(std::int32_t file_id);

class Aso {
  public:
    struct Section {
        std::int32_t id = 0;     // +0x00 animation/action id — SPARSE, not an index
        // +0x04. ** A PRIORITY LEVEL [C-DIS], not a kind. ** FUN_00446550 (SetSection)
    // REFUSES the transition when newType < curType — which is why walking cannot
    // interrupt a swing, and why an attack plays to completion. This sat as
    // "2|3|4 — meaning [UNKNOWN]; not a loop flag" while it was the whole
    // animation-cancel rule.
    std::int32_t type = 0;
        std::int32_t code_off = 0;  // +0x08 BYTE offset from codeBase
        std::int32_t code_len = 0;  // +0x0C length in DWORDS
    };

    static Aso load(const std::filesystem::path& path);
    static Aso parse(const std::vector<std::uint8_t>& bytes);

    bool ok() const { return !code_.empty(); }
    const std::vector<Section>& sections() const { return sections_; }
    const Section* find(std::int32_t id) const;
    const std::vector<std::uint32_t>& code() const { return code_; }

  private:
    std::vector<Section> sections_;
    std::vector<std::uint32_t> code_;  // the whole blob, indexed in dwords
};

// One actor's script state. Tick it at 60 Hz.
class AsoVm {
  public:
    void bind(const Aso* aso);
    // Switch to a section by ID. Sections are addressed by id, not index; the VM's
    // initial state is id 2 (idle for the players), present in 112/115 files.
    //
    // Section `type` is a PRIORITY (2 locomotion < 3 attack < 4 special/reaction) and
    // SetSection (FUN_00446550) REFUSES a transition to a lower one — which is exactly
    // why walking cannot interrupt a swing and an attack plays to its own authored
    // length. Returns false when refused, so callers can simply ask every tick and let
    // the running animation win. `force` bypasses the check for the internal
    // end-of-section chain and for hard resets.
    bool set_section(std::int32_t id, bool force = false);
    std::int32_t section() const { return section_; }

    // Advance one 60 Hz tick. Runs instructions until an anime_Wait yields (or the
    // section ends), then holds for that many ticks.
    void tick();

    // What the script currently wants drawn. `dir` is deliberately absent.
    //
    // ## The script sets every tag. Nothing in code does. [C-DIS]
    //
    // `anime_Pattern(tag, frame)` (funcid 3) is the ONLY writer. Handler @0x00447528:
    // `obj+0xc0 = tag; obj+0xc8 = frame`. The negative is exhaustive: no instruction in
    // the image stores an immediate 0x12c..0x135 to memory except to actor+0x994 (the
    // SECTION id), and writes to the tag field number ZERO. Code only picks the
    // *section*, via `FUN_00446550(&actor[0x8f0], id, flag)` = CAniSprite::SetSection.
    //
    // ## Tags come in PAIRS, switched by GLOBAL FLAG 149 [C-DIS]
    //
    // C_111's 0x12c..0x135 are NOT attacks. Each is the flag-149 alternate of a
    // locomotion tag — same poses, different weapon in Yunica's hand:
    //
    //   0x12c = walk (alt of 0x0)     0x12f = alt of 0x4     0x134 = alt of 0x9
    //   0x12d = run  (alt of 0x1)     0x131 = idle           0x135 = alt of 0x10a
    //   0x12e, 0x130 = jump family    0x133 = alt of 0x5
    //
    // The alternates sit behind `Flag_IsEqualInt(149, 1)`. **0x132 is in neither C_111
    // nor C_010 — it is not a tag at all, and the "gap" that suggested a missing attack
    // never existed.**
    //
    // Yunica's real attacks are in C_114, reached by `anime_File(11)` from sections
    // 0x100..0x112, and their weapon variants are INTERLEAVED rather than blocked:
    // 0x100<->0x101, 0x102<->0x103, 0x104<->0x105, 0x107<->0x110, 0x109<->0x13a,
    // 0x10b<->0x13b/0x13c. Sections 0x10c..0x111 carry no flag-149 test — those moves
    // are weapon-independent.
    //
    // ** flag 149 selects Yunica's DISPLAYED WEAPON: 1 = her axe, 0 = the Crimson
    // Lotusblade. ** [H-high]
    //
    // The red blade is the **Crimson Lotusblade**, obtained on Floor 11. Per the game's
    // own documentation: *"The Lotusblade is used when her fire skill is selected,
    // otherwise her axe."* Her fire skill is Phoenix Flame. So flag 149 is the
    // axe-vs-Lotusblade display switch, and every piece fits:
    //
    //   - flag 149 == 1 -> the 0x12c.. set (axe) = "Yunica as she looks walking around
    //     in the game", confirmed by someone who has played it.
    //   - flag 149 == 0 -> the 0x0.. set (Lotusblade), shown only with the fire skill
    //     selected — and only obtainable from Floor 11 onward.
    //   - FUN_0046af60 toggles it and forces +0x994 = 2 (idle), restarting the
    //     animation: exactly what swapping the weapon in hand requires.
    //
    // ** The polarity is inverted from what a cold read suggests, and that is the
    // lesson.** DAT_0076bb70's INITIAL value is 0 = the Lotusblade — a weapon the
    // player cannot have for the first ten floors. So gameplay must write 1, and
    // reading "default 0" as "the default state" is what made this look backwards.
    // **A global's initial value is not its normal value** — the same trap as the RNG
    // seeds sitting in .data as decoys for what timeGetTime() overwrites at startup.
    //
    // [H-high] rather than [C-DIS]: the write of 1 has not been watched, and "fire
    // skill selected" is the documented behaviour rather than a disassembled predicate.
    // Two earlier labels for this state ("Boost", "weapon tier") were retracted; this
    // one at least explains the polarity, the restart, and the Floor-11 gate together.
    //
    // ## Two of MY claims died here, and the second lesson is the useful one
    //
    // build/source_map/PLAYER_ANIM.md called 0x12c..0x135 "ten 8-directional ACTION
    // animations", inferred from their ABSENCE IN THE DEMO FILE. Absence from a
    // cutscene set is exactly what a second weapon's locomotion looks like — cutscenes
    // fix the weapon. Nobody rendered them.
    //
    // This comment then asserted "the ASO does not drive attacks", from a sweep that
    // RAN every section through this VM and collected the tags. The sweep was sound and
    // the conclusion was still false: the emissions sit behind a flag test this VM
    // never satisfies from a cold start. ** A negative derived by EXECUTING a branchy
    // script is not a negative — it is a statement about the inputs you happened to
    // supply. ** A static scan of the bytecode finds all nine tags immediately.
    //
    // Section id != AIA tag. Section 0x10a -> tag 0x10a matches by coincidence; sections
    // 0x100..0x112 emit C_114's tags rather than their own ids.
    std::int32_t anim_tag() const { return anim_tag_; }
    std::int32_t frame() const { return frame_; }
    std::int32_t file_id() const { return file_id_; }
    std::int32_t dir_offset() const { return dir_offset_; }

    // anime_FootSmoke (funcid 19) fired during the last tick. The handler sets
    // bit 0 of actor+0x3c0, and only when IsLand (actor+0x388). 362 uses across the
    // corpus.
    //
    // ** It does NOT drive the footstep SOUND. ** This comment used to call that
    // [UNKNOWN] and name FootSmoke "the obvious candidate", reasoning that the
    // dedicated anime_PlayFootfall (funcid 43) has ZERO uses in the corpus so
    // something else must do it. The premise was right and the candidate was wrong:
    // the something else is a tick countdown in the actor update, with no script
    // involvement at all. FUN_0045c490 counts actor+0x4bf down once per tick and
    // fires at zero; FUN_0045d600 then builds the sound id straight from the surface
    // table and plays it:
    //
    //   0045d6b2  MOV DL,[EDI + 0x6d5dad]   ; surface +0x0D = variant_mask
    //   0045d6c8  AND ECX,EAX               ; rng & variant_mask
    //   0045d6ca  ADD ECX,EBX               ; + base_sound_id  (surface +0x06)
    //   0045d6cf  CALL 0x0045d7a0           ; the SE play
    //   0045d6ef  MOV byte ptr [ESI+0x4bf],CL   ; reload the interval (walk/run column)
    //
    // So FootSmoke is the visual puff and nothing more, and the cadence lives in
    // cleria/surface.hpp. See also the README ledger: the game's footsteps are NOT
    // paced by ASO events — a claim this project made and then refuted.
    bool foot_smoke() const { return foot_smoke_; }
    // ... and the bit itself, actor+0x3c0 & 1: it stays set until FUN_0045c490's dust hook takes it
    // [BYTES 0x45d23f..0x45d257], which is not while that hook's 5-tick cooldown +0x4c2 runs.
    bool foot_smoke_bit() const { return foot_smoke_bit_; }
    void clear_foot_smoke_bit() { foot_smoke_bit_ = false; }
    // anime_Jump this tick: the host sets +0x330 = +0x420 = v (the cast's +0x408 when v == 0),
    // y += it, +0x388 = 0. anime_Dash: the host sets knock channel 1 = (cos, sin)(heading) * d.
    bool jumped() const { return jumped_; }
    float jump_v() const { return jump_v_; }
    bool dashed() const { return dashed_; }
    float dash_d() const { return dash_d_; }
    // anime_SMode(x) (funcid 0x21) this tick, summed: the host adds x * 0.6 to the Boost gauge
    // while boosting (FUN_004472e0 case 0x21 reads the arg word as a float).
    float smode() const { return smode_; }
    bool own_jump() const { return own_jump_; }   // +0x9d6: keeps the mover off the air section
    void set_own_jump(bool v) { own_jump_ = v; }
    // FUN_00446680(m): run the current section NOW, skipping waits, until its Marker(m), its
    // end, or a section change (at most 1001 steps). A no-op when the marker is already m.
    void fast_forward_to_marker(std::int32_t m);

    // anime_GenerateCollision (funcid 0x23) armed a melee hit-volume THIS tick
    // (build/attack_re/ATTACK.md). It is the sole melee attack trigger: the ASO section
    // issues it at whatever frame the swing connects, and it arms the whole box in one
    // op. `armed_hitbox()` is true only on the tick it fires; the consumer counts down
    // `lifetime` frames itself (the engine decrements actor+0x118 once per attacker tick
    // in the hit scan). Both the player and 49 monster scripts use this.
    // The armed hit volume: GenerateCollision's 13 args decoded exactly as FUN_004494e0 does
    // (hit_volume.hpp). Its token (+0xf0) is the ATTACKER's hit serial, filled by the host.
    using Hitbox = HitVolume;
    bool armed_hitbox() const { return hitbox_armed_; }
    const Hitbox& hitbox() const { return hitbox_; }

    // anime_SePlay3D (funcid 0x2a) — sound effects fired by the animation itself, which
    // is how the game times every swing, impact and monster noise (1072 uses across the
    // shipped scripts; see build/aso_re/ASOVM.md). `id` is an EFFECTDEF id, ready for
    // Audio::play. Valid for the tick that produced them, like armed_hitbox(); a single
    // tick runs many ops before it yields, so more than one can fire at once.
    struct SoundEvent {
        std::int32_t id = 0;      // arg[0] — EFFECTDEF id
        float dx = 0, dy = 0, dz = 0;  // arg[1..3] — offset from the actor (unused: no panning)
    };
    const std::vector<SoundEvent>& sounds() const { return sounds_; }

    // anime_ChangeSpecular (funcid 0x1f, `fffffffi`; build/mech_re/ASO_NOOPS.md §1 [BYTES 0x447887..0x4478e7]):
    // a write of the actor's specular tint struct +0x260 — B (+0x268) from the first colour, A (+0x264) from
    // the second, t = 0, the step per tick, the hold count. The base word is not written. The monsters'
    // attack wind-up flash. Valid for the tick that produced it; the last one of a tick wins (one struct).
    struct SpecularEvent {
        std::uint32_t b = 0, a = 0;   // 0xRRGGBB: the start and the end colour
        float step = 0;
        int count = 0;
    };
    const SpecularEvent* specular() const { return specular_set_ ? &specular_ : nullptr; }

    // anime_CameraDistanceOffset (funcid 0x0f, `f`; ASO_NOOPS.md §2 [BYTES 0x447635..0x447640]): vm+0xd8, added to
    // the AIA region's bill[0] by FUN_00446000 — the sprite quad is drawn that many world units nearer the eye
    // along the eye -> feet ray (a depth bias; the camera does not read it). STATE: 0 from a section's first tick
    // until the script sets it (the VM zeroes vm+0xcc..+0x10b at pc 0).
    float sprite_depth_offset() const { return depth_offset_; }

    // anime_GenerateDebris (funcid 0x26, varargs; ASO_NOOPS.md §3.1 [BYTES 0x447980..0x447a39]): a0 the count,
    // a1 spin, a2 never expires, a3 static, a4..a6 not read, a7.. the bag of debris ids (argc - 7 of them).
    // The host resolves the colour and the reviver gate from the cast and calls the spawner (pickup.hpp).
    // Valid for the tick that produced it; one per tick is the corpus maximum.
    struct DebrisEvent {
        std::int32_t count = 0;
        bool spin = false, keep = false, fixed = false;
        std::vector<std::int32_t> bag;
    };
    const DebrisEvent* debris() const { return debris_set_ ? &debris_ : nullptr; }

    // anime_FireBullet (funcid 41 / 0x29) — projectile spawns, 186 uses. Hugo and Toal
    // both fire (Yunica never does), as do many enemies. The two ints are surfaced raw
    // and the rest kept as the script's own floats, because what they SELECT is still
    // being reverse-engineered: `id` is NOT a CASTINFO index (its values run past that
    // table's 186 entries, and the ones that do land in range hit unrelated boss names).
    // Meanings land in build/projectile_re/PROJECTILE.md.
    struct BulletEvent {
        std::int32_t kind = 0;  // arg[0] — a small enum; only 0/1/2 appear in the corpus
        std::int32_t id = 0;    // arg[1] — selects which projectile
        float f[19] = {};       // arg[2..20], raw; heading/offset/speed live in here
    };
    const std::vector<BulletEvent>& bullets() const { return bullets_; }

    // anime_GenerateEffect (funcid 0x25, `iffffiiiff`) = FUN_004496f0: an EF2 effect at
    // origin + (cos, sin)(angle + facing) x fwd, y + voff (build/ai_spec/EFFECTHIT.md §6). The
    // hit fields (rtype / status / power / knock) arm the EF2's hit emitters — [not modelled]
    // until the EF2 particle simulation exists; the effect is placed, not yet harmful.
    struct EffectEvent {
        std::int32_t id = 0;
        float fwd = 0, angle_deg = 0, voff = 0, scale = 1;
        std::int32_t attach = 0, rtype = 0, status = 0;
        float power = 0, knock = 0;
    };
    const std::vector<EffectEvent>& effects() const { return effects_; }

    // anime_GenerateLocus (funcid 0x24, `iiiiiiffii`) = FUN_00464960: a GoLocusSword sword sweep
    // (src/cleria/locus.hpp, build/ability_re/LOCUS.md). Surfaced raw, in script order; the host
    // spawns the LocusSword with the owner's token, facing and SL2 file.
    struct LocusEvent {
        std::int32_t i[10] = {};   // [0..5], [8], [9] as ints ([6],[7] hold the float bits)
        float bonus = 0, knock = 0;   // [6], [7]
    };
    const std::vector<LocusEvent>& loci() const { return loci_; }

    // Actor capability fields the script drives (ASOVM.md §4, funcids 20..28). These are
    // STATE, not per-tick events: a swing lowers Movable/Turnable during the commit and
    // opens Cancelable/AttackReserve near the end, which is what gives each character its
    // own attack feel. They reset to permissive defaults on a section change — the engine
    // keeps them on the actor, but every section it enters sets what it needs, and
    // defaulting open here means a script that never restores one cannot leave the actor
    // permanently rooted. That is a deliberate safety deviation.
    float movable() const { return movable_; }        // funcid 26, 0 = rooted in place
    bool turnable() const { return turnable_; }       // funcid 27
    bool actable() const { return actable_; }         // funcid 22, accepts a new action
    bool cancelable() const { return cancelable_; }   // funcid 23, cancel window open
    bool attack_reserve() const { return atk_reserve_; }  // funcid 21, combo input buffered
    // anime_AttackBreak (funcid 0x14; ASO_NOOPS.md §6 [BYTES 0x4476fb..0x447707]): byte vm+0xe5, the combo-buffer
    // penalty window the player update reads (player_actions.hpp In::attack_break). Adol's two sets only.
    bool attack_break() const { return atk_break_; }
    bool durable() const { return durable_; }         // funcid 24, super-armor
    bool untouchable() const { return untouchable_; } // funcid 28, i-frames
    bool jump_enable() const { return jump_enable_; } // funcid 25

    // The section to fall into when the current one runs off its end. The engine keeps
    // this as the queued-next field [0x2a]; sections that mean to repeat end with a jmp
    // back to their own loop point instead of falling off (ASOVM.md §5).
    void set_queued(std::int32_t id) { queued_ = id; }
    std::int32_t queued() const { return queued_; }
    // True on the tick the section ran off its end and chained away.
    bool section_ended() const { return section_ended_; }

    // Inputs the script polls.
    void set_is_land(bool v) { is_land_ = v; }
    void set_is_stun(bool v) { is_stun_ = v; }
    // The game flag array — the SAME g_flags the XSO scene VM uses, and now proven so
    // [C-DIS]: `flag[i]` (i < 0x200) IS the global array at **0x0076b91c + i*4**, from
    // the handler's `LEA EDX,[EAX*0x4 + 0x76b91c]`. That mapping self-checks against
    // two globals we already knew independently:
    //
    //   flag[161] == 0x0076bba0  — the demo-mode global; and flag 161 selects
    //                              anime_File(6) = C_102, the demo sprite set. ✔
    //   flag[150] == 0x0076bb74  — the player character index. ✔
    //   flag[149] == 0x0076bb70  — the animation-set switch (see anim_tag() above).
    //
    // Animation sections are chains of Flag_IsEqualInt-guarded variants: a failed guard
    // jumps to the next variant, so without flags every guard fails and NOTHING plays.
    // Not optional.
    //
    // ** That last sentence was already here, and I ignored it. ** I swept every
    // section of every character's ASO through this VM WITHOUT SETTING FLAGS, collected
    // the tags that came out, and published "the ASO does not drive the player's
    // attacks". The attack emissions sit behind Flag_IsEqualInt(149,1) — a guard that
    // cannot pass with no flags — so the sweep could not have seen them, and the file
    // I was editing said as much. **Executing a flag-guarded script proves nothing
    // about the branches your inputs exclude; scan the bytecode statically instead.**
    // The game-flag table the Flag_ ops read (0x76b91c). A writable table also takes the
    // Flag_SetInt / AddInt / ... writes; a const one (headless previews) is read-only.
    void set_flags(std::int32_t* flags, std::size_t count) {
        set_flags(static_cast<const std::int32_t*>(flags), count);
        wflags_ = flags;
    }
    void set_flags(const std::int32_t* flags, std::size_t count) {
        flags_ = flags;
        wflags_ = nullptr;
        flag_count_ = count;
    }

    bool ok() const { return aso_ && aso_->ok() && cur_ != nullptr; }

    // The two player-state queries of the Flag_ family (M_900.ASO, Hugo's eyes): Flag_IsEquip(item)
    // (FUN_00443ce0: the item is worn / held) and Flag_SetMagicHolder (flag 208 = FUN_0046d560). A VM
    // without them answers 0 and writes nothing.
    std::function<bool(int)> equip_fn;
    std::function<int()> magic_slot_fn;

  private:
    // Returns false when the section ends.
    bool step(std::size_t& pc);

    const Aso* aso_ = nullptr;
    const Aso::Section* cur_ = nullptr;
    std::int32_t section_ = 2, prev_section_ = 2;
    std::size_t pc_ = 0;
    std::int32_t wait_ = 0;
    std::int32_t anim_tag_ = -1, frame_ = 0, file_id_ = -1, dir_offset_ = 0;
    std::int32_t marker_ = -1, marker_wait_ = -1;
public:
    // anime_Marker's last value = the actor's +0x9bc (sprite[0x33]) that brains read.
    // The sprite starts zeroed, so "never set" reads as 0.
    std::int32_t marker() const { return marker_ < 0 ? 0 : marker_; }
    // Brains write both registers directly: +0x9bc (the marker) and +0x9c0 (the block
    // value — Marker(n) holds while it equals n; M460 writes 3 to park section 8 on its
    // final Marker(3)). Both are cleared when the VM next starts a section at pc 0.
    void set_marker(std::int32_t v) { marker_ = v; }
    void set_marker_block(std::int32_t v) { marker_wait_ = v; }
    void set_durable(bool v) { durable_ = v; }                 // brains write +0x9d8 too
    void add_wait(std::int32_t n) { wait_ += n; }              // vm[0x2e] += n (M860's freeze: T+0x9a8 += 1)
    void set_frame(std::int32_t f) { frame_ = f; }             // a brain forcing +0x9b8 (M760)
    void set_local(int n, std::int32_t v) { if (n >= 0 && n < 32) locals_[n] = v; }   // obj[0x3b+n]
    std::int32_t marker_block() const { return marker_wait_; }
private:
    // FUN_004472e0: whenever the VM runs a section from its first word, it memsets
    // obj[0x33..0x42] (the marker, the block value, ..., locals 0..7) and sets the block
    // value obj[0x34] = -1 — both on a fresh section and on a chain.
    void reset_section_regs();
    std::int32_t r_ = 0;             // the comparison register
    std::int32_t locals_[32] = {};   // obj[0x3b + n]
    const std::int32_t* flags_ = nullptr;
    std::int32_t* wflags_ = nullptr;
    // FUN_004472e0's Flag_ operand: an index below 0x200 is the global table, anything above is
    // the VM's own register obj[idx - 0x1c5] = locals_[idx - 0x200] (obj[0x3b + n]). [BYTES 0x448468..]
    std::int32_t flag_get(std::int32_t i) const;
    void flag_put(std::int32_t i, std::int32_t v);
    std::size_t flag_count_ = 0;
    bool is_land_ = true, is_stun_ = false;
    bool foot_smoke_ = false, foot_smoke_bit_ = false;
    bool jumped_ = false, dashed_ = false, own_jump_ = false;
    float jump_v_ = 0, dash_d_ = 0;
    float smode_ = 0;
    float depth_offset_ = 0;         // vm+0xd8
    bool hitbox_armed_ = false;
    Hitbox hitbox_;
    static constexpr std::size_t kMaxSoundsPerTick = 8;   // a script never issues near this many
    static constexpr std::size_t kMaxBulletsPerTick = 8;
    std::vector<SoundEvent> sounds_;
    SpecularEvent specular_;
    bool specular_set_ = false;
    DebrisEvent debris_;
    bool debris_set_ = false;
    std::vector<BulletEvent> bullets_;
    std::vector<EffectEvent> effects_;
    std::vector<LocusEvent> loci_;
    float movable_ = 1.0f;
    bool turnable_ = true, actable_ = true, cancelable_ = false, atk_reserve_ = false;
    bool atk_break_ = false;
    bool durable_ = false, untouchable_ = false, jump_enable_ = true;
    std::int32_t queued_ = 2;   // idle: [0x29]=[0x2a]=[0x2b]=2 at init (ASOVM.md §5)
    std::int32_t queued_jump_ = 2;  // [0x2b]: the follow-on after section 3 (the jump)
    bool end_yield_ = false;        // the last step was `end` (yield; chain next tick)
    bool section_ended_ = false;
};

}  // namespace cleria
