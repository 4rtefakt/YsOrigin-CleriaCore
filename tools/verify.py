#!/usr/bin/env python3
"""The standard verification of the build_diff tree, with one summary at the end.

    python tools/verify.py            # before a push: the Release build, the whole ctest suite (-j8, cotest as
                                      # its shards), the goldens, every drive case (drive_regress.py --jobs)
    python tools/verify.py --smoke    # per commit: the Release build, ctest without cotest, the goldens,
                                      # the smoke drive tier (drive_regress.py --smoke)

The game data: CLERIA_ASSETS, else ./assets (a worktree has none). ctest runs on the asset root the tree
was configured with (CLERIA_TEST_ASSETS). Every step runs even after a failure; exit 1 if any failed.
"""
import os, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.environ.get("CLERIA_ASSETS") or os.path.join(ROOT, "assets")
TOOL = lambda t: os.path.join(ROOT, "tools", t)


def main():
    smoke = "--smoke" in sys.argv[1:]
    exe = os.path.join(ROOT, "build_diff", "Release", "cleria-view" + (".exe" if os.name == "nt" else ""))
    steps = [
        ("build", ["cmake", "--build", "build_diff", "--config", "Release", "--parallel"]),
        ("ctest", ["ctest", "--test-dir", "build_diff", "-C", "Release", "-j8", "--output-on-failure"]
                  + (["-E", "cotest"] if smoke else [])),
        ("goldens", [sys.executable, TOOL("goldens.py"), "compare"]),
        ("drive", [sys.executable, TOOL("drive_regress.py"), "--exe", exe, "--assets", ASSETS]
                  + (["--smoke"] if smoke else [])),
    ]
    env = dict(os.environ, CLERIA_ASSETS=ASSETS)
    done = []
    for name, argv in steps:
        print(f"==== {name}: {' '.join(argv)}", flush=True)
        t0 = time.time()
        code = subprocess.run(argv, cwd=ROOT, env=env).returncode
        done.append((name, code, time.time() - t0))
    print("==== verify" + (" --smoke" if smoke else ""))
    for name, code, secs in done:
        print(f"{'ok  ' if code == 0 else 'FAIL'} {name:8s} {secs:6.0f}s")
    sys.exit(1 if any(code for _, code, _ in done) else 0)


if __name__ == "__main__":
    main()
