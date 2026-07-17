#include "cleria/xso.hpp"

#include <cstring>
#include <stdexcept>

namespace cleria {
namespace {

// Per-class instruction length in words (class 2 is computed). Matches the game
// interpreter FUN_004472e0 and tools/xso_dis.py _FIXED_LEN.
int fixed_len(std::uint32_t cls) {
    switch (cls) {
        case 0: case 1: case 0xC: case 0xD: return 1;
        case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 0xA:
        case 0xB: case 0xE: case 0xF: case 0x10: case 0x11: case 0x12: case 0x13:
            return 2;
        default: return -1;  // unknown
    }
}

const char* store_name(std::uint32_t sub) {
    switch (sub) {
        case 0x64: return "set";
        case 0x65: return "copy";
        case 0x66: return "set_reg";
        case 0x67: return "add";
        case 0x69: return "sub";
        case 0x6B: return "mul";
        case 0x71: return "and";
        case 0x73: return "or";
        case 0x75: return "xor";
        case 0x77: return "zero";
        default: return nullptr;
    }
}

}  // namespace

Xso Xso::parse(std::span<const std::uint8_t> b) {
    if (b.size() < 0x24 || std::memcmp(b.data(), "XSR\0", 4) != 0)
        throw std::runtime_error("XSO: not an XSR script");
    std::uint32_t words;
    std::memcpy(&words, b.data() + 0x1C, 4);
    const std::size_t end = 0x24 + std::size_t(words) * 4;
    if (end > b.size())
        throw std::runtime_error("XSO: code region exceeds file");
    Xso x;
    x.code_.resize(words);
    std::memcpy(x.code_.data(), b.data() + 0x24, std::size_t(words) * 4);

    // String pool: count @ header 0x20, then `count` u32 offsets after the code,
    // then a NUL-terminated pool. Offsets are relative to the pool base.
    std::uint32_t count;
    std::memcpy(&count, b.data() + 0x20, 4);
    const std::size_t off_table = end;                 // right after the code
    const std::size_t pool = off_table + std::size_t(count) * 4;
    if (count <= 4096 && pool <= b.size()) {
        x.strings_.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t so;
            std::memcpy(&so, b.data() + off_table + std::size_t(i) * 4, 4);
            const std::size_t at = pool + so;
            std::size_t n = 0;
            while (at + n < b.size() && b[at + n] != 0) ++n;
            x.strings_.emplace_back(reinterpret_cast<const char*>(b.data() + at), n);
        }
    }
    return x;
}

std::vector<XsoInstr> Xso::disasm() const {
    std::vector<XsoInstr> out;
    std::uint32_t pc = 0;
    const std::uint32_t n = std::uint32_t(code_.size());
    while (pc < n) {
        const std::uint32_t w = std::uint32_t(code_[pc]);
        XsoInstr in;
        in.pc = pc; in.word = w; in.cls = w >> 24;
        if (in.cls == 2) {
            const std::uint32_t argoff = 1 + ((w >> 8) & 0xF);
            const std::uint32_t nargs = w & 0xFF;
            in.sub = (w >> 12) & 0xFFF;
            in.length = argoff + nargs;
            for (std::uint32_t i = 0; i < nargs; ++i)
                if (pc + argoff + i < n) in.ops.push_back(code_[pc + argoff + i]);
        } else {
            const int fl = fixed_len(in.cls);
            if (fl < 0) { in.unknown = true; in.length = 1; out.push_back(in); return out; }
            in.length = std::uint32_t(fl);
        }
        out.push_back(in);
        pc += in.length;
    }
    return out;
}

std::vector<Grant> Xso::grants() const {
    std::vector<Grant> out;
    for (const XsoInstr& in : disasm()) {
        const char* op = (in.cls == 2) ? store_name(in.sub) : nullptr;
        if (!op || in.ops.empty()) continue;
        const std::int32_t idx = in.ops[0];
        if (idx < 0 || idx >= 0x200) continue;  // g_flags only
        Grant g;
        g.op = op;
        g.index = idx;
        const bool imm = (in.sub == 0x64 || in.sub == 0x67 || in.sub == 0x69 || in.sub == 0x6B);
        if (in.sub == 0x77) { g.value = 0; g.has_value = true; }
        else if (imm && in.ops.size() > 1) { g.value = in.ops[1]; g.has_value = true; }
        else { g.has_value = false; }
        out.push_back(std::move(g));
    }
    return out;
}

int XsoVm::run(const Xso& script, std::uint32_t entry, int max_steps) {
    const std::vector<std::int32_t>& code = script.words();
    const std::uint32_t n = std::uint32_t(code.size());
    auto word = [&](std::uint32_t i) -> std::uint32_t {
        return (i < n) ? std::uint32_t(code[i]) : 0;
    };

    std::uint32_t pc = entry;
    int steps = 0;
    for (; steps < max_steps && pc < n; ++steps) {
        const std::uint32_t old = pc;
        const std::uint32_t w = word(old);
        const std::uint32_t cls = w >> 24;
        pc = old + 1;  // provisional; each class fixes it up

        switch (cls) {
            case 0: break;                                     // nop
            case 1: return steps;                              // end
            case 3: case 0xB: pc = word(old + 1); break;       // jump
            case 4: pc = old + 2; break;                       // 2-word nop
            // conditional jumps on the accumulator (target at old+1):
            case 5:  pc = (acc <  0) ? word(old + 1) : old + 2; break;
            case 6:  pc = (acc == 0) ? word(old + 1) : old + 2; break;
            case 7:  pc = (acc <= 0) ? word(old + 1) : old + 2; break;
            case 8:  pc = (acc >  0) ? word(old + 1) : old + 2; break;
            case 9:  pc = (acc != 0) ? word(old + 1) : old + 2; break;
            case 0xA: pc = (acc >= 0) ? word(old + 1) : old + 2; break;
            case 0xC: case 0xD: acc = 0; break;                // acc = 0
            case 0xE: acc = std::int32_t(word(old + 1)); pc = old + 2; break;
            case 0xF: acc += std::int32_t(word(old + 1)); pc = old + 2; break;
            case 0x10: acc -= std::int32_t(word(old + 1)); pc = old + 2; break;
            case 0x11: acc &= std::int32_t(word(old + 1)); pc = old + 2; break;
            case 0x12: acc |= std::int32_t(word(old + 1)); pc = old + 2; break;
            case 0x13: acc ^= std::int32_t(word(old + 1)); pc = old + 2; break;
            case 2: {
                const std::uint32_t argoff = 1 + ((w >> 8) & 0xF);
                const std::uint32_t nargs = w & 0xFF;
                const std::uint32_t sub = (w >> 12) & 0xFFF;
                auto op = [&](std::uint32_t i) { return std::int32_t(word(old + argoff + i)); };
                pc = old + argoff + nargs;
                if (sub == 0x5F && nargs >= 2) {            // acc = (flag[op0]==op1)
                    acc = (flag(op(0)) == op(1)) ? 1 : 0;
                } else if (sub == 0x60 && nargs >= 2) {     // acc = (flag[op0]==flag[op1])
                    acc = (flag(op(0)) == flag(op(1))) ? 1 : 0;
                } else if (sub == 0x5C && nargs >= 4) {     // two fused equality tests, AND
                    acc = (flag(op(0)) == op(1) && flag(op(2)) == op(3)) ? 1 : 0;
                } else if (sub == 0x5D && nargs >= 4) {     // two fused equality tests, OR
                    acc = (flag(op(0)) == op(1) || flag(op(2)) == op(3)) ? 1 : 0;
                } else if ((sub == 0x61 || sub == 0x62 || sub == 0x63) && nargs >= 2) {
                    // The comparison family. op1 selects the operator, and the encoding
                    // is exactly (jump class - 4), mirroring the class 5..0xA jumps.
                    // 0x63's right-hand side is the OLD accumulator, so it must be read
                    // before acc is assigned.
                    const std::int32_t a = flag(op(0));
                    const std::int32_t b = (sub == 0x61) ? op(2)
                                         : (sub == 0x62) ? flag(op(2))
                                                         : acc;
                    acc = compare(a, op(1), b);
                } else if (sub == 0x90 && nargs >= 2) {     // bit test
                    acc = ((flag(op(0)) & op(1)) != 0) ? 1 : 0;
                } else if (sub == 0x81 && nargs >= 1) {     // percentage roll: rand%100 < op0
                    acc = (int(rng_next() >> 17) % 100 < op(0)) ? 1 : 0;
                } else if (nargs >= 1) {
                    const std::int32_t i0 = op(0);
                    const char* sn = store_name(sub);
                    if (sn && i0 >= 0) {
                        switch (sub) {
                            case 0x64: flag(i0) = op(1); break;
                            case 0x65: flag(i0) = flag(op(1)); break;
                            case 0x66: flag(i0) = acc; break;
                            case 0x67: flag(i0) += op(1); break;
                            case 0x69: flag(i0) -= op(1); break;
                            case 0x6B: flag(i0) *= op(1); break;
                            case 0x71: flag(i0) &= op(1); break;
                            case 0x73: flag(i0) |= op(1); break;
                            case 0x75: flag(i0) ^= op(1); break;
                            case 0x77: flag(i0) = 0; break;
                        }
                        if (i0 < 0x200) {
                            Grant g; g.op = sn; g.index = i0;
                            g.value = flag(i0); g.has_value = true;
                            grants.push_back(std::move(g));
                        }
                    } else if (sub == 0x116) {   // give-item(op0)
                        items.push_back(i0);
                    } else if (sub == 0xD5 && nargs >= 3) {  // "Acquired X" box, item = op2
                        acquires.push_back(op(2));
                    } else if (sub == 0x169) {   // enable/show object (name = string op0)
                        obj_enable.push_back(script.string(op(0)));
                    } else if (sub == 0x175) {   // set object to opened state (name op0)
                        obj_opened.push_back(script.string(op(0)));
                    } else if ((sub == 0x16B || sub == 0x16C) && nargs >= 2) {
                        // Object visibility: (string_pool_index, 0|1). Verified in
                        // S_SAVEOBJECTCHANGE.XSO, whose pool is [save_01, save_02,
                        // save_03, eft_heal]: it shows exactly one save statue and
                        // hides the other two (all three are placed at one spot).
                        obj_visible[script.string(op(0))] = (op(1) != 0);
                    } else if (sub == 0x12c && host && nargs >= 7) {
                        // The SPAWN op. Operand layout [C-DIS][C-COR] over 1198 sites:
                        //   (entity_id, cast_type, x, y, z, yaw_deg, group)
                        // x/y/z/yaw are float BIT PATTERNS in the operand words —
                        // reinterpret, do not convert (op5's 0x43340000 is 180.0f,
                        // not the integer 1127481344).
                        auto f = [&](std::uint32_t i) {
                            float v;
                            const std::uint32_t raw = std::uint32_t(op(i));
                            std::memcpy(&v, &raw, 4);
                            return v;
                        };
                        host->spawn(op(0), op(1), f(2), f(3), f(4), f(5), op(6));
                    } else if (sub == 0xa0) {
                        // The bulk SCT spawn (FUN_004341a0). The operand is the SCT
                        // basename; the host is pre-loaded with the room's placements,
                        // so we ignore it and let the host spawn them. Runs once, before
                        // any 0x12c. A null host makes this inert (existing tests).
                        if (host) host->spawn_placements();
                    } else if (sub == 0x13f && nargs >= 1) {
                        // "Is entity op0 alive?" CONDITION is [C-DIS]; the result
                        // DESTINATION is [H-high] — ENTITY.md pins the test but not the
                        // write target. Every other query op in this VM writes acc
                        // (0x5F/0x60/0x81/0x90), so acc is the inference, not a read.
                        // A null host answers "not alive", which keeps scripts running.
                        acc = (host && host->is_alive(op(0))) ? 1 : 0;
                    }
                    // all other sub-ops: no-op (skipped by length)
                }
                break;
            }
            default: return steps;  // unknown class -> stop
        }
    }
    return steps;
}

}  // namespace cleria
