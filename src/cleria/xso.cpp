#include "cleria/xso.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
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

XsoVm::Step XsoVm::step_one(const Xso& script, std::uint32_t& pc, bool coroutine) {
    const std::vector<std::int32_t>& code = script.words();
    const std::uint32_t n = std::uint32_t(code.size());
    auto word = [&](std::uint32_t i) -> std::uint32_t {
        return (i < n) ? std::uint32_t(code[i]) : 0;
    };
    if (pc >= n) return Step::End;                         // ran off the end -> script end
    const std::uint32_t old = pc;
    const std::uint32_t w = word(old);
    const std::uint32_t cls = w >> 24;
    pc = old + 1;  // provisional; each class fixes it up

        switch (cls) {
            case 0: break;                                     // nop
            case 1: return Step::End;                          // end
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
                // Wait_* ops park the burst — coroutine (tick) mode only; in run-to-
                // completion they fall through to the generic no-op (CUTSCENE_COROUTINE.md §4).
                if (coroutine) {
                    if (sub == 0xf2) {                         // Wait_Time(op0): frame countdown
                        const int f = nargs >= 1 ? op(0) : 0;
                        if (f <= 0) return Step::Continue;     // 0-frame wait is a true nop
                        co_wait = f - 1; return Step::Park;
                    }
                    if (sub == 0xf0) return Step::Park;         // Wait_Interval: park exactly 1 frame
                    switch (sub) {                             // condition waits (rewind + re-test)
                        case 0xf1: case 0xf3: case 0xf4: case 0xf5: case 0xf6: case 0xf7:
                        case 0xf8: case 0xf9: case 0xfd: case 0xfe: case 0xff: case 0x100:
                        case 0x101: case 0x104: case 0x105: case 0x106:
                            if (eval_wait(sub, script, old, argoff, nargs)) return Step::Continue;
                            pc = old; return Step::Park;       // rewind to this op; re-poll next frame
                        default: break;
                    }
                    // StartScript (0xb4) / StartScript2 (0xb8): hand the resolved child NAME
                    // to the manager (CUTSCENE_COROUTINE.md §5). The name is a string-pool
                    // index (corpus-verified: S_1000's StartScript operands index its pool of
                    // child-script names). StartScript2's explicit two-string path is the
                    // concatenation of its two operands. Coroutine-only + gated on the sink,
                    // so run-to-completion never spawns.
                    if (on_start_script) {
                        if (sub == 0xb4 && nargs >= 1) {
                            on_start_script(script.string(op(0))); return Step::Continue;
                        }
                        if (sub == 0xb8 && nargs >= 2) {
                            on_start_script(script.string(op(0)) + script.string(op(1)));
                            return Step::Continue;
                        }
                    }
                    // Fix (0xb6, has a slot arg) / Unfix (0xb7): the ref-counted world freeze.
                    if (sub == 0xb6) { if (on_fix) on_fix(); return Step::Continue; }
                    if (sub == 0xb7) { if (on_unfix) on_unfix(); return Step::Continue; }
                }
                // Dialogue / caption ops forward to the host. Handled here (not in the
                // nargs>=1 store chain below) so 0xd2's zero-arg case isn't skipped. A host
                // that doesn't render dialogue (ActorWorld) inherits the no-op defaults, so
                // this is inert during grant extraction. (CUTSCENE_DIALOGUE.md)
                if (host) {
                    if (sub == 0xd2) { host->window_close(); return Step::Continue; }  // Window_Close
                    if (sub == 0xd3 && nargs >= 4) {          // Window_Msg(speaker,voice,name,text)
                        const int ni = op(2), ti = op(3);
                        host->window_msg(op(0), op(1),
                                         ni >= 0 ? script.string(ni) : std::string(),
                                         ti >= 0 ? script.string(ti) : std::string());
                        return Step::Continue;
                    }
                    if (sub == 0xe2 && nargs >= 1) {          // Caption(name) — pre-rendered art
                        host->caption(script.string(op(0))); return Step::Continue;
                    }
                    if (sub == 0xc3 && nargs >= 4) {          // Scroll_MoveTo(x,y,z,frames,priority)
                        auto f = [&](std::uint32_t i) {
                            float v; const std::uint32_t raw = std::uint32_t(op(i));
                            std::memcpy(&v, &raw, 4); return v;
                        };
                        host->scroll_moveto(f(0), f(1), f(2), op(3)); return Step::Continue;
                    }
                    if (sub == 0xc4 && nargs >= 1) {          // Scroll_Return(frames,priority)
                        host->scroll_return(op(0)); return Step::Continue;
                    }
                    if (sub == 0xcf && nargs >= 4) {          // Shake(ampX,ampY,ampZ,frames)
                        auto f = [&](std::uint32_t i) {
                            float v; const std::uint32_t raw = std::uint32_t(op(i));
                            std::memcpy(&v, &raw, 4); return v;
                        };
                        host->shake(f(0), f(1), f(2), op(3)); return Step::Continue;
                    }
                    // Screen fader ops (float args are bit patterns like scroll/shake).
                    auto fb = [&](std::uint32_t i) {
                        float v; const std::uint32_t raw = std::uint32_t(op(i));
                        std::memcpy(&v, &raw, 4); return v;
                    };
                    if (sub == 0xc8 && nargs >= 1) { host->fader_current(fb(0)); return Step::Continue; }
                    if (sub == 0xc9 && nargs >= 1) { host->fader_target(fb(0));  return Step::Continue; }
                    if (sub == 0xca && nargs >= 2) { host->fader_color(op(0), op(1)); return Step::Continue; }
                    if (sub == 0xca && nargs >= 1) { host->fader_color(op(0), 0xff); return Step::Continue; }
                    if (sub == 0xcc && nargs >= 1) { host->fader_time(op(0));   return Step::Continue; }
                }
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
                } else if (sub == 0x3c && nargs >= 1) {     // Reg_IsEqualInt(i)
                    // The Reg_* family head: compare on acc (EVENTVM.md §5a).
                    acc = (acc == op(0)) ? 1 : 0;
                } else if (sub == 0x80) {
                    // Reg_EntryLabel(s): the StartScript entry-label MARKER (EVENTVM.md).
                    // No state change — recognized explicitly so acc is knowingly intact.
                } else if (sub == 0x8c && nargs >= 1) {     // Flag_IsEquip(itemId)
                    // acc = FUN_00443ce0(itemId) "is item equipped" (EVENTVM_HANDLERS_2.md).
                    acc = (host && host->is_equipped(op(0))) ? 1 : 0;
                } else if (sub == 0x9a && nargs >= 1) {
                    // IsXXX(i): the story-bitset query — 34-case jump table 0x56e778
                    // (EVENTVM_HANDLERS_2.md §4) over the dword @0x76bc80 = FLAG 217
                    // ((0x76bc80-0x76b91c)/4). op0>0x21 -> false. op0 7/8 are the two
                    // character-availability escapes: itemArr[flag152/flag153]==1
                    // (FUN_00420210). op0 9..0x21 map to bits 7..31 (bit = op0-2): the
                    // escapes are spliced INTO the bit sequence. Bit meanings [UNKNOWN],
                    // the query itself is [C-DIS].
                    const std::int32_t o = op(0);
                    if (o < 0 || o > 0x21) {
                        acc = 0;
                    } else if (o == 7 || o == 8) {
                        const std::int32_t idx = flag(o == 7 ? 152 : 153);
                        acc = (idx >= 0 && idx < 54 && item_arr[idx] == 1) ? 1 : 0;
                    } else {
                        acc = (flag(217) >> ((o <= 6) ? o : o - 2)) & 1;
                    }
                } else if (sub == 0xc1) {                   // IsFix()
                    // acc = (fix ctr 0x75ad90 + pause ctr 0x75ad94) != 0 (FUN_0040b2c0).
                    acc = (host && host->is_fix()) ? 1 : 0;
                } else if (sub == 0xc2) {                   // IsUnderWaterWorld()
                    // acc = (*0x76a544 == 2); CleriaCore has no underwater mode yet.
                    acc = (host && host->is_underwater_world()) ? 1 : 0;
                } else if ((sub == 0x139 || sub == 0x13a || sub == 0x13b) && nargs >= 5) {
                    // Chr_IsInSphere / Cylinder / Box: the geometry comes from the
                    // operands (float BIT PATTERNS), only the actor position from the
                    // host (workers FUN_00570c10/c80/cf0 read actor +0x50/54/58 — dumped
                    // 2026-07-23, see OP_13D_REGION.md addendum). Null actor -> false.
                    auto f = [&](std::uint32_t i) {
                        float v; const std::uint32_t raw = std::uint32_t(op(i));
                        std::memcpy(&v, &raw, 4); return v;
                    };
                    float p[3];
                    if (!host || !host->actor_pos(op(0), p)) {
                        acc = 0;
                    } else if (sub == 0x139) {
                        // sphere(center xyz, r): full 3D squared distance vs r².
                        const float dx = p[0] - f(1), dy = p[1] - f(2), dz = p[2] - f(3);
                        const float r = f(4);
                        acc = (dx * dx + dy * dy + dz * dz <= r * r) ? 1 : 0;
                    } else if (sub == 0x13a && nargs >= 6) {
                        // cylinder(center xyz, r, halfH): XZ distance vs r², then
                        // |dy| <= halfH (FUN_0041f170 = fabs, [C-COR] 3 docs).
                        const float dx = p[0] - f(1), dy = p[1] - f(2), dz = p[2] - f(3);
                        const float r = f(4), hh = f(5);
                        acc = (dx * dx + dz * dz <= r * r && std::fabs(dy) <= hh) ? 1 : 0;
                    } else if (sub == 0x13b && nargs >= 7) {
                        // box(corner A xyz, corner B xyz): per-axis min/max normalize
                        // (the worker swaps), then inclusive AABB containment.
                        acc = 1;
                        for (int a3 = 0; a3 < 3; ++a3) {
                            const float lo = std::min(f(1 + a3), f(4 + a3));
                            const float hi = std::max(f(1 + a3), f(4 + a3));
                            if (p[a3] < lo || p[a3] > hi) { acc = 0; break; }
                        }
                    } else {
                        acc = 0;
                    }
                } else if (sub == 0x13e && nargs >= 1) {    // Chr_IsThisChr(id)
                    // acc = (resolve(id) == script-bound actor +0x240), FUN_005706c0.
                    acc = (host && host->chr_is_this(op(0))) ? 1 : 0;
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
                    } else if (sub == 0x12d && host && nargs >= 1) {
                        // Chr_Delete(id): remove the actor (cutscene NPC despawn).
                        host->despawn(op(0));
                    } else if (sub == 0x13f && nargs >= 1) {
                        // "Is entity op0 alive?" CONDITION is [C-DIS]; the result
                        // DESTINATION is [H-high] — ENTITY.md pins the test but not the
                        // write target. Every other query op in this VM writes acc
                        // (0x5F/0x60/0x81/0x90), so acc is the inference, not a read.
                        // A null host answers "not alive", which keeps scripts running.
                        acc = (host && host->is_alive(op(0))) ? 1 : 0;
                    } else if (sub == 0x131 && nargs >= 1) {
                        // Chr_IsSeedLiving(group): the barrier poll (FUN_00570640,
                        // EVENTVM.md §6). acc = any live entity tagged with group op0.
                        // A null host answers "none alive", so a barrier script falls
                        // through to grant its flag (matches the run-to-completion path).
                        acc = (host && host->any_seed_alive(op(0))) ? 1 : 0;
                    } else if (sub == 0x13d && nargs >= 2) {
                        // Chr_IsInEventBox(entityId, boxNameIdx): the region-talk trigger
                        // predicate (OP_13D_REGION.md). op1 is a string-pool index -> the box
                        // name. acc = actor op0 inside that named .SEV box. A null host / an
                        // unresolved box answers false (no trigger).
                        acc = (host && host->chr_in_event_box(op(0), script.string(op(1)))) ? 1 : 0;
                    } else if (sub == 0x11a && host && nargs >= 7) {
                        // Eft_LocatePosAddStr: place a named effect. x/y/z/a/b are float
                        // BIT PATTERNS in the operand words (as 0x12c's are), the name is
                        // a string-pool index. The barrier ward comes through here.
                        auto f = [&](std::uint32_t i) {
                            float v; const std::uint32_t raw = std::uint32_t(op(i));
                            std::memcpy(&v, &raw, 4); return v;
                        };
                        host->locate_effect(op(0), f(1), f(2), f(3), f(4), f(5),
                                            script.string(op(6)));
                    } else if (sub == 0x119 && host && nargs >= 1) {
                        // Eft_Kill(name): remove the named located effect (barrier break).
                        host->kill_effect(script.string(op(0)));
                    }
                    // all other sub-ops: no-op (skipped by length)
                }
                break;
            }
            default: return Step::Unknown;  // unknown class -> stop
        }
    return Step::Continue;
}

int XsoVm::run(const Xso& script, std::uint32_t entry, int max_steps) {
    std::uint32_t pc = entry;
    int steps = 0;
    for (; steps < max_steps; ++steps) {
        const Step s = step_one(script, pc, /*coroutine*/false);
        if (s == Step::End || s == Step::Unknown) return steps;
    }
    return steps;
}

// One frame of a resumable script (CUTSCENE_COROUTINE.md §3): if a wait counter is armed
// count it down, else burst-execute until a Wait_* op parks or the script ends.
XsoVm::Tick XsoVm::tick() {
    if (!co_script) return Tick::Ended;
    if (co_wait > 0) { if (--co_wait > 0) return Tick::Waiting; }   // still counting down
    for (int guard = 0; guard < 2'000'000; ++guard) {              // bound a wait-less loop
        const Step s = step_one(*co_script, co_pc, /*coroutine*/true);
        if (s == Step::Park) return Tick::Waiting;
        if (s == Step::End || s == Step::Unknown) { co_script = nullptr; return Tick::Ended; }
        // Step::Continue -> next op in this frame's burst
    }
    return Tick::Waiting;   // a script with no wait would spin forever — yield instead of hang
}

// Condition-wait predicate: true => the burst proceeds past the op; false => the VM
// rewinds and re-polls next frame. A null host answers true for all (never blocks).
bool XsoVm::eval_wait(std::uint32_t sub, const Xso& script, std::uint32_t old,
                      std::uint32_t argoff, std::uint32_t nargs) {
    const std::vector<std::int32_t>& code = script.words();
    const std::uint32_t n = std::uint32_t(code.size());
    auto op = [&](std::uint32_t i) {
        const std::uint32_t k = old + argoff + i;
        return (k < n) ? std::int32_t(code[k]) : 0;
    };
    switch (sub) {
        case 0xf1: return host ? host->wait_fader() : true;               // fade finished
        case 0xf3: return host ? host->wait_prompt() : true;             // advance pressed
        case 0xf4: return host ? host->wait_script_done(script.string(op(0))) : true;
        case 0xf5: return host ? host->wait_window(true) : true;         // msg window open
        case 0xf6: return host ? host->wait_window(false) : true;        // msg window closed
        case 0xf7: return host ? host->wait_sound(nargs >= 1 ? op(0) : 0) : true;
        case 0xf8: return nargs >= 2 ? (flag(op(0)) == op(1)) : true;    // Wait_FlagEqualInt
        default:   return true;   // chr/mesh-region waits (0xf9/fd/fe/ff/100/101/104/105/106):
                                  // proceed — a stand-in until those subsystems are modelled
    }
}

// ---- ScriptManager: the concurrent GoScript runtime (CUTSCENE_COROUTINE.md §2/§5) -------

namespace {
bool iequal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}
}  // namespace

std::size_t ScriptManager::start_root(std::shared_ptr<const Xso> s, std::string name) {
    Inst in;
    in.script = std::move(s);
    in.name = std::move(name);
    live.push_back(std::move(in));
    const std::size_t idx = live.size() - 1;
    tick_inst(live[idx]);   // immediate first tick this frame (§5a)
    return idx;
}

bool ScriptManager::start(const std::string& name) {
    // Single-instance dedup: the engine refuses a second live spawn of the same name
    // (unless AllowMultiInstance, which no room controller in this corpus sets). §5a.
    for (const Inst& i : live)
        if (!i.finished && iequal(i.name, name)) return false;
    if (!loader) return false;
    std::shared_ptr<const Xso> s = loader->load_script(name);
    if (!s) return false;                 // target doesn't exist -> skip (engine would throw)
    Inst in;
    in.script = std::move(s);
    in.name = name;
    live.push_back(std::move(in));
    tick_inst(live.back());               // child runs its first burst the same frame
    return true;
}

void ScriptManager::tick_inst(Inst& in) {
    if (in.finished || !in.script) return;
    // Load this instance's coroutine registers into the shared scratch VM, run one frame,
    // then save them back. g_flags stay in `vm` (shared across every instance). cur_inst_ lets
    // the Fix/Unfix sinks attribute the ref-count to this script.
    cur_inst_ = &in;
    vm.co_script = in.script.get();
    vm.co_pc = in.pc;
    vm.co_wait = in.wait;
    vm.acc = in.acc;
    vm.locals = std::move(in.locals);
    const XsoVm::Tick t = vm.tick();
    in.pc = vm.co_pc;
    in.wait = vm.co_wait;
    in.acc = vm.acc;
    in.locals = std::move(vm.locals);
    vm.locals.clear();
    cur_inst_ = nullptr;
    if (t == XsoVm::Tick::Ended) in.finished = true;
}

void ScriptManager::tick_all() {
    if (paused) return;                   // global freeze: no countdown, no burst (§3)
    // Tick the scripts live at the start of the frame; children queued this frame are
    // spawned+ticked in the drain below (so a child spawned this frame still runs now, §5a).
    const std::size_t n = live.size();
    for (std::size_t i = 0; i < n; ++i) tick_inst(live[i]);
    // Drain StartScript spawns. start() ticks each child once, which may queue more.
    while (!spawn_queue.empty()) {
        std::vector<std::string> batch;
        batch.swap(spawn_queue);
        for (const std::string& nm : batch) start(nm);
    }
    // Record how many GoScripts were resident together this frame (before the reap drops
    // the ones that just finished) — the concurrency the manager actually sustained.
    peak_live = std::max(peak_live, live.size());
    // Reap finished scripts (the driver skips them; a separate pass removes them, §5d). A
    // script that ended still holding a Fix is auto-released on teardown (the engine's
    // FUN_0056eab0 unfix path), so the world doesn't stay frozen forever.
    for (Inst& i : live)
        if (i.finished && i.fix_held > 0) { fix_count -= i.fix_held; i.fix_held = 0; }
    if (fix_count < 0) fix_count = 0;
    live.erase(std::remove_if(live.begin(), live.end(),
                              [](const Inst& i) { return i.finished; }),
               live.end());
}

bool ScriptManager::any_live() const {
    for (const Inst& i : live)
        if (!i.finished) return true;
    return false;
}

}  // namespace cleria
