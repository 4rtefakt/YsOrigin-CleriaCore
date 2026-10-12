#include "cleria/aso.hpp"
#include "cleria/vfs.hpp"   // mods: asset opens go through vfs::resolve

#include <cstdio>
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
    kSMode = 0x21,         // anime_SMode=f  — the Boost gauge add while boosting (Toal's skills)
    kGenerateCollision = 0x23,  // anime_GenerateCollision — arms the melee hit-volume
    kGenerateLocus = 0x24,      // anime_GenerateLocus=iiiiiiffii — a GoLocusSword sweep (FUN_00464960)
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
    // The rest of the Flag_ family (FUN_004472e0 0x448468..0x4486d8 [BYTES]); F[i] is
    // AsoVm::flag_get / flag_put, cmp is FUN_00570240.
    kFlagIsEqualFlag = 0x60,   // R = (F[a0] == F[a1])
    kFlagCmpInt = 0x61,        // R = cmp(F[a0], a1, a2)
    kFlagCmpFlag = 0x62,       // R = cmp(F[a0], a1, F[a2])
    kFlagCmpR = 0x63,          // R = cmp(F[a0], a1, R)
    kFlagSetInt = 0x64,        // F[a0] = a1
    kFlagCopy = 0x65,          // F[a0] = F[a1]
    kFlagSetR = 0x66,          // F[a0] = R
    kFlagAddInt = 0x67,        // F[a0] += a1
    kFlagAddFlag = 0x68,       // F[a0] += F[a1]
};

}  // namespace

// FUN_00570240(a, op, b) [EXPORT]: the Flag_Cmp* comparator. 0 false, 1 <, 2 ==, 3 <=, 4 >,
// 5 !=, 6 >=, anything else true (a = the flag, b = the operand).
static bool flag_compare(std::int32_t a, std::int32_t op, std::int32_t b) {
    switch (op) {
        case 0: return false;
        case 1: return a < b;
        case 2: return a == b;
        case 3: return a <= b;
        case 4: return a > b;
        case 5: return a != b;
        case 6: return a >= b;
        default: return true;
    }
}

// The table's 269 names, in its own order [BYTES 0x68a800 + 4n, every pointer followed]. Of note: 6 / 7 / 8 =
// C_102 / C_103 / C_104 (Yunica's, Hugo's, Toal's event sheets), 11 = C_114 (Yunica's attacks), 12 = C_115 (Toal's
// demon form), 13 = C_101, 14..17 = C_110..C_113 (the gameplay sheets of Adol, Yunica, Hugo, Toal).
const char* aso_aia_name(std::int32_t id) {
    struct Names {
        char n[269][6];
        Names() {
            static const std::int16_t c6[12] = {102, 103, 104, 105, 106, 114, 115, 101, 110, 111, 112, 113};
            static const std::int16_t c88[6] = {120, 121, 122, 131, 132, 133};
            int i = 0;
            auto put = [&](char c, int v) { std::snprintf(n[i++], 6, "%c_%03d", c, v); };
            for (int v = 0; v <= 50; v += 10) put('C', v);          //   0..5
            for (int v : c6) put('C', v);                          //   6..17
            for (int v = 100; v <= 790; v += 10) put('M', v);       //  18..87
            for (int v : c88) put('C', v);                         //  88..93
            for (int v = 200; v <= 790; v += 10) put('C', v);       //  94..153
            for (int v = 200; v <= 900; v += 10) put('F', v);       // 154..224
            for (int v = 800; v <= 990; v += 10) put('C', v);       // 225..244
            for (int v = 800; v <= 990; v += 10) put('M', v);       // 245..264
            put('M', 861); put('C', 119); put('C', 981); put('C', 982);   // 265..268
        }
    };
    static const Names names;
    return id >= 0 && id < 269 ? names.n[id] : "";
}

Aso Aso::parse(const std::vector<std::uint8_t>& b) {
    Aso a;
    if (b.size() < 0x10 || std::memcmp(b.data(), "XSR\0", 4) != 0) return a;
    const std::uint32_t n_sec = rd32(&b[0x08]);
    a.default_file_ = std::int32_t(rd32(&b[0x0C]));
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
    std::ifstream f(vfs::resolve(p), std::ios::binary);
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
    if (file_out_) *file_out_ = file_id_ = aso_->default_file();   // FUN_00446620: sprite[0x23] = header +0xc
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
            if (file_out_) *file_out_ = file_id_;
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
            // anime_Raise [EXPORT 004472e0.c:345-357]: when the section exists, the ungated FUN_00446620 and
            // the register reset FUN_00445a70 (vm+0xcc..+0x10b, marker wait -1), and the new section runs on in
            // the same tick; a missing section is skipped. (It used to yield here, which the tick took for the
            // section's end: the raised section never ran. Users: C_990's Ignis wind-ups, M_230's 303.)
            pc = next;
            if (set_section(ai(0), /*force*/true)) reset_section_regs();
            return true;
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
            if (is_land_) foot_smoke_ = foot_smoke_bit_ = true;
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
        case kSMode: {                                 // case 0x21: *(float*)args
            float v; const std::uint32_t raw = std::uint32_t(ai(0)); std::memcpy(&v, &raw, 4);
            smode_ += v;
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
        case 0x14: atk_break_ = (ai(0) != 0); break;       // anime_AttackBreak: byte vm+0xe5 [BYTES 0x4476fb]
        case 0x0f: {                                       // anime_CameraDistanceOffset: vm+0xd8 = the raw float
            const std::uint32_t raw = std::uint32_t(ai(0));
            std::memcpy(&depth_offset_, &raw, 4);
            break;
        }
        case 0x1f: {                                       // anime_ChangeSpecular [BYTES 0x447887..0x4478e7]
            auto af = [&](std::uint32_t i) {
                float v; std::uint32_t raw = std::uint32_t(ai(i));
                std::memcpy(&v, &raw, 4); return v;
            };
            auto rgb = [&](std::uint32_t i) {              // cvttss2si each, then << 16 | << 8 | (no mask)
                return (std::uint32_t(int(af(i))) << 16) | (std::uint32_t(int(af(i + 1))) << 8) | std::uint32_t(int(af(i + 2)));
            };
            if (argc >= 8) {
                specular_.b = rgb(0); specular_.a = rgb(3);
                specular_.step = af(6); specular_.count = ai(7);
                specular_set_ = true;
            }
            break;
        }
        // anime_GenerateCollision2 (0x28) is INERT in the engine: its kind-10 object only counts a delay and a
        // life down, nothing reads it (ASO_NOOPS.md §4 [BYTES 0x447a78..0x447b8c]; no RNG draw). Nothing to do.
        case 0x28: break;
        case 0x26:                                         // anime_GenerateDebris [BYTES 0x447980..0x447a39]
            if (argc >= 7) {
                debris_.count = ai(0);
                debris_.spin = ai(1) != 0; debris_.keep = ai(2) != 0; debris_.fixed = ai(3) != 0;
                debris_.bag.clear();
                for (std::uint32_t k = 7; k < argc; ++k) debris_.bag.push_back(ai(k));
                debris_set_ = true;
            }
            break;
        // FUN_004472e0 case 0x16 writes BOTH +0x9d0 (actable) and +0x9d1 (the jump-enable byte that
        // case 0x19 writes alone): Actable(1) also allows the jump (the mover's gate 0x46520c).
        case kActable:       actable_ = jump_enable_ = (ai(0) != 0); break;
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
        case kGenerateLocus: {
            // FUN_00464960 reads args[0..9] as dwords: [6],[7] as floats (0x464a4a/0x464a54), the
            // rest as ints (build/ability_re/LOCUS.md).
            if (loci_.size() < kMaxBulletsPerTick && argc >= 10) {
                LocusEvent ev;
                for (std::uint32_t k = 0; k < 10; ++k) ev.i[k] = ai(k);
                std::uint32_t raw = std::uint32_t(ev.i[6]); std::memcpy(&ev.bonus, &raw, 4);
                raw = std::uint32_t(ev.i[7]); std::memcpy(&ev.knock, &raw, 4);
                loci_.push_back(ev);
            }
            break;
        }
        case 0x09:                                         // anime_Special (FUN_00449ed0): surfaced raw
            if (specials_.size() < kMaxBulletsPerTick) {
                SpecialEvent ev;
                for (std::uint32_t k = 0; k < 8; ++k) ev.a[k] = ai(k);
                specials_.push_back(ev);
            }
            break;
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
        case kFlagIsEqualInt:
            // The variant selector. 541 uses — an animation section is a chain of
            // these, each guarding one File+Pattern block.
            r_ = (flag_get(ai(0)) == ai(1)) ? 1 : 0;
            break;
        case kFlagIsEqualFlag: r_ = (flag_get(ai(0)) == flag_get(ai(1))) ? 1 : 0; break;
        case kFlagCmpInt:      r_ = flag_compare(flag_get(ai(0)), ai(1), ai(2)) ? 1 : 0; break;
        case kFlagCmpFlag:     r_ = flag_compare(flag_get(ai(0)), ai(1), flag_get(ai(2))) ? 1 : 0; break;
        case kFlagCmpR:        r_ = flag_compare(flag_get(ai(0)), ai(1), r_) ? 1 : 0; break;
        case kFlagSetInt:      flag_put(ai(0), ai(1)); break;
        case kFlagCopy:        flag_put(ai(0), flag_get(ai(1))); break;
        case kFlagSetR:        flag_put(ai(0), r_); break;
        case kFlagAddInt:      flag_put(ai(0), flag_get(ai(0)) + ai(1)); break;
        case kFlagAddFlag:     flag_put(ai(0), flag_get(ai(0)) + flag_get(ai(1))); break;
        // Flag_IsEquip(i) (0x8c, FUN_00443ce0) / Flag_SetMagicHolder() (0x8e: flag 208 = FUN_0046d560, the
        // bracelet's slot) [EXPORT 004472e0.c:1390-1406]. Only M_900.ASO calls them (Hugo's eyes' shot).
        case 0x8c:             r_ = (equip_fn && equip_fn(ai(0))) ? 1 : 0; break;
        case 0x8e:             if (magic_slot_fn) flag_put(208, magic_slot_fn()); break;
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
    depth_offset_ = 0.0f;                         // vm+0xd8
    for (int n = 0; n < 8; ++n) locals_[n] = 0;   // obj[0x3b..0x42]
    movable_ = 0.0f;
    turnable_ = actable_ = jump_enable_ = false;
    cancelable_ = atk_reserve_ = durable_ = untouchable_ = false;
    atk_break_ = false;                           // vm+0xe5, inside the zeroed range
    own_jump_ = false;
}

std::int32_t AsoVm::flag_get(std::int32_t i) const {
    if (i >= 0 && i < 0x200) return (flags_ && std::size_t(i) < flag_count_) ? flags_[i] : 0;
    const std::int32_t n = i - 0x200;
    return (n >= 0 && n < 32) ? locals_[n] : 0;
}

void AsoVm::flag_put(std::int32_t i, std::int32_t v) {
    if (i >= 0 && i < 0x200) {
        if (wflags_ && std::size_t(i) < flag_count_) wflags_[i] = v;
        return;
    }
    const std::int32_t n = i - 0x200;
    if (n >= 0 && n < 32) locals_[n] = v;
}

void AsoVm::fast_forward_to_marker(std::int32_t m) {
    if (!cur_ || marker() == m) return;
    marker_wait_ = -1;
    const std::int32_t sec = section_;
    const std::size_t end = std::size_t(cur_->code_off) / 4 + std::size_t(cur_->code_len);
    // The jump section's falling hold is a 128-pass counter loop (Flag 512, 8 ops a pass), so the
    // skip runs well past 1000 ops before it reaches Marker(3); the engine's has no cap. This bound
    // is only a runaway guard.
    for (int guard = 0; guard < (1 << 16); ++guard) {
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
    smode_ = 0;
    hitbox_armed_ = false;
    section_ended_ = false;
    sounds_.clear();
    specular_set_ = false;
    debris_set_ = false;
    bullets_.clear();
    effects_.clear();
    loci_.clear();
    specials_.clear();
    if (!ok()) return;
    // The wait gate `[0x2e] == 0 || --[0x2e] == 0` [EXPORT 004472e0.c:118]: a negative count never reaches 0
    // in a session, so anime_Wait(-1) holds its frame for good (the last frame of the player's death
    // section 5, issue #77) instead of running on to the section's end and chaining to idle.
    if (wait_ < 0) return;
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
            if (wait_ < 0) return;               // anime_Wait(-1): held for good (the gate above)
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
