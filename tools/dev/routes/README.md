The route audit of floors 4-7 (build/audit/ROUTE_F47_HUGO.md, the section "The room walks"). Data goes to
CLERIA_ROUTE_DIR (default: this folder); the game is the worktree's build_diff/Release/cleria-view.exe, assets from
CLERIA_ASSETS.

    python jump.py        # the reach table: jump.json (a running jump per character, second press, dash)
    python collect.py     # every room: exits, objects, the collision mesh after the room is cleared -> rooms/, rooms.json
    python table.py       # nav.py over each mesh: per pair of doors / objects the hardest gate -> table.json, rooms/*.nav
    python route.py       # the global search (over.py: the links the static pass cannot see) -> route.json
    python docgen.py      # the docs' table (walked.py: the "walked" column)

Looking at one room: map.py (top-down heights), sec.py (layers and walls along a segment), objs.py (object boxes,
what moved), why.py (a "no way": the nearest missing links), jl.py (a pair's jumps and a hops= string).
