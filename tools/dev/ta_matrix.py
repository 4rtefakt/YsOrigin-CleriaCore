"""ta_matrix.py <cleria-view.exe> [char...]: every Time Attack stage of each character (1 Yunica, 2 Hugo, 3 Toal; default
all three) entered alone from a title boot's stage select. One line a stage: the room, @Flag_Inv's level and HP, flag
156 at each `dflags`, the scripts seen, the tick the stage was won at. Full logs: <scratch>/ta_matrix/c<char>_r<row>.log.
The game-side drive is DRIVE (default: the state at t300 and t900; "400:flag=156,0;570:scripts;571:dflags" forces the
fight's end to see the post-win path). Scratch = CLERIA_DEV_SCRATCH (default: the system temp folder); assets =
CLERIA_ASSETS; three runs at a time."""
import concurrent.futures, os, re, subprocess, sys, tempfile
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import drive_regress as dr
EXE = os.path.abspath(sys.argv[1])
ASSETS = os.environ.get("CLERIA_ASSETS", dr.ASSETS)
OUT = os.path.join(os.environ.get("CLERIA_DEV_SCRATCH", tempfile.gettempdir()), "ta_matrix")
DRIVE = os.environ.get("DRIVE", "300:dflags;301:scripts;900:dflags;901:scripts;910:quit")
COUNT = {1: 13, 2: 13, 3: 14}
os.makedirs(OUT, exist_ok=True)


def run(ch, row):
    with tempfile.TemporaryDirectory() as saves:
        open(os.path.join(saves, "record.ta2"), "wb").write(dr.record_file(dr.clear_data((1, 1, 1), tiers=[(2, 0)])))
        # A keyboard-only session (`keys`: no row hover from the pointer the book's cursor warp moved); Bonus Modes,
        # Time Attack, the character's bar, Difficulty's default; down to the row.
        title = ("1:keys;40:down;45:down;50:ok;100:ok;" + "".join("%d:down;" % (150 + 8 * i) for i in range(ch - 1)) + "190:ok;270:ok;" +
                 "".join("%d:down;" % (430 + 2 * i) for i in range(row)) + "520:ok;900:quit")
        env = dict(os.environ, CLERIA_MUTE="1", CLERIA_DRIVE=DRIVE, CLERIA_SAVE_DIR=saves, CLERIA_SHOT_DIR=saves,
                   CLERIA_DRIVE_LOCKSTEP="1", CLERIA_TITLE_DRIVE=title)
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
with concurrent.futures.ThreadPoolExecutor(3) as pool:
    for line in pool.map(lambda j: run(*j), [(c, r) for c in chars for r in range(1, COUNT[c])]):
        print(line, flush=True)
