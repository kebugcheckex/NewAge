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
| Strings    | `genie::LangFile` (language DLLs, via pcrio + iconv) |
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
- **Legacy platform code.** A Windows-only `LoadStringA` path for 32-bit builds,
  SFML for sound, custom combo-box popups.

AGE logic worth porting as-is:

- Editor version → `genie::GameVersion` mapping: `AGE_Frame/Other.cpp:19`
  (already ported to `src/core/VersionProfile.cpp`).
- Save-time version upgrade: DE1/DE2 files are saved in the latest format, and
  pre-C15 DE2 data copies `UnitHeaders[].TaskList` into each unit
  (`AGE_Frame/Other.cpp:1287-1317`). **Not ported yet.** `Session::saveAs`
  currently writes back the loaded format.
- DE2 key/value string file parsing: `LoadTXT` / `TranslatedText`
  (`AGE_Frame/Other.cpp:2067-2118`).
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
  pcrio from the sibling checkouts (`GENIEUTILS_DIR`, `PCRIO_DIR`, defaulting to
  `../genieutils` and `../pcrio`) into a proper `genie::genieutils` static
  target. We don't `add_subdirectory()` upstream's CMakeLists because it uses
  directory-wide include paths and GCC-only flags. We use the working trees, not
  submodules, because genieutils has uncommitted changes in use (SWGB fields).
  Switch to submodules once those are committed.
- **Layout:**

  ```
  src/app/     main.cpp
  src/core/    Session, VersionProfile            (QtCore only, no widgets)
  src/model/   FieldDesc, descriptor tables, Qt item models   (QtCore only)
  src/ui/      MainWindow, UnitBrowser; later OpenDialog, PropertyEditor
  tests/       Qt Test: load/save round-trip, models, UnitBrowser smoke test
  ```

- **Targets:** `genieutils` → `newage_core` → `newage_model` → `newage_ui`
  (all static) → `NewAge` (exe). Each test links the lowest layer it needs:
  `roundtrip_test` → core, `model_test` → model, `ui_test` → ui.
- **Sample data:** the gitignored `data/` folder holds
  `empires2_x1_p1.dat` (The Conquerors, `tc`) and `empires2_x2_p1.dat`
  (HD Edition, `aokhd`, file version `VER 5.7`). Tests that need them skip
  when they are missing.

## 3. Core module milestones

| #  | Milestone | Status |
|----|-----------|--------|
| M0 | Build skeleton: Qt window, genieutils linked, open a `.dat` and show counts | Done |
| M1 | Headless load → save → compare round-trip test | Done; passes on both samples |
| M2 | `Session` + settings + open dialog / profiles | `Session` done; open dialog is a temporary picker |
| M3 | `NameProvider` for language strings | Planned |
| M4 | Field descriptors + generic property editor | Read-only subset done (see M4a) |
| M5 | First vertical slice: Civs → Units with undo | Read-only browser done (see M4a) |

### M0: Build skeleton
`MainWindow` has File → Open / Save As / Exit. Once a file is open it shows
the unit browser, with file version and counts in the status bar. This proved
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
- `UnitBrowser` (`src/ui/`): civ combo (starts on civ 1, since civ 0 is Gaia),
  filter box, unit list, field tree. Switching civ keeps the same unit
  selected. List labels use the internal name until M3 exists.
- Not shown yet: list-valued data (attacks/armours, costs, damage graphics,
  tasks) and type-specific sub-structs beyond Speed.

### M2: Session and settings
- `newage::Session` owns the `DatFile`, the detected `GameVersion`, the path
  and the modified flag, and emits `opened` / `closed` / `modifiedChanged`.
  The UI never owns genie data directly.
- Next steps:
  - Add the language files to `Session`.
  - Port the save-time version upgrade (see section 1).
  - Replace the temporary `QInputDialog` version picker with an `OpenDialog`
    built around a **profile**: game version + `.dat` path + language paths,
    stored in `QSettings` with a recent-profiles list.
  - Don't port AGE's drive-letter / DRS / loose-SLP path options until sprite
    preview is needed.

### M3: NameProvider
A single `QString text(int id)` over three sources:
1. language DLLs through `genie::LangFile` (lookup order x1p1 → x1 → base, as AGE
   does),
2. the DE2 key/value `.txt` string file (port of `LoadTXT`),
3. custom-name ini files.

List labels ("123 - Archer") and ID-reference fields all depend on it.

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
- `EntityListModel` (`QAbstractListModel`) + `QSortFilterProxyModel` for search.
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
