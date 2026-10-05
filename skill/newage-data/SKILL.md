---
name: newage-data
description: Read Age of Empires, AoE II and Star Wars Galactic Battlegrounds game data (units, techs, effects, civs in Genie .dat files) with newage-cli. Use when asked about unit stats, costs, train or research times, techs, unique techs, civ bonuses or effects, or what a mod changes, for AoE, AoE II (AoK, TC, HD, DE) or SWGB.
---

# newage-data

`newage-cli` reads Genie engine `.dat` files and prints JSON. It is the command-line side of the NewAge editor and shows the same fields as the editor.

**This version is read-only.** It has `info`, `schema`, `lookup`, `list`, `get`, `batch` and `mods list`. There is no `set`, `apply` or `mods create` yet, so you cannot change data with it. If the user asks for a change, read the current values, say exactly which entity, civ, field key and new value the change needs, and tell them to make it in the NewAge editor inside a mod. Never edit a `.dat` file any other way.

## Setup

- Run `newage-cli` from `PATH`. If it isn't there and you are in a NewAge source checkout, use `build/<preset>/Release/newage-cli.exe` (or `Debug`). Otherwise ask the user where NewAge is installed (the CLI is `newage-cli.exe` next to `NewAge.exe`).
- Pick the data source with options shared by every command, or set the matching environment variable once per task:

  | Option | Env var | Meaning |
  |--------|---------|---------|
  | `--game DIR` | `NEWAGE_GAME` | Game install folder. Ask the user if neither is set. |
  | `--dataset FILE` | `NEWAGE_DATASET` | Which `.dat` when the game has several, such as HD's `empires2_x1_p1.dat`. Default: the newest. |
  | `--mod NAME\|DIR` | `NEWAGE_MOD` | Read this mod's copy of the data. NAME is a mod title from `mods list`. |
  | `--mods-folder DIR` | `NEWAGE_MODS_FOLDER` | Where mods are, when detection picks the wrong folder (DE has one per profile). |
  | `--dat FILE --version KEY` | | A loose `.dat` without language strings. Keys: `aoe`, `ror`, `aoede`, `aok`, `tc`, `tcv`, `aokhd`, `aoe2de`, `swgb`, `cc`, `ef`, `ef2`. Not combinable with `--game`. |
  | `--locale CODE` | | Language folder for HD/DE names. Default `en`. |

- Start with `newage-cli info`. It reports the game, data set, `version`, the file that was read (`dat`), the mod, and entity counts. If `--mod` is set and the mod has no `.dat` of its own, `dat` is the game's file. A mod's `.dat` can be older than the game's (compare `fileVersion`), in which case it lacks newer units and techs.
- Civs differ between games: AoE and AoE DE have no Spanish, Franks or Britons, for example. If `lookup civ` finds no match, the civ isn't in that game. Tell the user, and don't swap in another game without saying so.
- Every call loads the whole `.dat`: about 1 s on AoE2 DE. Use `batch` and multi-ID `get` (below) instead of many separate calls.

## Output and errors

- stdout carries exactly one JSON object per call. stderr carries JSON lines `{"warning": {...}}` and `{"diagnostic": {...}}` (for example missing language files or loader messages). Read stdout; glance at stderr only when something looks wrong.
- Failure: non-zero exit and `{"error": {"code", "message", "kind"?, "id"?, "civ"?, "key"?, "value"?}}` on stdout.

  | Exit | Codes | What to do |
  |------|-------|------------|
  | 1 | `usage` | Fix the command line; the message says what is wrong (for example `get unit needs a civ.`). |
  | 2 | `load_failed`, `no_dataset`, `mod_not_found`, `mods_unsupported` | Wrong game folder, data set, mod or version key. Check with the user. |
  | 3 | `unknown_kind`, `unknown_entity`, `inactive_entity`, `unknown_field` | Bad table, ID or field key. Re-check with `lookup` or `schema`. |

## Identifiers

- **Entities, civs and reference values are numeric IDs**, in arguments and output. Names are annotations only. Several entities often share a name (hidden helper units and techs reuse the visible one's string), so never act on a name alone.
- **Resolve every name with `lookup`** and use the ID it returns. Matching is case-insensitive over the display name and internal name; results come exact first, then prefix, then substring, each row with `match`. If there is more than one plausible match, pick by `internalName` and context, or show the candidates to the user and ask.
- **`lookup` TEXT matches names, never IDs**, so `lookup resource 269` finds nothing. To name a bare ID, read the `label` the field already carries (full output, not `--compact`), or run `lookup <table>` without TEXT and find the ID in the list.
- **Some IDs have no name or text**: unnamed resources, or `language_*` IDs with no string in the locale. Report the raw ID and say it has no name. Don't guess what it is.
- **Field keys are the only text identifiers**: `hit_points`, `cost1.amount`, `train_time`, `research_time`, `required_tech1`, `command2.amount`. Get them from `schema`; don't guess. Numbered slots are one-based.
- In shells that glob (bash, zsh), quote field patterns: `--fields 'cost*'`.

## Data model

- **Kinds:** `civ`, `unit`, `tech`, `effect`.
- **Units are per civ.** Each civ has its own full copy of every unit, so every unit read needs `--civ N`, and the copies can differ. Say which civ you read. When the civ doesn't matter, use civ 1 or the civ the user mentioned, and say so. Civ 0 is Gaia in AoE II.
- **Techs and effects are global.** A tech's `civ` field (`ownerCiv` in lists) is the civ that owns it: unique techs and civ bonuses belong to one civ, `-1` means any civ. Passing `--civ N` with techs adds `active`: whether that civ can research the tech.
- **Effects do the work.** A tech runs one effect (the tech's `effect` field). Civ bonuses, unique techs, upgrades and the tech tree are all effects made of commands (`command1.*`, `command2.*`, ...). Command `type` says what a command does (`lookup effect-type`).
- **Civs** have names only; `get civ` returns no fields yet.
- **Most civ bonuses are not techs.** A civ's tech-tree effect holds its permanent bonuses (cost cuts, work rates, reload times) next to the Disable Tech commands (type 102) that shape its tech tree. A separate team-bonus effect holds the team bonus. A civ links to both, but `get civ` can't show that link, and the civ's own tech in `list tech --owner-civ` (such as tech 542 "Spanish" in AoE2 DE) often has `effect` -1. Find these effects with `lookup effect <civ name>`. Their names vary by game: `Spanish Tech Tree` in DE and `Spanish Technology Tree` in HD, plus `Spanish Team Bonus`. Skip the type 102 commands; what's left are the bonuses.
- **`-1` is not always empty.** In reference fields (`unit`, `class`, `resource`, `tech`, ...) `-1` means unset. In `commandN.amount` it is a real value: Inquisition's faster conversion is a `-1` on Convert Min/Max Adjustment. Never drop `amount` fields because they are `-1`.
- **Labels:** a reference field has `labelKind` (the table its value points into) and, when the target has a name, `label`. Look up a `labelKind` table with `lookup <labelKind>` to see all values.
- **Costs:** `costN.resource` is a resource ID (`lookup resource`: 0 food, 1 wood, 2 stone, 3 gold, 4 population headroom), `costN.amount` the amount, and `costN.paid` whether it is deducted (population headroom is checked, not paid, so it has `paid` 0).
- **Attack and armour amounts are packed.** An Attribute Modifier command with attribute 8 (armour) or 9 (attack) stores `class * 256 + value` in `amount`: `1030` is class 4 (melee) +6, `770` is class 3 (pierce) +2. Decode it before reporting.
- **Times** (`train_time`, `research_time`) are in game seconds.
- Coverage is partial: units have identity, a few stats, costs, the first train location and some graphics; attacks, armours, range and reload time are not available yet. If a field the user wants isn't in `schema`, say so instead of guessing.

## Commands

```
newage-cli info
newage-cli schema [civ|unit|tech|effect]
newage-cli lookup <table> [TEXT] [--civ N]
newage-cli list <kind> [--civ N] [--owner-civ N] [--all] [--limit N] [--offset N]
newage-cli get <kind> <id>... [--civ N] [--fields KEY,PATTERN*,...] [--compact]
newage-cli batch FILE|-
newage-cli mods list
```

- **`schema [kind]`** lists each field's `key`, `name`, `group`, `type`, `editable`, `min`/`max` and `labelKind`. `conditional` fields exist only on some entities. Effects use `commandN.*` templates. Run it once per task for the kinds you need.
- **`lookup <table> [TEXT]`** turns words into IDs. Data tables: `civ`, `unit` (needs `--civ`), `tech` (rows have `ownerCiv`), `effect`. Fixed tables: `resource`, `unit-class`, `attribute`, `effect-type`, `unit-type`, `tech-type`, `resource-mode`, `tech-modifier-mode`, `enable-mode`, `upgrade-mode`. Without TEXT it returns the whole table in ID order.
- **`list <kind>`** returns `{total, offset, limit?, items: [{id, name, internalName, ownerCiv?}]}`. `list unit` needs `--civ` and hides empty slots; `list tech --civ N` hides techs that civ can't research. `--all` shows hidden rows with `active`. `--owner-civ N` lists the techs owned by civ N, which is how to find a civ's unique techs and bonuses. Page with `--limit`/`--offset`; `total` is the count before paging.
- **`get <kind> <id>...`** returns `{kind, civ?, items: [{id, name, internalName, active?, fields}]}`, items in the order asked. Each field has `key`, `value` (the stored number, the only thing that counts), and annotations: `label` for references, `text` for language-string IDs. `--fields` takes keys and `*` patterns (`cost*`, `command1.*`). `--compact` turns `fields` into a `{key: value}` object with keys sorted alphabetically, which is best for bulk reads. It drops `label` and `text`, so when you will report reference fields (such as `train_location` or `costN.resource`) or help text, read those fields without `--compact`. One bad ID fails the whole call.
- **`batch FILE|-`** runs a JSON array of read requests (`info`, `schema`, `lookup`, `list`, `get`) with one load, from a file or stdin. Requests use the option names in JSON: `{"op": "get", "kind": "unit", "ids": [4, 24], "civ": 1, "fields": ["hit_points", "cost*"], "compact": true}`, `{"op": "list", "kind": "tech", "ownerCiv": 14}`, `{"op": "lookup", "table": "unit", "text": "archer", "civ": 1}`. Source options go on the command line and apply to all requests; `--civ` and other command options are rejected there. The output is `{"results": [...], "failed": N}` with one entry per request, each either that request's normal result or `{"error": ...}`. Exit 0 means the batch ran; check `failed`.
- **`mods list`** lists the mods of the data set without loading it (fast): `{modsFolder, otherModsFolders?, mods: [{title, dir, author, description, hasDat}]}`. `hasDat` false means the mod uses the game's data for this data set. Use `title` with `--mod`.

## Workflows

Use one step per load: resolve names in one `batch`, then read everything in a second.

**"What does the Archer cost and how long does it train?"**

```sh
newage-cli lookup unit archer --civ 1                                   # -> 4 "Archer" (exact)
newage-cli get unit 4 --civ 1 --fields 'cost*,train_*' --compact
```

Report the costs with resource names, skipping costs whose `paid` is 0 or whose resource is -1, plus the train time and location, and say which civ's copy you read.

**"What does the Spanish unique tech Supremacy do?"**

```sh
echo '[{"op":"lookup","table":"civ","text":"spanish"},{"op":"lookup","table":"tech","text":"supremacy"}]' | newage-cli batch -
# civ 14; tech 440 with ownerCiv 14
newage-cli get tech 440                       # costs, research_time, research_location, effect -> 495
newage-cli get effect 495                     # commands with labels for type, class, attribute
```

Confirm the tech's `ownerCiv` is the civ the user means; several civs can own techs with the same name. A tech's `language_description` field has the in-game help text in `text`, a quick check against what you decode from the effect.

**"List the Spanish techs and bonuses"**: this takes two sources, and you can resolve both in one `batch`.

```sh
echo '[{"op":"list","kind":"tech","ownerCiv":14},{"op":"lookup","table":"effect","text":"spanish"}]' | newage-cli batch -
# techs: unique units' enable techs, Supremacy, Inquisition, "C-Bonus, ..." techs
# effects: 446 Spanish Tech Tree, 490 Spanish Team Bonus
```

From the techs: unique techs have player-facing names, and the enable techs (`... (make avail)`) unlock the unique units. Bonuses granted as techs usually have internal names such as `C-Bonus, ...`. Then `get` all the techs' effects plus the tech-tree and team-bonus effects in one call. Most of the civ's bonuses are in the tech-tree effect, so a report built only from `--owner-civ` misses them.

**"What is the Spanish unique army?"**: do the step above, then `get unit` each unit enabled by the civ's `Enable/Disable Unit` commands (`--civ 14`). Read `train_location` without `--compact` so it keeps its label. Also include the tech-tree commands that target those units by ID or by their `class`. Check the unit's actual `class` with `lookup unit-class` before you claim a class bonus covers it. For example, the Spanish reload bonus targets class 44 (Hand Cannoneer), but the Conquistador is class 23 (Conquistador), so the bonus doesn't reach it.

**Comparing several entities or civs**: one `batch` with a `get` per civ, or one `get` with many IDs and `--compact`, rather than a call per item.

**Finding effects that touch a unit**: there is no reverse-reference command yet. Read effects in pages with `batch` and `--compact` and filter for `commandN.unit`, `commandN.to_unit` or `commandN.class` matching the unit or its class.

**Inside a mod**: `mods list` to get the title, then add `--mod "<title>"` to every call. To see what the mod changed, read the same entities with and without `--mod` and compare.
