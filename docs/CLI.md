# NewAge CLI: design for agent access

Status: proposal, revision 4. P0 (section 4.1) is in the code: field keys, numeric unit, tech and effect command Type and effect command Mode, `refName`, descriptor parsing, `EntityKind`, and `DataService`. Kinds stay in `newage_model`; `DataService` and `RequestHandler` are `newage_api`. P1 is done: `RequestHandler` and the `newage-cli` console executable serve `info`, `schema`, `lookup`, `list`, `get`, `batch` and `mods list` with shared source options; `cli_test` and `cli_process_test` check their JSON output. The DE load time is measured in section 3. P2 is done: `set`, `apply` (with `--expect` and `--dry-run`), `mods create` and the mod-only save rule are in the code, with the skill's write rules; `cli_test`, `cli_process_test` and `api_test` cover conflicts, atomic patches and the game-data guard. P3 has started: `diff` is in the code (section 6.6); `compare-civs` and `refs` are next. Effects and the remaining tech fields are editable, text fields included, and an effect's `command_count` adds and removes commands (section 8). The first `tech create` slice is described in [the tech creation plan](cli-tech-create.md).

## 1. Goal

Let AI agents read and edit Genie `.dat` data through a command-line tool, `newage-cli`, which they call from a shell guided by a skill file (section 9). People use the GUI; the CLI is for machines. Typical requests an agent receives:

- "What does the Archer cost and how long does it train?"
- "Make the Spanish unique tech cheaper."
- "Make the Castle 10% cheaper in my balance mod."
- "Apply these 40 balance changes and save once."

The CLI keeps the GUI's guarantees: the same fields, the same range checks, units edited per civ, and safe saving (temporary file, then replace). It never writes the original game data; every write goes into a mod.

## 2. Decisions so far

| # | Question | Decision |
|---|----------|----------|
| 1 | Output format | Structured only: JSON on stdout, nothing meant for people. No text mode. |
| 2 | Writing a unit for every civ | Future work, not in this plan. Each unit edit names one civ (section 7). |
| 3 | How things are named | Entities, civs and reference values are numeric IDs, in input and output. Names appear only as annotations in output and through `lookup`, which turns text like "Spanish" into candidate IDs. |
| 4 | Game data | Never written. Writes need a mod (`--mod`); data that can't have mods is read-only in the CLI. |
| 5 | MCP | Deferred. Agents use a skill file that calls the CLI. The design leaves room for a persistent session and MCP later (section 12). |
| 6 | Field coverage | Same as the GUI: what the GUI can read or edit, the CLI can read or edit, from the same descriptor tables. New coverage goes to the fields that matter for balance and modding first (section 8). |
| 7 | Picking up the GUI's last game folder and mod | Useful, not now. Agents use `--game` / `NEWAGE_GAME` and ask the user otherwise. |
| 8 | Data without mods | Read-only: classic AoE/AoK/TC/SWGB installs and loose `.dat` files. Writing original game files, even with a backup, is out of scope. |
| 9 | Lists inside an entity (attacks, armours, train locations) | Edit existing entries only. Adding and removing entries has its own problems and needs a separate design (section 8). |

## 3. Current state and what it means for a CLI

- **The data layers are already headless.** `newage_core` and `newage_model` link QtCore only, so a console program can use them under `QCoreApplication`, with no Widgets and no display.
- **The GUI executable can't double as the CLI.** `NewAge` is built with `WIN32` (GUI subsystem), so on Windows it has no console to print to. A separate console executable is cleaner.
- **The descriptor tables are the schema.** `unitFields()`, `techFields()` and `effectFields()` give each field's group, applicability, getter, optional setter and int range. Exposing exactly these is what keeps CLI and GUI coverage equal.
- **Fields are identified by display name** ("Hit points", "Cost 1 amount"). The CLI needs stable keys that don't change when a label is reworded.
- **Some values are text, not numbers.** Unit `Type` and tech `Type` are read as strings (`"70 - Combatant"`, `"2 - Age"`). With numeric IDs everywhere, they must become ints with a label, like reference fields.
- **Entity logic lives in Qt list models.** The label rule, whether a slot is in use, tech availability, the writers and the reference naming sit in `UnitListModel`, `TechListModel` and `EffectListModel`. The reference-naming switch is written out three times.
- **Value parsing is private to `FieldTreeModel`** (`parseValue`) and reports only success or failure, not why.
- **Effects were read-only** in the GUI. Their descriptors now have setters, so the GUI and the CLI edit them alike.
- **Every CLI call reloads the `.dat`.** Measured on 2026-10-04 against AoE2 DE's `empires2_x2_p1.dat` (12 MB, `VER 8.9`) on an i7-12700F from an NVMe SSD, with the file in the OS cache, median of 5 runs: a Release `newage-cli info` takes about 1.2 s, `get` the same, and a Debug build about 4.2 s. Nearly all of it is reading the `.dat`: a loose-file `--dat` open without language strings takes the same 1.2 s, and `mods list`, which loads no data, 18 ms. A `batch` of 10 `get` requests also takes 1.2 s, so section 6.4 (`batch`) keeps an agent's work at one load per step without a long-running process. A cold first read was not measured.

## 4. Architecture

```
          argv (one command)          batch file / stdin (several reads)
                  │                              │
           CommandLine parser                    │
                  └───────────────┬──────────────┘
                                  ▼
                 RequestHandler: JSON request → JSON result
                                  ▼
                 DataService (QtCore only): open, lookup, list, get, set, apply, mods
                                  ▼
       EntityKind registry (civ, unit, tech, effect): descriptors, keys, labels
                                  ▼
             Session (owns DatFile), Mods, GameInstall, NameProvider
```

Every command becomes a JSON request object handled by `RequestHandler`. A request is `{"op": "info"}` plus the fields that op uses. Source options are not in the request; the caller passes them beside it, because `batch` shares one open. Tests drive the handler in-process. A later `serve` mode or MCP server would feed it the same objects (section 12).

### 4.1 Refactor first (no change in behaviour)

These also simplify the GUI, and everything else depends on them.

1. **Stable field keys.** Add `QString key` to `FieldDesc<T>`, set explicitly in each table: snake_case, dotted for numbered slots. Examples: `hit_points`, `line_of_sight`, `speed`, `cost1.resource`, `cost1.amount`, `cost1.paid`, `train_location`, `train_time`, `research_time`, `required_tech1`, `command2.amount`. A `model_test` case checks that keys are unique per kind and pins the list, so renaming one is a deliberate change.
2. **Numeric `Type` fields.** Unit and tech `Type` read as ints and get a label kind (`RefKind::UnitType`, `RefKind::TechType`), so the GUI still shows `70 - Combatant` and the CLI returns `70` with label `"Combatant"`.
3. **One label function.** Move the three `RefNamer` switches into `refName(const Session &, RefKind, int id, int civ)` in `src/model/`. List models and the CLI both call it.
4. **Value parsing on descriptors.** Move `FieldTreeModel::parseValue` to a free function that takes a descriptor (value type and range) and returns the parsed value or the reason it was rejected. `FieldTreeModel::setData` calls it.
5. **Entity kinds without list models.** A type-erased interface so the CLI treats every kind the same way:

   ```cpp
   // One entity type as the CLI sees it. IDs are indices, as in the list
   // models. `civ` is ignored by global kinds.
   class EntityKind
   {
   public:
       virtual QString key() const = 0;            // "civ", "unit", "tech", "effect"
       virtual bool perCiv() const = 0;
       virtual int count(const Session &, int civ) const = 0;
       virtual bool isActive(const Session &, int civ, int id) const = 0;
       virtual QString name(const Session &, int civ, int id) const = 0;   // label rule
       virtual QString internalName(const Session &, int civ, int id) const = 0;
       // Applicable fields with values, labels, editability and ranges.
       virtual QList<FieldValue> fields(const Session &, int civ, int id) const = 0;
       // Parses, range-checks and stores; marks the session modified.
       virtual SetResult set(Session &, int civ, int id, const QString &key, const QVariant &value) = 0;
   };
   ```

   The list models then delegate `name()`, `isActive()` and their writers to the matching kind. Tech availability (`OtherCiv`, `DisabledByTechTree`) moves into the tech kind. Effects keep building their table per effect (`effectFields(effect, version)`); the interface hides that.
6. **`DataService`.** Opens a source (section 5), owns a `Session`, exposes the kinds, applies edits with a change list (target, key, old, new), and saves under the mod-only rule.

### 4.2 Targets

- `newage_api` (static, QtCore): entity kinds, `DataService`, `RequestHandler`, JSON conversion. Separate from `newage_model`, which stays about Qt item models.
- `newage-cli` (console executable, links `newage_api` only, `QCommandLineParser`). Not `newage` because `newage.exe` and `NewAge.exe` collide on case-insensitive file systems.
- `cli_test` (Qt Test) drives `RequestHandler`; game-data cases skip without `data/`, like the other tests.

## 5. Choosing the data, and where writes go

Options shared by every command:

| Option | Meaning |
|--------|---------|
| `--game DIR` | Game install folder, detected with `detectInstall`. |
| `--dataset FILE` | Which `.dat` when the folder has several (`empires2_x1_p1.dat`). Default: newest, as in the GUI. |
| `--mod NAME\|DIR` | Use this mod of the data set (`modDataset`). A name is matched against mod titles in `--mods-folder`, else the first of `modsFolders()`. |
| `--mods-folder DIR` | Overrides mods-folder detection (DE's profile is a guess). |
| `--dat FILE --version KEY` | A loose `.dat`, no strings, version key from `VersionProfile.cpp`. Read-only. |
| `--locale CODE` | Language folder for HD/DE. Default `en`. |

`NEWAGE_GAME`, `NEWAGE_DATASET`, `NEWAGE_MOD` and `NEWAGE_MODS_FOLDER` give defaults, so the skill can set them once per task.

**Write rule.** `set` and `apply` require `--mod`. The result goes to `<mod>/resources/_common/dat/<name>.dat`. If the mod has no `.dat` yet, the edit starts from the game's data and the save creates the mod's copy, as in the GUI. There is no option to write anywhere else. As a second check, the save path is refused if `isGameDataFile` says it belongs to the game. A new mod comes from `mods create`. Data sets without mod support (the CD-era games, loose `.dat` files) are read-only in the CLI. Writing original game files, even with a backup, is out of scope.

## 6. Commands

```
newage-cli info                                    # game, data set, mod, file version, counts, kinds
newage-cli schema [civ|unit|tech|effect]           # field keys, names, groups, types, ranges, editable, label kind
newage-cli lookup <table> [TEXT] [--civ N]         # text → candidate IDs, or the whole table
newage-cli list <kind> [--civ N] [--owner-civ N] [--all] [--limit N --offset N]
newage-cli get <kind> <id>... [--civ N] [--fields KEY,KEY,cost*] [--compact]
newage-cli batch FILE|-                            # several read requests, one load
newage-cli set <kind> <id> KEY=VALUE... --mod M [--civ N] [--expect KEY=VALUE...] [--dry-run]
newage-cli apply PATCH.json --mod M [--dry-run]
newage-cli mods list
newage-cli mods create --title T [--author A] [--description D] [--from game|mod:NAME]
newage-cli compare-civs unit <id> [--fields ...]   # phase 3: which civ copies differ, section 7
newage-cli refs <kind> <id>                        # phase 3: what refers to this entity
newage-cli diff [kind] --mod M [--civ N] [--fields ...] [--limit N --offset N]   # mod vs game, field by field, section 6.6
```

- Every argument that names an entity, a civ or a reference value is a number. Field keys are the one textual identifier (section 6.1).
- `list` hides inactive rows (empty unit slots, techs the civ can't research) unless `--all`, mirroring the GUI options. `--owner-civ N` lists techs whose `Civ` is N, which is how "the Spanish techs" (unique techs and civ bonuses) are found.
- `list unit` needs `--civ`. `list tech` takes an optional `--civ`: with it, techs that civ can't research are inactive; without it, no tech is hidden. `--owner-civ` (`-1` for techs any civ can research) is for techs only, and `civ` and `effect` take neither option. A civ or owner civ out of range is `unknown_entity` with kind `civ`.
- `list` returns `{"kind", "civ"?, "ownerCiv"?, "all"?, "offset", "limit"?, "total", "items"}`. Items are in ID order with `id`, `name` and `internalName`; tech items also have `ownerCiv`. With `--all`, items whose activity is known (units, and techs when a civ is given) have `active`. `total` counts the rows that pass the filters, before `--offset` and `--limit`, so an agent can page. In a request the options are `civ`, `ownerCiv`, `all` (bool), `offset` and `limit` (non-negative ints).
- `get` takes several IDs, so related entities come back in one call and one load. Items come back in the order of the IDs, duplicates included. The civ rules are those of `list`: units need `--civ`, techs take an optional one, which adds `active` (whether that civ can research the tech), and `civ` and `effect` take none. Unit labels in tech and effect fields name the given civ's copy, else civ 0's.
- `get` fails as a whole when one ID is bad: out of range is `unknown_entity`, an empty unit slot `inactive_entity`. `--fields` (`"fields"` in a request) takes keys and `*` patterns; fields come back in descriptor order. A pattern that matches no key in the kind's `schema` is `unknown_field`; one that matches only fields this entity lacks (a conditional field, a command number past the effect's last) is not an error. Effect command patterns are checked against the `commandN.*` templates, so `command3.amount` and `command1.*` are known keys. Civs have no fields yet, so `get civ` returns only names.
- In a request: `{"op": "get", "kind": "unit", "ids": [4], "civ": 1, "fields": ["hit_points", "cost*"], "compact": true}`. `ids` is a non-empty array of ints.
- `set` checks every assignment before changing anything and saves once. Writing the current value is reported as unchanged and doesn't count as an edit.
- `--expect` is compare-and-set: if a current value differs from the expected one, nothing is written and the command fails with `conflict`. The skill tells agents to pass the values they read, so a change made in between (in the GUI, or by another agent) isn't silently lost.
- `--dry-run` validates and reports the changes without saving.
- `set` takes `KEY=VALUE` pairs, so values are text on the command line: a whole number or a decimal is sent as a JSON number, anything else as text, which the field parser rejects with `bad_value`. `--expect` has the same form and may only name keys that `set` writes. `--civ` is for units; a global kind with `--civ` is `usage`.
- `set` and `apply` check the request, then that a mod is named, before loading the data: no mod is `mod_required` (exit 5) and a loose `.dat` is `mods_unsupported` (exit 2), each without the load.
- `mods create` needs `--title` and makes `<mods folder>/<title>/info.json`; it loads no data. `--from mod:NAME` also copies that mod's `.dat` for this data set when it has one. `--mods-folder` picks the folder as in `mods list`. The result has `title`, `dir`, `dat` (where the mod's `.dat` goes), `hasDat` (whether `--from` copied one) and a `note` that the mod starts disabled in the game's mod manager. A title the folder can't take (taken, reserved or illegal characters) is `bad_value`; an unknown source mod is `mod_not_found`.

### 6.1 IDs, keys and `lookup`

Numbers are the identifiers because names are ambiguous: several units and techs can share a language name (hidden helper units and techs often reuse the visible one's string), and internal names differ between versions. `lookup` is the bridge from the user's words to IDs:

```
newage-cli lookup civ spanish
newage-cli lookup tech supremacy
newage-cli lookup unit conquistador --civ 9
newage-cli lookup resource                 # the whole table: 0 Food Storage, 1 Wood Storage, ...
```

```json
{
  "table": "tech", "query": "supremacy",
  "matches": [
    {"id": 440, "name": "Supremacy", "internalName": "Supremacy", "ownerCiv": 9, "match": "exact"}
  ]
}
```

- Tables: `civ`, `unit`, `tech`, `effect` (data) and `resource`, `unit-class`, `attribute`, `effect-type`, `unit-type`, `tech-type`, `resource-mode`, `tech-modifier-mode`, `enable-mode`, `upgrade-mode` (fixed lists from `ResourceNames`, `EffectNames` and friends). Every `labelKind` that isn't a data kind is one of these tables.
- The mode tables label an effect command's `mode` field, whose label kind depends on the command type: `resource-mode` (Resource Modifier: 0 Set, else +/-), `tech-modifier-mode` (Tech Cost and Tech Time Modifier: 0 Set, 2 Multiply in DE, else +/-), `enable-mode` (Enable/Disable Unit: 0 Disable, else Enable) and `upgrade-mode` (DE Upgrade Unit: -1 All, else On map). They list the values the data uses, but every value gets a label, because the games treat any other value alike: DE data uses -1 for adding and for enabling.
- Matching is case-insensitive on the language name and the internal name; exact matches first, then prefix, then substring. With no `TEXT`, the whole table is returned.
- Civs have only internal names in the `.dat` (`Civ::Name`); civ language names are a later addition.
- `unit` needs `--civ` (`"civ"` in a request) and leaves out empty unit slots; the other tables reject `--civ`. A civ out of range is `unknown_entity` with kind `civ`; an unknown table is `unknown_kind`.
- Rows of data tables have `internalName`; tech rows also have `ownerCiv` (`Tech::Civ`, `-1` for any civ). Fixed-list rows have only `id` and `name`. With no `TEXT`, the result has no `query`, rows have no `match`, and rows are in ID order; with `TEXT`, rows are ordered by match quality, then ID. Unit results carry `civ`.
- So "Modify the Spanish tech Supremacy" becomes: `lookup civ spanish` → 9, `list tech --owner-civ 9` or `lookup tech supremacy` → 440, `get tech 440`, then `set`. The IDs are illustrative; the real ones come from the data.

**Field keys stay textual.** Fields have no natural number: a number would be the descriptor's position, which shifts whenever a field is added, and a wrong number would silently edit a different field. A mistyped key fails with `unknown_field`. `schema` lists every key with its display name and type.

`schema` with no kind returns `{"kinds": [...]}` in registry order. With a kind it returns `{"kind": "unit", "perCiv": true, "fields": [...]}`. Each field has `key`, `name`, `group`, `type`, and `editable`; editable integers also have `min` and `max`, and editable strings have `max`, the longest text the field holds. Reference fields have `labelKind`. A `conditional` field may be absent on a particular entity; use `get` to see the applicable fields. Effects have a variable number of commands, so their schema uses `commandN.*` templates, where `N` is a one-based command number. A type such as `int|float` means the stored type depends on the command type. Schema uses the selected source's game version and requires an open data source.

### 6.2 Output shapes

`info` reports the open source. `version` is the profile key; `fileVersion` is the string stored in the file. `counts` are each kind's count, with units counted in civ 0 (the copy the GUI status line reads from the first civ). `readOnly` is true when there is no mod save path. `dat` is the file that was read, which is the game file when the mod has no copy yet. Empty `game` and `mod` mean a loose file.

```json
{
  "game": "C:/Games/AoE2DE",
  "dataset": "empires2_x2_p1.dat",
  "version": "aoe2de",
  "fileVersion": "VER 5.8",
  "mod": "",
  "dat": "C:/Games/AoE2DE/resources/_common/dat/empires2_x2_p1.dat",
  "readOnly": true,
  "counts": {"civ": 43, "unit": 1400, "tech": 800, "effect": 700},
  "kinds": ["civ", "unit", "tech", "effect"]
}
```

`get unit 4 --civ 1 --fields hit_points,cost*`:

```json
{
  "kind": "unit", "civ": 1,
  "items": [
    {
      "id": 4, "name": "Archer", "internalName": "ARCHR",
      "fields": [
        {"key": "hit_points", "name": "Hit points", "group": "Stats", "type": "int", "value": 30, "editable": true, "min": -32768, "max": 32767},
        {"key": "cost1.resource", "name": "Cost 1 resource", "group": "Costs", "type": "int", "value": 1, "labelKind": "resource", "label": "Wood Storage", "editable": true, "min": -32768, "max": 32767},
        {"key": "cost1.amount", "name": "Cost 1 amount", "group": "Costs", "type": "int", "value": 25, "editable": true, "min": -32768, "max": 32767}
      ]
    }
  ]
}
```

- The result always has `items`, even for one ID, so one ID and several read the same way. Tech items have `active` when a civ is given.
- `value` is always the stored number. `label` (reference fields) and `text` (string-ID fields) are annotations and are never accepted as input.
- Floats use the shortest form that reads back as the same float (`0.2`), as `FieldTreeModel::displayText` does, so writing back what was read changes nothing.
- `--compact` returns each item's `"fields": {"hit_points": 30, ...}` for bulk reads.
- `label` is left out when the reference has no name (`-1`, or a missing entity); `labelKind` stays.

`set` and `apply` result:

```json
{
  "saved": "C:/Users/.../mods/local/Balance/resources/_common/dat/empires2_x2_p1.dat",
  "changes": [
    {"kind": "tech", "id": 440, "key": "cost1.amount", "old": 1000, "new": 800}
  ],
  "unchanged": []
}
```

`saved` is the path written, and an empty string when nothing was saved: a dry run, or no value changed. A dry run also has `"dryRun": true`. Changes carry `civ` only for units. In a request: `{"op": "set", "kind": "unit", "id": 4, "civ": 1, "set": {"hit_points": 35}, "expect": {"hit_points": 30}, "dryRun": true}`, `{"op": "apply", "edits": [...], "dryRun": true}` and `{"op": "mods-create", "title": "Balance", "author": "Me", "description": "...", "from": "game"}`. Neither can be in a `batch`.

Errors exit non-zero with a JSON body on stdout (warnings, such as missing language files, go to stderr as JSON lines, and so do the loader's own messages, such as `Terrains: 100`, as `{"diagnostic": ...}`; a successful write prints them too):

```json
{"error": {"code": "out_of_range", "message": "hit_points accepts -32768..32767", "kind": "unit", "id": 4, "civ": 1, "key": "hit_points", "value": 99999}}
```

| Exit | Codes |
|------|-------|
| 0 | success |
| 1 | `usage` |
| 2 | `load_failed`, `no_dataset`, `mod_not_found`, `mods_unsupported` |
| 3 | `unknown_kind`, `unknown_entity`, `inactive_entity`, `unknown_field`, `not_applicable`, `read_only`, `bad_value`, `out_of_range` |
| 4 | `conflict` |
| 5 | `mod_required`, `game_data_protected`, `save_failed` |

### 6.3 Patch files

`apply` takes edits that succeed or fail together. They are checked and applied one after another, so each edit sees the ones before it (an `expect` is compared with what the earlier edits left), and the file is saved once at the end. If any edit fails, or the save fails, every entity the patch touched is restored and nothing is saved. A dry run restores them too.

```json
{
  "edits": [
    {"kind": "tech", "id": 440, "set": {"cost1.amount": 800}, "expect": {"cost1.amount": 1000}},
    {"kind": "unit", "id": 82, "civ": 1, "set": {"cost1.amount": 585}}
  ]
}
```

A result's `changes` list converts directly back into a patch (`old` as `expect`, or swapped to undo), so a dry run can be reviewed and then applied. `set` is a patch of one entity, and `apply` reads the file (or `-` for stdin) with the same rules.

An edit's `set` is an object of key to value; `expect` has the same shape and only keys that `set` writes. A number field takes a number or numeric text; a string field takes text, which must be Latin-1 (the `.dat` stores bytes) and no longer than the field's `max`. A unit edit needs `civ` and a global kind takes none. Edits run in the order of the file. JSON objects are unordered, so the keys of one edit run in a fixed order: for effects, `command_count` first, then each `commandN.type`, then the rest, so a patch can add commands and fill them in; otherwise alphabetical.

### 6.4 `batch`

`batch` reads a JSON array of read requests (`info`, `schema`, `lookup`, `list`, `get`) from a file or stdin (`-`), loads the data once, and returns the results in the same order. A failed request returns its error in place; the others still run. Writes stay in `set` and `apply`, so a batch can never save by accident: a `set`, `apply` or `mods-create` request in a batch is `usage` in its place, and so is `diff`, which loads a second file.

```json
[
  {"op": "lookup", "table": "civ", "text": "spanish"},
  {"op": "get", "kind": "tech", "ids": [440, 441], "fields": ["cost*", "research_time"]}
]
```

```json
{
  "results": [
    {"table": "civ", "query": "spanish", "matches": [...]},
    {"error": {"code": "unknown_entity", "message": "No tech 441.", "kind": "tech", "id": 441}}
  ],
  "failed": 1
}
```

- The output is an object, like every other command's, so a top-level `error` always means the batch as a whole failed. Each entry of `results` is exactly what the request prints on its own: its result, or `{"error": ...}`. `failed` counts the entries that are errors.
- The exit status is 0 when the batch ran, whatever its entries did; check `failed`. The batch as a whole fails with `usage` (exit 1) when the input can't be read, isn't JSON, or isn't a non-empty array, and with the open error (exit 2) when the data can't be loaded.
- Source options (`--game`, `--mod`, `--dat`, ...) apply to the whole batch. Per-request options such as `civ` go inside each request; `--civ` and the other command options are rejected on `batch`.
- Requests are checked before the data is opened. A request that isn't an object, has an unknown op, or is itself a `batch` is `usage` in its place. When no request passes those checks, the data isn't opened.
- As a handler request (for a future `serve` mode): `{"op": "batch", "requests": [...]}`.

### 6.5 `mods list`

`mods list` lists the mods of the chosen data set without loading its data, so it is quick even on DE. The mods folder is `--mods-folder` (or `NEWAGE_MODS_FOLDER`) when given, else the first of `modsFolders()`: `<game>/mods` for HD, and for DE the `mods/local` folder of the profile the game has used for mods, else the most recently changed one. This is the folder `--mod NAME` is matched in.

```json
{
  "game": "C:/Games/AoE2DE",
  "dataset": "empires2_x2_p1.dat",
  "modsFolder": "C:/Users/.../Games/Age of Empires 2 DE/76561198000000000/mods/local",
  "otherModsFolders": ["C:/Users/.../Games/Age of Empires 2 DE/0/mods/local"],
  "mods": [
    {"title": "Balance", "dir": "C:/Users/.../mods/local/Balance", "author": "Me", "description": "Cheaper archers", "hasDat": true}
  ]
}
```

- `mods` are sorted by title. The title comes from the mod's `info.json`, else its folder name; `author` and `description` are empty strings when it has none.
- `hasDat` says whether the mod has its own copy of this data set's `.dat`. A write to a mod without one starts from the game's data, so it depends on the data set: an HD mod may have the HD `.dat` and not the Conquerors one.
- `otherModsFolders` lists the other folders found (DE profiles, or the detected folder when `--mods-folder` points elsewhere). It is left out when there are none. When DE has no profile with a mods folder, `modsFolder` is empty, `mods` is empty and a warning says to pass `--mods-folder`.
- A loose `.dat` or a data set without mods (the CD-era games) is `mods_unsupported`; a missing game folder or data set is `no_dataset`. Both exit 2.
- `--mod` and the command options are rejected; `NEWAGE_MOD` is ignored, since the skill often sets it.
- As a handler request: `{"op": "mods-list"}`. It can't be in a `batch`, which is for reads of the loaded data.

### 6.6 `diff`

`diff` compares the mod's `.dat` with the game's current `.dat` for the same data set, and lists every entity that differs. It is read-only. Its main use is a mod made for an older game version: the game's updates since then show up next to the mod's own edits, so it tells what the mod would undo or lacks.

```json
{
  "mod": "C:/Users/.../mods/local/Balance",
  "modDat": "C:/Users/.../mods/local/Balance/resources/_common/dat/empires2_x2_p1.dat",
  "hasDat": true,
  "gameDat": "C:/Games/AoE2DE/resources/_common/dat/empires2_x2_p1.dat",
  "fileVersion": {"mod": "VER 8.8", "game": "VER 8.9"},
  "counts": {"civ": {"mod": 60, "game": 63}, "unit": {"mod": 2701, "game": 2750}, "tech": {...}, "effect": {...}},
  "changed": 884, "onlyInGame": 145, "onlyInMod": 5, "total": 1034,
  "offset": 0, "limit": 2,
  "items": [
    {"status": "onlyInGame", "kind": "civ", "id": 60, "name": "Saxons", "internalName": "Saxons"},
    {"status": "changed", "kind": "unit", "id": 125, "civs": [11], "name": "Monk", "internalName": "MONKX",
     "fields": [{"key": "standing_graphic1", "mod": 998, "game": 19519}, {"key": "icon", "mod": 33, "game": 910}],
     "unlisted": true}
  ]
}
```

- Entities are matched by ID. `status` is `changed`, `onlyInGame` (a civ, unit, tech or effect the mod predates, or a unit slot the mod emptied) or `onlyInMod`. Items come in kind order (civ, unit, tech, effect), then by ID. `name` and `internalName` are the mod's, or the game's when the mod lacks the entity or has a blank placeholder there (an empty internal name), which is how a game update's new techs look in an older mod.
- `fields` (changed items only) are the fields whose stored values differ, in `schema` order, with `mod` and `game` values. A field that only one side has, such as an effect command past the other side's last, is `null` on the other side. `labelKind`, `modLabel`/`gameLabel` and `modText`/`gameText` annotate values as in `get`.
- `unlisted: true` means the entity also differs in data no field covers yet (attacks, armors, damage graphics, ...; section 8). A changed item can have only that and an empty `fields`. It is not checked when `--fields` is given.
- Units are compared in every civ both files have. Civs with the very same difference share one item, listed in `civs`. `--civ N` compares only that civ. Civs themselves are compared by presence only, since they have no fields yet.
- Effect bytes that no field reads (parameters a command type ignores) are not differences.
- Each side keeps its own file version. Entities are compared as the older of the two versions writes them, so a field that only the newer format stores doesn't make everything differ.
- `[kind]` limits the comparison to one kind; `--fields` (key patterns, as in `get`) needs a kind and limits the fields compared. `changed`, `onlyInGame`, `onlyInMod` and `total` count every item; `--offset` and `--limit` page `items`.
- `hasDat: false` means the mod has no `.dat` of its own, so it plays with the game's data and nothing is compared: totals are 0.
- No mod is `mod_required` (exit 5) and a loose `.dat` is `mods_unsupported` (exit 2), both before loading. It loads both files, about twice an `info`'s time, plus the comparison (about 20 s for all of DE in a debug build). It can't be in a `batch`.
- As a handler request: `{"op": "diff", "kind": "unit", "civ": 1, "fields": ["hit_points"], "offset": 0, "limit": 50}`, every member optional.

## 7. Civs and units

How the data is laid out:

- **Techs are global.** `DatFile::Techs` holds one copy. A tech's `Civ` field marks a unique tech or civ bonus for one civ; `-1` means any civ. "The Spanish tech X" is still a single global record.
- **Units are per civ.** Each `Civ` holds its own `Units` array (`Civ::Units`, `Civ::UnitPointers`), a full copy of every unit for that civ. The GUI edits only the selected civ's copy, which is why the Units tab has a civ combo.
- **Civ differences usually come from effects, not from different unit copies.** Civ bonuses and unique techs are effects run by techs, and each civ's tech tree (`Civ::TechTreeID`) is an effect that disables what the civ can't build. The per-civ unit copies are mostly identical in stats but can differ, for example in building graphics per architecture set.

So "copy across civs" matters only when a user wants to change a unit's base stats for everyone: the change has to be written into every civ's copy, or civs keep the old value.

In this plan, every unit read and write names one civ (`--civ N`, or `"civ": N` in a patch edit), as in the GUI. There is no `--civ all` and no automatic copying. A user who asks for a change in several civs gets one patch edit per civ, which the agent lists in its dry run so the user sees each civ being changed.

`compare-civs unit <id>` (phase 3) is read-only: it reports, per field, whether the civ copies agree and lists the outliers. It helps agents notice that a unit differs between civs before editing one copy.

Future work (section 12): an all-civs write, including how to treat civs whose value already differs from the rest, which were probably changed on purpose.

## 8. Field coverage

The CLI reads and writes exactly what the descriptor tables describe, so the GUI and the CLI gain fields together. Coverage today: of units, mostly identity, a few stats, costs and the first train location; of techs, every field but the ID (names and string IDs, type, civ, effect, icon, required techs and their count, costs, location, research time, button, hotkey); of effects, the internal name, the command count and every command field. Proposed order for new descriptors, judged by what balance and modding requests ask for:

1. **Combat:** attacks and armours (class/amount lists), range, reload time, accuracy, blast, projectile unit, minimum range.
2. **Units:** all train locations, garrison, population, creation flags, enabled flag, upgrade lines.
3. **Techs:** all research locations (DE), repeatable (C15+).
4. **Later:** graphics, sounds, terrain, civ resources, language strings (needs writing language files).

**Lists inside an entity** (attacks, armours, train locations) are edited in place only: existing entries can be changed, none added or removed. Their keys are positional (`attack2.class`, `attack2.amount`, `train_location2`), which is stable precisely because entries don't move. Adding and removing entries has its own problems (counts stored alongside lists, other data referring to entries by position, per-version limits) and needs its own design, shared by the GUI and the CLI.

Effect commands are the exception. Nothing refers to a command by position, so `command_count` resizes the list at the end: new commands have no type (255) and unused parameters (-1, amount 0), and shrinking drops the last ones. A new command's parameters are written after its type (section 6.3). A parameter its type doesn't use is hidden in `get` but can still be written, because the type that uses it may come later in the same patch. Fixed-size slots work the same way: a tech's unused required tech or cost entry can be filled in.

**Adding entities** (a new tech or effect) is not supported. Writing into an unused slot is: a tech with no effect and an effect with no commands are free, and filling one in through a patch makes it a new tech or effect. The proposed CLI workflow for appending a civ-specific tech, its effect, and locale strings is in [CLI creation of civilization-specific technologies](cli-tech-create.md).

## 9. The agent skill

A skill file teaches an agent when and how to use `newage-cli`. It lives in the repo at `skill/newage-data/SKILL.md`, ships next to the executable, and is installed by copying it to `~/.claude/skills/newage-data/` (or a project's `.claude/skills/`). It stays short and leaves the details to `schema` and `lookup`, so it doesn't go stale as fields are added.

The file in the repo covers P1 and P2: the read commands and the write rules from the outline below. It has not yet been run through the real-request check at the end of this section against a DE and an HD install.

Draft outline:

```markdown
---
name: newage-data
description: Read and edit Age of Empires, AoE II and Star Wars Galactic Battlegrounds game data (units, techs, effects, civs in .dat files) with newage-cli. Use when asked about unit stats, costs, techs or civ bonuses, or to make balance changes in a mod.
---

## Setup
- Game folder: $NEWAGE_GAME, else ask the user. Run `newage-cli info` first.

## Rules
- Use numeric IDs. Resolve every name with `newage-cli lookup`; if there are several matches, show them and ask.
- Units are per civ: always pass --civ, and say which civ you read or changed. Techs are global. To change a unit in several civs, write one patch edit per civ and show them all in the dry run.
- Only change existing entries; entries in unit lists (attacks, armours, train locations) can't be added or removed. Effect commands can: set `command_count`, then the new commands' fields, in one patch.
- Only write into a mod. List mods with `newage-cli mods list`; create one (ask for a title) with `mods create`.
- Read before writing, pass what you read as --expect, and run --dry-run first. Show the user the changes, then apply.
- Use `batch` and multi-ID `get` to keep the number of calls down.
- Exit codes and error codes: ...

## Workflow examples
- "Make the Spanish unique tech cheaper": lookup civ → list tech --owner-civ → get → set --dry-run → set.
- Balance patch: write PATCH.json → apply --dry-run → apply → get to verify.
- Tell the user to enable a new mod in the game's mod manager.
```

The skill is validated by running a handful of real requests through an agent against a DE install and an HD install before it ships.

## 10. Concurrency with the GUI

The GUI keeps data in memory and doesn't watch the file. If the CLI saves a mod's `.dat` while the GUI has it open, a later save from the GUI overwrites the CLI's change, and the other way round. The skill says so (don't edit a mod that's open in NewAge), and `--expect` catches conflicts between agents. Later: a `QFileSystemWatcher` in `MainWindow` that offers to reload when the open file changes on disk.

## 11. Phases

| Phase | Scope | Done when |
|-------|-------|-----------|
| P0 | Refactor (4.1): field keys, numeric `Type`, `refName`, descriptor parsing, `EntityKind`, `DataService`; list models delegate | Existing tests pass unchanged; key test added |
| P1 | Read-only CLI: `info`, `schema`, `lookup`, `list`, `get`, `batch`, `mods list`; source options | `cli_test` covers each op and error code; DE load time measured and recorded here |
| P2 (done) | Writes: `set`, `apply`, `--expect`, `--dry-run`, `mods create`, mod-only save rule; the skill file | Tests for conflicts, atomic patches and the game-data guard; round-trip test passes after a no-op save; skill tried on real requests |
| P3 (`diff` done) | `compare-civs`, `refs`, `diff` (all read-only) | `cli_test` covers each command |
| Fields (effects and techs done) | Section 8: editable effects and command counts, the remaining tech fields, text fields, sequential `apply` | `model_test` covers each descriptor; `cli_test` builds a tech and its effect in one patch and checks that a failed patch restores everything |
| Tech creation (proposed) | Append a tech and effect in a mod through `tech create`, with validation and locale strings; see [the detailed plan](cli-tech-create.md) | Both modes reload correctly, dry-run and failure leave files unchanged, strings do not collide, and the new tech works only for its civ in game |

Field coverage (section 8) grows alongside, in the GUI and the CLI together.

## 12. Deferred work

Not in this plan, but the design leaves room for each:

- **Persistent session and MCP.** `RequestHandler` already takes JSON requests, so a `newage-cli serve` (one request per line on stdin, data kept in memory, explicit `save`) or an MCP server over stdio would be another front end, not a rewrite. Revisit if load time makes one load per call too slow even with `batch` (about 1.2 s per load on DE, section 3), or if a host needs MCP rather than a shell.
- **All-civs unit writes** (section 7), with a rule for civs whose value already differs.
- **Three-way `diff`** against a saved copy of the game's older `.dat` (`--base FILE`), to tell the mod's own edits from the game's later updates, and a patch of the mod's edits to replay onto a fresh mod with `apply`.
- **Using the GUI's last game folder and mod** from `QSettings` as defaults for the CLI.
- **Writing original game files**, which would at least need a backup and restore story. Out of scope.
- **Adding and removing entries** in unit lists (section 8), and adding whole entities other than the proposed tech/effect workflow. Needs a separate discussion.

## 13. Open questions

None block P0 to P2. The items in section 12 are to be discussed when they come up.
