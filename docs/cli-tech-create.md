# CLI creation of civilization-specific technologies

Status: partially implemented. This document defines the complete CLI workflow for creating a technology and effect in a mod without Advanced Genie Editor.

## Implemented first slice

`newage-cli --mod NAME tech create SPEC.json [--dry-run]` and `{"op":"tech-create","spec":{...},"dryRun":true}` now create an automatic hidden civ bonus with one new effect. `-` reads the spec from stdin. The spec requires `mode: "automatic"`, numeric `civ`, distinct tech and effect `internalName` values, an explicit `prerequisites` array, and at least one command. Commands currently support attribute effect types 0, 4, and 5 with `unit` and/or `class`, `attribute`, and `amount`. Optional `expect.techCount`, `expect.effectCount`, `expect.fileVersion`, and `expect.sourceFingerprint` reject stale specs. The response contains the assigned IDs, descriptor-visible stored fields, the source fingerprint, `dryRun`, `saved`, and `warnings`. The command is rejected in `batch` and follows the mod-only write rule.

The service validates IDs, count limits, names, and the civ tech-tree disable list before appending records. Dry runs and errors restore vector lengths and the modified flag. A per-mod lock and source fingerprint check protect this command from concurrent CLI writes and source changes between load and save. The game `.dat` is left untouched. Researchable mode, other effect command types, localized strings, overlap and button warnings, and cross-file recovery remain in the delivery order below; unsupported spec keys and modes are rejected.

## Motivation and existing behavior

`newage-cli` can set fields on existing technologies and effects, resize an effect's command list, and save a mod `.dat`. It cannot append whole technology or effect records. Filling unused slots works, but it depends on the particular game version having suitable slots and makes mod updates harder to reason about. The Super Chinese Villager mod fills blank tech and effect slots; its technology is restricted to Chinese through `Tech::Civ`, but it reuses Great Wall's language string IDs. A creation workflow should append independent records and allocate independent strings.

Technologies and effects are global indexed arrays in `DatFile`; a tech's `Civ` restricts ownership, its `EffectID` links an effect, and the effect owns its commands. Existing civilization bonuses demonstrate an automatic tech with no research location, cost, or time; a visible researchable tech additionally needs a location, button, cost, time, prerequisite, and display strings. Sources: [local genieutils data structures](../extern/genieutils/include/genie/dat/DatFile.h), [tech structure](../extern/genieutils/include/genie/dat/Research.h), [genieutils examples](https://github.com/Krakenmeister/genieutils-examples), and the [AoE2DE mod file layout](https://ugc.aoe2.rocks/mods/).

## User-facing command

Add `newage-cli --mod NAME tech create SPEC.json [--dry-run]`. Keep all source options (`--game`, `--dataset`, `--mods-folder`, `--locale`) and the existing mod-only write rule. Also accept `-` for a JSON spec on stdin, as `apply` does. Reject this command in `batch` because it writes. The request-handler equivalent is `{"op":"tech-create", "spec":{...}, "dryRun":true}`.

The spec has two modes: `automatic` for a civ bonus and `researchable` for a player-facing tech. Require a numeric `civ` ID and a distinctive `internalName`; neither the civ name nor the tech name is an authoritative identifier. Require an effect `internalName` and at least one command. A first release should create exactly one tech and one new effect. Sharing an existing effect can be added later, with an explicit reference rather than an implicit name match.

An illustrative shape, with numerical IDs resolved beforehand through `lookup`, is:

```json
{
  "mode": "researchable",
  "civ": 6,
  "internalName": "Chinese Super Villager",
  "prerequisites": [104],
  "research": {"building": 109, "button": 7, "time": 5},
  "costs": [{"resource": 1, "amount": 10, "paid": true}],
  "effect": {
    "internalName": "Chinese Super Villager Effect",
    "commands": [{"type": 5, "class": 4, "attribute": 13, "amount": 5}]
  },
  "strings": {"locale": "en", "name": "Super Villagers", "description": "Villagers work faster", "help": "Villagers work faster", "techTree": "Super Villagers"},
  "expect": {"techCount": 1510, "effectCount": 1500, "fileVersion": "VER 8.9"}
}
```

The values above illustrate the API shape, not a ready-to-apply balance patch. The command must resolve all supplied IDs and effect parameters against the selected data set, and `--dry-run` must show the exact stored fields, assigned IDs, planned string IDs, and warnings. The spec may accept explicit language IDs for advanced users, but the ordinary workflow should allocate distinct name, description, help, and tech-tree IDs after checking the game's strings, the mod's strings, and technology references in the selected `.dat`. State clearly that this cannot rule out collisions with other separately enabled mods.

For `automatic`, require an explicit prerequisite list (which may be empty), set no research building, zero time and paid costs, and no button. For `researchable`, require a building, button, time, costs, and strings; allow empty prerequisites only when deliberately specified. Do not silently copy a template tech. Set or reject every field that materially affects behavior, including type, icon, full-tech mode, repeatability, hotkey, and extra DE research locations. Initially support one research location and reject requests for several until the CLI can read and write all of them.

## Data-service implementation

1. Add a creation operation in `DataService` beneath the command, rather than modifying the vectors in `src/cli/main.cpp`. Construct fresh `genie::Effect` and `genie::Tech` records, call `setGameVersion` for the loaded version, initialize required-tech and cost slots, and append them to `DatFile::Effects` and `DatFile::Techs`. Assign each ID from the corresponding array size before append; link the new tech to the new effect ID. Check the tech count against the serialized 16-bit count and the effect command count against its 16-bit count before mutation.
2. Reuse `TechFields` and `EffectFields` parsing, ranges, and reference labels where possible. Convert the guided spec into the same stored field values that `set`/`apply` use. Reject unsupported or unknown spec keys instead of dropping them. Validate the assembled records as a whole because individual field setters do not establish cross-field relationships.
3. Extend the transaction machinery so a dry run or failed validation restores both vector lengths, the session's modified flag, and any other changed records. Do not retain pointers or references into a vector while appending. Save once through the existing temporary-file-then-replace path, and keep the game-file write guard.
4. Protect against stale IDs. Check the expected counts and file version before planning, expose a source-file fingerprint for stronger optimistic checks, and recheck the source before committing. A cooperative per-mod lock should cover CLI writes; external editors remain a possible conflict, so a changed source must fail with `conflict` rather than overwrite it.
5. Make the result machine-readable: assigned tech and effect IDs; the values supplied or defaulted; locale path and allocated string IDs; warnings; `dryRun`; and `saved` paths. Existing `get`, `list --owner-civ`, and `diff` should then work on the appended records without special cases. `diff` should report them as `onlyInMod`.

## Validation and localized text

Validate that the civ and every referenced tech, unit, class, attribute, resource, building, and effect command type exist for the selected game version. Ensure prerequisites are distinct, have the correct required count, and do not include the new tech itself. Check that the selected civ's tech-tree effect does not disable the new tech ID. For a researchable tech, flag a button conflict at the chosen building for that civ and likely activation state; the engine's final button behavior still requires an in-game test. Warn when commands overlap, such as applying the same work-rate multiplier to Civilian class and to Farmer units in that class. Keep warnings distinct from errors.

Write visible tech text to the mod's `resources/<locale>/strings/key-value/key-value-modded-strings-utf8.txt`, preserving existing entries and formatting. The game `.dat` and mod locale file are separate files, so a two-file update cannot use one atomic rename. Stage both, validate the staged content, commit the locale file before the `.dat`, and keep a recoverable prior copy or journal for rollback if the `.dat` replacement fails. If a crash leaves only new strings, a rerun should detect and reuse or clean up those exact entries safely. Automatic hidden bonuses may omit player-facing strings; never reuse another tech's IDs by default.

## Delivery order and tests

1. Implement append and rollback for tech/effect records, including version initialization and the current mod-only guard. Add focused service or API tests that create records, check IDs and links after reload, and prove dry-run, validation failure, and save failure leave the mod unchanged.
2. Add the guided CLI parser and JSON result. Test both automatic and researchable specs, expected-count conflicts, invalid references, command overlap warnings, serialization bounds, and a second invocation against an already changed mod.
3. Add locale string allocation and writing. Test preservation of unrelated strings, collision detection, failure recovery across both files, and correct text after reload. Test a mod with no prior `.dat` or locale file as well as an existing mod.
4. Run a real AoE II DE smoke test with the Chinese civ and another civ. Confirm the new tech or bonus works, the Great Wall tech and strings still work, and `diff` shows only the intended appended records. Rebuild against an updated game `.dat` to verify that the saved spec can be replayed when appended IDs move.

After the command and locale support work, revise [the civ-tech skill](../skill/newage-civ-tech/SKILL.md) to use `tech create`, dry run, save, `diff`, and in-game verification. Until then, that skill's AGE step describes the current tooling limit rather than the target workflow.
