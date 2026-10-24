/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <common/Result.h>
#include <mod_api/Ts4ModApi.h>
#include <nlohmann/json.hpp>

#include "mod_game/Guard.h"

namespace ts4_mod
{

/// The owner of a callback: the extender (0) or one DLL mod.
using HookOwner = std::uint32_t;
inline constexpr HookOwner k_ExtenderOwner = 0;

/// The highest number of native functions that all hook managers of the process can hook at one time.
inline constexpr std::size_t k_MaxHooks = 64;

/// When a callback runs: before or after the original function.
enum class HookPhase
{
	Before,
	After,
};

/// A callback on a native function.
struct HookCallback
{
	HookOwner owner = k_ExtenderOwner;
	/// The name of the callback in the log. Each owner gives a different name to each of its callbacks on a function.
	std::string name;
	HookPhase phase = HookPhase::Before;
	/// The number of parameters that the callback reads, from 0 to TS4_CALL_MAX_ARGUMENTS. The other values in
	/// `Ts4Call::arguments` are 0.
	std::uint32_t parameterCount = 0;
	std::function<void(const Ts4Call& call)> function;
};

/// The state of one callback, for the log and the inspector.
struct HookCallbackStatus
{
	HookOwner owner = k_ExtenderOwner;
	std::string name;
	HookPhase phase = HookPhase::Before;
	bool stopped = false;
	std::uint64_t calls = 0;
	/// Why the callback stopped, or empty.
	std::string error;
};

/// The state of one hook, for the log and the inspector.
struct HookStatus
{
	std::string function;
	bool installed = false;
	/// The calls on the main thread, which ran the callbacks.
	std::uint64_t calls = 0;
	/// The calls on other threads, and the calls from a callback of the same hook. These calls ran only the original
	/// function.
	std::uint64_t directCalls = 0;
	std::vector<HookCallbackStatus> callbacks;
};

/// The one owner of the native hooks of the extender. It puts one MinHook hook on each native function that has
/// callbacks, and calls the callbacks of all owners in the order of the registrations: the `Before` callbacks, the
/// original function, then the `After` callbacks.
///
/// Rules of a hooked function: it has at most TS4_CALL_MAX_ARGUMENTS parameters. Each parameter fits in one integer
/// register (an integer or a pointer), and the result is an integer, a pointer or nothing. The hook gives these values
/// to the original function with no change. A function with a floating-point parameter, a parameter on the stack, or
/// a floating-point result cannot have a hook.
///
/// Threads: the callbacks run only on the main thread (the thread that made the manager, or `Options::mainThreadId`).
/// A call on another thread runs only the original function. Call the functions of the manager on the main thread,
/// and not from a callback. Only `Statuses` and `Describe` can run on any thread.
///
/// A guard surrounds each callback. If a callback throws a C++ exception or causes a fault that the filter catches,
/// the manager stops each callback of that owner on all hooks, and calls `Options::ownerFailed`.
///
/// When the manager goes out of scope, it removes each hook that it installed.
class HookManager
{
public:
	struct Options
	{
		/// The thread that runs the callbacks. 0 is the thread that calls `Make`.
		std::uint32_t mainThreadId = 0;
		/// The filter of the guard around each callback.
		ExceptionFilter filter = &CatchCallbackFault;
		/// Called on the main thread after a callback of `owner` failed, with the reason. The callbacks of the owner
		/// are already stopped.
		std::function<void(HookOwner owner, const std::string& reason)> ownerFailed;
	};

	/// Start MinHook (one time in each process) and make a manager.
	[[nodiscard]] static ts4::Result<std::unique_ptr<HookManager>> Make(Options options);
	[[nodiscard]] static ts4::Result<std::unique_ptr<HookManager>> Make() { return Make(Options {}); }

	~HookManager();
	HookManager(const HookManager&) = delete;
	HookManager& operator=(const HookManager&) = delete;
	HookManager(HookManager&&) = delete;
	HookManager& operator=(HookManager&&) = delete;

	/// Add a callback on the native function at `target`, which `functionName` names in the log. The first callback
	/// on a function installs the hook. Return an error, and change nothing, if the callback is not correct, if the
	/// owner has a callback with the same name on the function, if the owner stopped, or if the hook cannot go on
	/// the function.
	[[nodiscard]] ts4::Result<void> AddCallback(const void* target, std::string_view functionName, HookCallback callback);

	/// Remove the callbacks of `owner` from all hooks, for example when the extender unloads a mod. A hook with no
	/// callbacks left goes too.
	void RemoveOwner(HookOwner owner);

	/// Remove the hook on `target` and its callbacks. Return false if there is no hook, or if the manager cannot
	/// remove it now.
	bool Remove(const void* target);

	/// Remove all hooks.
	void RemoveAll();

	/// True if a callback of `owner` failed.
	[[nodiscard]] bool OwnerFailed(HookOwner owner) const;
	/// True if the manager has a hook on `target`.
	[[nodiscard]] bool Hooked(const void* target) const;
	[[nodiscard]] std::uint32_t MainThreadId() const { return _mainThreadId; }

	[[nodiscard]] std::vector<HookStatus> Statuses() const;
	[[nodiscard]] nlohmann::json Describe() const;

	struct Hook;

private:
	explicit HookManager(Options options);

	/// Stop each callback of `owner`. The hooks call it after a callback failed.
	void FailOwner(HookOwner owner, const std::string& reason);
	[[nodiscard]] ts4::Result<void> CheckCaller(std::string_view what) const;
	bool RemoveLocked(const void* target);

	Options _options;
	std::uint32_t _mainThreadId = 0;
	mutable std::mutex _mutex;
	std::map<const void*, std::unique_ptr<Hook>> _hooks;
	std::vector<HookOwner> _failedOwners;
};

} // namespace ts4_mod
