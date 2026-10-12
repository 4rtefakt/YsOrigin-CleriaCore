"""The drive cases of the Extra versions and of Adol (build/mode_re/EXTRA_VERSIONS.md, ADOL.md), kept in their own
file; tools/drive_regress.py adds them to its CASES. CLERIA_TYPE=1 boots a room as the Extra version (the Type Select
answer); CLERIA_DMG=1 turns the `[extra]` lines on."""

_X = dict(CLERIA_DRIVE_LOCKSTEP="1", CLERIA_DMG="1")
# Title -> Bonus Modes -> Arena Mode -> the character's bar -> Type Select "Extra" -> Difficulty (Normal) -> row 0.
_ARENA_EXTRA = "1:keys;40:down;45:down;50:ok;100:down;110:ok;{bar}190:ok;240:down;255:ok;330:ok;590:ok;790:quit"


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
    return c
