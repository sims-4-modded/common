/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

/*
 * The interface between the extender (the mod loader) and a DLL mod.
 *
 * The extender and each mod are different modules. Each one has its own C runtime, its own heap and its own copy of
 * the C++ standard library. Thus only plain C types go across this interface: integers, doubles, pointers to
 * functions and to text. A text that goes across is UTF-8 with a terminating zero. The receiver copies a text that
 * it keeps; the pointer is valid only during the call. No C++ exception and no Python exception may go across: each
 * function of a mod and of the extender catches its exceptions.
 *
 * A DLL mod exports these four functions (see the names below):
 * - `Ts4ModApiVersion` gives the version of this interface that the mod uses. The extender refuses a mod with a
 *   different version, and does not call its other functions.
 * - `Ts4ModGetInfo` gives the id, the name and the version of the mod.
 * - `Ts4ModInit` gets the table of the extender. A result of 0 tells that the mod cannot start: the extender then
 *   removes all the registrations of the mod, unloads the DLL, writes the reason to the log, and the game continues.
 * - `Ts4ModShutdown` tells the mod that the game stops or that the extender unloads the mod. The mod puts back each
 *   value of the game that it changed. After the call, the extender calls no function of the mod.
 *
 * Threads: the extender calls `Ts4ModInit`, `Ts4ModShutdown` and each task on the main thread. A mod can call `log`
 * and `postTask` from any thread.
 *
 * The size of the table: the extender can add functions at the end of `Ts4HostApi` with no change of the version. A
 * mod checks `size` with `TS4_HOST_API_HAS` before it uses a function after `TS4_HOST_API_FIRST_SIZE`.
 *
 * Hooks: the extender owns each change of the code of the game. A mod never installs a hook itself. The functions
 * that ask the extender for a callback before or after a game function come at the end of the table in a later
 * step.
 */

#ifndef TS4_MOD_API_H
#define TS4_MOD_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The version of this interface. A change that a mod of the old version cannot use increases it.
#define TS4_MOD_API_VERSION 1u

typedef enum Ts4LogLevel
{
	TS4_LOG_DEBUG = 0,
	TS4_LOG_INFO = 1,
	TS4_LOG_WARN = 2,
	TS4_LOG_ERROR = 3,
} Ts4LogLevel;

typedef void (*Ts4TaskFn)(void* user);

/// The table that the extender gives to `Ts4ModInit`. It is valid until `Ts4ModShutdown` returns. Each function gets
/// `context` as its first parameter. A function that registers something returns 1, or 0 if it refuses.
typedef struct Ts4HostApi
{
	uint32_t apiVersion; ///< TS4_MOD_API_VERSION of the extender.
	uint32_t size;       ///< sizeof(Ts4HostApi) of the extender.
	void* context;

	const char* loaderVersion;
	const char* gameVersion;    ///< The version of the game, for example "1.128.90.1030".
	const char* gameFolder;     ///< The folder of the game. A mod never writes into it.
	const char* userFolder;     ///< Documents\Electronic Arts\The Sims 4: the saves, the options and Mods.
	const char* settingsFolder; ///< The folder of the settings of the mods.
	const char* logFolder;      ///< The folder of the log files.

	/// Write a line to the log of the extender. Any thread.
	void (*log)(void* context, Ts4LogLevel level, const char* text);
	/// Run `task` one time on the main thread, between two ticks of the simulation. Any thread can post a task.
	uint32_t (*postTask)(void* context, Ts4TaskFn task, void* user);
} Ts4HostApi;

/// The size of the table of the first extender of version 1.
#define TS4_HOST_API_FIRST_SIZE (offsetof(Ts4HostApi, postTask) + sizeof(void*))

/// True if the table of the extender has the function `member`.
#define TS4_HOST_API_HAS(api, member) ((api)->size >= offsetof(Ts4HostApi, member) + sizeof((api)->member))

/// The id, the name and the version of a mod. The texts are static texts of the mod.
typedef struct Ts4ModInfo
{
	const char* id;      ///< A short name with no spaces, for example "mod_template".
	const char* name;    ///< The name that the extender shows.
	const char* version; ///< The version of the mod, for example "0.1.0".
} Ts4ModInfo;

typedef uint32_t (*Ts4ModApiVersionFn)(void);
typedef void (*Ts4ModGetInfoFn)(Ts4ModInfo* info);
typedef uint32_t (*Ts4ModInitFn)(const Ts4HostApi* host);
typedef void (*Ts4ModShutdownFn)(void);

#define TS4_MOD_EXPORT_API_VERSION "Ts4ModApiVersion"
#define TS4_MOD_EXPORT_GET_INFO "Ts4ModGetInfo"
#define TS4_MOD_EXPORT_INIT "Ts4ModInit"
#define TS4_MOD_EXPORT_SHUTDOWN "Ts4ModShutdown"

#ifdef __cplusplus
}
#endif

#endif // TS4_MOD_API_H
