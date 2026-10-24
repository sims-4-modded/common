# The Sims 4 mods: common

The shared code of the mod loader (the script extender) and the DLL mods of The Sims 4. The mod loader
(`mod_loader`) and each DLL mod include this repository as a git submodule (`external/common`).

## What is in it

| Library | Folder | What it does |
|---|---|---|
| `ts4_common` | `src/common` | `ts4::Result` (headers only) |
| `ts4_mod_core` | `src/mod_core` | Pure logic that does not need the game: the game version |
| `ts4_mod_api` | `src/mod_api` | `mod_api/Ts4ModApi.h`: the C interface between the extender and a DLL mod |
| `ts4_mod_sdk` | `src/mod_sdk` | The C++ side of that interface in a DLL mod (`ModHost`) |

Each library has one folder under `src`, with its headers and sources directly in it. The folder name is the target
name without `ts4_`. Each library puts `src` on the include path, so an include line names the library folder and the
file, for example `#include <common/Result.h>` or `#include <mod_core/GameVersion.h>`. The folder names of common
start with `mod_` where a mod could have the same name (`core`, `game`), so an include path never finds two files.

## The interface between the extender and a mod

`src/mod_api/Ts4ModApi.h` is a versioned C interface (`TS4_MOD_API_VERSION`), on the model of the Masters of Albion
mod loader. Only plain C types go across it, because the extender and each mod have their own C runtime and their
own copy of the standard library.

A DLL mod exports `Ts4ModApiVersion`, `Ts4ModGetInfo`, `Ts4ModInit` and `Ts4ModShutdown`. The extender refuses a mod
with a different interface version. If `Ts4ModInit` returns 0, the extender removes the registrations of the mod,
unloads it, writes the reason to the log, and the game continues.

Version 1 gives the log, the folders, the game version and tasks on the main thread. The extender owns every hook;
the functions that ask it for a callback on a game function come later, at the end of `Ts4HostApi`.

## Build

Requirements: Visual Studio with the C++ tools, and the vcpkg checkout `D:\sims_4\vcpkg`.

```bat
tools\preset.bat x64-release
```

The script prepares the MSVC x64 environment, then configures, compiles and tests the preset `x64-release`
(Ninja, MSVC, RelWithDebInfo). The steps are also available alone: `tools\preset.bat x64-release configure`,
`compile` or `test`.

The dependencies come from vcpkg in manifest mode (`vcpkg.json`): nlohmann-json, spdlog (with fmt) and GoogleTest,
with the triplet `x64-windows-static`. Every DLL that goes to the game uses the static C runtime, so no other DLL
goes there. The presets use:

- `VCPKG_ROOT` = `D:/sims_4/vcpkg`,
- `VCPKG_DEFAULT_BINARY_CACHE` = `D:/sims_4/cache/vcpkg-archives`,
- `VCPKG_DOWNLOADS` = `D:/sims_4/cache/vcpkg-downloads`.

## Use in a mod project

```sh
git -c protocol.file.allow=always submodule add D:/sims_4/projects/common external/common
```

```cmake
add_subdirectory(external/common)
target_link_libraries(my_mod PRIVATE ts4_mod_sdk)
```

The submodule URL is the absolute local path `D:/sims_4/projects/common`. Git refuses local file submodules by
default, so each submodule command needs `-c protocol.file.allow=always`. The project of the mod sets
`CMAKE_MSVC_RUNTIME_LIBRARY` to the static C runtime and uses the same vcpkg triplet.

## Tests

The tests (GoogleTest, `tests/`) cover the pure parts with fakes, never with the game or game data.
`ctest --preset x64-release` runs them.
