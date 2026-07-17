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
        std::int32_t type = 0;   // +0x04 2|3|4 — meaning [UNKNOWN]; not a loop flag
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
    // The game flag array — the SAME g_flags the XSO scene VM uses (FUNCLIST.DEF is
    // the shared function enum for both VMs). Animation sections are chains of
    // Flag_IsEqualInt-guarded variants: a failed guard jumps to the next variant, so
    // without flags every guard fails and NOTHING plays. Not optional.
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
