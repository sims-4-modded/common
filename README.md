# The Sims 4 mods: common

The shared code of the mod loader (the script extender) and the DLL mods of The Sims 4. The mod loader
(`mod_loader`) and each DLL mod include this repository as a git submodule (`external/common`).

The code is a port of the shared code of the Masters of Albion mods (`D:\masters_of_albion\projects\common`) for this
game. The Unity and IL2CPP parts are not in it: a native function of the game has a signature in place of a managed
method.

## What is in it

| Library | Folder | What it does |
|---|---|---|
| `ts4_common` | `src/common` | `ts4::Result` (headers only) |
| `ts4_mod_core` | `src/mod_core` | Pure logic that does not need the game: the game version, frame callbacks, main-thread tasks, the slot swap of a code pointer, hotkeys (with the Windows virtual-key codes), the settings file, the menu field tables, the folders of the mods, the start-up guard, the crash dump rules and the signatures |
| `ts4_mod_api` | `src/mod_api` | `mod_api/Ts4ModApi.h`: the C interface between the extender and a DLL mod |
| `ts4_mod_sdk` | `src/mod_sdk` | The C++ side of that interface in a DLL mod (`ModHost`, `BindFields`, the log sink `HostLogSink`) |

Each library has one folder under `src`, with its headers and sources directly in it. The folder name is the target
name without `ts4_`. Each library puts `src` on the include path, so an include line names the library folder and the
file, for example `#include <common/Result.h>` or `#include <mod_core/Signature.h>`. The folder names of common
start with `mod_` where a mod could have the same name (`core`, `game`), so an include path never finds two files.
The C++ namespaces (`ts4`, `ts4_mod`, `ts4_mod::sdk`, `ts4_mod::settings`) do not follow the folders.

The Windows parts that run in the game (`src/mod_game`: module information, the signature scan of a loaded module,
the hotkey input, the messages of the game, crash dumps) come in a later step, on this interface.

## The interface between the extender and a mod

`src/mod_api/Ts4ModApi.h` is a versioned C interface (`TS4_MOD_API_VERSION`, now 1), on the model of the Masters of
Albion mod loader. Only plain C types go across it, because the extender and each mod have their own C runtime and
their own copy of the standard library.

A DLL mod exports `Ts4ModApiVersion`, `Ts4ModGetInfo`, `Ts4ModInit` and `Ts4ModShutdown`. The extender refuses a mod
with a different interface version. If `Ts4ModInit` returns 0, the extender removes the registrations of the mod,
unloads it, writes the reason to the log, and the game continues.

The table of the extender (`Ts4HostApi`) gives:

- the versions and the folders: `loaderVersion`, `gameVersion` (for example `1.128.90.1030`), `gameFolder`,
  `userFolder` (`Documents\Electronic Arts\The Sims 4`), `settingsFolder` and `logFolder`;
- `log`;
- `addFunctionCallback`: a callback before or after a native function;
- `postTask`: a task on the main thread;
- `addSettings` and `saveSettings`: the sections of `mods.json`;
- `addField`, `addAction` (with a hotkey), `addReadout` and `setStatusReader`: the menu bar;
- `showMessage`: a message on the screen of the game;
- `addQuery`, `listQueries` and `callQuery`: the inspector queries.

The extender can add functions at the end of `Ts4HostApi` with no change of the version. A mod checks `size` with
`TS4_HOST_API_HAS` before it uses a function after `TS4_HOST_API_FIRST_SIZE`. `ModHost::Check` refuses a table that is
smaller than the table of the first extender of version 1, and accepts a larger table of a newer extender.

### Function callbacks

The extender owns each hook. A mod never writes a code pointer or a byte of the game. It asks for a callback with a
`Ts4FunctionDesc`:

- the module: `TS4_MODULE_GAME` (`TS4_x64.exe`) or `TS4_MODULE_SIMULATION` (`Simulation_x64.dll`);
- a name for the log;
- the signature text of the first bytes of the function: two hex digits for each fixed byte and `??` for any byte;
- the number of parameters that the callback reads (0 to 4).

The extender installs one hook for each function and calls the callbacks of all mods in order, before or after the
function. It refuses a signature that matches no place or more than one place in its module. No code uses an RVA.
Callbacks on game Python functions come later, at the end of the table.

`mod_core/Signature.h` reads a signature text (also at compile time with `MakeSignature`, which stops the compile on
a bad text) and finds each match in a span of bytes. `FindUniqueSignature` gives the one match or an error.

### Inspector queries

A mod can add named queries (for example `sims.needs`) with `addQuery`: a C function that gets the parameters as JSON
text and gives the answer as JSON text through `Ts4Text`. The extender calls a query on the main thread. A mod lists
the queries of all mods with `listQueries` and calls one with `callQuery`, so the state of each mod stays in its own
DLL. In C++, `ModHost::AddQuery`, `ListQueries` and `CallQuery` do this.

### The folders of the mods

`MakeModFolders` (`mod_core/Folders.h`) puts the settings file, the log files and the marker file of the start in
`%LOCALAPPDATA%\Sims4Mods` (`mods.json`, `logs`, `startup-marker.json`). The environment variables
`TS4_MODS_SETTINGS` and `TS4_LOADER_LOG_FOLDER` can give other paths. It refuses each path in a game folder, because
the extender never writes into the game folder.

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
