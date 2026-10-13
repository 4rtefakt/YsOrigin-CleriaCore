"""arena_matrix.py <cleria-view.exe> [char...] [rows=0,1,..] [diff=n]: every Arena stage of each character (1 Yunica,
2 Hugo, 3 Toal; default all three; 0 Adol's Felghana version, 4 his Napishtim version; rows 0..6) entered from a title boot's stage select and played by the pilot. One line
a stage: the room, InitArena's level and HP, the 0x18E ops run / of the stage's script (ARENA.md §2.4: 13 13 14 20 18
22 3), the monsters spawned, the frames the stage was cleared in, the SP banked, the exit code. Full logs:
<scratch>/arena_matrix/c<char>_r<row>.log. `diff=n`: n more downs on the Difficulty list (0 = its default, Normal).
The game-side drive is DRIVE (default: one-hit kills, no damage taken, `nearest` + `atk` every 9 ticks to END, default
9000, then `stats` and the live scripts; "150:every=40,END,killall" clears the waves without a fight — and without SP).
SWEEP=n adds a `killall` every n ticks: the ground pilot cannot reach the fliers (STUCK lists the casts left alive).
Scratch = CLERIA_DEV_SCRATCH (default: the system temp folder); assets = CLERIA_ASSETS; JOBS runs at a time (default 3)."""
import concurrent.futures, os, re, subprocess, sys, tempfile
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import drive_regress as dr
EXE = os.path.abspath(sys.argv[1])
ASSETS = os.environ.get("CLERIA_ASSETS", dr.ASSETS)
OUT = os.path.join(os.environ.get("CLERIA_DEV_SCRATCH", tempfile.gettempdir()), "arena_matrix")
END = int(os.environ.get("END", "9000"))
DRIVE = os.environ.get("DRIVE", "10:opt=OneHitKill=1;11:opt=DamageTaken=0;130:every=9,END,nearest=1.2;133:every=9,END,atk").replace("END", str(END))
if os.environ.get("SWEEP"): DRIVE += ";%s:every=%s,%d,killall" % (os.environ["SWEEP"], os.environ["SWEEP"], END)
DRIVE += ";%d:stats;%d:scripts;%d:actors;%d:quit" % (END + 5, END + 6, END + 7, END + 10)
OPS = [13, 13, 14, 20, 18, 22, 3]
os.makedirs(OUT, exist_ok=True)
args = sys.argv[2:]
kv = dict(a.split("=", 1) for a in args if "=" in a)
chars = [int(a) for a in args if "=" not in a] or [1, 2, 3]
rows = [int(r) for r in kv.get("rows", "0,1,2,3,4,5,6").split(",")]
diff = int(kv.get("diff", "0"))


def title_to_row(mode, ch, row, diff=0):
    """The title drive to a stage select row. Adol (0 Felghana, 4 Napishtim; both bought) is the fourth bar and passes
    Type Select before Difficulty, so his later steps come 60 ticks after the others'."""
    adol = ch in (0, 4)
    t = 60 if adol else 0
    s = "1:keys;40:down;45:down;50:ok;" + mode + "".join("%d:down;" % (150 + 8 * i) for i in range(3 if adol else ch - 1)) + "190:ok;"
    if adol: s += ("240:down;" if ch == 4 else "") + "255:ok;"
    s += "".join("%d:down;" % (250 + t + 4 * i) for i in range(diff)) + "%d:ok;" % (270 + t)
    return s + "".join("%d:down;" % (430 + t + 2 * i) for i in range(row)) + "%d:ok;" % (520 + t)


def run(ch, row):
    with tempfile.TemporaryDirectory() as saves:
        open(os.path.join(saves, "record.ta2"), "wb").write(dr.record_file(dr.clear_data((1, 1, 1), tiers=[(2, 0)], shop=[5, 6, 7, 8, 9, 10] + ([0, 1] if ch in (0, 4) else []))))
        # A keyboard-only session; Bonus Modes, Arena Mode, the character's bar, Difficulty, down to the row.
        title = title_to_row("100:down;110:ok;", ch, row, diff) + "700:quit"
        env = dict(os.environ, CLERIA_MUTE="1", CLERIA_NYA="1", CLERIA_DRIVE=DRIVE, CLERIA_SAVE_DIR=saves, CLERIA_SHOT_DIR=saves,
                   CLERIA_DRIVE_LOCKSTEP="1", CLERIA_TITLE_DRIVE=title)
        try:
            r = subprocess.run([EXE, ASSETS], env=env, capture_output=True, text=True, timeout=900)
            out, code = r.stdout + r.stderr, r.returncode
        except subprocess.TimeoutExpired as e:
            out, code = (e.stdout or "") + "\nTIMEOUT", -1
    open(os.path.join(OUT, "c%d_r%02d.log" % (ch, row)), "w", encoding="utf-8", errors="replace").write(out)
    g = lambda p: re.findall(p, out)
    spawned = g(r"\[arena\] Arena\(cast (\d+), level \d+, count \d+\): (\d+) spawned")
    left = sorted(set(g(r"\[actor\] id \d+ cast (\d+) enemy hp [1-9]")), key=int)
    return "c%d row %d %s level/HP %s ops %d/%d monsters %d cleared=%s banked=%s%s%s exit=%d" % (
        ch, row, g(r"\[stage\] row \d+ \(([^)]*)\): the run starts in (\S+)"), g(r"SetLevel\((\d+)\): HP (\d+)"), len(spawned), OPS[row],
        sum(int(n) for _, n in spawned), g(r"cleared in (\d+) frames"), g(r"ArenaSave: SP (\d+) banked"),
        " STUCK on casts %s" % left if left else "", " no-brain %s" % sorted(set(g(r"no brain[^\n]*"))) if g(r"no brain") else "", code)


with concurrent.futures.ThreadPoolExecutor(int(os.environ.get("JOBS", "3"))) as pool:
    for line in pool.map(lambda j: run(*j), [(c, r) for c in chars for r in rows]):
        print(line, flush=True)
