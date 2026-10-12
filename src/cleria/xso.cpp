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
        // Flag_Clr is 0x7a ([EXPORT 00566350 case 0x7a]: *flag = 0). 0x77 is Flag_AndFlagShl
        // (flag[o0] &= FUN_004459d0(flag[o1], o2)); it was long listed here as "zero". Neither
        // has a call site in the shipped corpus, so the correction changes no script's run.
        case 0x7a: return "zero";
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
        if (in.sub == 0x7a) { g.value = 0; g.has_value = true; }
        else if (imm && in.ops.size() > 1) { g.value = in.ops[1]; g.has_value = true; }
        else { g.has_value = false; }
        out.push_back(std::move(g));
    }
    return out;
}

// ---- typed operand reads (audit event_vm #11) -------------------------------------------
// The descriptor words between the op word and its operands hold a 2-bit type per operand:
// type(i) = (desc[i >> 3] >> ((i & 7) * 4)) & 3, 0 int / 1 float / 2 string. Each accessor
// returns the literal only for its own type (else 0 / NULL). A string operand starting with
// '$' names this script's variable block: $I0/$I1 (+0x244/+0x248), $F0/$F1 (+0x24c/+0x250),
// $S0/$S1 (+0x258/+0x278); the int accessor converts $F and the float accessor $I.
namespace {
// "$Xn" -> kind X and slot n (0/1), else false.
bool var_ref(const std::string& s, char* kind, int* slot) {
    if (s.size() != 3 || s[0] != '$' || (s[2] != '0' && s[2] != '1')) return false;
    *kind = s[1]; *slot = s[2] - '0';
    return true;
}
}  // namespace

std::uint32_t XsoVm::op_type(const Xso& sc, std::uint32_t old, std::uint32_t i) const {
    const std::vector<std::int32_t>& c = sc.words();
    if (old >= c.size()) return 0;
    const std::uint32_t argoff = 1 + ((std::uint32_t(c[old]) >> 8) & 0xF);
    const std::uint32_t d = old + 1 + (i >> 3);
    if (d >= old + argoff || d >= c.size()) return 0;
    return (std::uint32_t(c[d]) >> ((i & 7) * 4)) & 3;
}

namespace {
std::int32_t raw_op(const Xso& sc, std::uint32_t old, std::uint32_t i) {
    const std::vector<std::int32_t>& c = sc.words();
    if (old >= c.size()) return 0;
    const std::uint32_t k = old + 1 + ((std::uint32_t(c[old]) >> 8) & 0xF) + i;
    return k < c.size() ? c[k] : 0;
}
}  // namespace

std::int32_t XsoVm::op_int(const Xso& sc, std::uint32_t old, std::uint32_t i) const {
    const std::uint32_t t = op_type(sc, old, i);
    const std::int32_t r = raw_op(sc, old, i);
    if (t == 0) return r;
    char k; int n;
    if (t == 2 && var_ref(sc.string(r), &k, &n)) {
        if (k == 'I') return vars.i[n];
        if (k == 'F') return std::int32_t(vars.f[n]);              // cvttss2si
    }
    return 0;
}

float XsoVm::op_float(const Xso& sc, std::uint32_t old, std::uint32_t i) const {
    const std::uint32_t t = op_type(sc, old, i);
    const std::int32_t r = raw_op(sc, old, i);
    if (t == 1) { float v; std::memcpy(&v, &r, 4); return v; }
    char k; int n;
    if (t == 2 && var_ref(sc.string(r), &k, &n)) {
        if (k == 'F') return vars.f[n];
        if (k == 'I') return float(vars.i[n]);
    }
    return 0.0f;
}

std::string XsoVm::op_str(const Xso& sc, std::uint32_t old, std::uint32_t i) const {
    if (op_type(sc, old, i) != 2) return std::string();             // NULL
    std::string str = sc.string(raw_op(sc, old, i));
    char k; int n;
    if (var_ref(str, &k, &n) && k == 'S') return vars.s[n];
    return str;
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
                // Typed reads: `op` = the int accessor, `opf` float, `ops` string (+ $-vars).
                auto op = [&](std::uint32_t i) { return op_int(script, old, i); };
                auto opf = [&](std::uint32_t i) { return op_float(script, old, i); };
                auto ops = [&](std::uint32_t i) { return op_str(script, old, i); };
                pc = old + argoff + nargs;
                // Wait_* ops park the burst — coroutine (tick) mode only; in run-to-
                // completion they fall through to the generic no-op (CUTSCENE_COROUTINE.md §4).
                if (coroutine) {
                    if (sub == 0xf2) {                         // Wait_Time(op0): frame countdown
                        // 0x56de74: +0x1e8 = op0 - 1, clamped at 0; it always yields, so
                        // Wait_Time(N) waits max(1, N - 1) frames (audit event_vm #14).
                        const int f = nargs >= 1 ? op(0) : 0;
                        co_wait = f - 1 > 0 ? f - 1 : 0; return Step::Park;
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
                    // The handler 0x569716..0x56983a (CHR_OPS.md §10) [BYTES]: operand 1 (after
                    // the name), if non-zero, is an ACTOR id that becomes the child's owner +0x240
                    // (998 = the parent's own owner); ints at 2..3 -> $I0/$I1, strings at 4..6 ->
                    // $S0..(two kept), floats at 7..9 -> $F0.. (two kept). No operand = owner 0.
                    auto marshal = [&](std::uint32_t first, int* owner_id) {
                        Vars v;
                        *owner_id = (first < nargs) ? op(first) : 0;
                        for (std::uint32_t k = 0; k < 2; ++k)
                            if (first + 1 + k < nargs) v.i[k] = op(first + 1 + k);
                        for (std::uint32_t k = 0; k < 2; ++k)
                            if (first + 3 + k < nargs) v.s[k] = ops(first + 3 + k);
                        for (std::uint32_t k = 0; k < 2; ++k)
                            if (first + 6 + k < nargs) v.f[k] = opf(first + 6 + k);
                        return v;
                    };
                    if (on_start_script) {
                        int oid = 0;
                        if (sub == 0xb4 && nargs >= 1) {
                            const Vars v = marshal(1, &oid);
                            on_start_script(ops(0), v, oid); return Step::Continue;
                        }
                        if (sub == 0xb8 && nargs >= 2) {
                            const Vars v = marshal(2, &oid);
                            on_start_script(ops(0) + ops(1), v, oid);
                            return Step::Continue;
                        }
                    }
                    // Fix(slot) (0xb6) / Unfix (0xb7): one fix per script (FUN_0056e8d0).
                    if (sub == 0xb6) { if (on_fix) on_fix(nargs >= 1 ? op(0) : 0); return Step::Continue; }
                    if (sub == 0xb7) { if (on_unfix) on_unfix(); return Step::Continue; }
                    // Window_Msg / Window_Menu / Window_Treasure fix slot 0 when this script
                    // holds no fix (audit event_vm #27).
                    if ((sub == 0xd3 || sub == 0xd4 || sub == 0xd5 || sub == 0xd7) && on_window_fix) on_window_fix();
                }
                // 0xb5 AllowMultiInstance(i) [EXPORT 00566350 case 0xb5]: GoScript+0x238 = (i != 0).
                if (sub == 0xb5) {
                    if (on_allow_multi && nargs >= 1) on_allow_multi(op(0) != 0);
                    return Step::Continue;
                }
                // Dialogue / caption ops forward to the host. Handled here (not in the
                // nargs>=1 store chain below) so 0xd2's zero-arg case isn't skipped. A host
                // that doesn't render dialogue (ActorWorld) inherits the no-op defaults, so
                // this is inert during grant extraction. (CUTSCENE_DIALOGUE.md)
                if (host) {
                    if (sub == 0xd2) { host->window_close(); return Step::Continue; }  // Window_Close
                    if (sub == 0xd4 && nargs >= 3) {          // Window_Menu(x, y, title, (text, label)...)
                        std::vector<std::string> texts, labels;
                        for (int k = 3; k + 1 < nargs; k += 2) { texts.push_back(ops(k)); labels.push_back(ops(k + 1)); }
                        host->window_menu(opf(0), opf(1), ops(2), texts, labels);
                        return Step::Continue;
                    }
                    if (sub == 0xd6 && nargs >= 1) { host->set_msg_speed(op(0)); return Step::Continue; }
                    if (sub == 0xd7) { host->menu_init(); return Step::Continue; }
                    if (sub == 0xd8 && nargs >= 2) {
                        std::string text = ops(0);
                        if (const int bi = shop_filter ? blessing_index(ops(1)) : -1; bi >= 0) {   // mods: a [Done] row
                            ShopQuery q;
                            q.kind = ShopQuery::Kind::Row; q.sub = 0xd8; q.index = bi; q.value = -1; q.text = text;
                            q.script = script_name;
                            const GrantDecision d = shop_filter(q);
                            if (d.action == GrantDecision::Action::Replace) text = d.text;
                        }
                        host->menu_add(text, ops(1));
                        return Step::Continue;
                    }
                    if (sub == 0xd9) { host->menu_select(nargs >= 1 ? ops(0) : std::string()); return Step::Continue; }
                    if (sub == 0xda && nargs >= 1) { host->menu_assign_cancel(ops(0)); return Step::Continue; }
                    if (sub == 0xdb) { host->menu_cursor_last(); return Step::Continue; }
                    if (sub == 0xdc && nargs >= 1) { host->menu_rename_last(ops(0)); return Step::Continue; }
                    // 0xdd Menu_AddSpShop(text, label, price) [BYTES 0x56a149..0x56a1c9]: the record
                    // text is op0 with sprintf("%d", op2) appended (fmt 0x69f63c), then Menu_Add.
                    if (sub == 0xdd && nargs >= 3) {
                        std::int32_t price = op(2);
                        std::string text;
                        if (const int bi = shop_filter ? blessing_index(ops(1)) : -1; bi >= 0) {   // mods: the row
                            price = shop_value(ShopQuery::Kind::Price, 0xdd, bi, price);
                            text = ops(0) + std::to_string(price);
                            ShopQuery q;
                            q.kind = ShopQuery::Kind::Row; q.sub = 0xdd; q.index = bi; q.value = price; q.text = text;
                            q.script = script_name;
                            const GrantDecision d = shop_filter(q);
                            if (d.action == GrantDecision::Action::Replace) text = d.text;
                        } else {
                            text = ops(0) + std::to_string(price);
                        }
                        host->menu_add(text, ops(1));
                        return Step::Continue;
                    }
                    if (sub == 0xe0) { host->save_menu_enter(); return Step::Continue; }
                    if (sub == 0x8b && nargs >= 1) { host->event_xx(op(0)); return Step::Continue; }
                    if (sub == 0x17e && nargs >= 3) { host->mesh_drop_item(ops(0), op(1), op(2)); return Step::Continue; }
                    if (sub == 0x130 && nargs >= 2) { host->chr_attach_script(op(0), ops(1)); return Step::Continue; }
                    if (sub == 0x12f && nargs >= 4) { host->chr_locate_dummy(op(0), opf(1), opf(2), opf(3)); return Step::Continue; }
                    if (sub == 0x141 && nargs >= 2) { host->chr_jump(op(0), opf(1)); return Step::Continue; }
                    if (sub == 0x148 && nargs >= 3) { host->chr_emotion(op(0), op(1), opf(2)); return Step::Continue; }
                    if (sub == 0x153 && nargs >= 2) { host->chr_shadow(op(0), op(1) != 0); return Step::Continue; }
                    if (sub == 0x18a && nargs >= 3) { host->mesh_fall(ops(0), opf(1), opf(2)); return Step::Continue; }
                    if (sub == 0x16e && nargs >= 3) { host->mesh_alpha_target(ops(0), opf(1), op(2)); return Step::Continue; }
                    if (sub == 0xa4 && nargs >= 1) { host->bgm_default(op(0)); return Step::Continue; }
                    if (sub == 0xa2 && nargs >= 2) { host->scene_clip(opf(0), opf(1)); return Step::Continue; }
                    if (sub == 0xb9 && nargs >= 1) { host->bgm_play(op(0)); return Step::Continue; }
                    if (sub == 0xba) { host->bgm_stop(nargs >= 1 ? op(0) : 0); return Step::Continue; }
                    if (sub == 0x85) { host->heal_hp(); return Step::Continue; }
                    // 0x86 GiveO2(): the oxygen gauge 0x76c1b8 = 6000.0 [DOC EVENTVM_HANDLERS_2.md, 0x56880a].
                    if (sub == 0x86) { host->give_o2(); return Step::Continue; }
                    if (sub == 0x14e && nargs >= 2) { host->chr_muteking(op(0), op(1)); return Step::Continue; }
                    if (sub == 0x96) { host->save_cont_data(); return Step::Continue; }
                    if (sub == 0x160 && nargs >= 11) {        // (id, lv, 9 floats)
                        float f[9];
                        for (std::uint32_t k = 0; k < 9; ++k) f[k] = opf(2 + k);
                        host->chr_set_status(op(0), op(1), f);
                        return Step::Continue;
                    }
                    if (sub == 0xbc && nargs >= 2) { host->se_play_vol(op(0), op(1)); return Step::Continue; }
                    if (sub == 0xbc && nargs >= 1) { host->se_play(op(0)); return Step::Continue; }
                    if (sub == 0xc0 && nargs >= 3) { host->se_volume(op(0), op(1), op(2)); return Step::Continue; }
                    if (sub == 0xbb && nargs >= 2) { host->bgm_volume(op(0), op(1)); return Step::Continue; }
                    // 0xbf Se_Stop(id, ms) [BYTES 0x569ae7 -> FUN_0043a1c0(ecx = op0, edx = op1)].
                    if (sub == 0xbf && nargs >= 2) { host->se_stop(op(0), op(1)); return Step::Continue; }
                    // 0x10a GlareLevel(i) [BYTES 0x56a85a: byte 0x76c1c5 = op0].
                    if (sub == 0x10a && nargs >= 1) { host->glare_level(op(0) & 0xff); return Step::Continue; }
                    // 0x158 Chr_ZTest(id, on) [BYTES FUN_00571790: +0x294 bit 0x400 = (op1 != 0)].
                    if (sub == 0x158 && nargs >= 2) { host->chr_ztest(op(0), op(1) != 0); return Step::Continue; }
                    // 0x118 Eft_LocateChr(eff, id, f dist, f deg, f dy, f scale, mode, name)
                    // [BYTES 0x56b112: FUN_0056f600(op0, op1, op3, op4, op5, op6, name), xmm3 = op2].
                    if (sub == 0x118 && nargs >= 8) {
                        host->locate_effect_chr(op(0), op(1), opf(2), opf(3), opf(4), opf(5), op(6), ops(7));
                        return Step::Continue;
                    }
                    // 0x11c Eft_SetAlphaTarget(name, f alpha, frames) [BYTES 0x56b24e: FUN_0056fba0].
                    if (sub == 0x11c && nargs >= 3) { host->effect_alpha_target(ops(0), opf(1), op(2)); return Step::Continue; }
                    if (sub == 0x8f && nargs >= 1) { host->set_skill(op(0)); return Step::Continue; }
                    // ---- the floor 1..3 completion pass (build/event_re/FLOOR13_OPS.md) ----
                    // 0x15c Chr_Damage(id, f atk, b, c) [EXPORT case 0x15c: op0 int, op1 float, op2
                    // int (!= 0), op3 int -> FUN_00571b80]. @Touch_Toge passes "$I0" as the float.
                    if (sub == 0x15c && nargs >= 4) { host->chr_damage(op(0), opf(1), op(2) != 0, op(3)); return Step::Continue; }
                    // 0x14d Chr_CollisionVanish(id, on) [case 0x14d -> FUN_005712a0].
                    if (sub == 0x14d && nargs >= 2) { host->chr_collision_vanish(op(0), op(1) != 0); return Step::Continue; }
                    // 0x165 Chr_Stop(id, i) [case 0x165 -> FUN_00571e00; op1 is fetched, unused].
                    if (sub == 0x165 && nargs >= 1) { host->chr_stop(op(0)); return Step::Continue; }
                    // 0x163 Chr_NoForce / 0x164 Chr_AniDisable(id, on) [cases 0x163 / 0x164 ->
                    // FUN_00571d60 / FUN_00571db0: +0x130 bit 0x10000 / 0x80000 = (on != 0)].
                    if (sub == 0x163 && nargs >= 2) { host->chr_no_force(op(0), op(1) != 0); return Step::Continue; }
                    if (sub == 0x161 && nargs >= 2) { host->chr_slant_fire(op(0), opf(1)); return Step::Continue; }
                    if (sub == 0x164 && nargs >= 2) { host->chr_ani_disable(op(0), op(1) != 0); return Step::Continue; }
                    // 0xd0 PadTilt(i) [case 0xd0 -> FUN_005d3500(op0)].
                    if (sub == 0xd0 && nargs >= 1) { host->pad_tilt(op(0)); return Step::Continue; }
                    // 0xbd Se_Play3DPos(se, x, y, z) [case 0xbd]: operand 1 is read as a POINTER to
                    // three floats (thunk_FUN_00566250), the same shape as 0x112's operand 0.
                    // [not modelled] a non-float operand 1 (the engine then reads its scratch float).
                    if (sub == 0xbd && nargs >= 2) {
                        float x = opf(1), y = 0.0f, z = 0.0f;
                        if (op_type(script, old, 1) == 1) {
                            const std::int32_t wy = raw_op(script, old, 2), wz = raw_op(script, old, 3);
                            std::memcpy(&y, &wy, 4);
                            std::memcpy(&z, &wz, 4);
                        }
                        host->se_play_pos(op(0), x, y, z);
                        return Step::Continue;
                    }
                    // 0x15e ChrAllInEventBox_SetForce(box, vec, frames, grounded) [case 0x15e: op0
                    // string, op1 a float POINTER (fx, fy, fz), op4 int, op5 int -> FUN_00571ef0].
                    // 0x15f ChrAll_SetPointForce(point, strength, frames, rmin, rmax, grounded) [case
                    // 0x15f: op0 a float pointer (x, y, z), op3 float (XMM2), op4 int, op5 / op6 float,
                    // op7 int -> FUN_005720b0].
                    auto fvec = [&](std::uint32_t i, float v[3]) {
                        v[0] = opf(i); v[1] = v[2] = 0.0f;           // [not modelled] a non-float i
                        if (op_type(script, old, i) == 1) {
                            const std::int32_t w1 = raw_op(script, old, i + 1), w2 = raw_op(script, old, i + 2);
                            std::memcpy(&v[1], &w1, 4);
                            std::memcpy(&v[2], &w2, 4);
                        }
                    };
                    if (sub == 0x15e && nargs >= 6) {
                        float v[3]; fvec(1, v);
                        host->force_in_box(ops(0), v[0], v[1], v[2], op(4), op(5) != 0);
                        return Step::Continue;
                    }
                    if (sub == 0x15f && nargs >= 8) {
                        float v[3]; fvec(0, v);
                        host->force_to_point(v[0], v[1], v[2], opf(3), op(4), opf(5), opf(6), op(7) != 0);
                        return Step::Continue;
                    }
                    // 0xbe Se_Play3DChr(se, id) [case 0xbe -> FUN_0043a150].
                    if (sub == 0xbe && nargs >= 2) { host->se_play_chr(op(0), op(1)); return Step::Continue; }
                    // 0x11f Eft_SetAlphaCurrent(name, f) [case 0x11f -> FUN_0056fc70].
                    if (sub == 0x11f && nargs >= 2) { host->effect_alpha_current(ops(0), opf(1)); return Step::Continue; }
                    // 0x16f Mesh_SetAlphaCurrent(name, f) [case 0x16f -> FUN_00572ba0].
                    if (sub == 0x16f && nargs >= 2) { host->mesh_alpha_current(ops(0), opf(1)); return Step::Continue; }
                    // 0x17c Mesh_AttachEffect(mesh, frame, eff) [case 0x17c -> FUN_00572bd0].
                    if (sub == 0x17c && nargs >= 3) { host->mesh_attach_effect(ops(0), ops(1), op(2)); return Step::Continue; }
                    // 0x95 SetTimeScale(f) [case 0x95: DAT_006d6078 = f].
                    if (sub == 0x95 && nargs >= 1) { host->set_time_scale(opf(0)); return Step::Continue; }
                    // 0x7f SetWeaponLevel(a, b, c, d) [BYTES 0x568582..0x5685ee].
                    if (sub == 0x7f && nargs >= 4) {
                        for (std::uint32_t k = 0; k < 4; ++k) host->weapon_level(int(k), op(k));
                        return Step::Continue;
                    }
                    // 0x82 SetLevel(n) [BYTES 0x5684cf..0x5684ef] / 0x89(i, v) [BYTES 0x5688f1..0x56892d].
                    if (sub == 0x82 && nargs >= 1) { host->set_level(op(0)); return Step::Continue; }
                    if (sub == 0x89 && nargs >= 2) { host->equip_slot(op(0), op(1)); return Step::Continue; }
                    // 0x18E Arena(cast, level, count) [BYTES 0x56a915..0x56a972 -> FUN_005722c0], 0x10D DebugArena(cast)
                    // (FUN_00572770: the same spawner, level 0), 0x18F ArenaSave() [BYTES 0x56a8d1..0x56a910].
                    if (sub == 0x18e && nargs >= 3) { host->arena(op(0), op(1), op(2)); return Step::Continue; }
                    if (sub == 0x10d && nargs >= 1) { host->arena(op(0), 0, 0); return Step::Continue; }
                    if (sub == 0x18f) { host->arena_save(); return Step::Continue; }
                    // 0xb2 AddPlayerSP(i) [case 0xb2 -> FUN_004fa290]: the SP wallet 0x76a75c += i,
                    // clamped to 0..999999 (and mirrored into the player's +0xc4).
                    if (sub == 0xb2 && nargs >= 1) {
                        const long long v = (long long)host->gold() + op(0);
                        host->set_gold(int(std::clamp(v, 0LL, 999999LL)));
                        return Step::Continue;
                    }
                    if (sub == 0xd3 && nargs >= 4) {          // Window_Msg(speaker,voice,name,text)
                        host->window_msg(op(0), op(1), ops(2), ops(3));   // type-0 -1 -> ""
                        return Step::Continue;
                    }
                    if (sub == 0xe2 && nargs >= 1) {          // Caption(name) — pre-rendered art
                        host->caption(ops(0)); return Step::Continue;
                    }
                    // Standing bust portraits: name is a string index, x/y are float bit patterns.
                    if ((sub == 0xe3 || sub == 0xe5) && nargs >= 3) {
                        auto f = [&](std::uint32_t i) { return opf(i); };
                        if (sub == 0xe3) host->portrait_open(ops(0), f(1), f(2));
                        else host->portrait_move(ops(0), f(1), f(2), nargs >= 4 ? op(3) : 0, nargs >= 5 ? op(4) : 1);
                        return Step::Continue;
                    }
                    if (sub == 0xe6 && nargs >= 5) {
                        const std::uint32_t c[4] = {std::uint32_t(op(1)), std::uint32_t(op(2)), std::uint32_t(op(3)), std::uint32_t(op(4))};
                        host->portrait_color(ops(0), c); return Step::Continue;
                    }
                    if (sub == 0xe7 && nargs >= 2) { host->portrait_priority(ops(0), op(1)); return Step::Continue; }
                    if (sub == 0xe8 && nargs >= 2) { host->portrait_fade_speed(ops(0), opf(1)); return Step::Continue; }
                    if (sub == 0xe4 && nargs >= 1) {          // Portrait_Close(name)
                        host->portrait_close(ops(0)); return Step::Continue;
                    }
                    if (sub == 0xc3 && nargs >= 4) {          // Scroll_MoveTo(x,y,z,frames,priority)
                        auto f = [&](std::uint32_t i) { return opf(i); };
                        host->scroll_moveto(f(0), f(1), f(2), op(3)); return Step::Continue;
                    }
                    if (sub == 0xc4 && nargs >= 1) {          // Scroll_Return(frames,priority)
                        host->scroll_return(op(0)); return Step::Continue;
                    }
                    if (sub == 0xeb && nargs >= 1) { host->camera_copy_set(op(0)); return Step::Continue; }
                    if (sub == 0xec && nargs >= 5) {          // Camera_Change(dist, yaw, pitch, fov, frames)
                        host->camera_change(opf(0), opf(1), opf(2), opf(3), op(4)); return Step::Continue;
                    }
                    if (sub == 0xcf && nargs >= 4) {          // Shake(ampX,ampY,ampZ,frames)
                        auto f = [&](std::uint32_t i) { return opf(i); };
                        host->shake(f(0), f(1), f(2), op(3)); return Step::Continue;
                    }
                    if (sub == 0xce && nargs >= 2) { host->zoom_blur(opf(0), opf(1)); return Step::Continue; }   // ZoomBlur(s, r)
                    // Screen fader ops (float args are bit patterns like scroll/shake).
                    auto fb = [&](std::uint32_t i) { return opf(i); };
                    if (sub == 0xc8 && nargs >= 1) { host->fader_current(fb(0)); return Step::Continue; }
                    if (sub == 0xc9 && nargs >= 1) { host->fader_target(fb(0));  return Step::Continue; }
                    if (sub == 0xca && nargs >= 2) { host->fader_color(op(0), op(1)); return Step::Continue; }
                    if (sub == 0xca && nargs >= 1) { host->fader_color(op(0), 0xff); return Step::Continue; }
                    if (sub == 0xcc && nargs >= 1) { host->fader_time(op(0));   return Step::Continue; }
                    if (sub == 0xc5 && nargs >= 1) { host->camera_set(op(0), nargs >= 2 ? op(1) : 0); return Step::Continue; }
                    // Scene flow (SCENEFLOW.md). ChangeScene does NOT end the burst: the caller
                    // runs on to its next yield before the freeze.
                    if (sub == 0x97 && nargs >= 1) { host->change_scene(ops(0)); return Step::Continue; }
                    if (sub == 0xcd && nargs >= 2) {
                        host->crossfade(opf(0), opf(1));
                        if (nargs >= 3 && !ops(2).empty()) host->change_scene(ops(2));
                        return Step::Continue;
                    }
                    if (sub == 0x94 && nargs >= 3) { host->heal_spot(opf(0), opf(1), opf(2)); return Step::Continue; }
                    if (sub == 0xa7 && nargs >= 1) { host->warp_enable(op(0)); return Step::Continue; }
                    if (sub == 0x92 && nargs >= 1) { host->manual_enable(op(0)); return Step::Continue; }
                    if (sub == 0x93 && nargs >= 1) { host->topic_known(op(0)); return Step::Continue; }
                    if (sub == 0xa6) { host->go_warp_point(); return Step::Continue; }
                    // Scripted map objects (WAITOPS.md §4). 0x175 / 0x173 also fall through to
                    // the load-time bookkeeping below.
                    if ((sub == 0x170 || sub == 0x171) && nargs >= 6) {
                        host->mesh_move(ops(0), opf(1), opf(2), opf(3), opf(4), opf(5), sub == 0x171);
                        return Step::Continue;
                    }
                    if (sub == 0x16d && nargs >= 2) { host->mesh_stop_motion(ops(0), op(1)); return Step::Continue; }
                    if (sub == 0x16a && nargs >= 2) { host->mesh_sleep(ops(0), op(1) != 0); return Step::Continue; }
                    if (sub == 0x178 && nargs >= 1) { host->mesh_reset_position(ops(0)); return Step::Continue; }
                    // 0x174 Mesh_SetRotation(name, deg x3) / 0x179 Mesh_ResetRotation(name) [BYTES FUN_00572f10 /
                    // FUN_005730b0]; 0x12a Eft_Die(id, i) [BYTES 0x56b8b3].
                    if (sub == 0x174 && nargs >= 4) { host->mesh_set_rotation(ops(0), opf(1), opf(2), opf(3)); return Step::Continue; }
                    if (sub == 0x179 && nargs >= 1) { host->mesh_reset_rotation(ops(0)); return Step::Continue; }
                    if (sub == 0x12a && nargs >= 1) { host->eft_die(op(0)); return Step::Continue; }
                    // 0x129 ChangeFog / 0x17a Mesh_IsMotionFinish / 0x182 Mesh_SetMaterialColor (ZAVA.md §2): none yields.
                    if (sub == 0x129 && nargs >= 2) { host->change_fog(op(0), opf(1)); return Step::Continue; }
                    if (sub == 0x17a && nargs >= 2) {
                        acc = host->mesh_is_motion_finish(ops(0), std::uint32_t(op(1))) ? 1 : 0;
                        return Step::Continue;
                    }
                    if (sub == 0x182 && nargs >= 5) {
                        host->mesh_material_color(ops(0), ops(1), op(2), op(3), op(4));
                        return Step::Continue;
                    }
                    // 0xaa Light_Directional(i argb, f x, f y, f z) [BYTES 0x5695c9 -> FUN_00443ea0] / 0x172
                    // Mesh_Rotate(s mesh, f x, f y, f z, i frames) [BYTES 0x56d179 -> FUN_00572e70] (DALLES.md §2):
                    // neither yields.
                    if (sub == 0xaa && nargs >= 4) {
                        host->light_directional(std::uint32_t(op(0)), opf(1), opf(2), opf(3));
                        return Step::Continue;
                    }
                    if (sub == 0x172 && nargs >= 5) {
                        host->mesh_rotate(ops(0), opf(1), opf(2), opf(3), op(4));
                        return Step::Continue;
                    }
                    // 0x11b EftAll_AddCameraOffset(i, f) [BYTES 0x56b3cf]; 0x126 MirrorFloor(i) [BYTES 0x56b812];
                    // 0x127 SetDefaultCubeMapTexture(s) [BYTES 0x56b832].
                    if (sub == 0x11b && nargs >= 2) { host->effect_camera_offset(op(0), opf(1)); return Step::Continue; }
                    if (sub == 0x126 && nargs >= 1) { host->mirror_floor(op(0) != 0); return Step::Continue; }
                    // 0x11d Scene_SetWaterPlane(f) [BYTES 0x56b411..0x56b42b -> FUN_00432e60].
                    if (sub == 0x11d && nargs >= 1) { host->water_plane(opf(0)); return Step::Continue; }
                    if (sub == 0x127 && nargs >= 1) { host->cube_map(ops(0)); return Step::Continue; }
                    if (sub == 0x176 && nargs >= 2) { host->mesh_enable_event(ops(0), op(1) != 0); return Step::Continue; }
                    if (sub == 0x17b && nargs >= 2) { host->mesh_time_scale(ops(0), opf(1)); return Step::Continue; }
                    if (sub == 0x175 && nargs >= 2)
                        host->mesh_set_motion(ops(0), ops(1), nargs >= 3 && op(2) != 0, nargs >= 4 ? op(3) : 0);
                    if (sub == 0x173 && nargs >= 4) host->mesh_set_position(ops(0), opf(1), opf(2), opf(3));
                    if (sub == 0xc6) { host->camera_return(nargs >= 1 ? op(0) : 0); return Step::Continue; }
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
                    std::int32_t b = (sub == 0x61) ? op(2)
                                   : (sub == 0x62) ? flag(op(2))
                                                   : acc;
                    // CleriaCore mods (ShopQuery): @GrowNN's "enough SP?" test 0x61 (216, 6, price).
                    if (shop_filter && sub == 0x61 && op(0) == 216)
                        if (const int bi = blessing_index(script_name); bi >= 0)
                            b = shop_value(ShopQuery::Kind::Price, 0x61, bi, b);
                    acc = compare(a, op(1), b);
                } else if (sub == 0x90 && nargs >= 2) {     // bit test
                    acc = ((flag(op(0)) & op(1)) != 0) ? 1 : 0;
                } else if (sub == 0x81 && nargs >= 1) {     // percentage roll: rand%100 < op0
                    // idiv 100 then an UNSIGNED compare (audit event_vm #21).
                    acc = ((rng_next() >> 17) % 100u < std::uint32_t(op(0))) ? 1 : 0;
                } else if (sub == 0x3c && nargs >= 1) {     // Reg_IsEqualInt(i)
                    // The Reg_* family head: compare on acc (EVENTVM.md §5a).
                    acc = (acc == op(0)) ? 1 : 0;
                } else if (sub == 0x40 && nargs >= 1) {     // Reg_SetInt(i)
                    // [EXPORT 00566350 case 0x40]: acc = the int operand. @Break_* loads "$I0" /
                    // "$I1" (the SOB drop ids) with it before Reg_IsEqualInt(0) guards the roll.
                    acc = op(0);
                } else if (sub == 0x80) {
                    // Reg_EntryLabel(s) [EXPORT case 0x80]: acc = __stricmp(s, 0x76a474), the label
                    // of the door the player came in by (XsoHost::entry_label). S_3015's root picks
                    // its hint events with it (S_3015A / S_3015B). It was long read as a no-op.
                    const std::string a = ops(0), b = host ? host->entry_label() : std::string();
                    acc = 0;
                    for (std::size_t k = 0;; ++k) {
                        const int ca = k < a.size() ? std::tolower((unsigned char)a[k]) : 0;
                        const int cb = k < b.size() ? std::tolower((unsigned char)b[k]) : 0;
                        if (ca != cb || ca == 0) { acc = ca - cb; break; }
                    }
                } else if (sub == 0x9b || sub == 0x9c || sub == 0x9d || sub == 0x9e || sub == 0x9f ||
                           sub == 0xa1 || sub == 0xac || sub == 0xad || sub == 0xae) {
                    // Recognized, no VM-side state. 0x9b..0xa1 Scene_LoadStatic* (ROOMLOAD.md §2):
                    // load_room reads the room's own .SOB/.SEF/.SFO/.SCM/.SEN/.SEV/.SNF itself (504 of
                    // the 505 floor 1..3 sites name the room; [not modelled] S_3099's Hard/Nightmare
                    // pick of S_3099_1.SOB). 0xac Se_Load / 0xad Cast_Load / 0xae Portrait_Load are
                    // preloads; CleriaCore loads SEs, casts and portraits on first use.
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
                    // CleriaCore mods (ShopQuery): the shop's own scripts ask "already applied?".
                    if (shop_filter && script_name.size() >= 5) {
                        std::string sn = script_name.substr(0, 5);
                        for (char& ch : sn) ch = char(std::tolower((unsigned char)ch));
                        if (sn == "@grow") acc = shop_value(ShopQuery::Kind::Owned, 0x9a, o, acc) != 0 ? 1 : 0;
                    }
                } else if (sub == 0x6a && nargs >= 2) {     // Flag_SubFlag: flag[op0] -= flag[op1]
                    // [BYTES 0x567e4c..0x567e97] (the GROWAR / GROWLE price deduction).
                    const std::int32_t i0 = op(0);
                    if (i0 == 216 && blessing_index(script_name) >= 0) shop_paid = flag(op(1));
                    flag(i0) -= flag(op(1));
                    if (i0 >= 0 && i0 < 0x200) grants.push_back({"sub", i0, flag(i0), true});
                } else if (sub == 0x84 && nargs >= 1) {     // SetGoldflagNowZenny(i) [BYTES 0x56876d]
                    // 0: the live wallet 0x76a75c -> flag 216 (0x76bc7c); else flag 216 -> the wallet.
                    if (op(0) == 0) {
                        flag(216) = host ? host->gold() : 0;
                        grants.push_back({"set", 216, flag(216), true});
                    } else if (host) {
                        host->set_gold(flag(216));
                    }
                } else if (sub == 0xaf && nargs >= 1) {     // SetXXX(idx): the blessing grant
                    // Jump table 0x56e6f0 (BLESSING_SHOP.md §2): idx 0..6 set bit idx of flag 217
                    // (0x76bc80), 7 / 8 FUN_00420200(flag 152 / 153, 1) (the armor / boots Raval),
                    // 9..33 bit idx - 2. [not modelled] idx > 0x21 throws in the engine.
                    std::int32_t o = op(0);
                    bool apply = true;
                    if (shop_filter) {                      // CleriaCore mods (ShopQuery), not retail
                        ShopQuery q;
                        q.kind = ShopQuery::Kind::Buy; q.sub = 0xaf; q.index = o; q.value = shop_paid;
                        q.script = script_name;
                        const GrantDecision d = shop_filter(q);
                        if (d.action == GrantDecision::Action::Suppress) apply = false;
                        else if (d.action == GrantDecision::Action::Replace) o = d.index;
                    }
                    shop_paid = 0;
                    if (!apply) {
                    } else if (o == 7 || o == 8) {
                        const std::int32_t id = flag(o == 7 ? 152 : 153);
                        if (id >= 0 && id < 54) {
                            item_arr[id] = 1;
                            if (host) host->item_arr_set(id, 1);
                        }
                    } else if (o >= 0 && o <= 0x21) {
                        flag(217) |= std::int32_t(1u << (o <= 6 ? o : o - 2));
                        grants.push_back({"or", 217, flag(217), true});
                    }
                } else if (sub == 0xb0 && nargs >= 1) {     // SetRavalCostToFlag(i) [BYTES 0x5691fd]
                    // The equipped armor (op0 0: flag 152, id - 6) or boots (flag 153, id - 0x1e) picks a
                    // bucket from the byte LUT 0x56e818 / 0x56e848 (0..5 repeating), and the bucket's
                    // jump target (0x56e800 / 0x56e830) stores its cost into flag 218 (0x76bc84).
                    // [not modelled] an id outside the 24-entry range throws in the engine.
                    static const std::int32_t kCost[6] = {100, 300, 1000, 3000, 6000, 12000};
                    const std::int32_t k = op(0) == 0 ? flag(152) - 6 : flag(153) - 0x1e;
                    if (k >= 0 && k <= 0x17) {
                        flag(218) = kCost[k % 6];
                        // CleriaCore mods (ShopQuery): @GrowAr / @GrowLe's price, which their 0x62 compare and
                        // 0x6A deduction then read from flag 218.
                        if (const int bi = shop_filter ? blessing_index(script_name) : -1; bi == 7 || bi == 8)
                            flag(218) = shop_value(ShopQuery::Kind::Price, 0xb0, bi, flag(218));
                        grants.push_back({"set", 218, flag(218), true});
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
                    auto f = [&](std::uint32_t i) { return opf(i); };
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
                    // acc = (resolve(id) == the script's bound actor +0x240); 999 is the
                    // player (13), 998 the bound actor itself.
                    const int id = op(0);
                    const int r = id == 999 ? 13 : id == 998 ? owner : id;
                    acc = (owner != 0 && r == owner) ? 1 : 0;
                } else if (nargs >= 1) {
                    const std::int32_t i0 = op(0);
                    const char* sn = store_name(sub);
                    if (sn && i0 >= 0) {
                        const std::int32_t before = (grant_filter && i0 < 0x200) ? flag(i0) : 0;
                        switch (sub) {
                            case 0x64: flag(i0) = op(1); break;
                            case 0x65: flag(i0) = flag(op(1)); break;
                            case 0x66: flag(i0) = acc; break;
                            case 0x67: flag(i0) += op(1); break;
                            case 0x69: {
                                std::int32_t v = op(1);
                                // CleriaCore mods (ShopQuery): @GrowNN's deduction 0x69 (216, price).
                                if (const int bi = i0 == 216 ? blessing_index(script_name) : -1; bi >= 0) {
                                    v = shop_value(ShopQuery::Kind::Price, 0x69, bi, v);
                                    shop_paid = v;
                                }
                                flag(i0) -= v;
                                break;
                            }
                            case 0x6B: flag(i0) *= op(1); break;
                            case 0x71: flag(i0) &= op(1); break;
                            case 0x73: flag(i0) |= op(1); break;
                            case 0x75: flag(i0) ^= op(1); break;
                            case 0x7a: flag(i0) = 0; break;
                        }
                        int gi = i0;   // the cell the grant is recorded on
                        if (i0 < 0x200 && grant_filter) {   // CleriaCore mods (GrantQuery), not retail
                            GrantQuery q = filter_query(GrantQuery::Kind::Store, int(sub), i0);
                            q.old_value = before;
                            q.value = flag(i0);
                            const GrantDecision d = grant_filter(q);
                            if (d.action != GrantDecision::Action::Pass) {
                                flag(i0) = before;
                                gi = -1;
                                if (d.action == GrantDecision::Action::Replace && d.index >= 0 && d.index < 0x200) {
                                    flag(d.index) = d.value;
                                    gi = d.index;
                                }
                            }
                        }
                        if (gi >= 0 && gi < 0x200) {
                            Grant g; g.op = sn; g.index = gi;
                            g.value = flag(gi); g.has_value = true;
                            grants.push_back(std::move(g));
                        }
                    } else if (sub == 0x116) {
                        // Effect_GetItem(icon, flag, mesh): an item-icon sprite above the named mesh
                        // (FUN_00573210), visual only; the grant is the script's own Flag_SetInt
                        // (audit event_vm #28).
                        int item = i0;
                        bool show = true;
                        if (grant_filter) {                 // CleriaCore mods (GrantQuery), not retail
                            const GrantDecision d = grant_filter(filter_query(GrantQuery::Kind::Icon, int(sub), i0));
                            if (d.action == GrantDecision::Action::Suppress) show = false;
                            else if (d.action == GrantDecision::Action::Replace) item = d.index;
                        }
                        if (show) {
                            items.push_back(item);
                            if (host) host->effect_get_item(item, nargs >= 2 ? op(1) : 0, nargs >= 3 ? ops(2) : std::string());
                        }
                    } else if (sub == 0xD5 && nargs >= 3) {  // Window_Treasure(x,y,item,s)
                        int item = op(2);
                        std::string text = nargs >= 4 ? ops(3) : std::string();
                        bool show = true;
                        if (grant_filter) {                 // CleriaCore mods (GrantQuery), not retail
                            GrantQuery q = filter_query(GrantQuery::Kind::Window, int(sub), item);
                            q.text = text;
                            const GrantDecision d = grant_filter(q);
                            if (d.action == GrantDecision::Action::Suppress) show = false;
                            else if (d.action == GrantDecision::Action::Replace) { item = d.index; text = d.text; }
                        }
                        if (show) {
                            acquires.push_back(item);
                            if (host) host->acquire_item(opf(0), opf(1), item, text);
                        }
                    } else if (sub == 0x169) {   // Mesh_Delete(name): a destroy request
                        obj_deleted.push_back(ops(0));
                        obj_enable.push_back(ops(0));   // deprecated alias
                        // Live too (FUN_00572a30 destroys the object): it stops drawing and colliding
                        // now, as a load-time delete already does (the room's root_hidden). Without
                        // this a ward deleted by a StepMove script stayed solid until a reload.
                        if (host) { host->mesh_visible(ops(0), false); host->mesh_collision(ops(0), false); }
                    } else if (sub == 0x175) {
                        // Obj_SetState(objName, stateName, ?, ?) — BOTH operands are
                        // string-pool indices, and the second is a YMO ANIMATION CLIP name.
                        // Long read as "set to opened" because every early site was a chest
                        // (`0x175(box, OPEN, 0, -1)`), but S_1004's root uses the same op for
                        // its stepping stones and switch: `0x175(step00, UP)`, `(step00, STOP)`,
                        // `(switch01, TURN_ON)`, `(switch01, OFF)` — pool entries 10..13. So it
                        // is a general "pose this object to this named clip".
                        obj_opened.push_back(ops(0));
                        if (nargs >= 2) obj_state[ops(0)] = ops(1);
                    } else if (sub == 0x188 && nargs >= 4) {
                        // Mesh_DistanceToAlpha(a, b, f div, f k) (FUN_00573aa0): a's alpha from
                        // its frame height below b. Dispatch [BYTES 0x56d99a..0x56da14]: op2 ->
                        // xmm3 (the divisor), op3 -> the stack float. S_1004's STEPSHADOWUPDATE
                        // fades each step_NN_shadow (a, d7 0.999) by its stepNN's lift (b).
                        if (host) host->mesh_distance_alpha(ops(0), ops(1), opf(2), opf(3));
                    } else if (sub == 0x189 && nargs >= 5) {
                        // Mesh_SyncronizePos(a, b, x, y, z) (FUN_00573b80; [BYTES 0x56da24..
                        // 0x56daab] ops in order): a takes b's position on the nonzero axes and
                        // b's +0x298 & 0x60 (hidden / no collision) bits.
                        obj_sync[ops(0)] = {ops(1), op(2) != 0, op(3) != 0, op(4) != 0};
                        if (host) host->mesh_sync_pos(ops(0), ops(1), op(2) != 0, op(3) != 0, op(4) != 0);
                    } else if (sub == 0x173 && nargs >= 4) {
                        // Obj_SetPos(objName, x, y, z) — float BIT PATTERNS, as 0x12c's are.
                        // S_1004 lays its stepping stones out per character with this (Toal
                        // gets 7 stones, Yunica/Hugo 4 in different places), so without it the
                        // room shows whatever the SOB authored rather than the live layout.
                        auto f = [&](std::uint32_t i) { return opf(i); };
                        obj_place[ops(0)] = {f(1), f(2), f(3)};
                    } else if (sub == 0x186 && nargs >= 5) {
                        // Mesh_SetSphereCollision(name, r, ox, oy, oz): a hittable child sphere.
                        if (host) host->mesh_sphere_collision(ops(0), opf(1), opf(2), opf(3), opf(4));
                    } else if (sub == 0x187 && nargs >= 8) {
                        // Mesh_Locate(ymo, name, x, y, z, rx, ry, rz, i): recorded for the load
                        // pass, and spawned live through the host.
                        ObjLocateArgs a{ops(0), ops(1), opf(2), opf(3), opf(4), opf(5), opf(6), opf(7)};
                        obj_locate.push_back({a.ymo, a.name, a.x, a.y, a.z, a.rx, a.ry, a.rz});
                        if (host) host->mesh_locate(a);
                    } else if (sub == 0x16B && nargs >= 2) {
                        // Mesh_SetVisible(name, on) (FUN_00572aa0: bit 0x20 = hidden).
                        // S_SAVEOBJECTCHANGE shows one of three statues stacked at one spot.
                        obj_visible[ops(0)] = (op(1) != 0);
                        if (host) host->mesh_visible(ops(0), op(1) != 0);
                    } else if (sub == 0x16C && nargs >= 2) {
                        // Mesh_EnableCollision(name, on) (FUN_00572ae0: bit 0x40 = no collision).
                        obj_collide[ops(0)] = (op(1) != 0);
                        if (host) host->mesh_collision(ops(0), op(1) != 0);
                    } else if (sub == 0x12c && host && nargs >= 7) {
                        // The SPAWN op. Operand layout [C-DIS][C-COR] over 1198 sites:
                        //   (entity_id, cast_type, x, y, z, yaw_deg, group)
                        // x/y/z/yaw are float BIT PATTERNS in the operand words —
                        // reinterpret, do not convert (op5's 0x43340000 is 180.0f,
                        // not the integer 1127481344).
                        auto f = [&](std::uint32_t i) { return opf(i); };
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
                    } else if (sub == 0x140 && host && nargs >= 4) {
                        // Chr_Pos(id, x, y, z): teleport. x/y/z are float bit patterns.
                        auto f = [&](std::uint32_t i) { return opf(i); };
                        host->chr_pos(op(0), f(1), f(2), f(3));
                    } else if (sub == 0x133 && host && nargs >= 2) {
                        host->chr_dir(op(0), opf(1));            // Chr_Dir(id, deg)
                    } else if (sub == 0x143 && host && nargs >= 5) {
                        // Chr_MoveTo(id, x, z, arriveRadius, speedScale) (FUN_00570ad0; audit
                        // event_vm #32): op3 is the arrival radius, op4 a multiplier on the
                        // actor's walk speed. The actor's Y comes from the floor.
                        host->chr_moveto(op(0), opf(1), opf(2), opf(3), opf(4));
                    } else if (sub == 0x154 && host && nargs >= 3) {
                        // Chr_Alpha(id, startA, endA, frames) [iffi] (FUN_005715d0): a fade.
                        host->chr_alpha(op(0), opf(1), opf(2), nargs >= 4 ? op(3) : 0);
                    } else if (sub == 0x151 && host && nargs >= 2) {
                        // Chr_Animate(id, motion): pin the actor's rendered AIA tag to `motion`
                        // (a dialogue pose) when >= 0; -1 clears back to auto idle/walk.
                        host->chr_animate(op(0), op(1));
                    } else if (sub == 0x136 && host && nargs >= 2) {
                        host->chr_lookat(op(0), op(1));   // Chr_LookAtChr (FUN_005707b0): mode 1 / release
                    } else if (sub == 0x134 && host && nargs >= 2) {
                        host->chr_dir_chr(op(0), op(1));  // Chr_DirChr (FUN_00570960): +0x308 once, mode 0
                    } else if (sub == 0x138 && host && nargs >= 2) {
                        host->chr_look_dir(op(0), opf(1));   // Chr_LookAtDir (FUN_00570870), deg in XMM2
                    } else if (sub == 0x132 && host && nargs >= 2) {
                        host->chr_turn_speed(op(0), opf(1)); // Chr_TurnSpeed (FUN_005714d0), mult in XMM2
                    } else if (sub == 0x152 && host && nargs >= 3) {
                        host->chr_pattern(op(0), op(1), op(2));   // Chr_Pattern (FUN_005712e0)
                    } else if (sub == 0x145 && host && nargs >= 2) {
                        host->chr_run(op(0), op(1) != 0);         // Chr_RunOrWalk (FUN_00570fb0)
                    } else if (sub == 0x162 && host && nargs >= 5) {
                        // Chr_Tremble(id, ticks, period, dx, dy) (FUN_00571ce0) [iiiff]
                        host->chr_tremble(op(0), op(1), op(2), opf(3), opf(4));
                    } else if (sub == 0x159 && host && nargs >= 2) {
                        host->chr_foot_sound(op(0), op(1));       // Chr_FootSound (FUN_00571320): on & 1
                    } else if (sub == 0x15d && host && nargs >= 2) {
                        host->chr_fly(op(0), op(1) != 0);         // Chr_FlyOnOff (FUN_00571bc0): setne
                    } else if (sub == 0x144 && host && nargs >= 6) {
                        // Chr_FlyTo(id, x, y, z, radius, scale) (FUN_00570b30) [ifffff]
                        host->chr_flyto(op(0), opf(1), opf(2), opf(3), opf(4), opf(5));
                    } else if (sub == 0x137 && host && nargs >= 3) {
                        auto f = [&](std::uint32_t i) { return opf(i); };
                        host->chr_lookat_pos(op(0), f(1), f(2));   // Chr_LookAtPos (id,x,z)
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
                        acc = (host && host->chr_in_event_box(op(0), ops(1))) ? 1 : 0;
                    } else if (sub == 0x10e && host && nargs >= 4) {
                        // EnclosePlayer(xmin, zmin, xmax, zmax) (handler 0x56a977): the arena
                        // rectangle DAT_0076a5cc..d8, float bit patterns. The mode is flag 190,
                        // set separately with 0x64. build/ai_spec/ARENA_AND_BOSSBAR.md.
                        auto f = [&](std::uint32_t i) { return opf(i); };
                        host->enclose(f(0), f(1), f(2), f(3));
                    } else if (sub == 0x11a && host && nargs >= 7) {
                        // Eft_LocatePosAddStr: place a named effect. x/y/z/a/b are float
                        // BIT PATTERNS in the operand words (as 0x12c's are), the name is
                        // a string-pool index. The barrier ward comes through here.
                        auto f = [&](std::uint32_t i) { return opf(i); };
                        host->locate_effect(op(0), f(1), f(2), f(3), f(4), f(5),
                                            ops(6));
                    } else if (sub == 0x117 && host && nargs >= 4) {
                        // Eft_LocatePos(id, x, y, z, a, b, name): spawn a named effect at a
                        // world position (EVENTVM_HANDLERS_2: FUN_0056ef90). Same layout +
                        // render path as 0x11a; this is the cleansed-statue glow (eff 84/85
                        // "eft_clear", blue for Yunica/Hugo, purple for Toal) and battle
                        // spawn-in FX. Was unhandled, so those effects never appeared.
                        auto f = [&](std::uint32_t i) { return opf(i); };
                        const float a = nargs > 4 ? f(4) : 1.0f, b = nargs > 5 ? f(5) : 0.0f;
                        std::string nm = nargs > 6 ? ops(6) : std::string();
                        if (nm.empty()) nm = "eft117_" + std::to_string(op(0));  // unique-ish key
                        host->locate_effect(op(0), f(1), f(2), f(3), a, b, nm);
                    } else if (sub == 0x119 && host && nargs >= 1) {
                        // Eft_Kill(name): remove the named located effect (barrier break).
                        host->kill_effect(ops(0));
                    } else if (sub == 0x112 && host && nargs >= 5) {
                        // WaterWallHitchk [BYTES 0x56ad63..0x56ae23]: FUN_00566340(0) is a pointer to
                        // operand 0 read as three words (x, y, z), then FUN_00566250(3) radius and
                        // (4) step, into a new GoXx's FUN_0058f9a0 (type 0x16, the falling probe).
                        // [not modelled] a non-float operand 0: the engine then reads its scratch
                        // float and the two words after it; here y = z = 0.
                        float x = opf(0), y = 0.0f, z = 0.0f;
                        if (op_type(script, old, 0) == 1) {
                            const std::int32_t wy = raw_op(script, old, 1), wz = raw_op(script, old, 2);
                            std::memcpy(&y, &wy, 4);
                            std::memcpy(&z, &wz, 4);
                        }
                        host->water_wall_hitchk(x, y, z, opf(3), opf(4));
                    } else if (sub == 0x111 && host && nargs >= 22) {
                        // FireBullet [BYTES 0x56aaa5..0x56ad5e -> FUN_00571500 0x571500..0x5715c9]:
                        // ints 0 (actor), 1 (kind), 2 (effect); operands 3 and 6 through
                        // FUN_00566340 = pointers to three words each (pos, dir; not normalised);
                        // floats 9..13; ints 14..16 (the block's int slots +0x158..+0x160); floats
                        // 17..21. FUN_00571500 resolves the actor (0x3e6 = the script's own) and,
                        // when it exists, calls FUN_00593550(ecx = actor, edx = kind, effect, &pos,
                        // &dir, 9..21, NULL) (build/event_re/DUELS.md §3). [not modelled] a
                        // non-float operand 3 / 6 (the engine then reads its scratch float and the
                        // words after it; here those words are the next operands' raw values).
                        auto vec3 = [&](std::uint32_t i, float* v) {
                            v[0] = opf(i);
                            for (std::uint32_t k = 1; k < 3; ++k) {
                                const std::int32_t w = op_type(script, old, i) == 1 ? raw_op(script, old, i + k) : 0;
                                std::memcpy(&v[k], &w, 4);
                            }
                        };
                        FireBulletOp fb;
                        fb.actor = op(0);
                        if (fb.actor == 0x3e6) fb.actor = owner;
                        fb.kind = op(1);
                        fb.effect = op(2);
                        vec3(3, fb.pos);
                        vec3(6, fb.dir);
                        for (std::uint32_t k = 0; k < 5; ++k) fb.f[k] = opf(9 + k);         // +0x144..+0x154
                        for (std::uint32_t k = 0; k < 3; ++k) {                             // +0x158..+0x160: the ints' bits
                            const std::int32_t v = op(14 + k);
                            std::memcpy(&fb.f[5 + k], &v, 4);
                        }
                        for (std::uint32_t k = 0; k < 5; ++k) fb.f[8 + k] = opf(17 + k);   // +0x164..+0x174
                        host->fire_bullet(fb);
                    } else if (sub == 0x125 && host && nargs >= 1) {
                        host->fire_drop(op(0));           // FireDrop: operand 0 only (weather.hpp)
                    } else if (sub == 0x122 && host && nargs >= 1) {
                        host->frame_smoke(op(0));         // FrameSmoke(i)
                    } else if (sub == 0x123 && host && nargs >= 3) {
                        host->fire_blur(opf(0), opf(1), opf(2));   // FireBlur(fff)
                    } else if (on_unhandled) {
                        on_unhandled(sub);                 // all other sub-ops: skipped by length
                    }
                } else if (on_unhandled) {
                    on_unhandled(sub);                     // an unmodelled zero-operand op
                }
                break;
            }
            default: return Step::Unknown;  // unknown class -> stop
        }
    return Step::Continue;
}

int XsoVm::run(const Xso& script, std::uint32_t entry, int max_steps, bool skip_cycles) {
    std::uint32_t pc = entry;
    int steps = 0;
    // The state at an earlier backward jump (taken again at the 1st, 2nd, 4th, 8th ... one, so a loop that
    // settles late is still found).
    struct Snap {
        std::uint32_t pc = 0; int steps = 0;
        std::int32_t acc = 0, shop_paid = 0, flags[0x200], items[54];
        std::unordered_map<int, std::int32_t> locals;
        Vars vars;
        std::uint32_t rs[4];
    } snap;
    bool have = false;
    skip_cycles = skip_cycles && !host;
    auto same = [&](std::uint32_t at) {
        const std::uint32_t* r = rs_ext ? rs_ext : rs;
        return at == snap.pc && acc == snap.acc && shop_paid == snap.shop_paid &&
               std::memcmp(g_flags, snap.flags, sizeof snap.flags) == 0 &&
               std::memcmp(item_arr, snap.items, sizeof snap.items) == 0 && std::memcmp(r, snap.rs, sizeof snap.rs) == 0 &&
               locals == snap.locals && vars.i[0] == snap.vars.i[0] && vars.i[1] == snap.vars.i[1] &&
               vars.f[0] == snap.vars.f[0] && vars.f[1] == snap.vars.f[1] && vars.s[0] == snap.vars.s[0] &&
               vars.s[1] == snap.vars.s[1];
    };
    unsigned back = 0;
    for (; steps < max_steps; ++steps) {
        const std::uint32_t was = pc;
        const Step s = step_one(script, pc, /*coroutine*/false);
        if (s == Step::End || s == Step::Unknown) return steps;
        if (!skip_cycles || pc > was) continue;
        if (have && same(pc)) {                      // steps + 1 done; the loop is steps + 1 - snap.steps long
            const int period = steps + 1 - snap.steps, left = max_steps - (steps + 1);
            steps += left / period * period;
            skip_cycles = false;
            continue;
        }
        if ((++back & (back - 1)) == 0) {
            snap.pc = pc; snap.steps = steps + 1; snap.acc = acc; snap.shop_paid = shop_paid;
            std::memcpy(snap.flags, g_flags, sizeof snap.flags);
            std::memcpy(snap.items, item_arr, sizeof snap.items);
            std::memcpy(snap.rs, rs_ext ? rs_ext : rs, sizeof snap.rs);
            snap.locals = locals; snap.vars = vars;
            have = true;
        }
    }
    return steps;
}

int blessing_index(const std::string& script) {
    std::string s = script;
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    if (!s.empty() && s[0] == '@') s.erase(0, 1);
    if (s.size() != 6 || s.compare(0, 4, "grow") != 0) return -1;
    if (s.compare(4, 2, "ar") == 0) return 7;
    if (s.compare(4, 2, "le") == 0) return 8;
    if (!std::isdigit((unsigned char)s[4]) || !std::isdigit((unsigned char)s[5])) return -1;
    return (s[4] - '0') * 10 + (s[5] - '0');
}

std::int32_t XsoVm::shop_value(ShopQuery::Kind kind, int sub, int index, std::int32_t value) const {
    if (!shop_filter) return value;
    ShopQuery q;
    q.kind = kind; q.sub = sub; q.index = index; q.value = value; q.script = script_name;
    const GrantDecision d = shop_filter(q);
    return d.action == GrantDecision::Action::Replace ? d.value : value;
}

GrantQuery XsoVm::filter_query(GrantQuery::Kind kind, int sub, int index) const {
    GrantQuery q;
    q.kind = kind;
    q.sub = sub;
    q.index = index;
    q.flags = g_flags;
    q.script = script_name;
    q.object = vars.s[0];
    q.owner = owner;
    return q;
}

// One frame of a resumable script (CUTSCENE_COROUTINE.md §3): if a wait counter is armed
// count it down, else burst-execute until a Wait_* op parks or the script ends.
XsoVm::Tick XsoVm::tick() {
    if (!co_script) return Tick::Ended;
    if (co_wait > 0) { if (--co_wait > 0) return Tick::Waiting; }   // still counting down
    for (int guard = 0; guard < 2'000'000; ++guard) {              // bound a wait-less loop
        ++burst_ops;
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
    (void)argoff;
    auto op = [&](std::uint32_t i) { return op_int(script, old, i); };
    switch (sub) {
        case 0xf1: return host ? host->wait_fader() : true;               // fade finished
        case 0xf3: return host ? host->wait_prompt() : true;             // advance pressed
        case 0xf4: {                                                      // Wait_Script(name)
            const std::string nm = op_str(script, old, 0);
            if (nm == "@Flag_Inv") return true;                          // 0x69f640: proceeds
            return host ? host->wait_script_done(nm) : true;
        }
        case 0xf5: return host ? host->wait_window(true) : true;         // msg window open
        case 0xf6: return host ? host->wait_window(false) : true;        // msg window closed
        case 0xf7: return host ? host->wait_sound(nargs >= 1 ? op(0) : 0) : true;
        case 0xf8: return nargs >= 2 ? (flag(op(0)) == op(1)) : true;    // Wait_FlagEqualInt
        case 0x100: return host ? !host->chr_moving(op(0)) : true;       // Wait_ChrMoving: proceed when STILL
        // WAITOPS.md §3.1-3.3. A missing object hangs 0x104/0x105 in the original (kept).
        case 0x104: {                                                     // Wait_MeshMoving
            if (!host) return true;
            bool found = false;
            const bool mv = host->mesh_moving(op_str(script, old, 0), &found);
            return found && !mv;
        }
        case 0x105: {                                                     // Wait_MeshMotionFinish
            if (!host) return true;
            bool found = false;
            const bool fin = host->mesh_motion_finished(op_str(script, old, 0),
                                                         std::uint32_t(op_int(script, old, 1)), &found);
            return found && fin;
        }
        case 0x101:                                                       // Wait_ChrLeaveMesh
            return host ? !host->chr_on_mesh(op(0), op_str(script, old, 1)) : true;
        default:   return true;   // 0xf9/0xfd/0xfe/0xff/0x106: 0 corpus sites (WAITOPS.md §3.4-3.8)
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
    in.born = serial_++;
    live.push_back(std::move(in));
    const std::size_t idx = live.size() - 1;
    tick_inst(live[idx]);   // immediate first tick this frame (§5a)
    return idx;
}

bool ScriptManager::start(const std::string& name, const XsoVm::Vars& vars, int owner) {
    // Single-instance dedup: the engine refuses a second live spawn of the same name
    // (unless AllowMultiInstance, which no room controller in this corpus sets). §5a.
    for (const Inst& i : live)
        if (!i.finished && !i.multi && iequal(i.name, name)) return false;
    if (!loader) return false;
    std::shared_ptr<const Xso> s = loader->load_script(name);
    if (!s) return false;                 // target doesn't exist -> skip (engine would throw)
    Inst in;
    in.script = std::move(s);
    in.name = name;
    in.vars = vars;
    in.owner = owner;
    in.born = serial_++;
    live.push_back(std::move(in));
    tick_inst(live.back());               // child runs its first burst the same frame
    return true;
}

void ScriptManager::fix(Inst& in, int slot) {
    if (slot < 0 || slot > 1) return;
    if (in.fix_slot >= 0 && fix_counts[in.fix_slot] > 0) --fix_counts[in.fix_slot];
    if (fix_counts[0] + fix_counts[1] == 0) scene_mark_ = serial_;   // a scene begins (skip_tail_step)
    ++fix_counts[slot];
    in.fix_slot = slot;
    if (on_fix_taken) on_fix_taken(slot, fix_counts[slot] == 1);
}

void ScriptManager::unfix(Inst& in) {
    if (in.fix_slot >= 0 && fix_counts[in.fix_slot] > 0) --fix_counts[in.fix_slot];
    in.fix_slot = -1;
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
    vm.vars = in.vars;
    vm.owner = in.owner;
    vm.script_name = in.name;          // the grant filter's context (CleriaCore mods)
    vm.burst_ops = 0;
    const XsoVm::Tick t = vm.tick();
    vm.script_name.clear();
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
    for (std::size_t i = 0; i < n; ++i) {
        if (paused) return;               // a scene change froze the VM mid-frame (SCENEFLOW §1.4)
        tick_inst(live[i]);
    }
    // Drain StartScript spawns. start() ticks each child once, which may queue more.
    while (!spawn_queue.empty() && !paused) {
        std::vector<Spawn> batch;
        batch.swap(spawn_queue);
        for (const Spawn& sp : batch) start(sp.name, sp.vars, sp.owner);
    }
    // Record how many GoScripts were resident together this frame (before the reap drops
    // the ones that just finished) — the concurrency the manager actually sustained.
    peak_live = std::max(peak_live, live.size());
    // Reap finished scripts (the driver skips them; a separate pass removes them, §5d). A
    // script that ended still holding a fix is released on teardown (the engine's
    // FUN_0056eab0 unfix path), so the world doesn't stay frozen forever.
    for (Inst& i : live)
        if (i.finished) unfix(i);
    live.erase(std::remove_if(live.begin(), live.end(),
                              [](const Inst& i) { return i.finished; }),
               live.end());
}

bool ScriptManager::skip_tail_step(int patience, int long_wait) {
    if (paused) return false;
    bool more = false;
    const std::size_t n = live.size();
    for (std::size_t i = 0; i < n && !paused; ++i) {
        Inst& in = live[i];
        if (in.finished || in.born < scene_mark_ || in.tail_done) continue;
        if (in.fix_slot >= 0) { in.tail_done = true; continue; }    // it holds the world: the main drain's
        if (in.wait > long_wait) { in.tail_done = true; continue; } // a timer, not choreography
        if (in.tail_pcs.empty()) in.tail_pcs.push_back(in.pc);
        const std::uint32_t pc0 = in.pc;
        in.wait = 0;
        tick_inst(in);
        if (in.finished) continue;
        if (in.fix_slot >= 0) in.tail_done = true;
        else if (in.pc == pc0 && vm.burst_ops <= 1) { if (++in.tail_stood >= patience) in.tail_done = true; }
        else {
            in.tail_stood = 0;
            if (std::find(in.tail_pcs.begin(), in.tail_pcs.end(), in.pc) != in.tail_pcs.end()) in.tail_done = true;
            else in.tail_pcs.push_back(in.pc);
        }
        more = more || !in.tail_done;
    }
    while (!spawn_queue.empty() && !paused) {                       // as tick_all: the children start now
        std::vector<Spawn> batch;
        batch.swap(spawn_queue);
        for (const Spawn& sp : batch) more = start(sp.name, sp.vars, sp.owner) || more;
    }
    for (Inst& i : live)
        if (i.finished) unfix(i);
    live.erase(std::remove_if(live.begin(), live.end(), [](const Inst& i) { return i.finished; }), live.end());
    return more;
}

bool ScriptManager::any_live() const {
    for (const Inst& i : live)
        if (!i.finished) return true;
    return false;
}

}  // namespace cleria
