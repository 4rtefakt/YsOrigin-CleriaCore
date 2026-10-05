#include "cleria/aso.hpp"

#include <cstring>
#include <fstream>

namespace cleria {
namespace fs = std::filesystem;

namespace {

std::uint32_t rd32(const std::uint8_t* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

// Funcids we act on. The dispatch implements 1..0x8e; everything we don't model is
// deliberately a no-op — the VM must keep running, since an unknown call in the
// middle of a section still has to advance the pc correctly (argCount does that).
enum : std::uint32_t {
    kWait = 1,       // anime_Wait=i          — the tick/yield
    kFile = 2,       // anime_File=i          — selects the AIA
    kPattern = 3,    // anime_Pattern=ii      — (animTag, frame)
    kMarker = 4,     // anime_Marker=i        — blocks while obj[0x34] == a0
    kRaise = 5,      // anime_Raise=i         — rebind section
    kLocalSet = 6,   // anime_LocalSetInt=ii
    kLocalAdd = 7,   // anime_LocalAddInt=ii
    kLocalMul = 8,   // anime_LocalMulInt=ii
    kIsEqPrevAction = 10,  // anime_IsEqualPreviousAction=i
    kIsEqLocal = 11,       // anime_IsEqualLocalInt=ii
    kIsLand = 12,          // anime_IsLand=
    kIsStunNow = 13,       // anime_IsStunNow=
    kDirOffset = 16,       // anime_DirOffset=i
    kJump = 17,            // anime_Jump=f  FUN_00449460: if grounded, t = v (or +0x408 when 0)
    kDash = 18,            // anime_Dash=f  0x447665: knock channel 1 = (cos, sin)(heading) * d
    kFootSmoke = 19,       // anime_FootSmoke=
    kGenerateCollision = 0x23,  // anime_GenerateCollision — arms the melee hit-volume
    kSePlay3D = 0x2a,      // anime_SePlay3D=ifffi  — plays EFFECTDEF sound arg0 at the actor
    kFireBullet = 41,      // anime_FireBullet=iifff… — spawns a projectile (FUN_00593550)
    kGenerateEffect = 0x25,  // anime_GenerateEffect=iffffiiiff — an EF2 effect (FUN_004496f0)
    // Actor capability gates (ASOVM.md §4). Persistent state, raised/lowered across an
    // animation; together they are what makes each character's attack feel its own.
    kAttackReserve = 21,   // anime_AttackReserve=i  actor+0xe4 — combo input buffered
    kActable = 22,         // anime_Actable=i        actor+0xe0/0xe1 — accepts a new action
    kCancelable = 23,      // anime_Cancelable=i     actor+0xe2 — cancel window
    kDurable = 24,         // anime_Durable=i        actor+0xe8 — super-armor
    kJumpEnable = 25,      // anime_JumpEnable=i     actor+0xe1
    kMovable = 26,         // anime_Movable=f        actor+0xdc — 0 = rooted
    kTurnable = 27,        // anime_Turnable=i
    kUntouchable = 28,     // anime_Untouchable=i    actor+0xe7 — i-frames
    kFlagIsEqualInt = 95,  // Flag_IsEqualInt=ii   — R = (g_flags[a0] == a1)
};

}  // namespace

const char* aso_aia_name(std::int32_t id) {
    switch (id) {
        case 6:  return "C_102";   // Yunica  — cutscene set (flag 161 == 1)
        case 7:  return "C_103";   // Hugo    — cutscene set
        case 8:  return "C_104";   // Toal    — cutscene set
        case 11: return "C_114";   // Yunica  — variant
        case 12: return "C_115";   // Toal    — Boost / demon form
        case 13: return "C_101";
        case 15: return "C_111";   // Yunica  — GAMEPLAY set
        case 16: return "C_112";   // Hugo    — GAMEPLAY set
        case 17: return "C_113";   // Toal    — GAMEPLAY set
        default: return "";
    }
}

Aso Aso::parse(const std::vector<std::uint8_t>& b) {
    Aso a;
    if (b.size() < 0x10 || std::memcmp(b.data(), "XSR\0", 4) != 0) return a;
    const std::uint32_t n_sec = rd32(&b[0x08]);
    const std::size_t code_base = 0x10 + std::size_t(n_sec) * 0x14;
    if (code_base > b.size()) return a;

    a.sections_.reserve(n_sec);
    for (std::uint32_t i = 0; i < n_sec; ++i) {
        const std::uint8_t* r = &b[0x10 + std::size_t(i) * 0x14];
        Section s;
        s.id = std::int32_t(rd32(r + 0x00));
        s.type = std::int32_t(rd32(r + 0x04));
        s.code_off = std::int32_t(rd32(r + 0x08));   // BYTES from code_base
        s.code_len = std::int32_t(rd32(r + 0x0C));   // DWORDS
        a.sections_.push_back(s);
    }
    const std::size_t blob = b.size() - code_base;
    a.code_.resize(blob / 4);
    if (!a.code_.empty()) std::memcpy(a.code_.data(), b.data() + code_base, a.code_.size() * 4);
    return a;
}

Aso Aso::load(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::vector<std::uint8_t> b{std::istreambuf_iterator<char>(f),
                                std::istreambuf_iterator<char>()};
    return parse(b);
}

const Aso::Section* Aso::find(std::int32_t id) const {
    for (const Section& s : sections_)
        if (s.id == id) return &s;
    return nullptr;
}

void AsoVm::bind(const Aso* aso) {
    aso_ = aso;
    // The interpreter initialises its current section to 2 (FUN_00445bd0 sets
    // obj[0x29]=[0x2a]=[0x2b]=2) — for the players that is idle.
    set_section(2);
}

bool AsoVm::set_section(std::int32_t id, bool force) {
    if (!aso_ || !aso_->ok()) return false;
    const Aso::Section* s = aso_->find(id);
    if (!s) return false;
    // The priority refusal (FUN_00446550). Re-selecting the section already running is
    // a no-op rather than a restart, so a caller may ask for it every tick.
    if (!force && cur_) {
        if (id == section_) return true;
        if (s->type < cur_->type) return false;
    }
    // Capabilities are NOT touched here (FUN_00446620 leaves them): the VM zeroes them when it
    // next runs the section from its first word (reset_section_regs).
    prev_section_ = section_;
    section_ = id;
    cur_ = s;
    pc_ = std::size_t(s->code_off) / 4;
    wait_ = 0;
    end_yield_ = false;
    return true;
}

// Decode and execute one instruction. Encoding (52,280 instructions, 0 errors):
//   opcode = w>>24 · funcid = (w>>12)&0xfff · argOffset = (w>>8)&0xf · argCount = w&0xff
// For opcode 2 the args begin at pc+1+argOffset and the next pc is argsStart+argCount.
// The argOffset words in between are a TYPE DESCRIPTOR (4 bits/arg, 8 args/word;
// 0 = int32, 1 = float32) — which is why operands cannot be typed without it.
bool AsoVm::step(std::size_t& pc) {
    const std::vector<std::uint32_t>& c = aso_->code();
    if (pc >= c.size()) return false;
    const std::uint32_t w = c[pc];
    const std::uint32_t op = w >> 24;

    // Jump targets are a DWORD INDEX RELATIVE TO THE SECTION START, not an absolute
    // index and not a byte offset. Derived from C_010 section 2 (starts at dword
    // 466): `jmp 24` -> 490 = the first anime_Pattern after anime_File, i.e. the
    // idle loop-back; `jz 84` -> 550 = the next Flag_ variant block; `jmp 220` ->
    // 686 = the section's last dword (466+221). Every target lands on an
    // instruction boundary; read as absolute they land outside the section.
    const std::size_t base = cur_ ? std::size_t(cur_->code_off) / 4 : 0;
    auto target = [&]() -> std::size_t { return base + std::size_t(c[pc + 1]); };

    switch (op) {
        case 0x01:  // end: FUN_004472e0 advances the pc BEFORE dispatch, so `end` consumes
                    // itself and yields the tick; the next tick finds the pc at the
                    // section's length and chains to the queued follow-on.
            pc = pc + 1;
            end_yield_ = true;
            return false;
        case 0x03:  // jmp
            pc = target();
            return true;
        case 0x06:  // jz
            pc = (r_ == 0) ? target() : pc + 2;
            return true;
        case 0x09:  // jnz
            pc = (r_ != 0) ? target() : pc + 2;
            return true;
        case 0x02: break;  // call — below
        default:
            // The interpreter implements 0x04/0x05/0x07/0x08/0x0a-0x13 too, but no
            // shipped .ASO uses them. Stop rather than guess.
            return false;
    }

    const std::uint32_t funcid = (w >> 12) & 0xFFF;
    const std::uint32_t arg_off = (w >> 8) & 0xF;
    const std::uint32_t argc = w & 0xFF;
    const std::size_t args = pc + 1 + arg_off;
    const std::size_t next = args + argc;
    if (next > c.size()) return false;

    auto ai = [&](std::uint32_t i) -> std::int32_t {
        return i < argc ? std::int32_t(c[args + i]) : 0;
    };

    switch (funcid) {
        case kWait:
            wait_ = ai(0);
            pc = next;
            return false;  // yield: the tick is spent
        case kFile:
            file_id_ = ai(0);
            break;
        case kPattern:
            anim_tag_ = ai(0);
            frame_ = ai(1);
            break;
        case kMarker:
            // Blocks while obj[0x34] == a0. obj[0x34] is only ever written to -1
            // inside ani_script.cpp, so with no external writer this never blocks;
            // the writer lives outside that file and was not found. [UNKNOWN]
            marker_ = ai(0);
            if (marker_wait_ == marker_) { pc = pc; return false; }
            break;
        case kRaise:
            pc = next;
            set_section(ai(0));
            return false;
        case kLocalSet:
            if (ai(0) >= 0 && ai(0) < 32) locals_[ai(0)] = ai(1);
            break;
        case kLocalAdd:
            if (ai(0) >= 0 && ai(0) < 32) locals_[ai(0)] += ai(1);
            break;
        case kLocalMul:
            if (ai(0) >= 0 && ai(0) < 32) locals_[ai(0)] *= ai(1);
            break;
        case kIsEqPrevAction:
            r_ = (ai(0) == prev_section_) ? 1 : 0;
            break;
        case kIsEqLocal:
            r_ = (ai(0) >= 0 && ai(0) < 32 && locals_[ai(0)] == ai(1)) ? 1 : 0;
            break;
        case kIsLand:
            r_ = is_land_ ? 1 : 0;
            break;
        case kIsStunNow:
            r_ = is_stun_ ? 1 : 0;
            break;
        case kDirOffset:
            dir_offset_ = ai(0);
            break;
        case kFootSmoke:
            // The handler gates on IsLand (actor+0x388), same input as funcid 12.
            if (is_land_) foot_smoke_ = true;
            break;
        case kJump: {                                  // FUN_00449460
            if (is_land_) {
                float v; const std::uint32_t raw = std::uint32_t(ai(0)); std::memcpy(&v, &raw, 4);
                jumped_ = true; jump_v_ = v;               // 0.0 = use the cast's +0x408
                own_jump_ = true;                          // +0x9d6
                is_land_ = false;                          // +0x388 = 0 (a second Jump is a no-op)
            }
            break;
        }
        case kDash: {                                  // 0x447665
            float v; const std::uint32_t raw = std::uint32_t(ai(0)); std::memcpy(&v, &raw, 4);
            dashed_ = true; dash_d_ = v;
            break;
        }
        case kGenerateCollision: {
            // anime_GenerateCollision (0x23): arm the hit volume, FUN_004494e0 ->
            // FUN_0045e750 (build/ai_spec/HITVOLUME.md §1): [0] shape, [1] radius, [2] forward
            // distance, [3] angle offset (degrees), [4] vertical offset, [5] cylinder
            // half-height, [6] lifetime, [7] re-hit timer, [8] one-shot, [9] reaction type,
            // [10] status bits, [11] power, [12] knockback. The int args are int words, the
            // float args float bit patterns. (Before 2026-10-05 arg[2] was read as the radius.)
            auto af = [&](std::uint32_t i) {
                float v; std::uint32_t raw = std::uint32_t(ai(i));
                std::memcpy(&v, &raw, 4); return v;
            };
            // Each arg is converted by its own type nibble (0 int, 1 float; low nibble first).
            auto num = [&](std::uint32_t k) {
                const std::uint32_t tw = c[pc + 1 + k / 8];
                return ((tw >> (4 * (k % 8))) & 0xF) == 1 ? af(k) : float(ai(k));
            };
            float a[13] = {};
            for (std::uint32_t k = 0; k < 13 && k < argc; ++k) a[k] = num(k);
            hitbox_ = volume_from_args(a, 0);
            hitbox_.rtype &= 0xff; hitbox_.status &= 0xff;
            hitbox_armed_ = true;
            break;
        }
        case kAttackReserve: atk_reserve_ = (ai(0) != 0); break;
        case kActable:       actable_ = (ai(0) != 0); break;
        case kCancelable:    cancelable_ = (ai(0) != 0); break;
        case kDurable:       durable_ = (ai(0) != 0); break;
        case kJumpEnable:    jump_enable_ = (ai(0) != 0); break;
        case kTurnable:      turnable_ = (ai(0) != 0); break;
        case kUntouchable:   untouchable_ = (ai(0) != 0); break;
        case kMovable: {
            // The one float in this group: a movement-SPEED scale, not a boolean.
            float v; const std::uint32_t raw = std::uint32_t(ai(0));
            std::memcpy(&v, &raw, 4);
            movable_ = v;
            break;
        }
        case kFireBullet: {
            // 21 args, typed `ii` then floats. Surfaced raw: the consumer decides what to
            // spawn once PROJECTILE.md pins the meanings down.
            auto af = [&](std::uint32_t i) {
                float v; const std::uint32_t raw = std::uint32_t(ai(i));
                std::memcpy(&v, &raw, 4); return v;
            };
            if (bullets_.size() < kMaxBulletsPerTick) {
                BulletEvent b;
                b.kind = ai(0);
                b.id = ai(1);
                for (std::uint32_t i = 2; i < argc && i - 2 < 19; ++i) b.f[i - 2] = af(i);
                bullets_.push_back(b);
            }
            break;
        }
        case kGenerateEffect: {
            auto af = [&](std::uint32_t i) {
                float v; const std::uint32_t raw = std::uint32_t(ai(i));
                std::memcpy(&v, &raw, 4); return v;
            };
            if (effects_.size() < kMaxBulletsPerTick && argc >= 10) {
                EffectEvent ev;
                ev.id = ai(0);
                ev.fwd = af(1); ev.angle_deg = af(2); ev.voff = af(3); ev.scale = af(4);
                ev.attach = ai(5); ev.rtype = ai(6); ev.status = ai(7);
                ev.power = af(8); ev.knock = af(9);
                effects_.push_back(ev);
            }
            break;
        }
        case kSePlay3D: {
            // anime_SePlay3D (0x2a): arg0 is an EFFECTDEF id, args 1..3 a float xyz
            // offset from the actor, arg4 a flag. 1072 uses across the corpus — this
            // is how every swing, impact and enemy noise is timed, and it is why the
            // sounds come out character-specific for free: each character runs its own
            // .ASO (Yunica's swing issues 139 = player\p1_sw_slash0).
            // The positional offset is parsed but not yet used; the mixer has no
            // panning (see audio.hpp), so the sound plays flat.
            auto af = [&](std::uint32_t i) {
                float v; std::uint32_t raw = std::uint32_t(ai(i));
                std::memcpy(&v, &raw, 4); return v;
            };
            if (sounds_.size() < kMaxSoundsPerTick)
                sounds_.push_back({ai(0), argc > 3 ? af(1) : 0.0f, argc > 3 ? af(2) : 0.0f,
                                   argc > 3 ? af(3) : 0.0f});
            break;
        }
        case kFlagIsEqualInt: {
            // The variant selector. 541 uses — an animation section is a chain of
            // these, each guarding one File+Pattern block.
            const std::int32_t i = ai(0);
            const std::int32_t v =
                (flags_ && i >= 0 && std::size_t(i) < flag_count_) ? flags_[i] : 0;
            r_ = (v == ai(1)) ? 1 : 0;
            break;
        }
        default:
            break;  // unmodelled call: argCount still advances the pc correctly
    }
    pc = next;
    return true;
}

void AsoVm::reset_section_regs() {
    // FUN_004472e0 memsets vm+0xcc..+0x10b (actor +0x9bc..+0x9fb) and sets +0x9c0 = -1: the
    // marker, the block value, Movable (+0x9cc), the capability bytes (+0x9d0..+0x9d8: actable,
    // turnable, durable, the own-jump flag +0x9d6, ...) and locals 0..7. Scripts re-raise what
    // they need (C_010's locomotion sections start with Actable/Movable/Turnable(1)).
    marker_ = 0;
    marker_wait_ = -1;
    for (int n = 0; n < 8; ++n) locals_[n] = 0;   // obj[0x3b..0x42]
    movable_ = 0.0f;
    turnable_ = actable_ = jump_enable_ = false;
    cancelable_ = atk_reserve_ = durable_ = untouchable_ = false;
    own_jump_ = false;
}

void AsoVm::fast_forward_to_marker(std::int32_t m) {
    if (!cur_ || marker() == m) return;
    marker_wait_ = -1;
    const std::int32_t sec = section_;
    const std::size_t end = std::size_t(cur_->code_off) / 4 + std::size_t(cur_->code_len);
    for (int guard = 0; guard < 1001; ++guard) {
        if (pc_ >= end || marker() == m || section_ != sec) break;
        wait_ = 0;
        if (pc_ == std::size_t(cur_->code_off) / 4) reset_section_regs();
        if (!step(pc_)) { if (end_yield_) { end_yield_ = false; break; } }
    }
    wait_ = 0;
}

void AsoVm::tick() {
    foot_smoke_ = false;
    jumped_ = dashed_ = false;
    hitbox_armed_ = false;
    section_ended_ = false;
    sounds_.clear();
    bullets_.clear();
    effects_.clear();
    if (!ok()) return;
    if (wait_ > 0) { --wait_; return; }

    // FUN_004472e0 (0x447470..): a pc at or past the section's length chains to the
    // queued follow-on — [0x2a], or [0x2b] when leaving section 3 (the jump) — via the
    // UNGATED FUN_00446620, then keeps executing the new section in the same tick.
    if (cur_ && pc_ >= std::size_t(cur_->code_off) / 4 + std::size_t(cur_->code_len)) {
        section_ended_ = true;
        const std::int32_t next = section_ == 3 ? queued_jump_ : queued_;
        if (next == section_ || !set_section(next, /*force*/true)) {
            pc_ = std::size_t(cur_->code_off) / 4;   // missing target: restart, never stall
            return;
        }
    }
    if (cur_ && pc_ == std::size_t(cur_->code_off) / 4) reset_section_regs();

    // Run until something yields. The bound is a runaway guard: a section with no
    // Wait on its path would otherwise spin forever.
    for (int guard = 0; guard < 4096; ++guard) {
        const std::size_t before = pc_;
        if (!step(pc_)) {
            if (wait_ > 0) { --wait_; return; }  // consumed by anime_Wait
            if (end_yield_) { end_yield_ = false; return; }   // `end`: chain next tick
            if (pc_ == before) return;           // blocked (marker)
            // Section ran off its end. A section that means to repeat ends with a jmp
            // back to its own loop point, so falling off here means "chain to the queued
            // follow-on" (ASOVM.md §5) — that is what ends a swing after its authored
            // length instead of looping it. Restart in place only if the chain target is
            // missing, so a script can never stall.
            section_ended_ = true;
            if (queued_ == section_ || !set_section(queued_, /*force*/true))
                pc_ = cur_ ? std::size_t(cur_->code_off) / 4 : 0;
            return;
        }
    }
}

}  // namespace cleria
