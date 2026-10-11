---
name: newage-data
description: Read and edit Age of Empires, AoE II and Star Wars Galactic Battlegrounds game data (units, techs, effects, civs in Genie .dat files) with newage-cli. Use when asked about unit stats, costs, train or research times, techs, unique techs, civ bonuses or effects, what a mod changes, or to make balance changes in a mod, for AoE, AoE II (AoK, TC, HD, DE) or SWGB.
---

# newage-data

`newage-cli` reads Genie engine `.dat` files and prints JSON. It is the command-line side of the NewAge editor and shows the same fields as the editor.

**Writes go into a mod, never into the game's files.** `set` and `apply` need `--mod`; data without mods (the CD-era games, loose `--dat` files) is read-only. Use this CLI for ordinary field edits. To append an entirely new tech and effect, follow [newage-civ-tech](../newage-civ-tech/SKILL.md), which uses Advanced Genie Editor on a mod copy. Never copy over the game's own `.dat`.

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
- A successful write also prints loader lines such as `{"diagnostic": {"message": "Terrains: 100"}}` on stderr. They aren't problems.
- Failure: non-zero exit and `{"error": {"code", "message", "kind"?, "id"?, "civ"?, "key"?, "value"?}}` on stdout.

  | Exit | Codes | What to do |
  |------|-------|------------|
  | 1 | `usage` | Fix the command line; the message says what is wrong (for example `get unit needs a civ.`). |
  | 2 | `load_failed`, `no_dataset`, `mod_not_found`, `mods_unsupported` | Wrong game folder, data set, mod or version key. Check with the user. |
  | 3 | `unknown_kind`, `unknown_entity`, `inactive_entity`, `unknown_field` | Bad table, ID or field key. Re-check with `lookup` or `schema`. |
  | 3 | `not_applicable`, `read_only`, `bad_value`, `out_of_range` | A write the field doesn't take: the field isn't on this entity, isn't editable, the value is the wrong type, or it is out of `min`..`max` (for text, longer than `max`). Nothing was written. |
  | 4 | `conflict` | The value changed since you read it (`--expect`). Nothing was written. Read again and tell the user what changed before retrying. |
  | 5 | `mod_required`, `game_data_protected`, `save_failed` | No `--mod`, or the save path is a game file, or the save failed. Nothing was written. |

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
- Coverage is partial: units have identity, a few stats, costs, the first train location and some graphics; attacks, armours, range and reload time are not available yet. Techs and effects are fully covered. If a field the user wants isn't in `schema`, say so instead of guessing. Only fields with `editable: true` can be written.
- **Unit lists can't grow or shrink.** You can change an existing attack, armour or train location, but not add or remove one. **Effect commands can:** `command_count` adds empty commands at the end (type 255) or drops the last ones. A tech's six required techs and three costs are fixed slots, and an unused one (`-1`) can be filled in.
- **Text fields** (`type` `string`: `internal_name` of techs and effects) take Latin-1 text up to `max` characters. Player-facing names and help are `language_*` string IDs, not text; the CLI can point them at another string but can't write language files.

## Commands

```
newage-cli info
newage-cli schema [civ|unit|tech|effect]
newage-cli lookup <table> [TEXT] [--civ N]
newage-cli list <kind> [--civ N] [--owner-civ N] [--all] [--limit N] [--offset N]
newage-cli get <kind> <id>... [--civ N] [--fields KEY,PATTERN*,...] [--compact]
newage-cli batch FILE|-
newage-cli mods list
newage-cli mods create --title T [--author A] [--description D] [--from game|mod:NAME]
newage-cli set <kind> <id> KEY=VALUE... --mod M [--civ N] [--expect KEY=VALUE]... [--dry-run]
newage-cli apply PATCH.json|- --mod M [--dry-run]
newage-cli diff [kind] --mod M [--civ N] [--fields KEY,PATTERN*,...] [--limit N] [--offset N]
```

- **`schema [kind]`** lists each field's `key`, `name`, `group`, `type`, `editable`, `min`/`max` and `labelKind`. `conditional` fields exist only on some entities. Effects use `commandN.*` templates. Run it once per task for the kinds you need.
- **`lookup <table> [TEXT]`** turns words into IDs. Data tables: `civ`, `unit` (needs `--civ`), `tech` (rows have `ownerCiv`), `effect`. Fixed tables: `resource`, `unit-class`, `attribute`, `effect-type`, `unit-type`, `tech-type`, `resource-mode`, `tech-modifier-mode`, `enable-mode`, `upgrade-mode`. Without TEXT it returns the whole table in ID order.
- **`list <kind>`** returns `{total, offset, limit?, items: [{id, name, internalName, ownerCiv?}]}`. `list unit` needs `--civ` and hides empty slots; `list tech --civ N` hides techs that civ can't research. `--all` shows hidden rows with `active`. `--owner-civ N` lists the techs owned by civ N, which is how to find a civ's unique techs and bonuses. Page with `--limit`/`--offset`; `total` is the count before paging.
- **`get <kind> <id>...`** returns `{kind, civ?, items: [{id, name, internalName, active?, fields}]}`, items in the order asked. Each field has `key`, `value` (the stored number, the only thing that counts), and annotations: `label` for references, `text` for language-string IDs. `--fields` takes keys and `*` patterns (`cost*`, `command1.*`). `--compact` turns `fields` into a `{key: value}` object with keys sorted alphabetically, which is best for bulk reads. It drops `label` and `text`, so when you will report reference fields (such as `train_location` or `costN.resource`) or help text, read those fields without `--compact`. One bad ID fails the whole call.
- **`batch FILE|-`** runs a JSON array of read requests (`info`, `schema`, `lookup`, `list`, `get`) with one load, from a file or stdin. Requests use the option names in JSON: `{"op": "get", "kind": "unit", "ids": [4, 24], "civ": 1, "fields": ["hit_points", "cost*"], "compact": true}`, `{"op": "list", "kind": "tech", "ownerCiv": 14}`, `{"op": "lookup", "table": "unit", "text": "archer", "civ": 1}`. Source options go on the command line and apply to all requests; `--civ` and other command options are rejected there. The output is `{"results": [...], "failed": N}` with one entry per request, each either that request's normal result or `{"error": ...}`. Exit 0 means the batch ran; check `failed`.
- **`mods list`** lists the mods of the data set without loading it (fast): `{modsFolder, otherModsFolders?, mods: [{title, dir, author, description, hasDat}]}`. `hasDat` false means the mod uses the game's data for this data set. Use `title` with `--mod`.
- **`diff [kind] --mod M`** compares the mod's `.dat` with the game's current one (read-only, two loads, can't be in a `batch`). It returns `{fileVersion: {mod, game}, counts: {kind: {mod, game}}, changed, onlyInGame, onlyInMod, total, items}`. Each item has `status` (`changed`, `onlyInGame`, `onlyInMod`), `kind`, `id`, `name`, `internalName`, and for units `civs` (every civ with that same difference). Changed items list `fields: [{key, mod, game, ...labels}]`, with `null` on a side that lacks the field. `unlisted: true` means it also differs in data no field covers (attacks, armors, ...), which you can't read or fix with this CLI. `--fields` (needs a kind) limits the comparison and skips that check. `--civ` limits units to one civ. Page with `--limit`/`--offset`; the counts cover everything.

## Writing

Rules for every write:

1. **Resolve with `lookup`, read with `get`, then write.** Use the IDs and field keys you resolved, never names. The field must have `editable: true` in `schema`.
2. **Pass what you read as `--expect`** (or `expect` in a patch), so a change made in the meantime, in the NewAge editor or by another agent, is reported as `conflict` instead of being overwritten.
3. **Dry run first, show the changes, then write.** `--dry-run` runs every check and reports `changes` without saving. Show the user each change as entity, civ, field, old value and new value, and write only after they agree (or already told you to go ahead with exactly that change).
4. **Units are per civ.** `set` and each unit edit name one civ (`--civ N`, `"civ": N`). A change "for everyone" or "for the Spanish" is not one edit: write one edit per civ and list them all in the dry run, or tell the user it only reaches the civs you named. A global change to a unit's base stats means every civ's copy; civ bonuses and unique techs are effects, which are global: one edit changes it for every civ that runs the effect.
5. **One save per step.** `set` takes several `KEY=VALUE` pairs for one entity. For more than one entity, write a patch file and `apply` it. Edits run in order, each seeing the ones before it, and the file is saved once; if any edit fails, none are kept. `apply` reads the file, or stdin with `-`.
6. **Writing the value that is already there** is reported under `unchanged` and doesn't save. A result with `saved: ""` and an empty `changes` means nothing was written.
7. **After writing, `get` the entities again** (with `--mod`) and report the stored values, not what you meant to write.
8. **Don't write a mod that is open in NewAge.** The editor doesn't watch the file, so its next save would overwrite the change (and the other way round). Ask the user to close the mod or keep the editor away from it.
9. **A mod starts disabled.** After `mods create` or the first write, tell the user to enable the mod in the game's mod manager.

Writes need a mod:

- `mods list` shows the mods of the data set. Use a title with `--mod`. If the user names none, ask which mod to use. Never pick one yourself, and never write without one.
- `mods create --title "Balance"` makes a new mod folder (ask the user for the title). It loads no data. `--from mod:NAME` starts from a copy of another mod's `.dat`; the default `game` starts from the game's data, and the mod's own `.dat` appears on its first write. A mod made for one data set has no `.dat` for another: `hasDat` says which.
- A mod's `.dat` can be older than the game's after a game update. Check `fileVersion` in `diff` (or `info` with and without `--mod`) before writing, and tell the user when the mod is behind.

Value rules: ints must be whole numbers in the field's `min`..`max`; floats are read as decimals (`0.2`); strings are Latin-1 text up to `max` characters (in JSON, a string; on the command line, everything after `=`, so quote it in the shell). `KEY=VALUE` on the command line is text that the CLI turns into the field's type, and a negative value is fine (`research_time=-1`). Use the numbers from `get`; labels (`label`, `text`) are never accepted as input.

`set` examples:

```sh
newage-cli --mod Balance set unit 4 --civ 1 hit_points=35 cost1.amount=20 --expect hit_points=30 --expect cost1.amount=25 --dry-run
newage-cli --mod Balance set tech 440 research_time=45 --expect research_time=60
```

`apply` takes a patch (the same edits as `set`, many entities); `changes` from a result converts straight back into one (`old` becomes `expect`, or swap `old` and `new` to undo):

```json
{
  "edits": [
    {"kind": "tech", "id": 440, "set": {"cost1.amount": 800}, "expect": {"cost1.amount": 1000}},
    {"kind": "unit", "id": 82, "civ": 1, "set": {"cost1.amount": 585}, "expect": {"cost1.amount": 600}},
    {"kind": "unit", "id": 82, "civ": 2, "set": {"cost1.amount": 585}, "expect": {"cost1.amount": 600}}
  ]
}
```

The result is `{"saved": "<path or empty>", "dryRun"?: true, "changes": [{kind, id, civ?, key, old, new}], "unchanged": [...]}`. `set`, `apply` and `mods create` can't be used in a `batch`.

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

**Inside a mod**: `mods list` to get the title, then add `--mod "<title>"` to every call. To see what the mod changes, run `diff --mod "<title>"`.

**"My old mod broke after a game update"**: `diff --mod "<title>" --limit 0` first for `fileVersion`, `counts` and the totals, then page through `items` by kind (`diff unit --limit 100`, ...). A diff compares with the game's *current* data, so it mixes two things: the mod's own edits, and every game update since the mod was made, which the mod's old copy undoes. Tell them apart before suggesting a fix:

- `onlyInGame` civs, units, techs and effects are newer than the mod. The mod's `.dat` lacks them, which is the usual cause of breakage (new civs missing, crashes or missing units in game).
- Changes across many unrelated entities in the same fields (graphics, icons, names, new stats) are usually game updates. Changes that match the mod's description or title, and are consistent (the same field moved the same way across civs), are usually the mod's edits.
- Say which is which and how sure you are; with no copy of the old game data you can't prove it either way.

The fix is to rebuild the mod on the current data: create a new mod from the game (`mods create --title "<title> (updated)"`), and `apply` only the edits the user confirms are the mod's own, using the mod's values from the diff as `set` and the game's values as `expect`. Data that the CLI can't write (`unlisted` differences, added unit list entries) has to be redone in the NewAge editor; list those entities for the user.

A tech or effect the mod *added* sits at an ID that a game update may since have given to something else (then `diff` shows it `changed` with unrelated names), or that the game now leaves empty. Don't overwrite the game's entity. Move the mod's tech and effect to free slots instead: a tech with `effect` -1, `civ` -1 and an empty `internal_name`, and an effect with `command_count` 0, that `diff` doesn't list. Read every field of the mod's tech and effect with `get --mod`, then write them in one patch: the effect's `command_count` and commands, and the tech's fields with `effect` pointing at the new effect ID. Check that nothing else in the mod referred to the old IDs (other techs' `required_techN`, Disable Tech commands).

**"Give the Spanish villagers +10 HP"**: find the civ's tech-tree effect (`lookup effect spanish`), read its `command_count`, then one patch edit: `{"command_count": N+1, "commandN+1.type": 4, "commandN+1.unit": 83, "commandN+1.attribute": 0, "commandN+1.amount": 10}` with `"expect": {"command_count": N}`. A patch writes `command_count` first, then command types, then the rest, so the new command exists before its fields are set. Also cover the other villager units (or use `class` 4 with unit -1), and say which you chose.

**"Make the Spanish unique tech cheaper"**: `lookup civ spanish`, `list tech --owner-civ <id>` or `lookup tech <name>`, then `get tech <id> --fields 'cost*,research_time'`. Ask which mod to use (`mods list`), run `set tech <id> cost1.amount=<new> --expect cost1.amount=<old> --mod <title> --dry-run`, show the change, then run it without `--dry-run` and `get` it again. Check `ownerCiv` is the civ the user means.

**"Make the Castle 10% cheaper in my balance mod"**: the Castle is a unit, so find its civ copies. `lookup unit castle --civ <id>`, read `cost*` with `--compact` for the civ(s) in question, and compute each new amount from the value you read (round, and say how). Write one `apply` edit per civ, each with its `expect`, dry run, show them all, then apply. If the civ copies differ, say so before changing them.

**"Apply these 40 balance changes and save once"**: resolve the names with one `batch`, read the current values with another, write a patch with an `expect` for every edit, `apply --dry-run`, show a summary the user can check (the full list of changes if they ask), `apply`, then verify with a `batch` of `get` requests. A single failed check, such as a stale `expect` or an out-of-range value, writes nothing and names the edit (`kind`, `id`, `civ`, `key`); fix that edit and run the whole patch again.
