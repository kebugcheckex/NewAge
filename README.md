# NewAge

A Qt 6 editor for Genie engine game data (Age of Empires, Age of Empires II,
Star Wars: Galactic Battlegrounds), replacing a subset of
Advanced Genie Editor. See [docs/PLAN.md](docs/PLAN.md).

## Layout

```
cmake/Genieutils.cmake   builds genieutils + pcrio from sibling checkouts
src/core/                Session, VersionProfile (no widgets)
src/ui/                  Qt Widgets front end
src/app/                 main()
tests/                   Qt Test suite
docs/                    plan and design notes
```

## Building (Windows)

Requirements: Visual Studio 2026 (MSVC), CMake ≥ 3.25, vcpkg with `VCPKG_ROOT`
set. genieutils and pcrio must be checked out next to this repo:

```
D:\Source\NewAge
D:\Source\genieutils
D:\Source\pcrio
```

(or pass `-DGENIEUTILS_DIR=... -DPCRIO_DIR=...`).

```sh
cmake --preset msvc              # first run builds Qt etc. through vcpkg
cmake --build --preset msvc-debug
ctest --preset msvc-debug
```

The exe is at `build/msvc/Debug/NewAge.exe`. vcpkg copies the Qt DLLs next to
it, and a post-build step copies the Qt plugins it needs (`platforms/`,
`styles/`), because vcpkg does not deploy Qt 6 plugins.

Extra vcpkg install flags can be passed through the `VCPKG_INSTALL_OPTIONS`
environment variable. With a Scoop-installed vcpkg this is required:
`buildtrees` is a junction into `persist`, and Qt refuses to build under a
symlinked path. Point it at the real directory:

```sh
setx VCPKG_INSTALL_OPTIONS "--x-buildtrees-root=E:/Scoop/persist/vcpkg/buildtrees"
```

### Round-trip test

```sh
set NEWAGE_TEST_DAT=C:\path\to\empires2_x2_p1.dat
set NEWAGE_TEST_VERSION=aoe2de
ctest --preset msvc-debug
```

Version keys are listed in `src/core/VersionProfile.cpp`.
