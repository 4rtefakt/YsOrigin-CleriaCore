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
#include <string>
#include <vector>

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
    bool set_section(std::int32_t id);
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
    // ** What flag 149 MEANS is [UNKNOWN]. ** What it DOES is [C-DIS]: it is
    // DAT_0076bb70, toggled 0/1 by FUN_0046af60, which also forces +0x994 = 2 (idle),
    // restarting the animation. Rendering both sets: the flag-149 alternates show an
    // AXE, the default set (flag == 0) shows a long red glowing blade.
    //
    // Yunica fights with an axe, so the alternates are normal play and the default is
    // something else. The obvious guess was Boost — **the project owner checked in-game
    // and the red sword is NOT her Boost form**, so that reading is dead and no
    // replacement is offered here. An animation switch that restarts the state is the
    // shape of a transformation, a weapon change or a story mode; the data cannot tell
    // us which, and inventing a name for it would be exactly the kind of plausible
    // label this file has already had to retract twice.
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
    void set_flags(const std::int32_t* flags, std::size_t count) {
        flags_ = flags;
        flag_count_ = count;
    }

    bool ok() const { return aso_ && aso_->ok() && cur_ != nullptr; }

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
    std::int32_t r_ = 0;             // the comparison register
    std::int32_t locals_[32] = {};   // obj[0x3b + n]
    const std::int32_t* flags_ = nullptr;
    std::size_t flag_count_ = 0;
    bool is_land_ = true, is_stun_ = false;
    bool foot_smoke_ = false;
};

}  // namespace cleria
