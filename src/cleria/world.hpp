// The actor world — the pool the XSO spawn op fills, and the registry every query
// op resolves through. Derived from build/entity_re/ENTITY.md; see actor.hpp for why
// our Actor is not the engine's GoActor.
//
// ## The primary key is the INSTANCE id, not the cast id [C-DIS][C-COR]
//
// `entity+0x128` is a PER-INSTANCE id, and it is what `FUN_00434d50` — the lookup
// behind every query op — linear-scans for. It is emphatically not the cast type:
// EVNT_3002_THOR.XSO spawns FIVE instances of cast 35 with ids 167..171.
//
// The default id IS the cast type, though, which is why op0 and op1 correlate all over
// the corpus while staying distinct — and it is exactly the kind of correlation that
// invites collapsing two fields into one. They are two fields.
//
// ## Reserved ids [C-DIS]
//
//   13   the player — but the engine takes that identity from CASTINFO+0x60 == 13
//        (the CAST_ADOL record), NOT from the spawn op's operand.
//   998  "self" — resolved in the op against ctx+0x240, never through the registry.
//   999  -> DAT_0074ab90, which the spawn op ALSO points at the player (0x5704ce).
//        ** Do not read 999 as "not the player". ** Whether anything re-points it
//        later is [UNKNOWN].
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <list>

#include "cleria/actor.hpp"
#include "cleria/mathx.hpp"
#include "cleria/xso.hpp"

namespace cleria {


struct CastTable;
struct EnemyTable;
struct GameInfo;
struct EexTable;
struct Sct;

// Ids the engine gives meaning to. `kIdSelf` never reaches the registry.
inline constexpr int kIdPlayer = 13;
inline constexpr int kIdSelf = 998;
inline constexpr int kIdPlayerAlias = 999;
// The auto-id allocator scans upward from here — just past the 186 CASTINFO records,
// so an auto-id can never collide with a default (which is a cast type). [C-DIS]
inline constexpr int kAutoIdBase = 186;

// The simulation runs at a fixed 60 Hz (ACTOR.md §3: the step length is exactly
// 1000/60 ms, drained by a catch-up loop; per-step dt is constant, actor motion is
// not delta-scaled). physics_player.hpp's kTimerStep (~1/60) is the same clock.
inline constexpr int kSimHz = 60;
inline constexpr float kSimDt = 1.0f / float(kSimHz);


// Everything the spawn op (0x12c) carries. Named so the operand order stops being
// load-bearing at call sites.
struct SpawnRequest {
    int id = 0;         // op0. 0 => allocate; see auto_id().
    int cast_type = 0;  // op1 — indexes CASTINFO.
    Vec3 pos{};
    float yaw_degrees = 0;  // the op stores DEGREES; the engine converts (FUN_00443a80)
    int group = 0;          // op6. Only 1..4 are honoured; 0 = untagged.
    int level = 0;          // FUN_004202e0's level (op 0x18E's); 0 = the cast's own (ENEMYINFO +0xe8)
};

// The tables a spawn needs to turn a cast type into a live actor with real stats.
struct WorldTables {
    const CastTable* casts = nullptr;
    const EnemyTable* enemies = nullptr;
    const GameInfo* game = nullptr;
    const EexTable* eex = nullptr;  // optional: full-room boss stats (eex.hpp)

    bool ok() const { return casts && enemies && game; }
};

// ActorWorld IS the XSO host: it implements the primitive spawn/is_alive interface
// the VM calls, so a running script drives real actors. The interface stays free of
// world types (see xso.hpp); this is where the two meet.
class ActorWorld : public XsoHost {
  public:
    explicit ActorWorld(WorldTables t) : tables_(t) {}
    // The game flags (g_flags, 0x76b91c) the spawn's stat pass FUN_004202e0 reads for its difficulty tail:
    // 144 / 143 / 203 / 193 / 196 (DIFFICULTY.md §3.1). Null = Normal (the tools).
    const std::int32_t* stat_flags = nullptr;
    // The game's xorshift128 state (XsoVm::rs), for the draws the spawn op makes (the glow phase
    // seed). Null = no draw (headless tools).
    std::uint32_t* rng = nullptr;

    // CleriaCore's own (the mods' "spawn" filter, API 7; not retail): asked once for every ENEMY about to be built,
    // before the factory. It may name another enemy cast, another level (0 = the cast's own), scale the stats the
    // spawn computes, or skip the spawn (spawn() then returns null, as for an unknown cast). Null = nothing is asked.
    struct SpawnTweak { int cast = 0, level = 0; float hp = 1, atk = 1, def = 1, exp = 1, sp = 1; bool skip = false; };
    std::function<void(const SpawnRequest&, SpawnTweak&)> spawn_filter;

    // The spawn op, end to end (FUN_00570400). Returns null if the cast is unknown.
    //
    // ** An existing actor with this id is REUSED, not duplicated ** — step 1 of the
    // engine's own sequence looks the id up first and only allocates on a miss.
    Actor* spawn(const SpawnRequest& r);
    std::vector<int> arena_spawn(int cast, int level, int count);   // op 0x18E: the ids spawned

    // --- XsoHost: the primitive interface the VM calls -------------------------
    void spawn(int entity_id, int cast_type, float x, float y, float z,
               float yaw_deg, int group) override {
        spawn(SpawnRequest{entity_id, cast_type, {x, y, z}, yaw_deg, group});
    }
    bool is_alive(int entity_id) const override {
        // find() is non-const (it may resolve the player fast path), so this mirrors
        // its logic read-only rather than casting away const.
        if ((entity_id == kIdPlayer || entity_id == kIdPlayerAlias) && player_)
            return player_->alive();
        for (const Actor& a : actors_)
            if (a.instance_id == entity_id) return a.alive();
        return false;
    }
    // Region-query position source (ops 0x139/0x13a/0x13b): the worker's actor resolve
    // (FUN_00434d50 by id, player fast path) then position read (+0x50/+0x54/+0x58).
    bool actor_pos(int entity_id, float out[3]) const override {
        const Actor* a = nullptr;
        if ((entity_id == kIdPlayer || entity_id == kIdPlayerAlias) && player_) {
            a = player_;
        } else {
            for (const Actor& c : actors_)
                if (c.instance_id == entity_id) { a = &c; break; }
        }
        if (!a) return false;
        out[0] = a->pos.x; out[1] = a->pos.y; out[2] = a->pos.z;
        return true;
    }
    // op 0x131 Chr_IsSeedLiving(group): FUN_00570640 scans the live registry for any
    // entity whose +0x130 group bits include (group<<8). The player is never tagged, so
    // this is just "any living group-N seed enemy left?".
    bool any_seed_alive(int group) const override {
        for (const Actor& a : actors_)
            if (a.tag.in_group(group) && a.alive()) return true;
        return false;
    }
    // Cutscene actor control (ops 0x140/0x143/0x133/0x100). The player (id 13/999) is
    // NOT a world actor in the view — the host special-cases it — so these act on
    // spawned NPCs only; an absent id is a no-op.
    // 0x140 Chr_Pos (FUN_00570a40 -> FUN_00459ff0 with xmm2 = +0x308): the placement also
    // snaps the facing, +0x314 = +0x360 = +0x308 (the COMMANDED heading), mode untouched.
    void chr_pos(int id, float x, float y, float z) override {
        if (Actor* a = find(id)) {
            a->pos = {x, y, z}; a->cmoving = false;
            a->yaw = yaw_of_heading(a->look.cmd);
            a->look.dir = a->look.cmd;
        }
    }
    // 0x133 Chr_Dir (FUN_005708c0): +0x308 = FUN_00443a80((int)deg), look mode 0. The facing
    // TURNS there in the actor tick (tick_facing), it does not snap (CHR_OPS.md §6).
    void chr_dir(int id, float deg) override {
        if (Actor* a = find(id)) look_dir_cmd(a->look, falcom_heading(int(deg), rng));
    }
    // 0x134 Chr_DirChr (FUN_00570960): +0x308 = atan2(target - self) once, look mode 0.
    void chr_dir_chr(int id, int target) override {
        Actor* a = find(id); Actor* t = find(target);
        if (a && t)
            look_dir_cmd(a->look, float(std::atan2(double(t->pos.z) - double(a->pos.z),
                                                   double(t->pos.x) - double(a->pos.x))));
    }
    // 0x138 Chr_LookAtDir (FUN_00570870): look mode 3 on FUN_00443a80((int)deg), +0x370 = 0.
    void chr_look_dir(int id, float deg) override {
        if (Actor* a = find(id)) look_fixed_dir(a->look, falcom_heading(int(deg), rng));
    }
    // 0x132 Chr_TurnSpeed (FUN_005714d0): +0x36c = mult. The player's turn never reads it.
    void chr_turn_speed(int id, float mult) override {
        if (Actor* a = find(id)) a->look.mult = mult;
    }
    // 0x143 walk-to (FUN_00570ad0): until within max(0.025, radius), at +0x30c (CASTINFO+0x48,
    // 0.035 for most casts) x speed_scale x Movable per step (FUN_0045f4d0; WAITOPS.md §6.2).
    // [not modelled] an NPC's ASO Movable (taken as 1).
    void chr_moveto(int id, float x, float z, float radius, float scale) override {
        if (Actor* a = find(id)) {
            a->cmove_tgt = {x, a->pos.y, z};
            a->cmove_spd = a->speed30c * scale;
            const float r = std::max(0.025f, radius);
            a->cmove_r2 = r * r;
            a->cmoving = true;
        }
    }
    // 0x100 Wait_ChrMoving (FUN_00570bc0): +0x364 != 0 || +0x380 != 0 (a MoveTo or a FlyTo).
    bool chr_moving(int id) const override {
        for (const Actor& a : actors_) if (a.instance_id == id) return a.cmoving || a.fly_r2 != 0.0f;
        return false;
    }
    void chr_alpha(int id, float start, float end, int frames) override {   // 0x154 (FUN_005715d0)
        if (Actor* a = find(id)) {
            const float s = std::clamp(start, 0.0f, 1.0f), t = std::clamp(end, 0.0f, 1.0f);
            a->ai.alpha = s;
            a->alpha_to = t;
            a->alpha_step = (t - s) / float(frames == 0 ? 1 : frames);
        }
    }
    // The +0x4e0 lerp of the actor tick (FUN_0045c490 0x45d147). [H] It also runs under a Fix:
    // the scripts fade an actor and Wait_Time the fade out under their own Fix (TALKITEM's
    // Chr_Alpha(159, 1, 0, 50); Wait_Time(50)), so the view calls this every tick.
    void tick_alpha_fades() {
        for (Actor& a : actors_) {
            if (a.alpha_step == 0.0f) continue;
            a.ai.alpha += a.alpha_step;
            const bool short_of = a.alpha_step < 0.0f ? a.alpha_to < a.ai.alpha : a.ai.alpha < a.alpha_to;
            if (!short_of) { a.alpha_step = 0.0f; a.ai.alpha = a.alpha_to; }
        }
    }
    void chr_locate_dummy(int id, float x, float y, float z) override {  // 0x12F (FUN_00570350)
        // The factory on CASTINFO record 1 (CAST_C000: class 4 = talkable, radius 0.4, no sprite)
        // and the placement; its HP = CASTINFO+0x74 = 25 (FACTORY.md 0x4595aa: every cast).
        if (!find(id)) spawn(id, 1, x, y, z, 0.0f, 0);
        if (Actor* a = find(id)) {
            a->pos = {x, y, z};
            a->hp = a->max_hp = 25.0f;
        }
    }
    void chr_shadow(int id, bool on) override {                        // 0x153 (FUN_00570a80)
        if (Actor* a = find(id)) a->flags12c = (a->flags12c & ~1u) | (on ? 0u : 1u);
    }
    void chr_ztest(int id, bool on) override {                          // 0x158 (FUN_00571790)
        if (Actor* a = find(id)) a->ztest = on;
    }
    // 0x14D Chr_CollisionVanish (FUN_005712a0): +0x60 bit 0x80 = (on == 0), the bit the hit tests
    // and the body push require (EnemyAI::hittable).
    void chr_collision_vanish(int id, bool on) override {
        if (Actor* a = find(id)) a->ai.hittable = !on;
    }
    // 0x163 Chr_NoForce / 0x164 Chr_AniDisable (FUN_00571d60 / FUN_00571db0): +0x130 (SpawnTag::bits,
    // whose group field is bits 8..10) bit 0x10000 / 0x80000.
    void chr_slant_fire(int id, float deg) override {           // 0x161: +0x4ec (0x6bd0dc)
        if (Actor* a = find(id)) a->slant = deg * 0.017453292f;
    }
    void chr_no_force(int id, bool on) override {
        if (Actor* a = find(id)) a->tag.bits = (a->tag.bits & ~0x10000u) | (on ? 0x10000u : 0u);
    }
    void chr_ani_disable(int id, bool on) override {
        if (Actor* a = find(id)) a->tag.bits = (a->tag.bits & ~0x80000u) | (on ? 0x80000u : 0u);
    }
    // 0x165 Chr_Stop (FUN_00571e00): both move targets = the position, radii and speed 0.
    void chr_stop(int id) override {
        if (Actor* a = find(id)) {
            a->cmove_tgt = a->pos; a->cmoving = false; a->cmove_spd = 0.0f; a->cmove_r2 = 0.0f;
            a->fly_tgt = a->pos; a->fly_r2 = 0.0f;
        }
    }
    void chr_jump(int id, float v) override {                           // 0x141 (FUN_005709d0)
        if (Actor* a = find(id)) { a->vert.t = v; a->pos.y += v; a->vert.grounded = false; }
    }
    // 0x152 Chr_Pattern (FUN_005712e0): the sprite pin (+0xa1c/+0xa20). CHR_OPS.md §4.
    void chr_pattern(int id, int tag, int frame) override {
        if (Actor* a = find(id)) { a->pattern.tag = tag; a->pattern.frame = frame; }
    }
    // 0x167 Chr_AniChange (FUN_00571ec0): +0x97c = file, the AIA the sprite is decoded from. ANI_VARIANT.md.
    void chr_anichange(int id, int file) override {
        if (Actor* a = find(id)) a->aia_file = file;
    }
    // 0x145 Chr_RunOrWalk (FUN_00570fb0): +0x12c bit 0x20 = run. CHR_OPS.md §5.
    void chr_run(int id, bool run) override {
        if (Actor* a = find(id)) a->flags12c = (a->flags12c & ~0x20u) | (run ? 0x20u : 0u);
    }
    // 0x162 Chr_Tremble (FUN_00571ce0). CHR_OPS.md §7.
    void chr_tremble(int id, int ticks, int period, float dx, float dy) override {
        if (Actor* a = find(id)) tremble_set(a->tremble, ticks, period, dx, dy);
    }
    // 0x159 Chr_FootSound (FUN_00571320): +0x4be = on & 1.
    void chr_foot_sound(int id, int on) override {
        if (Actor* a = find(id)) a->footsteps = (on & 1) != 0;
    }
    // 0x15d Chr_FlyOnOff (FUN_00571bc0, stub setne): +0x398 = (on != 0).
    void chr_fly(int id, bool on) override {
        if (Actor* a = find(id)) a->flyer = on;
    }
    // 0x144 Chr_FlyTo (FUN_00570b30): the 3D move block. The script y is a FEET height; the
    // engine stores the ORIGIN (y + lift +0x414 + height +0x324). CleriaCore's pos is the feet
    // and lift is not modelled per actor, so the target is kept in feet space [H: lift = 0].
    void chr_flyto(int id, float x, float y, float z, float radius, float scale) override {
        if (Actor* a = find(id)) {
            a->fly_tgt = {x, y, z};
            const float r = std::max(0.025f, radius);   // 0x6bd0f8 = 0.025
            a->fly_r2 = r * r;
            a->fly_spd = scale;
        }
    }
    void chr_attach_script(int id, const std::string& s) override {    // 0x130
        if (Actor* a = find(id)) a->talk_script = s;
    }
    void chr_set_status(int id, int lv, const float* f) override {      // 0x160
        if (Actor* a = find(id)) {
            a->level = lv;
            a->hp = a->max_hp = f[0];
            a->atk = f[1];                                              // +0xa4
            a->def = f[2];                                              // +0xa8
            a->exp_reward = f[4];                                       // +0xb0 (f[3] = +0xac, not modelled)
            for (int k = 0; k < 4; ++k) a->resist[k] = f[5 + k];
        }
    }
    // Facing control (CHR_OPS.md §6). 0x136 Chr_LookAtChr (FUN_005707b0): target 0 releases
    // (mode 0, +0x370 = 0); otherwise mode 1 tracking the target, unchanged if it is not found.
    // 0x137 Chr_LookAtPos (FUN_00570820): mode 2 on the point. The turn is tick_facing's.
    // The player (13/999) resolves to the synced player_ actor; the view turns her.
    void chr_lookat(int id, int target) override {
        if (Actor* a = find(id)) look_at_actor(a->look, target, target == 0 || find(target) != nullptr);
    }
    void chr_lookat_pos(int id, float x, float z) override {
        if (Actor* a = find(id)) look_at_point(a->look, x, z);
    }
    // 0x151 Chr_Animate (FUN_00571000): the ASO motion +0x994 (-1 = back to the locomotion
    // motion) and the pattern pin released (+0xa1c = -1, +0xa20 = 0). The view's ASO tick plays
    // the section. [not modelled] +0x9a0/+0x9a4..+0x9ac/+0x9bc..+0x97c, the player's re-bind.
    void chr_animate(int id, int motion) override {
        if (Actor* a = find(id)) { a->anim_motion = motion; a->pattern = PatternPin{}; }
    }
    // Advance every cutscene walk-to one frame (called each tick, incl. while frozen —
    // NPCs move during a Fix). The mover (FUN_0045f4d0 0x45f607..0x45f6b4): +0x308 = the
    // heading to the target and the step goes along +0x308; the FACING (+0x314) turns toward it
    // at the turn rate in tick_facing, so a walker turns into its path instead of snapping. On
    // arrival +0x308 = +0x314 (it stops turning where it is).
    void advance_cutscene_moves() {
        for (Actor& a : actors_) {
            if (a.is_player()) continue;   // the view walks the player (CutsceneHost)
            if (a.fly_r2 != 0.0f) { advance_fly(a); continue; }
            if (!a.cmoving) continue;
            const float dx = a.cmove_tgt.x - a.pos.x, dz = a.cmove_tgt.z - a.pos.z;
            // The planar arrival test runs before the step; the step is the full speed (no
            // clamp; the engine can overshoot by less than one step).
            if (dx * dx + dz * dz <= a.cmove_r2) {
                a.cmoving = false;
                a.look.cmd = heading_of_yaw(a.yaw);
                continue;
            }
            a.look.cmd = float(std::atan2(double(dz), double(dx)));
            a.pos.x += std::cos(a.look.cmd) * a.cmove_spd; a.pos.z += std::sin(a.look.cmd) * a.cmove_spd;
        }
    }
    // The FlyTo branch of the mover (0x45f6d3..0x45f80b) [BYTES]: a 3D arrival test against
    // +0x380 (then +0x308 = +0x314, +0x380 = 0); else +0x308 = atan2(dz, dx) (+0x314 when
    // dxz == 0), pitch = atan2(dy, |dxz|), speed v = +0x30c x scale (x Movable [not modelled]: 1),
    // horizontal v cos(pitch) along +0x308 and, for a flyer, vertical v sin(pitch). A non-flyer
    // keeps only the horizontal part (the view's floor snap holds it down).
    // [not modelled] the 10%/tick decay and the integrator: the vertical step is applied direct.
    void advance_fly(Actor& a) {
        const float dx = a.fly_tgt.x - a.pos.x, dy = a.fly_tgt.y - a.pos.y, dz = a.fly_tgt.z - a.pos.z;
        if (dx * dx + dy * dy + dz * dz <= a.fly_r2) {
            a.look.cmd = heading_of_yaw(a.yaw);
            a.fly_r2 = 0.0f;
            return;
        }
        const float dxz = std::sqrt(dx * dx + dz * dz);
        a.look.cmd = dxz == 0.0f ? heading_of_yaw(a.yaw) : float(std::atan2(double(dz), double(dx)));
        const float pitch = float(std::atan2(double(dy), double(dxz)));
        const float v = a.speed30c * a.fly_spd;
        const float h = v * std::cos(pitch);
        a.pos.x += std::cos(a.look.cmd) * h; a.pos.z += std::sin(a.look.cmd) * h;
        if (a.flyer) a.pos.y += v * std::sin(pitch);
    }
    // The facing turn of the NPC mover (0x46002f..0x46011a), once per tick for every actor but
    // the player. `ai_live(a)` names the actors whose facing the enemy brain owns this tick
    // (the view's AI loop writes their yaw); for those +0x308 follows the facing so a later
    // Fix does not turn them back to a stale command [H].
    template <class AiLive>
    void tick_facing(AiLive ai_live) {
        for (Actor& a : actors_) {
            if (a.is_player()) continue;
            if (ai_live(a)) { a.look.cmd = heading_of_yaw(a.yaw); continue; }
            float txz[2];
            const float* tp = nullptr;
            if (a.look.mode == 1 && a.look.target != 0)
                if (const Actor* t = find(a.look.target)) { txz[0] = t->pos.x; txz[1] = t->pos.z; tp = txz; }
            const float goal = look_goal(a.look, a.pos.x, a.pos.z, tp);
            // Running = moving with the walk bit clear: +0x510 & 0x20 is re-set every tick and
            // cleared again only while a MoveTo/FlyTo runs with +0x12c & 0x20 (0x45f6c3/0x45f812).
            const bool moving = a.cmoving || a.fly_r2 != 0.0f;
            const bool running = moving && (a.flags12c & 0x20u) != 0;
            const float cur = heading_of_yaw(a.yaw);
            const float h = look_turn(cur, goal, npc_turn_rate(a.look, running));
            if (h != cur) a.yaw = yaw_of_heading(h);
        }
    }
    // The anim tick's head for every actor (FUN_004472e0 0x447306..0x447344): the tremble
    // timers, which run under a Chr_Pattern pin and under a Fix.
    void tick_tremble() {
        for (Actor& a : actors_) tremble_step(a.tremble);
    }

    // op 0x12d Chr_Delete(id): remove the actor with this instance id (never the
    // player). std::list keeps other Actor* handles valid across the erase.
    void despawn(int entity_id) override {
        for (auto it = actors_.begin(); it != actors_.end(); ++it)
            if (it->instance_id == entity_id && &*it != player_) { actors_.erase(it); return; }
    }

    // op 0xa0: spawn every placement in the room's SCT (set via set_placements). No-op
    // if no placements are set. Defined in world.cpp (needs the Sct/CastTable bodies).
    void spawn_placements() override;

    // The room's SCT placements, the source op 0xa0 spawns from. The caller sets this
    // to the loaded room's SCT before running its script. Not owned.
    void set_placements(const Sct* s) { placements_ = s; }

    // FUN_00434d50 — the lookup behind every query op. A linear scan by instance id.
    // Returns null when absent (the engine returns 0), and resolves the player fast
    // path for id 13 / 999.
    Actor* find(int id);

    // FUN_00434db0 — the auto-id allocator, used when the op's id operand is 0.
    // The cast type itself if free, else the first free id from 186 upward.
    int auto_id(int cast_type) const;

    // The player, or null before CAST_ADOL has spawned. The engine keeps three
    // globals pointing here (DAT_0074c09c / DAT_0074ab90 / DAT_00749d44); why three,
    // and whether they ever diverge, is [UNKNOWN], so we keep one and say so.
    Actor* player() { return player_; }

    std::list<Actor>& actors() { return actors_; }
    const std::list<Actor>& actors() const { return actors_; }
    std::size_t size() const { return actors_.size(); }

    // Drop every actor (e.g. a room change — the engine's FUN_00428ec0 destroy-all).
    // Invalidates every Actor* handed out, including the player pointer.
    void clear() {
        actors_.clear(); player_ = nullptr; effects_.clear(); placements_spawned_ = false;
        reset_arena();
    }
    // The arena rectangle DAT_0076a5cc..d8 {xmin, zmin, xmax, zmax}: set by op 0x10e, reset
    // to +/-FLT_MAX on every room load (FUN_00431840 0x431aae) and never by a script.
    float arena[4] = {-3.402823466e+38f, -3.402823466e+38f, 3.402823466e+38f, 3.402823466e+38f};
    void reset_arena() {
        arena[0] = arena[1] = -3.402823466e+38f;
        arena[2] = arena[3] = 3.402823466e+38f;
    }
    void enclose(float xmin, float zmin, float xmax, float zmax) override {
        arena[0] = xmin; arena[1] = zmin; arena[2] = xmax; arena[3] = zmax;
    }

    // ## Located effects (op 0x11a / 0x119) --------------------------------------
    // A script-placed, persistent effect instance: an eftlist effect id at a world
    // position, addressable by name for later Eft_Kill. The engine spawns a GoEffect;
    // the VM stays headless, so we just record what to draw and let the view resolve
    // the id -> EF2 and render it. The barrier ward (id 210, name "eft_barrier") lives
    // here from room-load until the barrier breaks.
    // `age` ticks up every sim frame; a ONE-SHOT effect is removed when it passes its EF2
    // lifetime (EFFECTS.md §: the effect object's tick has a lifetime gate `+0x130 >= +0x12c`
    // that sets the destroy bit). For 0x117/0x11A `a` is the scale and `b` the yaw in degrees
    // (BARRIER_OBJECTS.md §1), NOT a hold flag: the old "b != 0 = held until Eft_Kill" reading
    // is WRONG (every 0x11A site has b = 0). The live loop runs those placements through the
    // EF2 sim instead, where persistence is the emitters' own re-arm; this list keeps the
    // headless preview and the brains' owned effects.
    struct LocatedEffect {
        int id; float x, y, z, a, b; std::string name; int age = 0;
        int owner = 0, tag = -1;   // a brain's private owner tag (M530: fx+0x1a4 = self, fx+0x1d5)
    };
    void locate_effect(int id, float x, float y, float z, float a, float b,
                       const std::string& name) override {
        // Effects are addressed BY NAME (Eft_Kill looks a name up), so a name can only denote
        // one live effect: re-placing a name replaces it rather than stacking a second copy.
        // Without this, a script re-run piles duplicate auras on the goddess statue.
        if (!name.empty())
            for (LocatedEffect& e : effects_)
                if (e.name == name) { e = {id, x, y, z, a, b, name, 0}; return; }
        effects_.push_back({id, x, y, z, a, b, name, 0});
    }
    // Age every located effect one sim tick and drop the finished one-shots. `life_of(id)`
    // resolves the EF2's own lifetime in frames (the view owns the EF2 registry).
    void tick_effects(const std::function<int(int)>& life_of) {
        for (auto it = effects_.begin(); it != effects_.end();) {
            ++it->age;
            const bool persistent = it->b != 0.0f;      // held until Eft_Kill
            const int life = life_of(it->id);
            it = (!persistent && life > 0 && it->age >= life) ? effects_.erase(it) : it + 1;
        }
    }
    // A brain's own effect (owner = its instance id, tag = its private slot byte), and the
    // per-tick re-place of every live effect it owns with that tag: the position moves, the
    // age (the animation) keeps running. [not modelled] the rotation (no oriented draw).
    void locate_owned_effect(int id, float x, float y, float z, int owner, int tag) {
        effects_.push_back({id, x, y, z, 0.0f, 0.0f, std::string(), 0, owner, tag});
    }
    void move_owned_effects(int owner, int tag, float x, float y, float z) {
        for (LocatedEffect& e : effects_)
            if (e.owner == owner && e.tag == tag) { e.x = x; e.y = y; e.z = z; }
    }
    void kill_owned_effects(int owner, int tag) {
        for (auto it = effects_.begin(); it != effects_.end();)
            it = (it->owner == owner && it->tag == tag) ? effects_.erase(it) : it + 1;
    }
    void kill_effect(const std::string& name) override {
        for (auto it = effects_.begin(); it != effects_.end();)
            it = (it->name == name) ? effects_.erase(it) : it + 1;
    }
    // Drop the script-placed (named) effects; the live loop places them in the EF2 sim.
    void drop_named_effects() {
        for (auto it = effects_.begin(); it != effects_.end();)
            it = it->name.empty() ? it + 1 : effects_.erase(it);
    }
    const std::vector<LocatedEffect>& effects() const { return effects_; }

    // ## The tick (build/actor_re/ACTOR.md §2, §3) [C-DIS]
    //
    // ** Iteration order IS defined, and it is a correctness requirement, not a
    // detail. ** The earlier "order is undefined" note is retired: the engine updates
    // the PLAYER FIRST, then walks the live list in index (= spawn) order, skipping the
    // player. Removal is an order-preserving shift, so survivors keep their relative
    // order across deaths. Hit processing runs inside the victim's own update, so this
    // order is observable through gameplay — ACTOR.md §8 lists (player-first) +
    // (spawn-order) + (order-preserving removal) as correctness invariants.
    //
    // ** The COUNT is cached before the loop. ** An actor spawned mid-step (a script op
    // running inside another actor's update) is appended but NOT visited this step; its
    // first update is next step. Reproduced here by snapshotting the size.
    //
    // ** Per-actor AI is [UNKNOWN] and deliberately a no-op hook. ** The engine
    // dispatches through the 94-entry AI table (0x69b8c0) by actor+0x120; we have not
    // RE'd what a handler does, so `update_actor` does nothing but is the honest seam.
    // The loop STRUCTURE is validated; the loop BODY is an owned placeholder.
    //
    // Fixed 60 Hz: the sim dt is a constant 1/60 s (kSimHz). Actor motion is NOT
    // delta-scaled — the render/interpolation fraction is a separate, presentation-only
    // concern. Any variable-dt tick here would diverge from the engine.
    void step();

    // Request removal. The engine sets +0x11 bit 2 (GoActor::Kill, FUN_004593c0) after
    // the AI has played the death state; nothing is removed at that instant. We have no
    // death-anim state machine yet, so a killed actor requests destruction directly.
    void request_destroy(Actor& a) { a.header_flags |= kDestroyRequested; }

    // ** The deferred sweep — FUN_00428b10, ONCE PER RENDER FRAME, not per step. ** With
    // N catch-up steps in one frame, an actor killed in step 1 is still updated in
    // 2..N; only the frame-boundary sweep removes it. So this is called at FRAME
    // granularity by the caller, never inside step(). Removal is order-preserving, and
    // if the player actor is swept the player pointer is dropped (the engine re-scans
    // for another cast-13 actor; we have exactly one, so null is correct).
    std::size_t sweep_dead();

    // The engine's pool is a fixed 128 slots (overflow fails silently, does not throw).
    // We do NOT impose the cap: ACTOR.md §8 names the append/order/deferred-sweep
    // invariants as what matters, not the 128 ceiling. Exposed so a caller can warn.
    static constexpr std::size_t kEnginePoolCap = 128;

  private:
    WorldTables tables_;
    // ** A list, and that is load-bearing — a deque would dangle pointers. **
    // spawn()/find()/player() all hand out Actor*, and player_ lives from CAST_ADOL's
    // spawn to end of room. The engine's own storage is a slab of STABLE slots with a
    // separate pointer array for order (DAT_00730098), so a slot never moves once
    // spawned. std::list reproduces exactly that: append keeps order, and
    // order-preserving MIDDLE erase (what the sweep does) leaves every other node's
    // address untouched. std::deque was the first choice and is WRONG here — its
    // middle-erase invalidates all references, which is precisely the sweep's case.
    std::list<Actor> actors_;
    Actor* player_ = nullptr;
    const Sct* placements_ = nullptr;  // op 0xa0's source; not owned
    // op 0xa0 (SCT bulk spawn) uses AUTO-ids, so a second call would DOUBLE the room's
    // enemies. With events on the root runs twice (populate run-to-completion, then the
    // live coroutine), so guard it: spawn_placements is a no-op after the first call,
    // reset on clear() (room load). Explicit-id 0x12c spawns dedup by id already.
    bool placements_spawned_ = false;
    std::vector<LocatedEffect> effects_;  // op 0x11a placements; killed by 0x119
public:
    std::uint32_t hit_serial_counter_ = 0;   // DAT_007d9b78 (shared with the player's attacks)
private:
    bool id_in_use(int id) const;

    // The per-actor update hook (tick ORDER + the sweep around it are validated). Enemy
    // AI and movement are the handler's — src/cleria/enemy_ai.hpp, run by the view,
    // which holds each enemy's animation script and the room geometry it needs.
    void update_actor(Actor& a);
};

}  // namespace cleria
