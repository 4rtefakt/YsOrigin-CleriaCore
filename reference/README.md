# Reference screenshots (local, not committed)

Drop real-game screenshots of the title/menus/UI here as ground truth for CleriaCore's
reimplementation. **The images themselves are Falcom's copyrighted UI and are gitignored**
(same policy as `assets/`); only this README is tracked, so the convention is discoverable.
The shots live on this machine and are used by:

- **Claude, after context compaction** — re-read the relevant file instead of relying on memory.
- **RE / layout agents** — point them at `reference/<name>.png` as the target to match.

## How to capture
- Full-resolution, native aspect (a 16:9 grab is ideal). PNG preferred.
- One screen per file. If a screen has states (cursor on different items, a popup open), a few
  variants are welcome — suffix them (`_hugo`, `_normal`, `_open`).
- The Archipelago mod overlays (top-right "not connected…", the `press F8` line, the little AP
  flag, `[F6] to open AP chat`) are **not** part of the real UI — ignore them / crop if easy.

## Naming convention
| file | screen |
|---|---|
| `title_main.png` | Title menu (New Game / Load / Bonus Modes / Options / Exit) |
| `title_bonus.png` | Bonus Modes sub-list (Time Attack / Arena / Bonus Shop / Back) |
| `charselect.png` | Character Select (portrait + 3 bars + banner + description) |
| `charselect_hugo.png`, `charselect_toal.png` | …with a different character highlighted |
| `typeselect.png` | Type Select popup (Normal / Extra / Cancel) |
| `difficulty.png` | Difficulty popup (Easy … Nightmare) |
| `spbonus.png` | SP Bonus Acquisition popup (Enable / Disable) |
| `options.png` | Options / Config screen |
| `bonusshop.png` | Bonus Shop (Extra-skin unlock) |
| `arena.png` | Arena Mode |
| `pause_*.png` | In-game pause book pages (Equip / Item / Map / Save / Load / Option) |

Anything else: a short descriptive lowercase name is fine. When in doubt, add a line to the table.

## What's most useful to match
Exact **positions, sizes, and the font** — that's the layout RE this folder feeds
(`build/menu_re/MENU_LAYOUT.md`). A clean full-screen grab at a known resolution lets us read
element coordinates directly.
