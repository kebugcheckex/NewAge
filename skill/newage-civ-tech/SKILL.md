---
name: newage-civ-tech
description: Add a genuinely new civilization-specific technology and effect to an AoE II DE data mod, including a researchable tech or an automatic civ bonus. Use when a new tech must coexist with existing techs rather than repurpose one.
---

# Add a civilization-specific technology

Use the repo's [newage-data skill](../newage-data/SKILL.md) for source selection, ID lookup, field meanings, mod-only writes, and `diff`. This workflow covers the structural step that `newage-cli` cannot currently perform: appending a tech and an effect. Never save over the game's `.dat` or edit a mod that NewAge has open.

## Choose the intended behavior

- **Researchable tech:** give the tech a unique `civ` ID, an `effect` ID, a research location, an unused button at that building, costs, time, and any age/prerequisite. The civ restriction keeps it exclusive; check the actual in-game button and research behavior.
- **Automatic civ bonus:** give the tech the target `civ`, an `effect`, `research_location = -1`, zero time, zero paid costs, and prerequisites appropriate to when it should activate. Existing Chinese bonus techs in this data set show the pattern: tech 425 has no prerequisite, while techs 983 and 984 require Castle and Imperial Age respectively. An automatic bonus needs no player-facing button.
- **No technology needed:** if the request is just a permanent bonus at game start, adding commands to that civ's tech-tree effect may be simpler. Use a dedicated tech when the bonus needs its own identity, prerequisites, research action, or easier maintenance.

## Inspect and plan

1. Run `newage-cli info` on the game and mod, then `diff --mod <name> --limit 0`; check the data set and file versions before editing. Resolve the civ, target units/classes, reference techs, and effect command types with `lookup`, `get`, and `schema`. Confirm the selected command actually covers the intended villager variants; a class command can include other units.
2. Inspect a working tech with the desired behavior in the same game version. For a researchable Chinese tech, tech 462 (Great Wall) illustrates civ ownership, research location, costs, and prerequisites; inspect it as a reference only. For an automatic bonus, inspect a civ bonus tech and its effect. Record the planned costs, activation, command order, and expected gameplay result. Check for overlap between class-wide and unit-specific commands: in the Super Chinese Villager mod, Farmer units 214 and 259 are also Civilian class 4, so both work-rate multipliers can affect them.
3. Reserve **new, appended IDs**: `new tech ID = current tech count`, `new effect ID = current effect count`. IDs are zero-based array indices. Recheck both counts immediately before saving, because game updates or another editor can change them. Reserve fresh language string IDs as well; verify they are unused in the game's strings and the mod. Do not point the new tech at an existing tech's name, description, help, or tech-tree text IDs.

## Build the mod

1. Start a new mod from the current game data, or work on the explicitly selected existing mod. The original game's `.dat` is read-only. The bundled Advanced Genie Editor (`<game>/Tools_Builds/AdvancedGenieEditor3.exe`) can add records that NewAge cannot append. Open the game data as input and direct its compressed data-set output to `<mod>/resources/_common/dat/<dataset>.dat` before saving. Make a recoverable copy of an existing mod's `.dat` first.
2. Append one effect at the end of the Effects list. Give it a distinctive internal name and add the required commands. Set each command's type, target unit or class, attribute, and amount deliberately; inspect comparable working effects and use `lookup effect-type`, `lookup attribute`, and `lookup unit-class` for meanings. For attack or armor modifiers, account for the packed class/value encoding described in `newage-data`.
3. Append one tech at the end of the Techs list. Set a distinctive internal name, `civ` to the intended civilization, and `effect` to the new effect ID. Fill the researchable or automatic fields chosen above. Clear fields inherited from a copied template that do not belong to this tech, including extra prerequisites, costs, alternate research locations, and repeatability. Set only fresh language IDs; an icon may reuse a game asset if that is intentional.
4. For a visible researchable tech, put its new string IDs and text in the mod's locale file at `resources/<locale>/strings/key-value/key-value-modded-strings-utf8.txt`. Supply at least the displayed name and description, and any help or tech-tree string IDs the tech uses. Check the current game's string format and test the actual text in game. For an internal automatic bonus, custom display strings are optional if the tech is never surfaced to players; keep its internal name unique.
5. Save the mod copy. Do not run NewAge and Advanced Genie Editor on the same open mod simultaneously; both keep their own in-memory copy.

## Verify

1. Reload the mod with `newage-cli info`: the tech and effect counts should each be one greater than the starting mod, and the game counts should be unchanged. Run `diff --mod <name>` and inspect every changed item. A clean addition should show the appended tech and effect as `onlyInMod`; explain any other differences before considering the work done.
2. `get` the new tech and effect with `--mod`, and verify IDs, civ, linked effect, prerequisites, location, cost, and every command. Use `list tech --owner-civ <id>` and `list tech --civ <other-id>` as additional checks. Do not rely on names alone.
3. Enable the mod and test in game as the target civ and a second civ. For a researchable tech, check the button, text, cost, timing, one-time research, and effect. For an automatic bonus, check the intended activation age and effect. Verify that existing unique technologies and their text still work for both civs. Reopen and retest after a game update; appended IDs can collide with records added by that update, so rebuild from the updated game data and reassign IDs if needed.

The `.dat` structure and civ bonus behavior are supported by [genieutils' counted Techs/Effects serialization](../../extern/genieutils/src/dat/DatFile.cpp), [genieutils' tech fields](../../extern/genieutils/include/genie/dat/Research.h), and the [genieutils examples](https://github.com/Krakenmeister/genieutils-examples). The [AoE II DE UGC mod guide](https://ugc.aoe2.rocks/mods/) documents the mod file layout, including locale strings. `newage-cli` currently edits fields and effect-command counts but does not append whole tech or effect records; that limit is recorded in [the CLI design](../../docs/cli.md).
