/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

/*
 * The interface between the extender (the mod loader) and a DLL mod in the folder of DLL mods of the extender.
 *
 * The extender and each mod are different modules. Each one has its own C runtime, its own heap and its own copy of
 * the C++ standard library. Thus only plain C types go across this interface: integers, doubles, pointers to
 * functions and to text. A text that goes across is UTF-8 with a terminating zero. The receiver copies a text that
 * it keeps; the pointer is valid only during the call. No C++ exception and no Python exception may go across: each
 * function of a mod and of the extender catches its exceptions.
 *
 * A DLL mod exports these four functions (see the export names below):
 * - `Ts4ModApiVersion` gives the version of this interface that the mod uses. The extender refuses a mod with a
 *   different version, and does not call its other functions.
 * - `Ts4ModGetInfo` gives the id, the name and the version of the mod.
 * - `Ts4ModInit` gets the table of the extender. The mod registers its settings, its menu fields, its actions, its
 *   queries and its function callbacks. The extender refuses registrations after `Ts4ModInit` returns. A result of 0
 *   tells that the mod cannot start: the extender then removes all the registrations of the mod, unloads the DLL,
 *   writes the reason to the log, and the game continues.
 * - `Ts4ModShutdown` tells the mod that the game stops or that the extender unloads the mod. The mod puts back each
 *   value of the game that it changed. After the call, the extender calls no function of the mod.
 *
 * Threads: two threads of the game are important to a mod.
 * - The client main thread (in this interface, "the main thread") runs the main loop of the client. Each frame, it
 *   calls the tick of the extender. The extender runs these on this thread: `Ts4ModInit` (in the first tick),
 *   `Ts4ModShutdown`, the settings functions, the tasks, and the fields, the actions and the readouts of the menu bar.
 *   The queries also run on this thread.
 * - The simulation thread runs the simulation, the game Python and the cheat commands. A call from a script mod
 *   or from game Python into a DLL mod comes on this thread, not on the main thread.
 *
 * A function callback runs on the thread that calls the native function. The functions that the main loop of the
 * client calls run on the main thread. The swap chain of the game presents on a third thread, the render thread: the
 * menu bar draws there, but it calls no function of a mod there. A mod must know the thread of each function that it
 * hooks.
 *
 * The first call of each settings function comes in the first tick, after all inits and before the first work of the
 * tick. A mod can call `log`, `postTask` and `listQueries` of the table from any thread. To change the state of the
 * client from the simulation thread, a mod posts a task, or it keeps the state that two threads share under a lock.
 *
 * The size of the table: the extender can add functions at the end of `Ts4HostApi` with no change of the version. A
 * mod checks `size` with `TS4_HOST_API_HAS` before it uses a function after `TS4_HOST_API_FIRST_SIZE`. An older
 * extender gives a smaller table.
 *
 * Hooks: the extender owns each change of the code of the game. A mod never installs a hook and never writes a byte
 * of the game itself. It asks the extender for a callback before or after a native function, which a signature in a
 * module of the game names. The extender installs one hook for each function and calls the callbacks of all mods in
 * order. Thus two mods never put two different hooks on one function. The extender refuses a signature that does not
 * match exactly one place in its module. It scans `TS4_x64.exe` only after the activation wrapper of EA decrypted it.
 *
 * A later version of the table adds callbacks before or after a game Python function, at the end of `Ts4HostApi`.
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

/// The modules of the game that a function callback can name.
#define TS4_MODULE_GAME "TS4_x64.exe"
#define TS4_MODULE_SIMULATION "Simulation_x64.dll"

/// The highest number of parameters that a function callback gets.
#define TS4_CALL_MAX_ARGUMENTS 4u

typedef enum Ts4LogLevel
{
	TS4_LOG_DEBUG = 0,
	TS4_LOG_INFO = 1,
	TS4_LOG_WARN = 2,
	TS4_LOG_ERROR = 3,
} Ts4LogLevel;

/// When a function callback runs: before or after the original function.
typedef enum Ts4HookPhase
{
	TS4_HOOK_BEFORE = 0,
	TS4_HOOK_AFTER = 1,
} Ts4HookPhase;

/// The control of a menu field.
typedef enum Ts4FieldKind
{
	TS4_FIELD_TOGGLE = 0, ///< A check box. The value is `toggle`.
	TS4_FIELD_NUMBER = 1, ///< A slider. The value is `number`.
	TS4_FIELD_CHOICE = 2, ///< A list of names. The value is `choice`, the index of the name.
} Ts4FieldKind;

/// A place for a text that the receiver gives. The sender calls `assign` one time with the text. The receiver copies
/// it.
typedef struct Ts4Text
{
	void* context;
	void (*assign)(void* context, const char* utf8, size_t size);
} Ts4Text;

/// The current call of a hooked native function. A callback reads the values; it cannot change them.
typedef struct Ts4Call
{
	/// The first `parameterCount` parameters of the function, in order, as integers or pointers. The other values
	/// are 0.
	uintptr_t arguments[TS4_CALL_MAX_ARGUMENTS];
	/// After the function only: the integer or the pointer that the function returned. Before the function, 0.
	uintptr_t result;
} Ts4Call;

/// A native function of the game, found by a signature in one module. The signature starts at the first byte of the
/// function. The extender gives the parameters and the result of the function to the caller with no change.
typedef struct Ts4FunctionDesc
{
	const char* module; ///< TS4_MODULE_GAME or TS4_MODULE_SIMULATION.
	const char* name;   ///< The name of the function in the log, for example "ZoneLoad".
	/// The signature text: two hex digits for each fixed byte, "??" for any byte, with a space between two bytes.
	const char* signature;
	/// The number of parameters that the callback gets, from 0 to TS4_CALL_MAX_ARGUMENTS. Each parameter fits in one
	/// integer register.
	uint32_t parameterCount;
} Ts4FunctionDesc;

/// The value of a field.
typedef struct Ts4FieldValue
{
	uint32_t toggle;
	double number;
	uint32_t choice;
} Ts4FieldValue;

/// The value of a field now, with the range that a number has now. Some ranges depend on another field.
typedef struct Ts4FieldState
{
	Ts4FieldValue value;
	double min;
	double max;
	/// The field changes only after a restart, and the value is different from the value that the mod uses now.
	uint32_t restartPending;
} Ts4FieldState;

/// A value of the settings that the menu bar shows and changes. The extender copies each text.
typedef struct Ts4FieldDesc
{
	const char* menu;    ///< The menu of the menu bar, or null for the menu of the mod.
	const char* group;   ///< The heading of the part of the menu.
	const char* section; ///< The JSON object of the field in mods.json, for example "camera".
	const char* key;     ///< The name of the field in its JSON object.
	const char* label;
	const char* help;
	Ts4FieldKind kind;
	double min;         ///< Numbers only.
	double max;         ///< Numbers only.
	double step;        ///< Numbers only.
	const char* format; ///< The printf format of a number.
	uint32_t logarithmic;
	uint32_t restartOnly; ///< The change works only after a restart of the game.
	const char* const* choices;
	uint32_t choiceCount;
} Ts4FieldDesc;

/// Something that the user does with a button of the menu bar, with a hotkey, or with both.
typedef struct Ts4ActionDesc
{
	const char* menu;  ///< The menu of the button, or null for the menu of the mod.
	const char* group; ///< The heading of the part of the menu.
	const char* label; ///< The text of the button, or null for an action with no button.
	const char* help;
	/// The name of the hotkey in the object `hotkeys` of mods.json, for example "toggleCamera", or null for no hotkey.
	const char* hotkey;
	/// The hotkey when mods.json does not give one, for example "Ctrl+Shift+F5", or null for none.
	const char* defaultHotkey;
} Ts4ActionDesc;

typedef void (*Ts4CallFn)(void* user, const Ts4Call* call);
typedef void (*Ts4TaskFn)(void* user);
typedef uint32_t (*Ts4SettingsFn)(void* user, const char* json, Ts4Text* error);
typedef void (*Ts4FieldGetFn)(void* user, Ts4FieldState* state);
typedef uint32_t (*Ts4FieldSetFn)(void* user, const Ts4FieldValue* value, uint32_t save, Ts4Text* message);
typedef uint32_t (*Ts4EnabledFn)(void* user);
typedef void (*Ts4ReadFn)(void* user, Ts4Text* text);
/// A query of the inspector. `params` is a JSON object. Return 1 and give the answer as JSON text to `result`, or
/// return 0 and give an error message to `result`.
typedef uint32_t (*Ts4QueryFn)(void* user, const char* params, Ts4Text* result);

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
	const char* userFolder;     ///< Documents\Electronic Arts\The Sims 4: the options, the saves and Mods.
	const char* settingsFolder; ///< The folder of mods.json, outside the game folder.
	const char* logFolder;      ///< The folder of the log files, outside the game folder.

	/// Write a line to the log of the extender. Any thread.
	void (*log)(void* context, Ts4LogLevel level, const char* text);

	/// Call `callback` before or after each call of the native function. `name` must be different for each callback
	/// of the mod on one function. The extender refuses a function whose signature does not match exactly one place.
	uint32_t (*addFunctionCallback)(void* context, const Ts4FunctionDesc* function, Ts4HookPhase phase, const char* name,
	                                Ts4CallFn callback, void* user);
	/// Run `task` one time on the main thread, between two ticks. Any thread can post a task.
	uint32_t (*postTask)(void* context, Ts4TaskFn task, void* user);

	/// Give the JSON object `section` of mods.json to `load` now and after each change of the file. An object that is
	/// not in the file is "{}". `load` returns 0 and an error to refuse the settings: the mod then keeps its settings.
	uint32_t (*addSettings)(void* context, const char* section, Ts4SettingsFn load, void* user);
	/// Write a change (a JSON merge patch of the full file) to mods.json, for example after a hotkey.
	uint32_t (*saveSettings)(void* context, const char* patch);

	/// Show a field in the menu bar. `get` gives its value; `set` changes it and gives a message, or returns 0 and an
	/// error. With `save` 1, the extender writes the new value to mods.json.
	uint32_t (*addField)(void* context, const Ts4FieldDesc* field, Ts4FieldGetFn get, Ts4FieldSetFn set, void* user);
	/// Add an action. `enabled` can be null (always enabled).
	uint32_t (*addAction)(void* context, const Ts4ActionDesc* action, Ts4TaskFn run, Ts4EnabledFn enabled, void* user);
	/// Show lines of text in the menu bar, for example the needs of the active Sim. `read` runs on the main thread.
	uint32_t (*addReadout)(void* context, const char* menu, const char* group, Ts4ReadFn read, void* user);
	/// The status of the mod as a JSON object, for the menu bar and the log.
	uint32_t (*setStatusReader)(void* context, Ts4ReadFn read, void* user);

	/// Show a short message on the screen of the game (and in the log).
	void (*showMessage)(void* context, const char* text, double seconds);

	/// Add a query that the inspector can call, for example "sims.needs". Each query name has one owner. `help` is a
	/// short text for the list of the queries, or null. The extender calls `query` on the main thread.
	uint32_t (*addQuery)(void* context, const char* name, const char* help, Ts4QueryFn query, void* user);
	/// Give the queries of the extender and of all mods to `list`: a JSON array of objects with "name", "help" and
	/// "mod". Any thread. The list is complete after the inits of all mods.
	void (*listQueries)(void* context, Ts4Text* list);
	/// Call the query `name` with the JSON object `params` (or null for no parameters). Main thread only. Return 1 and
	/// give the answer as JSON text to `result`, or return 0 and give an error message to `result`.
	uint32_t (*callQuery)(void* context, const char* name, const char* params, Ts4Text* result);

	/* Add new functions only after this line. A mod checks each of them with TS4_HOST_API_HAS. */
} Ts4HostApi;

/// The size of the table of the first extender of version 1. Each extender of version 1 gives at least this size.
#define TS4_HOST_API_FIRST_SIZE (offsetof(Ts4HostApi, callQuery) + sizeof(void*))

/// 1 if the table `api` (a pointer) has the function `member`, else 0.
#define TS4_HOST_API_HAS(api, member) ((api)->size >= offsetof(Ts4HostApi, member) + sizeof((api)->member) ? 1 : 0)

/// What a mod tells about itself. The texts stay valid while the DLL is loaded.
typedef struct Ts4ModInfo
{
	const char* id;   ///< Letters, digits, '-' and '_', for example "mod_template".
	const char* name; ///< The name in the menu bar.
	const char* version;
} Ts4ModInfo;

/// The names of the exports of a DLL mod.
#define TS4_MOD_EXPORT_API_VERSION "Ts4ModApiVersion"
#define TS4_MOD_EXPORT_GET_INFO "Ts4ModGetInfo"
#define TS4_MOD_EXPORT_INIT "Ts4ModInit"
#define TS4_MOD_EXPORT_SHUTDOWN "Ts4ModShutdown"

typedef uint32_t (*Ts4ModApiVersionFn)(void);
typedef const Ts4ModInfo* (*Ts4ModGetInfoFn)(void);
typedef uint32_t (*Ts4ModInitFn)(const Ts4HostApi* host, uint32_t apiVersion);
typedef void (*Ts4ModShutdownFn)(void);

#ifdef __cplusplus
}
#endif

#endif // TS4_MOD_API_H
