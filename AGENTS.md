# Repository context

- **Do not hard-wrap Markdown.** Write each paragraph, list item and table row as one line and let the viewer wrap it. This holds for every `.md` in the repo, including this one.

NewAge is a C++20 / Qt 6 Widgets editor for Genie engine game data (AoE, AoE II, SWGB), replacing a subset of Advanced Genie Editor. It currently browses units and techs, edits selected numeric fields, and saves `.dat` files; undo/redo and save-time format upgrades are not implemented. See `README.md` for setup and `docs/PLAN.md` for design and roadmap; the plan includes future work, so check the code before assuming a feature exists.

## Architecture and conventions

- `src/core/`: QtCore-only session/data ownership, game-install detection, language lookup, version profiles, and JSON preferences.
- `src/model/`: QtCore-only item models and field descriptors. Extend `UnitFields` / `TechFields` and shared `FieldTreeModel` / `EntityListModel` machinery rather than adding per-field widget bindings.
- `src/ui/`: Qt Widgets, shared `EntityBrowser`, main window, options dialog. `src/app/main.cpp` initializes the application and settings.
- `extern/`: genieutils and pcrio git submodules, built through `cmake/Genieutils.cmake`; keep dependency changes deliberate.
- Follow nearby C++ style: namespace `newage`, four-space indentation, PascalCase types, camelCase methods, trailing-underscore data members. Register new application sources in the root `CMakeLists.txt`.

## Data invariants

- `Session` owns the `DatFile`. Units belong to individual civs; techs are global. Source-model row indices remain entity IDs; filtering uses a proxy.
- Field views snapshot values. Writer callbacks re-resolve entities and validate selection before editing; avoid retaining pointers into game data. Successful edits must mark the session modified and notify models.
- Preserve numeric range validation and temporary-file-then-replace saving. Round-trip checks compare decompressed payloads, not compressed bytes.
- `Config` stores preferences as JSON and preserves unknown keys; transient session settings use `QSettings`.

## Build and validation

Requires CMake >= 3.25, Qt 6 via vcpkg, and `VCPKG_ROOT` set. Initialize dependencies with `git submodule update --init`.

```sh
cmake --preset msvc
cmake --build --preset msvc-debug
ctest --preset msvc-debug
```

The Windows preset requires Visual Studio 2026. For Linux/Ninja use `ninja` / `ninja-debug` instead; see README for system dependencies. Build output lives under `build/<preset>/`; do not edit generated files.

Tests use Qt Test: `roundtrip_test`, `config_test`, `gamedata_test`, `model_test`, `ui_test`. Run relevant tests for behavior changes; register new tests in `tests/CMakeLists.txt`. Linux UI tests need an X display/Xvfb. Game-data cases skip without the gitignored `data/` samples; report those skips when validating. Optional inputs: `NEWAGE_TEST_DAT` with `NEWAGE_TEST_VERSION` (keys in `VersionProfile.cpp`), and `NEWAGE_TEST_GAME_DIR`. Small checked-in fixtures live in `tests/data/`.
