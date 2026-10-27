# The Sims 4 mods: common code

The shared code of the Sims 4 mod loader and the DLL mods: `Result`, the game version, the C interface of a DLL mod.

## Build

Requirements: Visual Studio with the C++ tools, and the vcpkg checkout of the workspace (`<workspace>\vcpkg`, where
the workspace is the folder that holds `projects`).

```bat
tools\preset.bat x64-release
```

## Tests

Run `ctest --preset x64-release` for the test suite. The tests of `Python.h` need Python 3.7: set `TS4_PYTHON37` to
its folder, or install it for the `py -3.7` launcher.

## How to install

Each project uses this repository as the git submodule `external/common`.
