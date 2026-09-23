# NewAge: Qt rewrite plan

NewAge is a Qt 6 replacement for [Advanced Genie Editor](../../AGE) (AGE).
The goal is **not** feature parity: only a chosen subset of AGE's editing
features will be rebuilt, on a cleaner architecture that makes adding more
entity types cheap.

This document covers project setup and the first core modules. Later feature
work is listed only as follow-ups.

## 1. What we are replacing

### Keep: the data layer

[genieutils](../../genieutils) (with [pcrio](../../pcrio)) has no wxWidgets
dependency and already handles all file formats:

| Area       | genieutils API                                    |
|------------|---------------------------------------------------|
| Game data  | `genie::DatFile` (civs, units, techs, graphics...) |
| Strings    | `genie::LangFile` (language DLLs, via pcrio + iconv). HD/DE key-value `.txt` files are not covered; NewAge parses those itself |
| Sprites    | `DrsFile`, `SlpFile`, `SmpFile`, `SmxFile`, `PalFile` |

NewAge links it unchanged.

### Replace: the UI

AGE is about 40k lines of wxWidgets on top of genieutils. Problems to avoid
repeating (paths relative to the AGE repo):

- **One god class.** `AGE_Frame.h` is 3.6k lines, mostly widget pointers.
  `AGE_Frame/Units.cpp` alone is 7k lines.
- **Hand-wired, untyped data binding.** Each `AGETextCtrl` holds a
  `std::vector<void*>` plus a `ContainerType` tag. Selecting a unit runs a large
  `switch` on the unit type that attaches each field to its box one call at a
  time (`Units.cpp:1284` onward).
- **Version checks mixed into UI code.** `if (GenieVersion >= ...)` checks for
  which game version has which field are spread through the widget code.
- **Duplicated list handling.** Every list (units, techs, graphics, map lands...)
  has its own near-identical Add / Insert / Delete / Copy / Paste handlers.
- **One mega-dialog for settings.** Game version, paths and language files are
  all chosen in a single dialog and persisted through ad-hoc `wxConfig` ini
  files (`AGE_Frame/Other.cpp:101-330`).
- **Every file picked by hand.** The user enters the `.dat` and up to three
  language files as separate paths, each with its own checkbox. The
  per-game "default" buttons (`OpenSaveDialog.cpp:168-330`) only fill in
  guessed paths under `<drive>:\Program Files`. NewAge opens a game
  installation folder instead (see M2b).
- **Legacy platform code.** A Windows-only `LoadStringA` path for 32-bit builds,
  SFML for sound, custom combo-box popups.

AGE logic worth porting as-is:

- Editor version → `genie::GameVersion` mapping: `AGE_Frame/Other.cpp:19`
  (already ported to `src/core/VersionProfile.cpp`).
- Save-time version upgrade: DE1/DE2 files are saved in the latest format, and
  pre-C15 DE2 data copies `UnitHeaders[].TaskList` into each unit
  (`AGE_Frame/Other.cpp:1287-1317`). **Not ported yet.** `Session::saveAs`
  currently writes back the loaded format.
- Language string lookup: `LoadTXT` / `TranslatedText`
  (`AGE_Frame/Other.cpp:2067-2118`), and the unit list label rule in
  `GetUnitName` (`AGE_Frame/Units.cpp:8-73`). Details and known bugs in M3.
- Palette / SLP loading and LRU sprite cache: `Loaders.cpp`.

## 2. Project setup (done in the scaffold)

- **Build:** CMake ≥ 3.25, C++20, `CMakePresets.json` (`msvc`, `ninja`).
- **Dependencies:** vcpkg manifest (`vcpkg.json`) with `qtbase`,
  `boost-iostreams`, `boost-interprocess`, `zlib`, `lz4`, `libiconv`. The
  builtin-baseline and overrides match AGE/genieutils, so binary-cache entries
  are shared. `boost-interprocess` (header-only, used by genieutils'
  `Compressor.cpp`) is missing from genieutils' own manifest.
- **vcpkg options:** the presets pass the `VCPKG_INSTALL_OPTIONS` environment
  variable to vcpkg. A Scoop-installed vcpkg needs
  `--x-buildtrees-root=<real path>` there, because Scoop's `buildtrees` is a
  junction and Qt refuses to build under a symlinked path (see README).
- **genieutils integration:** `cmake/Genieutils.cmake` compiles genieutils and
  pcrio from the `extern/` submodules (`GENIEUTILS_DIR`, `PCRIO_DIR` override
  them) into a proper `genie::genieutils` static target. We don't
  `add_subdirectory()` upstream's CMakeLists because it uses directory-wide
  include paths and GCC-only flags. genieutils comes from our fork
  (kebugcheckex/genieutils), which adds an iconv signature fix for vcpkg's
  libiconv on top of Tapsa/genieutils; pcrio is upstream Tapsa/pcrio.
- **Layout:**

  ```
  src/app/     main.cpp
  src/core/    Session, VersionProfile, Config    (QtCore only, no widgets)
  src/model/   FieldDesc, descriptor tables, Qt item models   (QtCore only)
  src/ui/      MainWindow, EntityBrowser, OptionsDialog; later OpenDialog, PropertyEditor
  tests/       Qt Test: load/save round-trip, config, models, browser smoke test
  ```

- **Targets:** `genieutils` → `newage_core` → `newage_model` → `newage_ui`
  (all static) → `NewAge` (exe). Each test links the lowest layer it needs:
  `roundtrip_test` and `config_test` → core, `model_test` → model,
  `ui_test` → ui.
- **Sample data:** the gitignored `data/` folder holds
  `empires2_x1_p1.dat` (The Conquerors, `tc`) and `empires2_x2_p1.dat`
  (HD Edition, `aokhd`, file version `VER 5.7`). Tests that need them skip
  when they are missing.

## 3. Core module milestones

| #  | Milestone | Status |
|----|-----------|--------|
| M0 | Build skeleton: Qt window, genieutils linked, open a `.dat` and show counts | Done |
| M1 | Headless load → save → compare round-trip test | Done; passes on both samples |
| M2 | `Session` + settings + open a game installation | Done except the recent list, locale choice and version combo (see M2b) |
| M3 | `NameProvider` for language strings | Done for unit labels and the two unit string-ID fields |
| M4 | Field descriptors + generic property editor | Read-only subset done (see M4a, M4b) |
| M5 | First vertical slice: Civs → Units with undo | Read-only unit and tech browsers done (see M4a, M4b) |

### M0: Build skeleton
`MainWindow` has File → Open / Save As / Exit. Once a file is open it shows
the unit and tech browsers, with file version and counts in the status bar. This proved
the MSVC + vcpkg + Qt + genieutils toolchain before any real UI work.

### M1: Round-trip test
`tests/RoundTripTest.cpp` loads each sample in `data/` (plus the file in
`NEWAGE_TEST_DAT`, version key in `NEWAGE_TEST_VERSION`, default `aoe2de`, when
set), saves it unchanged, and compares the **decompressed** payloads
(`DatFile::extractRaw`), because zlib output can differ byte-wise even when
the data is identical. It is the safety net for every change that follows.

genieutils does not always throw on a bad load: an unrecognised DE file
version prints "Unsupported version" and returns, and some wrong version
choices parse to zero civs. `Session::open` treats "no civs" as a failure.

### M4a: Read-only unit browser (done)
A thin first cut of M4 + M5, skipping M3:

- `FieldDesc<T>` (`src/model/FieldDesc.h`) has only `name`, `group`,
  `applies`, `get`. `unitFields()` (`src/model/UnitFields.cpp`) covers ~20
  scalar fields common to all versions: ID, Type, Class, internal name,
  language IDs, HP, LOS, Speed (`applies`: Type >= 20), garrison/resource
  capacity, collision size, standing/dying graphics, icon, Enabled, Hide in
  editor.
- `FieldTreeModel` is entity-agnostic: `setObject(fields, obj)` snapshots the
  applicable values, grouped under headings. Floats display in shortest
  round-trip form (`0.2`, not `0.200000003`). It holds no pointer into the
  `DatFile`, so it can't dangle.
- `UnitListModel` lists every unit slot of one civ (row == unit index). Slots
  with `Civ::UnitPointers[i] == 0` show as "(empty)" and are not selectable.
- `UnitBrowser` (`src/ui/`, now `EntityBrowser`, see M4b): civ combo (starts on civ 1, since civ 0 is Gaia),
  filter box, unit list, field tree. Switching civ keeps the same unit
  selected. List labels follow the M3 label rule ("82 - Castle"). The
  tooltip shows the internal name, and the filter matches either.
- `UnitFilterModel` (now `ListFilterModel`) wraps the list: the text filter,
  plus hiding empty slots when the `unitList.hideEmpty` option is on. Row ==
  unit index still holds in the source model.
- Not shown yet: list-valued data (attacks/armours, costs, damage graphics,
  tasks) and type-specific sub-structs beyond Speed.

### M4b: Read-only tech list (done)
The unit browser's layout for techs, and the first step towards "a new entity
type is a descriptor table plus a list model" (M5).

- **What "per civ" means.** Techs are global (`DatFile::Techs`, no ID field;
  the ID is the index), unlike units, and AGE lists them without a civ. NewAge
  lists every tech (row == tech ID, so the selection survives civ switches)
  and marks which ones the selected civ can research
  (`TechListModel::Availability`):
  - `OtherCiv`: `Tech::Civ` is set and names another civ (unique techs, civ
    bonuses, and in TC many Gaia-only techs with `Civ == 0`). Only stored
    from AoK on; older files read as `-1`, all civs.
  - `DisabledByTechTree`: the civ's tech tree effect (`Civ::TechTreeID`) has a
    "disable tech" command (type 102) with the tech ID in `D`.
  - Otherwise `Available`. Worked out once per civ switch.
- Unavailable techs are drawn grey but stay selectable, since they still
  have data to look at. The `techList.hideUnavailable` option hides them.
  Tooltips give the internal name and the reason.
- Shared pieces, extracted from the unit browser:
  - `EntityListModel` (`src/model/`): base for per-civ entity lists. It holds
    the civ, resets on `Session::closed`, and provides the "ID - name" label,
    tooltip, `SearchTextRole` and `ActiveRole` (empty unit slot / unavailable
    tech = inactive). Subclasses give `name()` (the M3 label rule),
    `isActive()`, `internalName()` and `showFields()`, which binds their
    descriptor table to a `FieldTreeModel`.
  - `ListFilterModel` (was `UnitFilterModel`): text filter plus hiding
    inactive rows.
  - `EntityBrowser` (`src/ui/`, was `UnitBrowser`): civ combo, filter, list,
    field tree for any `EntityListModel`. It takes the model, the `Config`
    getter that hides inactive rows, and the filter placeholder. A delegate
    greys inactive rows (the model layer is QtCore-only, so no colours in the
    model).
- `techFields()` (`src/model/TechFields.cpp`) works on `TechRef {id, tech}`
  because `genie::Tech` has no ID. Groups: General (ID, internal name,
  language name / description as string IDs, Type `0 - Regular` / `2 - Age`,
  Civ, Effect, Icon, Full tech mode), Requirements (4 or 6 required techs +
  count), Costs (3 × resource / amount / paid), Research location (first
  location's building, time, button).
- `MainWindow` shows the browsers as **Units** and **Techs** tabs. Each has its
  own civ combo. The status bar adds the tech count.
- Not shown yet: the Help and Tech tree string IDs (`LanguageDLLHelp` /
  `LanguageDLLTechTree` carry a 100000 / 150000 offset whose lookup rule
  needs checking), `Repeatable` (C15+), `Name2` (SWGB), DE's extra research
  locations, and names next to ID references (Civ, Effect, required techs,
  location), which wait for `RefKind`.
- Follow-up: tech tree effects also disable units (command type 2). The unit
  list could mark those the same way.

### M2: Session and settings
- `newage::Session` owns the `DatFile`, the detected `GameVersion`, the path
  and the modified flag, and emits `opened` / `closed` / `modifiedChanged`.
  The UI never owns genie data directly.
- `newage::Config` holds user preferences in a JSON file
  (`QStandardPaths::AppConfigLocation/config.json`), one object per section.
  Each entry is a typed getter/setter with its default in code. Missing or
  mistyped entries read as the default, and unknown ones survive a save.
  `changed()` fires on load and on any real change, and views re-read what
  they use. `main()` loads it (on a bad file: warning, then defaults).
  Tools > Options (`OptionsDialog`, one group box per section) edits it, and
  `MainWindow` saves it on OK. Entries: `unitList.hideEmpty`, `techList.hideUnavailable`. Adding an
  entry means a getter/setter in `Config` plus a widget in `OptionsDialog`.
  Transient state (last folder, last version) stays in `QSettings`.
- Next steps:
  - Port the save-time version upgrade (see section 1).
  - Don't port AGE's drive-letter / DRS / loose-SLP path options until sprite
    preview is needed.

### M2b: Open a game installation (design change)
AGE makes the user pick the `.dat` and each language file separately (see
section 1). NewAge's main entry point is instead **File > Open Game Folder**:
the user picks the installation directory and NewAge finds the files itself.

- **Detection** (`newage::GameInstall`, `src/core`, no widgets):
  `detectInstall(dir)` checks the known layouts below and returns every
  dataset it finds. Each dataset holds a version key, a `.dat` path, the
  language files in lookup order, and the available locales. Detection looks
  at data and language files, not executables, so renamed executables
  don't matter.
- **Several datasets in one folder.** Some installs hold more than one `.dat`
  (RoR has `data/` and `data2/`, HD has `empires2_x1_p1.dat` and
  `empires2_x2_p1.dat`, SWGB has `genie.dat` and `genie_x1.dat`). The open
  dialog lists them with the newest selected. With only one, it opens
  straight away.
- **Locale.** HD and DE keep strings under `resources/<locale>/`. The locale
  list comes from those folder names (leaving out `_common`, `_launcher`,
  `_packages`). The default is a new `language.locale` entry in `Config`, then
  the system UI language if present, then `en`. The DLL-based games have
  only one language, the one installed.
- **Version.** The layout decides the version key (`VersionProfile`). DE and
  HD `.dat` files also carry a version string, which genieutils checks on
  load. If detection is ambiguous (e.g. `tc` vs `tcv`), the dialog shows a
  version combo preset to the best guess.
- **Recent list.** A recent entry is install folder + dataset + locale, kept
  in `QSettings`. Reopening is one click.
- **Fallbacks.**
  - *File > Open Data File* stays for loose or modded `.dat` files. It asks
    for the version and optionally a game folder to take strings from. With
    no strings, labels show the internal name (see M3).
  - A folder that matches no layout gives an error listing what was looked
    for, not a guess.
- **Mods** (DE mods live under `%USERPROFILE%\Games\Age of Empires 2
  DE\<id>\mods`, HD under `<install>\mods`) are out of scope for now. A mod's
  `.dat` can be opened through *Open Data File*.

Known layouts. AoK HD and AoE2 DE were checked against real installs.
The others come from AGE's default buttons (`OpenSaveDialog.cpp:168-330`)
and must be checked before being trusted (case-insensitive: older games mix
`data` / `Data`).

| Game | `.dat` (relative to the folder) | Key | Language files, highest priority first |
|------|--------------------------------|-----|----------------------------------------|
| AoE | `data/empires.dat` | `aoe` | `language.dll` |
| RoR | `data2/empires.dat` | `ror` | `languagex.dll`, `language.dll` |
| AoE DE | `Data/empires.dat` | `aoede` | `Data/Localization/<locale>/strings.txt` |
| AoK | `data/empires2.dat` | `aok` | `language.dll` |
| TC | `data/empires2_x1_p1.dat` | `tc` | `language_x1_p1.dll`, `language_x1.dll`, `language.dll` |
| AoK HD ✔ | `resources/_common/dat/empires2_x1_p1.dat` (`tc`), `…/empires2_x2_p1.dat` (`aokhd`) | | `resources/<locale>/strings/key-value/key-value-modded-strings-utf8.txt`, `key-value-strings-utf8.txt` |
| AoE2 DE ✔ | `resources/_common/dat/empires2_x2_p1.dat` | `aoe2de` | `resources/<locale>/strings/key-value/key-value-modded-strings-utf8.txt`, then every other `*key-value*strings-utf8.txt` there (see M3) |
| SWGB | `Data/genie.dat` | `swgb` | `language.dll` |
| CC | `Data/genie_x1.dat` | `cc` | `language_x1.dll`, `language.dll` |
| EF2 (mod) | `Data/genie_x2.dat` | `ef2` | `language_x2.dll`, `language_x1.dll`, `language.dll` |

`Session::open` takes a dataset (`.dat` + version + language files) and loads
the strings with the data, so the two can't get out of step.

**Status (done).**
- `src/core/GameInstall.*`: `detectInstall(dir, locale)` returns the
  datasets newest first. Paths are matched case-insensitively. HD and DE
  both use `resources/_common/dat/empires2_x2_p1.dat`. HD is the folder
  that also has `empires2_x1_p1.dat`.
- File > Open Game Folder (Ctrl+O) opens the only dataset straight away, or
  asks which one, preselecting the last one used (`open/lastDataset` in
  `QSettings`). If language files are missing or fail to load, NewAge warns
  but opens the data anyway. File > Open Data File keeps the old `.dat` +
  version picker, with no strings.
- Checked against real installs: AoK HD (both datasets open, "Castle"
  resolves). AoE2 DE is detected and its 13 string files load, but its
  `VER 8.9` `.dat` doesn't load, because genieutils only reads up to
  `VER 8.4`. That is a data-layer gap, separate from this milestone.
- Not done yet: the locale is hard-coded to `en` (no `language.locale`
  entry), there is no recent list, and there is no version combo for
  ambiguous layouts. The DLL layouts are still unchecked against real files.

### M3: NameProvider
Turns string IDs from the data into text. List labels ("82 - Castle") and
ID-reference fields all depend on it.

**Where names come from.** A unit record holds no display name, only string
IDs into the language files:

| Field | Example (Castle, unit 82) |
|-------|---------------------------|
| `Unit::Name` | `CSTL` (internal name, what the list shows today) |
| `Unit::LanguageDLLName` | 5142 → "Castle" |
| `Unit::LanguageDLLCreation` | 6142 → "Build Castle" |
| `Unit::LanguageDLLHelp` | 26142 → long tooltip text |

Techs use the same scheme (`Tech::LanguageDLLName`, ...).

**Label rule** (as AGE's `GetUnitName`, `AGE_Frame/Units.cpp:8-73`):
1. The slot is empty (`UnitPointers[i] == 0`): "(empty)", as now.
2. `text(LanguageDLLName)` is non-empty: use it.
3. Otherwise use `Unit::Name`. If that is empty too: "(unnamed)".

**Sources.** `NameProvider` is built from the dataset's language file list
(M2b) and exposes `QString text(int id)`. The first file with a non-empty
string wins; negative IDs return empty.
- *Language DLLs* (AoE, RoR, AoK, TC, SWGB, CC): `genie::LangFile`, already
  compiled into the build with pcrio and iconv. Convert to UTF-8 on load.
- *Key-value text* (HD, DE, and AoE DE's `strings.txt`): UTF-8, one
  `<id> "<text>"` per line, `//` comments. Port `LoadTXT`
  (`AGE_Frame/Other.cpp:2067`) with fixes:
  - AGE stops at the first `"` after the opening one, so escaped quotes cut
    the string short (`3125 "Sent to \"%s\":"`; 73 such lines in DE's base
    file). Parse `\"`, `\\` and `\n` properly.
  - Skip lines whose key isn't a number (`IDS_OPT_ESC_MENU "..."`, about
    3,300 in DE's base file). No data field refers to them.
  - An empty string doesn't override a lower-priority file (AGE's `if (len)`).
- *DE extra files.* Besides the base and modded files, DE ships
  `key-value-paphos-strings-utf8.txt` (Chronicles, IDs 106440–428194) and
  several campaign files. AGE loads none of them. NewAge loads every
  `*key-value*strings-utf8.txt` in the folder. The paphos IDs don't overlap
  the base file, so the order among the extra files doesn't matter. Only
  "modded wins over base" does.

**SWGB per-civ IDs.** For SWGB and later, AGE shifts the ID by civ before the
lookup for Workers (class 58), Cargo Trader (unit 931), Commander (434) and
Temple (104). The offsets come from AGE's ini (`swgbOffsets`,
`AGE_Frame/Lists.cpp:991-997`). Port this into the label code, not into
`NameProvider`, because it depends on the unit and civ. It can wait until
SWGB is actually tested.

**Status (done).** `src/core/NameProvider.*` implements the above, and
`Session::names()` exposes it.
- The unit list uses the label rule.
- The field view marks "Language name" and "Language creation" as string IDs
  (`FieldDesc::isStringId`) and shows them as `5142 "Castle"`, with the full
  text in the tooltip.
- DLL strings are looked up lazily and cached.
- Key-value files are parsed up front. The parser also takes CRLF, a BOM, and
  a missing closing quote (the rest of the line is used).
- pcrio bug: `pcr_read_file` returns a half-initialised file for input that
  isn't a PE image, and `~LangFile` then crashes in `pcr_free`.
  `NameProvider` checks the MZ/PE signatures before handing a `.dll` to
  pcrio.
- Tests: `tests/GameDataTest.cpp` covers the parser (on
  `tests/data/key-value-strings-sample.txt`), the priority order, error
  reporting and layout detection on fake folders. `realInstall` runs against
  a real install when `NEWAGE_TEST_GAME_DIR` is set.

**Not in M3.** AGE's `AGE3NamesV0007.ini` ("custom names") only names armour
classes, terrain tables and civ resources, which have no strings in the game
files. It has nothing to do with unit names. Port it later with the fields
that need it.

**Testing.** No original TC language DLLs are available. Both samples in
`data/` can use the AoK HD strings instead (HD keeps TC's string IDs). Tests
read the install folder from `NEWAGE_TEST_GAME_DIR` and skip when it is
unset. A parser unit test with a small checked-in key-value snippet (escaped
quotes, `IDS_` keys, empty strings, override order) needs no game install.

### M4: Field descriptors + generic property editor
This is the core design change. Instead of hand-wiring widgets, each entity type
gets a table of descriptors:

```cpp
template <typename T>
struct FieldDesc {
    QString name, group;
    FieldType type;                 // U8, I16, I32, F32, String...
    RefKind ref;                    // None, Unit, Graphic, Sound, Tech, LangString
    genie::GameVersion minVersion;  // replaces scattered version checks
    std::function<bool(const T&)> applies;           // e.g. unit Type >= 70
    std::function<QVariant(const T&)> get;
    std::function<void(T&, const QVariant&)> set;
};
```

- A `FieldTableModel` (`QAbstractTableModel`) exposes the fields that apply to
  the current selection. A single `PropertyEditor` view (a `QTreeView` grouped
  by `group`) serves every entity type.
- Multi-select: show a blank value where the selected items differ; setting a
  value writes it to all of them (AGE supports this; keep it).
- Delegates by `RefKind`: an ID field shows "123 - Archer" and opens a picker.
- Unit specifics: `genie::Unit` has type-dependent sub-structs (`Bird`,
  `Type50`, `Creatable`, `Building`...). The `applies` predicate on
  `Unit::Type` handles those. Some fields are unions (`LanguageDLLName` /
  `LanguageDLLNameU16`) and must pick the member by game version.
- A descriptor-sanity test checks that every descriptor round-trips through
  get/set on a default-constructed object.
- Editing the language IDs: genieutils widens the 16-bit union members into
  the 32-bit ones on load, so reads are version-independent, but writes for
  pre-C15 versions must stay in `int16_t` range.

### M5: First vertical slice (Civs → Units)
- `EntityListModel` (`QAbstractListModel`) + `QSortFilterProxyModel` for search
  (read-only versions exist since M4b).
- Civ selector + unit list + `PropertyEditor`.
- All edits go through `QUndoStack` / `QUndoCommand`, which gives undo/redo
  for free (AGE has none) and drives `Session::setModified`.
- Generic Add / Insert / Delete / Copy / Paste on the list model, written
  **once** for all entity types.

After M5, each further entity type (techs, effects, graphics, sounds,
civs...) is mainly a descriptor table plus a list model, with no new UI code
per type.

## 4. Deliberately out of scope (unless needed later)

- Multiple editor windows (AGE supports up to 4).
- Auto-copy of edits across civilizations.
- The 32-bit `LoadStringA` language path.
- SFML sound playback (use Qt Multimedia if it's ever needed).
- The coloured-box theming, typing math expressions into fields, and the custom
  combo popups.
- Tech tree, random map and terrain border editors.
- Scenario files (`genie/script`; not compiled in).

## 5. Later, if needed

- **Sprite preview:** DRS / SLP / SMX frames decoded by genieutils, drawn into a
  `QImage` with a palette. Port `Loaders.cpp` (palettes, LRU cache).
- **Language string editing:** requires writing language files back
  (`LangFile::saveAs`).
- **Packaging:** `windeployqt` + an install target.

## 6. Risks and notes

- genieutils opens files with narrow `char*` paths, so on Windows paths must be
  representable in the ANSI code page. `Session` uses `QFile::encodeName`.
  Fix upstream later with wide paths if this becomes a problem.
- `genie::Terrain::setTerrainCount()` is global state inside genieutils.
  `Session::open` resets it to 0 (AGE's non-custom default).
- genieutils compiles `pcrio.c` as C++ and depends on iconv. vcpkg supplies
  `libiconv` on Windows. Its autotools build is why vcpkg downloads msys2; msys2
  is only a build tool and nothing from it is linked in.
- The first configure builds every dependency from source in both Debug and
  Release, and qtbase dominates that time. Later configures restore from the
  vcpkg binary cache. qtbase currently uses its default features, which include
  unused ones (`sql-psql`, `openssl`, `icu`, `dbus`, `network`). Trim them if a
  clean rebuild becomes a problem.
