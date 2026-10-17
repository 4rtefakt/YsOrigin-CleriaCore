# The reverse-engineering notes moved to yso

The notes that used to live here (`build/*_re/`, `ai_spec/`, `audit/`, `source_map/`, `xso_ops/`, `hud_re2/`
and the `build/*.md` trackers) are now kept, and corrected, in the reverse-engineering repository
[4rtefakt/yso](https://github.com/4rtefakt/yso). Write new findings there.

A path in a comment of this repository translates like this:

| Here | In yso |
|---|---|
| `build/<name>_re/...` | `notes/<name>/...` (`hud_re2/` is `notes/hud2/`) |
| `build/ai_spec/`, `audit/`, `source_map/`, `xso_ops/`, `re_coverage/` | `specs/<same>/` |
| `build/PROGRESS.md` and the other trackers | `status/` |

What stays here: `shell_re/` (CleriaCore's own shell, settings and mod API: the modding guide links to it),
`combat_re/ghidra_scripts/` (the Ghidra scripts the `ghidra-decompile` skill writes and runs), and the two
lists the tools read (`xso_ops/op_handler_map.txt`, `re_coverage/all_functions.txt`). yso's
`tools/collect.py` still copies those, and any note committed here later, into yso.
