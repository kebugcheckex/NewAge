# NewAge CLI: design for agent access

Status: proposal, revision 3. P0 (section 4.1) is in the code: field keys, numeric unit, tech and effect command Type, `refName`, descriptor parsing, `EntityKind`, and `DataService`. Kinds stay in `newage_model`; `DataService` and `RequestHandler` are `newage_api`. P1 has started: `RequestHandler` and the `newage-cli` console executable serve `info`, `schema`, `lookup`, `list`, `get` and `batch` with shared source options; `cli_test` and `cli_process_test` check their JSON output. `mods list` is not built yet.

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
- **Effects are read-only** in the GUI, so they are read-only in the CLI until the GUI can edit them.
- **Every CLI call reloads the `.dat`.** For DE that takes a noticeable time (to be measured). Section 6.4 (`batch`) reduces the number of loads an agent needs, without a long-running process.

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
newage-cli diff --against-game                     # phase 3: what the mod changed, field by field
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
- `mods create` prints the new folder and reminds that the mod starts disabled in the game's mod manager.

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

- Tables: `civ`, `unit`, `tech`, `effect` (data) and `resource`, `unit-class`, `attribute`, `effect-type`, `unit-type`, `tech-type` (fixed lists from `ResourceNames`, `EffectNames` and friends).
- Matching is case-insensitive on the language name and the internal name; exact matches first, then prefix, then substring. With no `TEXT`, the whole table is returned.
- Civs have only internal names in the `.dat` (`Civ::Name`); civ language names are a later addition.
- `unit` needs `--civ` (`"civ"` in a request) and leaves out empty unit slots; the other tables reject `--civ`. A civ out of range is `unknown_entity` with kind `civ`; an unknown table is `unknown_kind`.
- Rows of data tables have `internalName`; tech rows also have `ownerCiv` (`Tech::Civ`, `-1` for any civ). Fixed-list rows have only `id` and `name`. With no `TEXT`, the result has no `query`, rows have no `match`, and rows are in ID order; with `TEXT`, rows are ordered by match quality, then ID. Unit results carry `civ`.
- So "Modify the Spanish tech Supremacy" becomes: `lookup civ spanish` → 9, `list tech --owner-civ 9` or `lookup tech supremacy` → 440, `get tech 440`, then `set`. The IDs are illustrative; the real ones come from the data.

**Field keys stay textual.** Fields have no natural number: a number would be the descriptor's position, which shifts whenever a field is added, and a wrong number would silently edit a different field. A mistyped key fails with `unknown_field`. `schema` lists every key with its display name and type.

`schema` with no kind returns `{"kinds": [...]}` in registry order. With a kind it returns `{"kind": "unit", "perCiv": true, "fields": [...]}`. Each field has `key`, `name`, `group`, `type`, and `editable`; editable integers also have `min` and `max`. Reference fields have `labelKind`. A `conditional` field may be absent on a particular entity; use `get` to see the applicable fields. Effects have a variable number of commands, so their schema uses `commandN.*` templates, where `N` is a one-based command number. A type such as `int|float` means the stored type depends on the command type. Schema uses the selected source's game version and requires an open data source.

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

Errors exit non-zero with a JSON body on stdout (warnings, such as missing language files, go to stderr as JSON lines):

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

`apply` takes edits that succeed or fail together: all are checked against the loaded data, then applied, then saved once.

```json
{
  "edits": [
    {"kind": "tech", "id": 440, "set": {"cost1.amount": 800}, "expect": {"cost1.amount": 1000}},
    {"kind": "unit", "id": 82, "civ": 1, "set": {"cost1.amount": 585}}
  ]
}
```

A result's `changes` list converts directly back into a patch (`old` as `expect`, or swapped to undo), so a dry run can be reviewed and then applied.

### 6.4 `batch`

`batch` reads a JSON array of read requests (`info`, `schema`, `lookup`, `list`, `get`) from a file or stdin (`-`), loads the data once, and returns the results in the same order. A failed request returns its error in place; the others still run. Writes stay in `set` and `apply`, so a batch can never save by accident.

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

The CLI reads and writes exactly what the descriptor tables describe, so the GUI and the CLI gain fields together. Coverage today is a small subset: of units, mostly identity, a few stats, costs and the first train location; of techs, costs, research time and location; effects read-only. Proposed order for new descriptors, judged by what balance and modding requests ask for:

1. **Editable effect commands.** Civ bonuses, unique techs and upgrades are effects, so most balance changes are edits to effect amounts and targets.
2. **Combat:** attacks and armours (class/amount lists), range, reload time, accuracy, blast, projectile unit, minimum range.
3. **Units:** all train locations, garrison, population, creation flags, enabled flag, upgrade lines.
4. **Techs:** required techs editable, `Civ` editable, all research locations (DE), repeatable (C15+).
5. **Later:** graphics, sounds, terrain, civ resources, language strings (needs writing language files).

**Lists inside an entity** (attacks, armours, train locations, effect commands) are edited in place only: existing entries can be changed, none added or removed. Their keys are positional (`attack2.class`, `attack2.amount`, `train_location2`), which is stable precisely because entries don't move. Adding and removing entries has its own problems (counts stored alongside lists, other data referring to entries by position, per-version limits) and needs its own design, shared by the GUI and the CLI.

## 9. The agent skill

A skill file teaches an agent when and how to use `newage-cli`. It lives in the repo at `skill/newage-data/SKILL.md`, ships next to the executable, and is installed by copying it to `~/.claude/skills/newage-data/` (or a project's `.claude/skills/`). It stays short and leaves the details to `schema` and `lookup`, so it doesn't go stale as fields are added.

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
- Only change existing entries; entries in lists (attacks, armours, train locations) can't be added or removed.
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

The GUI keeps data in memory and doesn't watch the file. If the CLI saves a mod's `.dat` while the GUI has it open, a later save from the GUI overwrites the CLI's change, and the other way round. Phase 2 documents this in the skill (don't edit a mod that's open in NewAge), and `--expect` catches conflicts between agents. Later: a `QFileSystemWatcher` in `MainWindow` that offers to reload when the open file changes on disk.

## 11. Phases

| Phase | Scope | Done when |
|-------|-------|-----------|
| P0 | Refactor (4.1): field keys, numeric `Type`, `refName`, descriptor parsing, `EntityKind`, `DataService`; list models delegate | Existing tests pass unchanged; key test added |
| P1 | Read-only CLI: `info`, `schema`, `lookup`, `list`, `get`, `batch`, `mods list`; source options | `cli_test` covers each op and error code; DE load time measured and recorded here |
| P2 | Writes: `set`, `apply`, `--expect`, `--dry-run`, `mods create`, mod-only save rule; the skill file | Tests for conflicts, atomic patches and the game-data guard; round-trip test passes after a no-op save; skill tried on real requests |
| P3 | `compare-civs`, `refs`, `diff --against-game` (all read-only) | `cli_test` covers each command |

Field coverage (section 8) grows alongside, in the GUI and the CLI together.

## 12. Deferred work

Not in this plan, but the design leaves room for each:

- **Persistent session and MCP.** `RequestHandler` already takes JSON requests, so a `newage-cli serve` (one request per line on stdin, data kept in memory, explicit `save`) or an MCP server over stdio would be another front end, not a rewrite. Revisit if load time makes one load per call too slow even with `batch`, or if a host needs MCP rather than a shell.
- **All-civs unit writes** (section 7), with a rule for civs whose value already differs.
- **Using the GUI's last game folder and mod** from `QSettings` as defaults for the CLI.
- **Writing original game files**, which would at least need a backup and restore story. Out of scope.
- **Adding and removing entries** in lists inside an entity (section 8). Needs a separate discussion.

## 13. Open questions

None block P0 to P2. The items in section 12 are to be discussed when they come up.
