"""ta_matrix.py <cleria-view.exe> [char...]: every Time Attack stage of each character (1 Yunica, 2 Hugo, 3 Toal; default
all three; 0 Adol's Felghana version, 4 his Napishtim version) entered alone from a title boot's stage select. One line a stage: the room, @Flag_Inv's level and HP, flag
156 at each `dflags`, the scripts seen, the tick the stage was won at. Full logs: <scratch>/ta_matrix/c<char>_r<row>.log.
The game-side drive is DRIVE (default: the state at t300 and t900; "400:flag=156,0;570:scripts;571:dflags" forces the
fight's end to see the post-win path). Scratch = CLERIA_DEV_SCRATCH (default: the system temp folder); assets =
CLERIA_ASSETS; JOBS runs at a time (default 3)."""
import concurrent.futures, os, re, subprocess, sys, tempfile
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import drive_regress as dr
EXE = os.path.abspath(sys.argv[1])
ASSETS = os.environ.get("CLERIA_ASSETS", dr.ASSETS)
OUT = os.path.join(os.environ.get("CLERIA_DEV_SCRATCH", tempfile.gettempdir()), "ta_matrix")
DRIVE = os.environ.get("DRIVE", "300:dflags;301:scripts;900:dflags;901:scripts;910:quit")
COUNT = {0: 14, 1: 13, 2: 13, 3: 14, 4: 14}
SHOP = [0, 1]                                    # both Adol versions bought: his bar, and Type Select's two answers
os.makedirs(OUT, exist_ok=True)


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
        open(os.path.join(saves, "record.ta2"), "wb").write(dr.record_file(dr.clear_data((1, 1, 1), tiers=[(2, 0)], shop=SHOP if ch in (0, 4) else [])))
        # A keyboard-only session (`keys`: no row hover from the pointer the book's cursor warp moved); Bonus Modes,
        # Time Attack, the character's bar, Difficulty's default; down to the row.
        title = title_to_row("100:ok;", ch, row) + "900:quit"
        env = dict(os.environ, CLERIA_MUTE="1", CLERIA_DRIVE=DRIVE, CLERIA_SAVE_DIR=saves, CLERIA_SHOT_DIR=saves,
                   CLERIA_DRIVE_LOCKSTEP="1", CLERIA_TITLE_DRIVE=title)
        if ch in (0, 4): env["CLERIA_NYA"] = "1"         # his bar needs the data.nya gate
        try:
            r = subprocess.run([EXE, ASSETS], env=env, capture_output=True, text=True, timeout=300)
            out, code = r.stdout + r.stderr, r.returncode
        except subprocess.TimeoutExpired as e:
            out, code = (e.stdout or "") + "\nTIMEOUT", -1
    open(os.path.join(OUT, "c%d_r%02d.log" % (ch, row)), "w", encoding="utf-8", errors="replace").write(out)
    g = lambda p: re.findall(p, out)
    return "c%d row %2d %s level/HP %s 156=%s won=%s exit=%d scripts=%s" % (
        ch, row, g(r"\[stage\] (row \d+ \([^)]*\)): the run starts in (\S+)"), g(r"SetLevel\((\d+)\): HP (\d+)"), g(r" 156=(\d)"),
        g(r"\[run\] t(\d+) stage"), code, sorted(set(g(r"script (\S+) pc"))))


chars = [int(a) for a in sys.argv[2:]] or [1, 2, 3]
with concurrent.futures.ThreadPoolExecutor(int(os.environ.get("JOBS", "3"))) as pool:
    for line in pool.map(lambda j: run(*j), [(c, r) for c in chars for r in range(1, COUNT[c])]):
        print(line, flush=True)
