# Game data: the NI/NA archives, locating the install, the data folder

CleriaCore reads the user's own Ys Origin install. `src/cleria/ni_archive` reads the game's
archives natively; `src/cleria/gamedata` finds and validates an install and unpacks it into
CleriaCore's data folder. `apps/cleria-gamedata` is the command-line front end and
`apps/cleria-gamedatatest` is the test. Together they replace `tools/extract_assets.py`, which wraps
`YsOrigin-Archipelago/tools/ni_unpack.py`.

Evidence tags: **[DATA]** = checked against the files on disk (the Steam build, `yso_win.exe`
2024-01-08 "smallfix", archives dated 2026-06-27). **[BYTES]** = strings in `yso_win.exe`.
**[H]** = inference. The archive format is [DATA]-verified because the native unpack reproduces
every file in the Python-extracted `assets/` byte for byte, except one known extractor defect (§5).

---

## 1. Files in `release\`

| file | what |
|---|---|
| `data.ni` / `data.na` | the base archive: index / data. 15,945 entries; the `.na` is 1,357,081,806 bytes [DATA] |
| `data_us.ni` / `data_us.na` | the English overlay: 813 entries; the `.na` is 84,645,759 bytes [DATA] |
| `data.nu`, `data_us.nu` | one 20-byte record per TOC entry, in TOC order: `{u32 hash, SYSTEMTIME}` (the file timestamps, 2006..2022). `data.nu` is 318,900 bytes = 15,945 × 20 [DATA]. The unpacker does not need these. |
| `data.nya` | 1,024 bytes of high-entropy data, dated 2024-08-18 (older than the rest). Not referenced as a string in the exe. **[UNKNOWN]**, not needed. |
| `music\YSO_0xx.ogg`, `music\bgmdef.txt`, `music\bgmdef2.txt` | loose BGM (`release\music\` and `release\music\bgmdef.txt` are exe strings [BYTES 0x292bec, 0x297a8c]) |
| `yso_*.avi`, `yso_ins0[1-3].dat`, `yso_ed0[12].dat` | the movies (`build/event_re/MOVIES.md`) |
| `yso_ins04.dat` | the Felghana menu font (`find_felghana`) |

The exe's archive strings are `release\data_us.na`, `data_fr`, `data_de`, `data_sp` and `data_it`,
next to the `Language` keys `enu fra deu esp ita jpn` [BYTES 0x292bc8..0x292fe0]. So the game opens
`data.na` plus one language overlay. This install ships only `data_us`. [H]: the overlay is chosen
by `Language`; for `jpn`, data.* is used alone.

## 2. The index (`.ni`)

```
+0   char[4]  "NNI\0"
+4   u32      n_entries
+8   u32      names_size
+12  u32      flags        bit 0 = incremental link (not supported; 0 in both shipped indexes)
+16  TOC      n_entries × 16 bytes, encrypted
...  names    names_size bytes, encrypted
```

The file length is exactly `16 + 16·n + names_size` in both indexes [DATA].

**Cipher.** The TOC and the names blob are encrypted separately: the key restarts at the start of
each. Per byte:

```
k = k * 0x3D09 (mod 2^32)        k starts at 0x7C53F961
plain = cipher - (k >> 16)        (mod 256)
```

Known bytes [DATA] (asserted by the test): `data.ni`'s first TOC bytes `68 14 53 fa 7c 84 7f 54`
decrypt to `{hash 9, size 0x38cd6}`, and `data_us.ni`'s `9c 14 53 fa f6 05 7c 54` decrypt to
`{hash 0x3d, size 0xd50}`.

**TOC entry**, 4 × u32:

| field | meaning |
|---|---|
| `hash` | a 16-bit value (max 65,509), **ascending** through the TOC, with duplicates. It is also the key of the matching `.nu` record. [DATA] |
| `size` | the number of bytes **stored** in the `.na`. For a `.z` entry this includes the 8-byte prefix. It is **not** the unpacked size. The sizes add up to exactly the `.na` length in both archives [DATA]. (`ni_unpack.py`'s docstring calls it "decompressed", which is wrong. It worked anyway because it bounds each read by the next entry's `pos`.) |
| `pos` | offset into the `.na` |
| `namepos` | offset into the names blob of a NUL-terminated CP932 path, e.g. `MAP\S_00\S_0003\EVT_0001_HUGO.XSO.Z` |

Every shipped name is ASCII [DATA]. Some names contain a literal `?` byte: they were mangled when
the archive was built, e.g. `MAP\MAPOBJ\Z_TREE\H\?????.DDS.Z`.

## 3. The data (`.na`)

- **`.z` entries** (14,927 in data, all 813 in data_us): `u32 crc32` (standard CRC-32 of the
  *unpacked* bytes), `u32 unpacked size`, then a zlib stream. The stream exactly fills `size - 8`.
  The CRC and size are verified for every file during unpack [DATA].
- **Stored entries** (1,018, all in data: WAV, BMP, VSO, PSO, SL2, a few DDS and leftovers such
  as `.OLD`/`.BAK`): the raw bytes.
- **Zero-size entries**: there are 56, all in data.ni. 52 are `.z` entries with `size 0, pos 0`. They
  are the `?`-mangled names plus a few others (`MAP\S_41\COMMON\*\40_RS_KABE26/36.DDS`,
  `MAP\S_02\COMMON\*\02OBJECT86.DDS` and the `_C_` variants). The other 4 are stored `_C_BLACK1X1.DDS`.
  They hold no data, and the native reader writes them as empty files. [H] They are unused (there
  are no bytes to load).
- A few `pos` values are shared: zero-size entries sit on the next file's offset [DATA].

## 4. Layering and names

The unpacked tree is `data` with `data_us` laid over it: an overlay entry replaces the base entry
with the same name (compared case-insensitively). In the shipped data, **all 813 data_us entries
replace a data entry, and data_us adds no new names** [DATA]. The replacements are 526 MAP
(mostly `.XSO` event scripts), 149 IMAGE, 134 MENU, and 4 MISC (`ENEMYINFO`, `INVINFO`,
`CASTINFO`, `EEX`). This is the same result as running `extract_assets.py` on data.ni and then on
data_us.ni into the same folder. All 813 overlapping files in `assets/` hash to the data_us version.

The on-disk name follows `extract_assets.py`'s rule:

1. Decode the path from CP932 to UTF-8 (on Windows with `MultiByteToWideChar(932)`). The decode
   happens **before** `\` is treated as a separator, so a `0x5C` trail byte (e.g. ソ = `83 5C`)
   stays inside its character.
2. Replace `\` with `/` and drop the trailing `.z` / `.Z`.
3. Replace each of `<>:"|?*` with `_`. There is no other escaping and no handling of reserved names.

No two entries collide after sanitising [DATA].

## 5. Comparison with the Python-extracted `assets/`

`cleria-gamedata --install "D:\SteamLibrary\steamapps\common\Ys Origin" --out <scratch>`
**unpacked 15,945 files (3,064.4 MB) in 16.5 s.** It runs 8 worker threads, with the output on
the C: SSD and the source on D:.

The full listing and an MD5 of every common file were compared against
`YsOrigin-CleriaCore/assets` (15,998 files):

- **15,893 files are byte-identical.**
- **52 files differ: the zero-size `.z` entries.** `ni_unpack.py` reads a `.z` entry from its `pos` up
  to the next distinct `pos`. For `pos 0` that range is `ANI\C_010.ASO.Z`, so it wrote a copy of
  `ANI/C_010.ASO` (40,412 bytes) under all 52 names, e.g. `MAP/S_41/COMMON/H/40_RS_KABE26.DDS`.
  The native reader writes them empty, which is correct because those entries hold no bytes.
  Neither version is a valid DDS, so a texture lookup that hits one fails the same way in both trees.
- Only in `assets/`: `music/` (44 files: 42 `.ogg` + `bgmdef.txt`/`bgmdef2.txt`), `movie/` (8 files) and
  `yso_ins04.dat`. These are hand copies of loose install files. They are not archive content and
  are deliberately not copied (§7).
- Only in the data folder: `cleria-gamedata.manifest`.

`tools/drive_regress.py --assets <the unpacked folder>` passes statue_swap, blessing,
gameover_retry and gameover_title, the same as with `assets/`. One caveat: gameover_title is flaky
on **both** trees. When the desktop gives cleria-view a presentable surface, Return to Title goes
back to the title loop (`view_game.inl` `goto title_again`) instead of exiting, and the run times out.

The test re-checks a sample of these against whichever asset root it is given (`INVINFO.DAT`,
`SCENELIST.SL`, `C_010.ASO`, an overridden `.XSO`, a `MENU\TOPIC` DDS, `EFFECTDEF.TXT`).

## 6. Finding the install

`gamedata::find_installs()` is read-only. It searches:

1. Steam roots: the registry value `HKCU\Software\Valve\Steam` `SteamPath`, then
   `HKLM\SOFTWARE\WOW6432Node\Valve\Steam` `InstallPath`, `HKLM\SOFTWARE\Valve\Steam` `InstallPath`,
   then `%ProgramFiles(x86)%\Steam`.
2. Libraries: each root, plus every `"path"` in `<root>\steamapps\libraryfolders.vdf` (and
   `config\libraryfolders.vdf`), with `\\` unescaped.
3. In each library, the folder is `steamapps\common\<installdir>`. `installdir` comes from
   `steamapps\appmanifest_207350.acf` and defaults to `Ys Origin`. The app id comes from the
   install's `steam_appid.txt` [DATA].

Every folder that exists is validated and returned, valid ones first. On this machine the search
finds `D:\SteamLibrary\steamapps\common\Ys Origin` (SteamPath `c:/program files (x86)/steam`, and
the library listed in its vdf). GOG and other stores are not searched; for those, pass the folder.

`validate(dir)` accepts the install root or its `release\` folder.
- **Required:** `yso_win.exe`, `release\data.ni/.na` and `release\data_us.ni/.na`. Both indexes
  must also decrypt and parse, and every entry must fit inside its `.na`.
- **Warnings:** the optional files `release\music\bgmdef.txt` + `YSO_001.ogg`, the eight movies,
  and `release\yso_ins04.dat`.

## 7. The data folder

- Location: `$CLERIA_DATA`, else `%LOCALAPPDATA%\CleriaCore\data`. Off Windows it is
  `$XDG_DATA_HOME` or `~/.local/share`, under `CleriaCore/data`.
- Contents: the merged tree (an asset root, the same layout as `assets/`) plus
  `cleria-gamedata.manifest`. It looks like this:

  ```
  # CleriaCore game data (src/cleria/gamedata.hpp); written last, on success
  format=1
  install=D:\SteamLibrary\steamapps\common\Ys Origin
  files=15945
  bytes=3064397974
  source_hash=4fe2bdd8a7185276
  created=2026-10-06T15:50:27Z
  ```

  `source_hash` is an FNV-1a 64 hash over both `.ni` files and both `.na` sizes. `status()`
  recomputes it (about 0.8 MB of reading) and reports `stale` when the install changed after the
  unpack, for example after a Steam update.
- Install protocol:
  1. Delete the old manifest and write `.cleria-installing`.
  2. Create the directories.
  3. Unpack on N threads. N is the hardware thread count, capped at 8.
  4. Write the manifest to a `.tmp` file and rename it into place.
  5. Remove the marker.

  A cancel or failure leaves the marker and no manifest, which `status()` reports as None + partial.
  Nothing is ever deleted except the manifest and the marker. The unpack refuses an output folder
  that is non-empty and has neither file, and refuses the game folder itself.
- Music, movies and the font are **not copied**. `release_dir_for(asset_root)` reads the asset
  root's manifest and returns `<install>\release`. The lookups try it right after their environment
  overrides:
  - `BgmDefs::load`: `$CLERIA_MUSIC_DIR` → `<install>\release\music` → `<root>/music` → …
  - `find_movie`: `$CLERIA_MOVIE_DIR` → `<install>\release` → `<root>/movie` → `<root>/../release`
  - `find_felghana` (`apps/cleria-view/view_menus.inl`): `$CLERIA_FONT_FILE` →
    `<install>\release\yso_ins04.dat` → `<root>/yso_ins04.dat` → …

  A legacy `assets/` has no manifest, so its lookups are unchanged.

## 8. Asset-root resolution

- **cleria-view** (`default_assets`, used only when `argv[1]` is absent): `$CLERIA_ASSETS` →
  `gamedata::ready_data_dir()`, the data folder when its manifest is valid and no marker is present →
  `<exe>/../../assets` → `<exe>/assets` → the repo `assets/`. An explicit asset root always wins, so
  every existing command line and `CLERIA_DRIVE` / `drive_regress.py --assets` run is unchanged.
- **ctest:** CMake sets `CLERIA_TEST_ASSETS` to the data folder at configure time if its manifest
  is there (and no marker), else to `${CMAKE_SOURCE_DIR}/assets`. It is a cache variable that
  `-DCLERIA_TEST_ASSETS=...` overrides. The test apps' own fallback (`"assets"` when run with no
  argument) is unchanged.

## 9. API (`src/cleria/gamedata.hpp`, for the launcher)

```cpp
namespace cleria::gamedata {
Validation validate(const fs::path& dir);           // {ok, root, missing[], warnings[]}
std::vector<Validation> find_installs();            // Steam candidates, validated, ok first
fs::path data_dir();                                // $CLERIA_DATA | %LOCALAPPDATA%\CleriaCore\data
Status status(const fs::path& data = data_dir());   // {state None|Installing|Ready, data, install,
                                                    //  release, partial, stale, manifest}
fs::path ready_data_dir();                          // data_dir() if Ready, else {}
fs::path release_dir_for(const fs::path& asset_root);
InstallResult install(game_dir, out = data_dir(), ProgressFn = {}, const std::atomic<bool>* cancel = nullptr,
                      unsigned threads = 0);        // blocking; progress on the calling thread ~20 Hz
class InstallTask { bool start(game_dir, out = data_dir()); void cancel(); bool running() const;
                    Progress progress() const; InstallResult result() const; void wait(); };
struct Progress { files_done, files_total, bytes_done, bytes_total /* archive bytes */, bytes_written; };
struct GameArchives { archives[2]; files[] /* merged */; bool open(install_root); };
}
```

The low-level reader is `src/cleria/ni_archive.hpp`. It provides `ni::Archive::open/find/read`, a
per-thread `ni::NaReader`, `ni::decrypt`, `ni::cp932_to_utf8` / `utf8_to_cp932`,
`ni::clean_name` and `ni::lookup_key`. Inflate and CRC-32 come from miniz 3.0.2
(`third_party/miniz`, MIT), built as the `miniz` target with warnings off.

## 10. CLI

```
cleria-gamedata --find                          the installs found (exit 0 when one is valid)
cleria-gamedata --validate <dir>
cleria-gamedata --install [<dir>] [--out <d>]   default: the first valid install found, into data_dir()
cleria-gamedata --list [<dir>] [filter]         the merged entries: source archive, stored size, z flag, name
cleria-gamedata --status [<data dir>]
```
