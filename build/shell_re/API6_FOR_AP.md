# Mod API 6 for the Archipelago mod: a note to act on (2026-10-10)

For the session that owns `ysorigin.archipelago`. The reference is LUA_API.md §3, §4.1, §5.2 and §11; this is the
short version and the EXP rule the user asked for. Nothing here changes the mod's sources: `samples/mods/archipelago_lua`
is untouched and behaves as before on an API 6 build (`exp_factor` still works).

## 1. Why a kill gave 16 EXP at x100

The engine's order for a kill [CODE `src/cleria/exp_award.cpp` `kill_award` / `level_scale` = FUN_004fbb40
0x4fcb45..0x4fcc13, FUN_004fa2f0; `apps/cleria-view/view_game.inl` `award_exp`]:

1. `base`: the monster's EXP (ENEMYINFO).
2. the level difference: the player above the monster by `d` levels -> `base / F^d`, F = 1.5 Normal, **1.65 Hard**,
   1.8 Nightmare (below it: `base x 1.5^min(d, 3)`); then **x 0.6666**.
3. **x rate**: retail's 0x76a5fc (1.0). CleriaCore puts the Cheats' EXP rate and the mods' `exp_factor(level)` here.
4. x 1.1 on Easy, x 2 on Very Easy.
5. **floor, then at least 1**.
6. x (1 + 0.01 x combo): the hit combo meter, so the EXP total is a float (a kill adds e.g. 14.14).

`exp_factor` multiplies at step 3, **before** the floor of step 5. So the multiplier scales the true fraction, not the
floored 1: a low-floor monster whose step 2 result is 0.16 gives `floor(0.16 x 100) = 16` at x100 (and exactly 1 with
no multiplier, where "at least 1" does the work). It is not 100 x 1, and the "at least 1" rule can never help once the
factor is large. Reproduced in the drive case `lua_exp_floor` (tools/drive_regress.py): Hard, level 13 against a
level-3 M_130 (cast 17, base 33): 33 / 1.65^10 x 0.6666 = 0.147 -> x100 -> floor 14 -> x1.01 = 14.14. The mod could not fix
this through `exp_factor(level)`: it never saw the kill.

## 2. The rule: with multiplier M, every kill gives at least M / 2

```lua
-- in main.lua, beside the existing cleria.game.exp_factor(...) (keep that call as it is)
if cleria.combat ~= nil then                      -- API 6 (or: cleria.api_version >= 6)
    cleria.combat.add_filter("exp", function(q)
        if q.source ~= "kill" then return nil end -- bosses: untouched
        local m = q.factor                        -- what exp_factor returned for this kill (the product of all mods')
        if m <= 1 then return nil end             -- no multiplier: the game's own EXP
        return math.max(q.value, m / 2)           -- 50 at x100, 10 at x20
    end)
end
```

- `q.factor` is the factor of step 3 for this very kill, so the rule follows whatever `exp_factor` answered (the
  player's `ExpMultiplier` setting or the seed's scaling by level) without a second copy of that logic. If the floor
  should follow the seed's base multiplier rather than the level-scaled one, use that number instead of `q.factor`.
- `q.value` is the engine's final award (step 6); returning a number replaces it. The level-up loop runs after.
- The query also carries `base`, `scaled`, `rate` (the Cheats'), `diff_mult`, `floored`, `combo`, `chain`, `level`,
  `enemy_level`, `cast`: enough for any other rule (e.g. a floor only for monsters within N levels).
- The filter is not asked for an award of 0 (a revived monster killed a second time), so the floor cannot be farmed
  on revivers.
- On an older build `cleria.combat` is `nil`: the guard keeps the mod loading there.

Tested: `lua_exp_floor` runs this rule (as `math.max(q.value, M / 2)` with M = 100) and asserts the kill credits 50
(EXP 21936 -> 21986); `lua_exp_plain` is the same kill untouched (1.01).

## 3. What else API 6 adds

Feature test: `cleria.api_version >= 6`, or the function for `nil` (`cleria.combat`, `cleria.player.stats`, ...).
`ApiVersion=3` in mod.ini stays enough.

| | |
|---|---|
| `cleria.combat.add_filter(kind, fn)` | `kind` = `"exp"`, `"sp"`, `"damage"`, `"drop"`, `"pickup"`; `fn(q)` returns `nil` / `"pass"`, `"suppress"`, a number (the value instead) or `{action = "replace", value =, item =, ticks =}` |
| `"sp"` | every SP gain: `q.source` = `"kill"` / `"boss"` / `"pickup"` / `"give"` / `"script"`, `q.base`, `q.value`, `q.cast`. An SP chest's AddPlayerSP arrives as `"script"`: the mod's "took back the vanilla 5000 SP" could become a `"suppress"` here |
| `"damage"` | `q.dir` = `"dealt"` / `"taken"`, `q.cast`, `q.level`, `q.atk`, `q.power`, `q.def`, `q.formula`, `q.value`: damage traps or handicaps without touching HP |
| `"drop"` | a monster's drop that passed its roll: `q.item`, `q.slot`, `q.chance`, `q.cast`, `q.level`; suppress, or replace `item` and give the pickup its own `value` |
| `"pickup"` | a pickup being taken: `q.item`, `q.what` (`"sp"`, `"hp"`, `"atk_buff"`, `"def_buff"`, `"mp_buff"`, `"boost"`, `"oxygen"`, `"item"`), `q.value`, `q.ticks`; replace or suppress |
| events `enemy_killed`, `pickup_collected` | `e.cast`, `e.level`, `e.exp`, `e.sp`, `e.x/y/z`; `e.item`, `e.what`, `e.value`, `e.ticks`, `e.suppressed` |
| `cleria.player.stats()` | level, exp, exp_next, hp, max_hp, atk, def (+ `base_*`), mp, max_mp, boost, the three buffs and their ticks, x, y, z |
| `cleria.player.set_modifier(stat, add, mul)`, `modifier(stat)` | `"max_hp"`, `"atk"`, `"def"`: (the game's + add) x mul at every recompute; kept through level-ups and loads, not saved (set it again on `load` / `new_game` from the mod's save block) |
| `cleria.player.add_exp(n)` | EXP through the real level-up loop (an "EXP" item, a filler) |
| `cleria.game.spawn_pickup(item, x, y, z [, {value = n}])` | a collectable drop at a point |

Limits to know: the filters run inside the game tick (keep them cheap; an error stops the mod as any call does); a
non-finite answer is ignored; values are clamped (EXP 0..9999999, SP and damage 0..999999). Luck has no modifier
(nothing in the game reads it yet). Details and provenance: LUA_API.md §5.2.
