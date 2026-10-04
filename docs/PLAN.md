# NewAge: Qt rewrite plan

NewAge is a Qt 6 replacement for [Advanced Genie Editor](../../AGE) (AGE). The goal is **not** feature parity: only a chosen subset of AGE's editing features will be rebuilt, on a cleaner architecture that makes adding more entity types cheap.

This document covers project setup and the first core modules. Later feature work is listed only as follow-ups.

## 1. What we are replacing

### Keep: the data layer

[genieutils](../extern/genieutils) (with [pcrio](../extern/pcrio)) has no wxWidgets dependency and provides the data-format support:

| Area       | genieutils API                                    |
|------------|---------------------------------------------------|
| Game data  | `genie::DatFile` (civs, units, techs, graphics...) |
| Strings    | `genie::LangFile` (language DLLs, via pcrio + iconv). HD/DE key-value `.txt` files are not covered; NewAge parses those itself |
| Sprites    | `DrsFile`, `SlpFile`, `SmpFile`, `SmxFile`, `PalFile` |

NewAge builds the bundled genieutils fork through `cmake/Genieutils.cmake` (see section 2).

### Replace: the UI

AGE is about 40k lines of wxWidgets on top of genieutils. Problems to avoid repeating (paths relative to the AGE repo):

- **One god class.** `AGE_Frame.h` is 3.6k lines, mostly widget pointers. `AGE_Frame/Units.cpp` alone is 7k lines.
- **Hand-wired, untyped data binding.** Each `AGETextCtrl` holds a `std::vector<void*>` plus a `ContainerType` tag. Selecting a unit runs a large `switch` on the unit type that attaches each field to its box one call at a time (`Units.cpp:1284` onward).
- **Version checks mixed into UI code.** `if (GenieVersion >= ...)` checks for which game version has which field are spread through the widget code.
- **Duplicated list handling.** Every list (units, techs, graphics, map lands...) has its own near-identical Add / Insert / Delete / Copy / Paste handlers.
- **One mega-dialog for settings.** Game version, paths and language files are all chosen in a single dialog and persisted through ad-hoc `wxConfig` ini files (`AGE_Frame/Other.cpp:101-330`).
- **Every file picked by hand.** The user enters the `.dat` and up to three language files as separate paths, each with its own checkbox. The per-game "default" buttons (`OpenSaveDialog.cpp:168-330`) only fill in guessed paths under `<drive>:\Program Files`. NewAge opens a game installation folder instead (see M2b).
- **Legacy platform code.** A Windows-only `LoadStringA` path for 32-bit builds, SFML for sound, custom combo-box popups.

AGE logic worth porting as-is:

- Editor version → `genie::GameVersion` mapping: `AGE_Frame/Other.cpp:19` (already ported to `src/core/VersionProfile.cpp`).
- Save-time version upgrade: DE1/DE2 files are saved in the latest format, and pre-C15 DE2 data copies `UnitHeaders[].TaskList` into each unit (`AGE_Frame/Other.cpp:1287-1317`). **Not ported yet.** `Session::saveAs` currently writes back the loaded format.
- Language string lookup: `LoadTXT` / `TranslatedText` (`AGE_Frame/Other.cpp:2067-2118`), and the unit list label rule in `GetUnitName` (`AGE_Frame/Units.cpp:8-73`). Details and known bugs in M3.
- Palette / SLP loading and LRU sprite cache: `Loaders.cpp`.

## 2. Project setup (done in the scaffold)

- **Build:** CMake ≥ 3.25, C++20, `CMakePresets.json` (`msvc`, `ninja`).
- **Dependencies:** `vcpkg.json` pins the baseline and dependency versions for `qtbase`, `boost-iostreams`, `boost-interprocess`, `zlib`, `lz4`, and Windows-only `libiconv`. Qt default features are disabled; required features are selected explicitly. `boost-interprocess` supplies headers used by genieutils' `Compressor.cpp`.
- **vcpkg options:** the presets pass the `VCPKG_INSTALL_OPTIONS` environment variable to vcpkg. A Scoop-installed vcpkg needs `--x-buildtrees-root=<real path>` there, because Scoop's `buildtrees` is a junction and Qt refuses to build under a symlinked path (see README).
- **genieutils integration:** `cmake/Genieutils.cmake` compiles genieutils and pcrio from the `extern/` submodules (`GENIEUTILS_DIR`, `PCRIO_DIR` override them) into a proper `genie::genieutils` static target. We don't `add_subdirectory()` upstream's CMakeLists because it uses directory-wide include paths and GCC-only flags. genieutils comes from our fork (kebugcheckex/genieutils), which adds an iconv signature fix for vcpkg's libiconv on top of Tapsa/genieutils; pcrio is upstream Tapsa/pcrio.
- **Layout:**

  ```
  src/app/     main.cpp
  src/core/    Session, VersionProfile, Config, GameInstall, NameProvider    (QtCore only, no widgets)
  src/model/   FieldDesc, descriptor tables, Qt item models   (QtCore only)
  src/ui/      MainWindow, EntityBrowser, OptionsDialog
  tests/       Qt Test: load/save round-trip, config, game data, models, browser smoke test
  ```

- **Targets:** `genieutils` → `newage_core` → `newage_model` → `newage_ui` (all static) → `NewAge` (exe). Each test links the lowest layer it needs: `roundtrip_test`, `config_test`, and `gamedata_test` → core, `model_test` → model, `ui_test` → ui.
- **Sample data:** the gitignored `data/` folder holds `empires2_x1_p1.dat` (The Conquerors, `tc`) and `empires2_x2_p1.dat` (HD Edition, `aokhd`, file version `VER 5.7`). Tests that need them skip when they are missing.

## 3. Core module milestones

| #  | Milestone | Status |
|----|-----------|--------|
| M0 | Build skeleton: Qt window, genieutils linked, open a `.dat` and show counts | Done |
| M1 | Headless load → save → compare round-trip test | Done; passes on both samples |
| M2 | `Session` + settings + open a game installation | Done except the recent list, locale choice and version combo (see M2b) |
| M3 | `NameProvider` for language strings | Done for unit and tech labels and their displayed string-ID fields; SWGB per-civ offsets remain deferred |
| M4 | Field descriptors + generic property editor | Subset done: browsing (M4a, M4b), a few editable number fields (M4c) |
| M5 | First vertical slice: Civs → Units with undo | Unit and tech browsers with editing and saving; no undo yet (see M4c) |

### M0: Build skeleton

The initial window proved the MSVC + vcpkg + Qt + genieutils toolchain. `MainWindow` now has File → Open Game Folder / Open Data File / Save / Save As / Exit. Once a file is open it shows the unit and tech browsers, with file version and counts in the status bar.

### M1: Round-trip test

`tests/RoundTripTest.cpp` loads each sample in `data/` (plus the file in `NEWAGE_TEST_DAT`, version key in `NEWAGE_TEST_VERSION`, default `aoe2de`, when set), saves it unchanged, and compares the **decompressed** payloads (`DatFile::extractRaw`), because zlib output can differ byte-wise even when the data is identical. It is the safety net for every change that follows.

genieutils does not always throw on a bad load: an unrecognised DE file version prints "Unsupported version" and returns, and some wrong version choices parse to zero civs. `Session::open` treats "no civs" as a failure.

### M4a: Unit browser (done; editing added in M4c)

The initial read-only browser now shares the entity browser and supports the numeric edits described in M4c:

- `FieldDesc<T>` (`src/model/FieldDesc.h`) defines `name`, `group`, `applies`, `get`, string-ID metadata, an optional setter, and integer bounds. `unitFields()` (`src/model/UnitFields.cpp`) covers identity, language IDs, stats, costs, the first train location and time, collision size, graphics, and flags. Applicability predicates gate fields such as Speed (Type >= 20) and costs/training (Type >= 70).
- `FieldTreeModel` is entity-agnostic: `setObject(fields, obj)` snapshots the applicable values, grouped under headings. Floats display in shortest round-trip form (`0.2`, not `0.200000003`). It holds no pointer into the `DatFile`, so it can't dangle.
- `UnitListModel` lists every unit slot of one civ (row == unit index). Slots with `Civ::UnitPointers[i] == 0` show as "(empty)" and are not selectable.
- `EntityBrowser` (`src/ui/`, see M4b): civ combo (starts on civ 1, since civ 0 is Gaia), filter box, unit list, field tree. Switching civ keeps the same unit selected. List labels follow the M3 label rule ("82 - Castle"). The tooltip shows the internal name, and the filter matches either.
- `ListFilterModel` wraps the list: the text filter, plus hiding empty slots when the `unitList.hideEmpty` option is on. Row == unit index still holds in the source model.
- Not shown yet: attacks/armours, damage graphics, tasks, additional train locations, and most type-specific fields. The three resource-cost slots are already exposed as scalar fields.

### M4b: Tech browser (done; editing added in M4c)

The unit browser's layout for techs, and the first step towards "a new entity type is a descriptor table plus a list model" (M5).

- **What "per civ" means.** Techs are global (`DatFile::Techs`, no ID field; the ID is the index), unlike units, and AGE lists them without a civ. NewAge lists every tech (row == tech ID, so the selection survives civ switches) and marks which ones the selected civ can research (`TechListModel::Availability`):
  - `OtherCiv`: `Tech::Civ` is set and names another civ (unique techs, civ bonuses, and in TC many Gaia-only techs with `Civ == 0`). Only stored from AoK on; older files read as `-1`, all civs.
  - `DisabledByTechTree`: the civ's tech tree effect (`Civ::TechTreeID`) has a "disable tech" command (type 102) with the tech ID in `D`.
  - Otherwise `Available`. Worked out once per civ switch.
- Unavailable techs are drawn grey but stay selectable, since they still have data to look at. The `techList.hideUnavailable` option hides them. Tooltips give the internal name and the reason.
- Shared pieces, extracted from the unit browser:
  - `EntityListModel` (`src/model/`): base for per-civ entity lists. It holds the civ, resets on `Session::closed`, and provides the "ID - name" label, tooltip, `SearchTextRole` and `ActiveRole` (empty unit slot / unavailable tech = inactive). Subclasses give `name()` (the M3 label rule), `isActive()`, `internalName()` and `showFields()`, which binds their descriptor table to a `FieldTreeModel`.
  - `ListFilterModel` (was `UnitFilterModel`): text filter plus hiding inactive rows.
  - `EntityBrowser` (`src/ui/`, was `UnitBrowser`): civ combo, filter, list, field tree for any `EntityListModel`. It takes the model, the `Config` getter that hides inactive rows, and the filter placeholder. A delegate greys inactive rows (the model layer is QtCore-only, so no colours in the model).
- `techFields()` (`src/model/TechFields.cpp`) works on `TechRef {id, tech}` because `genie::Tech` has no ID. Groups: General (ID, internal name, language name / description as string IDs, Type `0 - Regular` / `2 - Age`, Civ, Effect, Icon, Full tech mode), Requirements (4 or 6 required techs, labelled as `Loom (22)` with unused `-1` slots hidden, + count), Costs (3 × resource / amount / paid), Research location (first location's building, time, button).
- `MainWindow` shows the browsers as **Units**, **Techs** and **Effects** tabs. Each has its own civ combo. The status bar adds the tech and effect counts. Double-clicking a tech's Effect field (not `-1`) switches to the Effects tab and selects that effect.
- Civ, Effect, and research location show `name (ID)` via `RefKind::Civ`, `RefKind::Effect`, and `RefKind::Unit`. Civ and effect names are the internal names; the location is the selected civ's unit name (language string, else internal name). `-1` and unknown IDs stay the plain number.
- Not shown yet: the Help and Tech tree string IDs (`LanguageDLLHelp` / `LanguageDLLTechTree` carry a 100000 / 150000 offset whose lookup rule needs checking), `Repeatable` (C15+), `Name2` (SWGB), and DE's extra research locations.
- Follow-up: tech tree effects also disable units (command type 2). The unit list could mark those the same way.

### M4c: Editing a few number fields, and saving (done)

- **Editable fields.** Units: Hit points, Line of sight, Speed, Costs (3 × resource / amount / paid, Type >= 70) and Train time (first train location, Type >= 70). Techs: Costs (3 × resource / amount / paid) and Research time (first research location). Everything else, including every text and string-ID field, stays read-only.
- `FieldDesc<T>` gained `set` and an int range (`minimum` / `maximum`). `numberField()` builds an editable descriptor from one accessor lambda (`[](auto &u) -> auto & { return u.HitPoints; }`). Integer members accept their C++ type's range, so a value can't wrap on save. Float members edit as text in their shortest form.
- `FieldTreeModel` holds no pointer into the `DatFile`. Rows remember their descriptor index, and `setData` goes through a `Writer` callback supplied by the entity list model's `showFields`. Writers look the entity up again on each edit. Unit writers reject a changed civ or inactive slot; tech writers require an existing tech but are independent of civ and research availability. `EntityListModel::entityEdited` sets `Session::setModified` and refreshes the list row.
- **Units are per civ.** An edit changes only the selected civ's copy of the unit, as in AGE without its auto-copy option. Techs are global.
- `EntityBrowser` edits the value column on double-click or F2, and the name cell forwards both. A delegate limits int spin boxes to the field's range.
- **Saving.** File > Save (Ctrl+S, enabled when modified) and Save As. `Session::saveAs` writes to a temporary file in the target folder, then replaces the target with `std::filesystem::rename`. A failed write leaves the old file untouched. Two Windows details:
  - genieutils keeps the input file open after `load()`, so `loadDat` calls `freelock()`.
  - `QTemporaryFile::close()` doesn't release the handle, so the temp file only reserves the name and is destroyed before genieutils writes to it.

  Closing or opening another file with unsaved changes asks Save / Discard / Cancel.

- Not done: undo/redo (`QUndoStack`, M5), multi-select edits, ID-reference pickers, and the save-time version upgrade (section 1).

### M2: Session and settings

- `newage::Session` owns the `DatFile`, the detected `GameVersion`, the path and the modified flag, and emits `opened` / `closed` / `modifiedChanged`. The UI never owns genie data directly.
- `newage::Config` holds user preferences in a JSON file (`QStandardPaths::AppConfigLocation/config.json`), one object per section. Each entry is a typed getter/setter with its default in code. Missing or mistyped entries read as the default, and unknown ones survive a save. `changed()` fires on load and on any real change, and views re-read what they use. `main()` loads it (on a bad file: warning, then defaults). Tools > Options (`OptionsDialog`, one group box per section) edits it, and `MainWindow` saves it on OK. Entries: `unitList.hideEmpty`, `techList.hideUnavailable`. Adding an entry means a getter/setter in `Config` plus a widget in `OptionsDialog`. Transient state (last folder, last version) stays in `QSettings`.
- Next steps:
  - Port the save-time version upgrade (see section 1).
  - Don't port AGE's drive-letter / DRS / loose-SLP path options until sprite preview is needed.

### M2b: Open a game installation (design change)

AGE makes the user pick the `.dat` and each language file separately (see section 1). NewAge's main entry point is instead **File > Open Game Folder**: the user picks the installation directory and NewAge finds the files itself.

- **Detection** (`newage::GameInstall`, `src/core`, no widgets): `detectInstall(dir, locale)` checks the known layouts below. Each dataset holds a title, a version key, a `.dat` path, and existing language files in lookup order. Detection looks at data and language files, not executables, so renamed executables don't matter. It does not enumerate available locales.
- **Several datasets in one folder.** Some installs hold more than one `.dat` (RoR has `data/` and `data2/`, HD has `empires2_x1_p1.dat` and `empires2_x2_p1.dat`, SWGB has `genie.dat` and `genie_x1.dat`). The open dialog preselects the last-used dataset filename if present, otherwise the first (newest) dataset. With only one, it opens straight away.
- **Locale.** The UI passes `en` to detection. There is no locale preference or picker yet. DLL-based games use their installed language files.
- **Version.** The layout selects a `VersionProfile`; genieutils refines DE versions from the file header. Folder opening has no version override for ambiguous layouts such as `tc` versus `tcv`.
- **Session settings.** `QSettings` remembers the last folders, loose-file version, and dataset filename. There is no recent-files menu.
- **Fallbacks.**
  - *File > Open Data File* opens loose or modded `.dat` files with a version picker and no language files. Labels fall back to internal names (see M3).
  - A folder that matches no layout gives an error listing what was looked for, not a guess.
- **Mods** (DE mods live under `%USERPROFILE%\Games\Age of Empires 2 DE\<id>\mods`, HD under `<install>\mods`) are out of scope for now. A mod's `.dat` can be opened through *Open Data File*.

Known layouts. AoK HD and AoE2 DE were checked against real installs. The others come from AGE's default buttons (`OpenSaveDialog.cpp:168-330`) and must be checked before being trusted (case-insensitive: older games mix `data` / `Data`).

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

`Session::open` takes a dataset (`.dat` + version + language files) and loads the strings with the data, so the two can't get out of step.

**Validation and remaining work.**

- `src/core/GameInstall.*`: `detectInstall(dir, locale)` returns the datasets newest first. Paths are matched case-insensitively. HD and DE both use `resources/_common/dat/empires2_x2_p1.dat`. HD is the folder that also has `empires2_x1_p1.dat`.
- File > Open Game Folder (Ctrl+O) opens the only dataset straight away, or asks which one, preselecting the last one used (`open/lastDataset` in `QSettings`). If language files are missing or fail to load, NewAge warns but opens the data anyway. File > Open Data File keeps the old `.dat` + version picker, with no strings.
- Earlier checks against real installs verified AoK HD (both datasets open, "Castle" resolves) and AoE2 DE detection and language-file loading. The bundled genieutils now recognizes `VER 8.9` as `GV_C32`; the previous `VER 8.4` limit is obsolete. Loading and round-tripping a real `VER 8.9` file still need revalidation with this dependency version. The DLL layouts are still unchecked against real files.
- Planned locale selection: enumerate language folders, excluding `_common`, `_launcher`, and `_packages`; prefer a future `language.locale` setting, then the system UI language if present, then `en`.
- Other planned work: a recent list storing install folder + dataset + locale, a version override for ambiguous layouts, and an optional game folder supplying strings for loose `.dat` files.

### M3: NameProvider

Turns string IDs from the data into text. List labels ("82 - Castle") and ID-reference fields all depend on it.

**Where names come from.** A unit record holds no display name, only string IDs into the language files:

| Field | Example (Castle, unit 82) |
|-------|---------------------------|
| `Unit::Name` | `CSTL` (internal name; fallback when the language name is missing) |
| `Unit::LanguageDLLName` | 5142 → "Castle" |
| `Unit::LanguageDLLCreation` | 6142 → "Build Castle" |
| `Unit::LanguageDLLHelp` | 26142 → long tooltip text |

Techs use the same scheme (`Tech::LanguageDLLName`, ...).

**Label rule** (as AGE's `GetUnitName`, `AGE_Frame/Units.cpp:8-73`):

1. The slot is empty (`UnitPointers[i] == 0`): "(empty)", as now.
2. `text(LanguageDLLName)` is non-empty: use it.
3. Otherwise use `Unit::Name`. If that is empty too: "(unnamed)".

**Sources.** `NameProvider` is built from the dataset's language file list (M2b) and exposes `QString text(int id)`. The first file with a non-empty string wins; negative IDs return empty.

- *Language DLLs* (AoE, RoR, AoK, TC, SWGB, CC): `genie::LangFile`, compiled into the build with pcrio and iconv. Strings are retrieved lazily, converted from UTF-8 to `QString`, and cached on lookup.
- *Key-value text* (HD, DE, and AoE DE's `strings.txt`): UTF-8, one `<id> "<text>"` per line, `//` comments. The parser follows `LoadTXT` (`AGE_Frame/Other.cpp:2067`) with fixes:
  - AGE stops at the first `"` after the opening one, so escaped quotes cut the string short (`3125 "Sent to \"%s\":"`; 73 such lines in DE's base file). Parse `\"`, `\\` and `\n` properly.
  - Skip lines whose key isn't a number (`IDS_OPT_ESC_MENU "..."`, about 3,300 in DE's base file). No data field refers to them.
  - An empty string doesn't override a lower-priority file (AGE's `if (len)`).
- *DE extra files.* Besides the base and modded files, DE ships `key-value-paphos-strings-utf8.txt` (Chronicles, IDs 106440–428194) and several campaign files. AGE loads none of them. NewAge loads every `*key-value*strings-utf8.txt` in the folder. The paphos IDs don't overlap the base file, so the order among the extra files doesn't matter. Only "modded wins over base" does.

**SWGB per-civ IDs.** For SWGB and later, AGE shifts the ID by civ before the lookup for Workers (class 58), Cargo Trader (unit 931), Commander (434) and Temple (104). The offsets come from AGE's ini (`swgbOffsets`, `AGE_Frame/Lists.cpp:991-997`). Port this into the label code, not into `NameProvider`, because it depends on the unit and civ. It can wait until SWGB is actually tested.

**Status.** `src/core/NameProvider.*` implements loading and lookup, and `Session::names()` exposes it. SWGB per-civ offsets remain deferred.

- Unit and tech lists prefer language names, then internal names, then "(unnamed)". Empty unit slots show "(empty)".
- The field view marks unit "Language name" / "Language creation" and tech "Language name" / "Language description" as string IDs (`FieldDesc::isStringId`) and shows resolved text alongside the ID, such as `5142 "Castle"`, with the full text in the tooltip.
- DLL strings are looked up lazily and cached.
- Key-value files are parsed up front. The parser also takes CRLF, a BOM, and a missing closing quote (the rest of the line is used).
- pcrio bug: `pcr_read_file` returns a half-initialised file for input that isn't a PE image, and `~LangFile` then crashes in `pcr_free`. `NameProvider` checks the MZ/PE signatures before handing a `.dll` to pcrio.
- Tests: `tests/GameDataTest.cpp` covers the parser (on `tests/data/key-value-strings-sample.txt`), the priority order, error reporting and layout detection on fake folders. `realInstall` runs against a real install when `NEWAGE_TEST_GAME_DIR` is set.

**Not in M3.** AGE's `AGE3NamesV0007.ini` ("custom names") only names armour classes, terrain tables and civ resources, which have no strings in the game files. It has nothing to do with unit names. Port it later with the fields that need it.

**Testing.** No original TC language DLLs are available. Both samples in `data/` can use the AoK HD strings instead (HD keeps TC's string IDs). Tests read the install folder from `NEWAGE_TEST_GAME_DIR` and skip when it is unset. A parser unit test with a small checked-in key-value snippet (escaped quotes, `IDS_` keys, empty strings, override order) needs no game install.

### M4: Field descriptors + generic property editor

Each entity type has a descriptor table in `src/model/`; `FieldDesc<T>` in `FieldDesc.h` is the current definition. `FieldTreeModel` (`QAbstractItemModel`) snapshots applicable fields, grouped by `group`, and `EntityBrowser` displays them in a shared `QTreeView`. Numeric types come from `QVariant`; descriptors provide optional setters and integer bounds. The original `FieldTableModel` / standalone `PropertyEditor` sketch has been superseded by this implementation.

- Implemented: applicability predicates for unit types and available collection entries, string-ID annotations, and numeric editing (M4a–M4c).
- Implemented validation: `ModelTest::editableFieldsRoundTrip` checks get/set round-trips for the supported editable descriptors.
- Planned: multi-select editing, showing a blank value where selected items differ and writing a new value to all selected items.
- `FieldDesc::ref` (`RefKind`) marks int fields that hold another entity's ID. `FieldTreeModel::setObject` takes a `RefNamer` from the list model and shows such values as `name (ID)`. Kinds: `Tech` (required techs), `Resource` (costs), `Civ` and `Effect` (tech general fields), `Unit` (research location).
- Planned: more `RefKind` values (train location) and ID pickers. `FieldType` and `minVersion` are not current descriptor members; add metadata only as the new fields require it.
- Planned: more type-specific unit fields (`Bird`, `Type50`, `Creatable`, `Building`, etc.) with appropriate applicability and version checks.
- Language IDs are currently read-only. genieutils widens the 16-bit union members into the 32-bit ones on load, so current reads are version-independent. Future setters must respect each format's stored width; older 16-bit fields must stay in `int16_t` range.

### M5: First vertical slice (Civs → Units)

- Implemented: `EntityListModel` (`QAbstractListModel`) and `ListFilterModel` (`QSortFilterProxyModel`) for lists and search; `EntityBrowser` combines the civ selector, entity list, and field tree for units and techs.
- Implemented: selected numeric edits and saving (M4c). Writers currently modify data directly and call `Session::setModified` through `entityEdited`.
- Planned: route edits through `QUndoStack` / `QUndoCommand` for undo/redo and modified-state tracking.
- Planned: shared Add / Insert / Delete / Copy / Paste operations on entity lists.

Techs and effects already reuse the shared browser. Effects are global (`DatFile::Effects`; the ID is the index), listed like techs, with one field group per command. The goal for further entity types (graphics, sounds, civs...) is to add a descriptor table and list model while reusing the UI.

## 4. Deliberately out of scope (unless needed later)

- Multiple editor windows (AGE supports up to 4).
- Auto-copy of edits across civilizations.
- The 32-bit `LoadStringA` language path.
- SFML sound playback (use Qt Multimedia if it's ever needed).
- The coloured-box theming, typing math expressions into fields, and the custom combo popups.
- Tech tree, random map and terrain border editors.
- Scenario files (`genie/script`; not compiled in).

## 5. Later, if needed

- **Sprite preview:** DRS / SLP / SMX frames decoded by genieutils, drawn into a `QImage` with a palette. Port `Loaders.cpp` (palettes, LRU cache).
- **Language string editing:** requires writing language files back (`LangFile::saveAs`).
- **Packaging:** `windeployqt` + an install target.

## 6. Risks and notes

- genieutils opens files with narrow `char*` paths, so on Windows paths must be representable in the ANSI code page. `Session` uses `QFile::encodeName`. Fix upstream later with wide paths if this becomes a problem.
- `genie::Terrain::setTerrainCount()` is global state inside genieutils. `Session::open` resets it to 0 (AGE's non-custom default).
- genieutils compiles `pcrio.c` as C++ and depends on iconv. vcpkg supplies `libiconv` on Windows. Its autotools build is why vcpkg downloads msys2; msys2 is only a build tool and nothing from it is linked in.
- Configuring without matching vcpkg binary-cache entries builds dependencies from source, typically in both Debug and Release; qtbase dominates that time. Matching cache entries can be reused on any configure. `vcpkg.json` disables Qt default features and lists the selected features explicitly, including `icu` and platform-conditional `dbus` and graphics backends.
