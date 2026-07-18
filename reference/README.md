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
| `title_main.png` | Title menu (New Game / Load / Bonus Modes / Options / Exit). **New Game is the selected row** — use it to check the exact cursor/text position & colour. |
| `title_bonus.png` | Bonus Modes sub-list (Time Attack / Arena / Bonus Shop / Back) |
| `charselect.png` | Character Select (portrait + 3 bars + banner + description) |
| `charselect_hugo.png`, `charselect_toal.png` | …with a different character highlighted |
| `charselect_arena_adol.png` | Char Select in Arena mode — the 4th/Adol row is present ("[Adol] Fiery-haired swordsman. Abilities unknown.") |
| `typeselect.png` | Type Select popup (Normal / Extra / Cancel), DIALOG0 9-slice frame |
| `typeselect_adol.png` | Adol's Type Select — **Felghana / Napishtim / Cancel** (his two skins) |
| `difficulty.png` | Difficulty popup (Easy … Nightmare) |
| `spbonus.png` | SP Bonus Acquisition popup (Enable / Disable) |
| `options.png` | Options / Config screen |
| `bonusshop.png` | Bonus Shop (Extra-skin unlock) |

### In-game pause book (`go_wndmain2`, framework B) — the ornate two-page book
| file | screen |
|---|---|
| `pause_equip.png` | Equipment page |
| `pause_inventory.png` | Item / Inventory page |
| `pause_load.png` | Load (save-slot list) page |
| `pause_topic.png` | Topic page (top level) |
| `pause_topic_character.png` | Topic → Character roster list (1. Yunica Tovah, 2. Hugo Fact, …) |
| `pause_topic_character_page.png` | Topic → a character bio spread (portrait + name + blurb, two per page) |
| `pause_topic_manual_basic.png` | Topic → Manual: "Basic Actions" (Attack / Jump / Power Shot / Jump Shot), 13 tabs |
| `pause_topic_manual_boost.png` | Topic → Manual: "Boost Mode" |
| `pause_topic_manual_dropitem.png` | Topic → Manual: "Drop Item" |
| `pause_topic_manual_equip.png` | Topic → Manual: "Equip" |
| `pause_topic_manual_inventory.png` | Topic → Manual: "Inventory" |

Anything else: a short descriptive lowercase name is fine. When in doubt, add a line to the table.

## What's most useful to match
Exact **positions, sizes, and the font** — that's the layout RE this folder feeds
(`build/menu_re/MENU_LAYOUT.md`). A clean full-screen grab at a known resolution lets us read
element coordinates directly.
