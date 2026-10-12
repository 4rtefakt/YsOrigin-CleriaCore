"""The drive cases of the Extra versions and of Adol (build/mode_re/EXTRA_VERSIONS.md, ADOL.md), kept in their own
file; tools/drive_regress.py adds them to its CASES. CLERIA_TYPE=1 boots a room as the Extra version (the Type Select
answer); CLERIA_DMG=1 turns the `[extra]` lines on."""

_X = dict(CLERIA_DRIVE_LOCKSTEP="1", CLERIA_DMG="1")
# Title -> Bonus Modes -> Arena Mode -> the character's bar -> Type Select "Extra" -> Difficulty (Normal) -> row 0.
_ARENA_EXTRA = "1:keys;40:down;45:down;50:ok;100:down;110:ok;{bar}190:ok;240:down;255:ok;330:ok;590:ok;790:quit"

_HUGO = ('5:opt=InfiniteHP=1;8:flag=116,1;9:flag=117,1;10:flag=118,1;11:flag=182,1;12:flag=183,1;13:flag=184,1;100:bracelet=116;11'
         '0:skill+;112:skill-;115:pos;200:near=17;201:atk;203:bullets;260:bracelet=117;265:flagdump;300:skill+;302:skill-;330:near'
         '=17;331:atk;420:pos;500:bracelet=118;505:flagdump;560:quit')


def cases(clear_data):
    c = {}
    # EXTRA_VERSIONS.md §2 / §5, Yunica Extra in a story room (S_1009, mode 0). Ventus (0x10d): three kind-6 shots
    # of effect 0x66 on the section's 12th tick; a save / load round trip keeps the version (the block's 0x76c21c);
    # Terra (0x10c): the kind-0x17 runner of effect 118, C 6 P 3, 1.5 ahead, pulsing every 4th tick (the room's
    # edge ends it at its fourth pulse); charged with a Lv 2 bracelet: four shots (0x110), the runner 3.5 ahead
    # with P 5 (0x10f).
    c["extra_yunica_story_skills"] = dict(
        room="S_10/S_1009/S_1009", env=dict(_X, CLERIA_TYPE="1"),
        drive="8:bracelet=116;9:flag=116,1;10:flag=182,2;11:flag=117,1;12:flag=183,2;43:skill+;45:skill-;60:bullets;90:saveload;"
              "120:bracelet=117;130:skill+;132:skill-;200:bracelet=116;210:mp=3000;220:skill+;350:skill-;420:bracelet=117;"
              "425:mp=3000;430:skill+;560:skill-;640:quit",
        expect=["the Extra version is in play (character 1)", "Yunica: 3 shots of 0x66 (section 0x10d)",
                "[bullet] kind 6 effect 102", "[drive] loaded S_1009", "runner: effect 118 C 6 P 3, 1.50 ahead",
                "runner pulse: effect 118", "Yunica: 4 shots of 0x66 (section 0x110)", "runner: effect 118 C 6 P 5, 3.50 ahead"],
        count={"[bullet] kind 6 effect 102": 3, "the Extra version is in play": 1})
    # ... and the normal version in the same room with the same inputs: none of it.
    c["extra_yunica_story_normal"] = dict(
        room="S_10/S_1009/S_1009", env=dict(_X),
        drive="8:bracelet=116;9:flag=116,1;10:flag=182,2;11:flag=117,1;12:flag=183,2;43:skill+;45:skill-;47:abil;60:bullets;"
              "120:bracelet=117;130:skill+;132:skill-;134:abil;200:quit",
        expect=["[abil] t47 sec 0x10d", "[abil] t134 sec 0x10c"], absent=["[extra]", "[bullet] kind 6"])
    # EXTRA_VERSIONS.md §1.1: Type Select is offered to a story New Game too (the Extra bought in record.ta2): New
    # Game -> Yunica -> Type Select "Extra" -> Difficulty -> the opening (skipped
    # into its first playable scene), as the Extra version.
    c["extra_yunica_story_new_game"] = dict(
        room=None, env=dict(_X), records={"record.ta2": clear_data((1, 0, 0), shop=[2])},
        title="40:ok;100:ok;150:down;165:ok;240:ok", drive="5:opt=CutsceneSkip=1;20:every=20,200,skiptap;220:quit",
        expect=["[content] event new_game", ">> entering S_0002", "the Extra version is in play (character 1)"])
    # ... and an Arena run (Stage 1, Normal) picked through Type Select: the Ventus volley and the Terra runner hit
    # the first wave; the five pulses on the arena's flat floor; the run records into record.are.
    c["extra_yunica_arena"] = dict(
        room=None, env=dict(_X, CLERIA_NYA="1"),
        records={"record.ta2": clear_data((1, 1, 1), tiers=[(2, 0)], shop=[2]), "record.ar2": [0]},
        title=_ARENA_EXTRA.format(bar=""),
        drive="11:opt=DamageTaken=0;100:bracelet=116;130:nearest=1.2;131:skill+;133:skill-;200:bracelet=117;230:nearest=2.5;"
              "231:skill+;233:skill-;300:opt=OneHitKill=1;310:every=9,2600,nearest=1.2;313:every=9,2600,atk;700:every=700,2600,killall",
        expect=["[stage] Arena stage select: Yunica", "the run starts in S_9109", "the Extra version is in play (character 1)",
                "Yunica: 3 shots of 0x66 (section 0x10d)", "hit Riffligan", "runner: effect 118 C 6 P 3, 1.50 ahead",
                "[arena] the run ends: record.ar + record.are saved"],
        count={"runner pulse: effect 118": 5}, files=["record.are"])
    # EXTRA_VERSIONS.md §3, Hugo Extra in a story room (S_1002, a Selnarg = cast 17). The Force Shield lasts 420
    # (two ticks in: 418); his eyes' plain shots (effect 130) fly 0.3 x 1.35; with the Terra bracelet flag 157 = 2
    # (the eyes' rope), with Ignis 1 (the wheel); the uncharged Trap Mine's fuse is 180 (laid at t303, the thunder
    # 0x88 at t483), and an attack meanwhile makes the laid eye fire the plain shot itself (its section outranks 7).
    c["extra_hugo_story"] = dict(
        room="S_10/S_1002/S_1002", char="2", env=dict(_X, CLERIA_TYPE="1"), drive=_HUGO,
        expect=["the Extra version is in play (character 2)", "shield 1/1/418", "fires 0x82 (class 3)", "speed 0.41", " 157=2 ",
                "section 7 refused: the direct shot", "t483 #", "fires 0x88 (class 2)", " 157=1 "],
        count={"refused: the direct shot": 1})
    # ... and the normal version, the same inputs: 360 ticks, speed 0.30, flag 157 untouched, the fuse 60 (the
    # thunder at t363), no shot from the laid eye.
    c["extra_hugo_story_normal"] = dict(
        room="S_10/S_1002/S_1002", char="2", env=dict(_X), drive=_HUGO,
        expect=["shield 1/1/358", "fires 0x82 (class 3)", "speed 0.30", "t265 flagdump", "t363 #", "fires 0x88 (class 2)", "t505 flagdump"],
        absent=["[extra]", "the direct shot", " 157="])
    # ... and an Arena run (Stage 1) picked through Type Select: the faster shots clear the waves.
    c["extra_hugo_arena"] = dict(
        room=None, env=dict(_X, CLERIA_NYA="1"),
        records={"record.ta2": clear_data((1, 1, 1), tiers=[(2, 0)], shop=[3]), "record.ar2": [0]},
        title=_ARENA_EXTRA.format(bar="158:down;"),
        drive="11:opt=DamageTaken=0;130:nearest=3;131:atk;133:bullets;300:opt=OneHitKill=1;310:every=9,2600,nearest=1.2;"
              "313:every=9,2600,atk;700:every=700,2600,killall",
        expect=["[stage] Arena stage select: Hugo", "the run starts in S_9109", "the Extra version is in play (character 2)",
                "speed 0.41", "[arena] the run ends: record.ar + record.are saved"], files=["record.are"])
    # EXTRA_VERSIONS.md §4, Toal Extra in a story room (S_1009). Ventus (0x10d), human form: the demon kick effect
    # 174 on the section's first tick; Ignis (0x10e): the fire effect 162 again every 12 ticks at 1.1; Terra (0x10c):
    # from tick 12 a random kind-2 bullet of effect 167 every 3rd tick, the runner (C 4, P 4, 0.5 ahead) at tick 18 —
    # the statue's collision ends it at its second pulse.
    c["extra_toal_story"] = dict(
        room="S_10/S_1009/S_1009", char="3", env=dict(_X, CLERIA_TYPE="1"),
        drive="5:opt=InfiniteHP=1;8:flag=116,1;9:flag=117,1;10:flag=118,1;11:flag=182,1;12:flag=183,1;13:flag=184,1;40:bracelet=116;"
              "50:skill+;52:skill-;120:bracelet=118;130:skill+;132:skill-;220:bracelet=117;225:mp=3000;230:skill+;232:skill-;"
              "250:bullets;330:quit",
        expect=["the Extra version is in play (character 3)", "t53 Toal: effect 174 x 1.0 (section 0x10d)",
                "t133 Toal: effect 162 x 1.1 (section 0x10e)", "t145 Toal: effect 162 x 1.1", "[bullet] kind 2 effect 167",
                "t250 runner: effect 167 C 4 P 4, 0.50 ahead", "runner pulse: effect 167", "runner gone at", "a wall"],
        count={"Toal: effect 174": 1})
    c["extra_toal_story_normal"] = dict(
        room="S_10/S_1009/S_1009", char="3", env=dict(_X),
        drive="5:opt=InfiniteHP=1;8:flag=116,1;9:flag=117,1;10:flag=118,1;11:flag=182,1;12:flag=183,1;13:flag=184,1;40:bracelet=116;"
              "50:skill+;52:skill-;54:abil;120:bracelet=118;130:skill+;132:skill-;134:abil;220:bracelet=117;225:mp=3000;230:skill+;"
              "232:skill-;234:abil;250:bullets;330:quit",
        expect=["[abil] t54 sec 0x10d", "[abil] t134 sec 0x10e", "[abil] t234 sec 0x10c"], absent=["[extra]", "effect 167"])
    # ... and an Arena run (Stage 1) picked through Type Select: the Terra runner's four pulses on the flat floor
    # (ticks 1, 6, 11, 16 of its 16) and its sparks hit the first wave.
    c["extra_toal_arena"] = dict(
        room=None, env=dict(_X, CLERIA_NYA="1"),
        records={"record.ta2": clear_data((1, 1, 1), tiers=[(2, 0)], shop=[4]), "record.ar2": [0]},
        title=_ARENA_EXTRA.format(bar="158:down;166:down;"),
        drive="11:opt=DamageTaken=0;100:bracelet=117;130:nearest=2.5;131:skill+;133:skill-;300:opt=OneHitKill=1;"
              "310:every=9,2600,nearest=1.2;313:every=9,2600,atk;700:every=700,2600,killall",
        expect=["[stage] Arena stage select: Toal", "the run starts in S_9109", "the Extra version is in play (character 3)",
                "runner: effect 167 C 4 P 4, 0.50 ahead", "runner gone at", "its life",
                "[arena] the run ends: record.ar + record.are saved"],
        count={"runner pulse: effect 167": 4}, files=["record.are"])
    # ADOL.md (Felghana version), a room boot as character 0 (CLERIA_CHAR=0; retail offers him in Time Attack and the
    # Arena only): C_101.ASO on C_110.AIA (his script names no file), the six-step ground combo (0x105 is its last
    # section), the skills at his costs 1000 / 600 / 300 (FUN_0046ea00's row 0), Ignis = his script's FireBullet(2, 20).
    c["adol_felghana_room"] = dict(
        room="S_10/S_1009/S_1009", char="0", env=dict(_X),
        drive="8:flag=116,1;9:flag=117,1;10:flag=118,1;11:flag=182,1;12:flag=183,1;13:flag=184,1;30:atk;45:atk;60:atk;75:atk;90:atk;"
              "105:atk;120:abil;150:bracelet=116;160:skill+;162:skill-;164:abil;220:bracelet=117;225:mp=3000;230:skill+;232:skill-;"
              "234:abil;300:bracelet=118;305:mp=3000;310:skill+;312:skill-;314:abil;330:bullets;340:quit",
        expect=["-> C_110.AIA", "[abil] t120 sec 0x105", "[abil] t164 sec 0x10d mp 2000", "[abil] t234 sec 0x10c mp 2400",
                "[abil] t314 sec 0x10e mp 2700", "[bullet] kind 2 effect 20"], absent=["[extra]"])
    # ... an Arena run from the title: the fourth bar (Felghana bought), Type Select "Felghana", Stage 1; the pilot
    # clears the eight waves; the record goes to record.ar (Felghana is the first version).
    c["adol_felghana_arena"] = dict(
        room=None, env=dict(_X, CLERIA_NYA="1"),
        records={"record.ta2": clear_data((1, 1, 1), tiers=[(2, 0)], shop=[0]), "record.ar2": [0]},
        title="1:keys;40:down;45:down;50:ok;100:down;110:ok;158:down;166:down;174:down;190:ok;255:ok;330:ok;590:ok;790:quit",
        drive="10:opt=OneHitKill=1;11:opt=DamageTaken=0;130:every=9,3000,nearest=1.2;133:every=9,3000,atk;700:every=700,3000,killall",
        expect=["[title] Arena Mode: Character Select", "[stage] Arena stage select: Adol, column 1, 1 row(s), cursor 0",
                "[stage] row 0 (Wailing Blue): the run starts in S_9109", "-> C_110.AIA", "[script] SetLevel(1): HP 60",
                "[arena] Arena(cast 53, level 8, count 0): 8 spawned", "[arena] ArenaSave: SP ", "(a record)",
                "[arena] the run ends: record.ar + record.are saved", "[stage] Arena stage select: Adol"],
        count={"[arena] Arena(": 13}, absent=["[gameover]", "not implemented"], files=["record.ar"])
    # ... and Time Attack (his 14 rows): the Boss Rush's first stage won by the pilot, the second entered; the death
    # there and its Retry keep him Adol (the snapshot's character 0).
    c["adol_felghana_time_attack"] = dict(
        room=None, env=dict(_X, CLERIA_NYA="1"),
        records={"record.ta2": clear_data((1, 1, 1), tiers=[(2, 0)], shop=[0])},
        title="1:keys;40:down;45:down;50:ok;100:ok;158:down;166:down;174:down;190:ok;255:ok;330:ok;590:ok;790:quit",
        drive="10:opt=OneHitKill=1;11:opt=DamageTaken=0;100:every=7,900,duel=0;130:every=9,900,nearest=1.2;133:every=9,900,atk;"
              "1000:hp=0;1160:ok;1300:stats;1301:flagdump;1310:quit",
        expect=["[stage] Time Attack stage select: Adol, column 1, 14 row(s), cursor 0", "the run starts in S_1080", "-> C_110.AIA",
                "stage 1 (Vagullion) won", ">> entering S_1099", "[death] HP 0", "[gameover] chose retry", ">> entering S_1099",
                "t1300 stats", "t1301 flagdump"],
        absent=[" 150="])           # flagdump lists the nonzero flags: 150 (the character) stays 0
    return c
