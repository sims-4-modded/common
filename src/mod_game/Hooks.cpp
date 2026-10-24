/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "mod_game/Hooks.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <exception>
#include <format>
#include <utility>

#include <MinHook.h>
#include <spdlog/spdlog.h>
#include <windows.h>

namespace ts4_mod
{

namespace
{
/// The type of a native function that the hooks support: four integer registers in, one integer register out.
using NativeFunction = std::uintptr_t (*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);

/// One callback and its state. The list of a hook and the status share it.
struct CallbackEntry
{
	explicit CallbackEntry(HookCallback value)
	    : callback(std::move(value))
	{
	}

	HookCallback callback;
	std::atomic<bool> stopped = false;
	std::atomic<std::uint64_t> calls = 0;
	/// Why the callback stopped. The manager writes and reads it with its lock.
	std::string error;
};

/// One place for a hook. MinHook makes the game call a fixed function for each place, because a code pointer cannot
/// carry data. Thus the places are in a table that lives as long as the process. This table is the only state of the
/// hooks outside a manager. A place stays in use after the removal of its hook if a call was in progress then,
/// because that call still uses the original function.
struct Slot
{
	std::atomic<bool> inUse = false;
	/// The trampoline that MinHook makes: it runs the original function.
	std::atomic<NativeFunction> original = nullptr;
	/// The hook of the manager, or null after the removal.
	std::atomic<HookManager::Hook*> hook = nullptr;
	std::atomic<std::uint32_t> mainThreadId = 0;
	/// The calls in progress through this place, on all threads.
	std::atomic<std::uint32_t> active = 0;
	std::atomic<std::uint64_t> directCalls = 0;
	/// True while the callbacks of this place run. Only the main thread reads and writes it.
	bool inCallbacks = false;
};

std::array<Slot, k_MaxHooks> s_slots;

/// The depth of the callbacks on this thread. The functions of the manager refuse to run in a callback.
thread_local int t_callbackDepth = 0;

/// Count a call in progress through a place, also if the original function throws.
class ActiveCall
{
public:
	explicit ActiveCall(Slot& slot)
	    : _slot(slot)
	{
		_slot.active.fetch_add(1, std::memory_order_acq_rel);
	}
	~ActiveCall() { _slot.active.fetch_sub(1, std::memory_order_acq_rel); }
	ActiveCall(const ActiveCall&) = delete;
	ActiveCall& operator=(const ActiveCall&) = delete;

private:
	Slot& _slot;
};

[[nodiscard]] const char* PhaseName(HookPhase phase)
{
	return phase == HookPhase::Before ? "before" : "after";
}

/// Start MinHook one time in the process. MinHook stays until the process ends, so that each trampoline stays valid.
[[nodiscard]] MH_STATUS StartMinHook()
{
	static std::once_flag once;
	static MH_STATUS status = MH_UNKNOWN;
	std::call_once(once, [] {
		status = MH_Initialize();
		if (status == MH_ERROR_ALREADY_INITIALIZED)
		{
			status = MH_OK;
		}
	});
	return status;
}
} // namespace

/// The hook on one native function.
struct HookManager::Hook
{
	HookManager& manager;
	const void* target = nullptr;
	std::string function;
	std::size_t slot = 0;
	std::atomic<std::uint64_t> calls = 0;
	/// The callbacks in the order of the registrations. Only the main thread changes the list, with the lock of the
	/// manager, and never while the callbacks run.
	std::vector<std::shared_ptr<CallbackEntry>> callbacks;

	/// Run the callbacks and the original function. The hook entry calls it on the main thread.
	std::uintptr_t Call(Slot& place, NativeFunction original, const std::array<std::uintptr_t, 4>& arguments)
	{
		calls.fetch_add(1, std::memory_order_relaxed);
		Ts4Call call {};
		std::ranges::copy(arguments, call.arguments);
		Run(place, HookPhase::Before, call);
		const auto result = original(arguments[0], arguments[1], arguments[2], arguments[3]);
		// A callback or the original function can remove the hook. Then this object does not exist any more.
		if (place.hook.load(std::memory_order_acquire) != this)
		{
			return result;
		}
		call.result = result;
		Run(place, HookPhase::After, call);
		return result;
	}

	void Run(Slot& place, HookPhase phase, const Ts4Call& call)
	{
		place.inCallbacks = true;
		++t_callbackDepth;
		for (const auto& entry : callbacks)
		{
			if (entry->callback.phase != phase || entry->stopped.load(std::memory_order_relaxed))
			{
				continue;
			}
			Ts4Call own = call;
			for (auto index = entry->callback.parameterCount; index < TS4_CALL_MAX_ARGUMENTS; ++index)
			{
				own.arguments[index] = 0;
			}
			entry->calls.fetch_add(1, std::memory_order_relaxed);
			struct Context
			{
				const std::function<void(const Ts4Call&)>* function;
				const Ts4Call* call;
			} context {.function = &entry->callback.function, .call = &own};
			const auto thunk = [](void* data) {
				const auto* values = static_cast<Context*>(data);
				(*values->function)(*values->call);
			};
			if (const auto guard = GuardedCall(thunk, &context, manager._options.filter); !guard.completed)
			{
				manager.FailOwner(entry->callback.owner,
				                  std::format("the callback {} {} {} failed with {}", entry->callback.name, PhaseName(phase),
				                              function, guard.Describe()));
			}
		}
		--t_callbackDepth;
		place.inCallbacks = false;
	}
};

namespace
{
/// The code that the game calls in place of the function of the hook in place `Index`. A C++ exception from the
/// original function goes back to the game through this function, as if the hook did not exist.
template <std::size_t Index>
std::uintptr_t HookEntry(std::uintptr_t first, std::uintptr_t second, std::uintptr_t third, std::uintptr_t fourth)
{
	auto& slot = s_slots[Index];
	const ActiveCall active(slot);
	const auto original = slot.original.load(std::memory_order_acquire);
	if (original == nullptr)
	{
		return 0;
	}
	if (GetCurrentThreadId() == slot.mainThreadId.load(std::memory_order_relaxed) && !slot.inCallbacks)
	{
		if (auto* hook = slot.hook.load(std::memory_order_acquire); hook != nullptr)
		{
			return hook->Call(slot, original, {first, second, third, fourth});
		}
	}
	slot.directCalls.fetch_add(1, std::memory_order_relaxed);
	return original(first, second, third, fourth);
}

template <std::size_t... Indices>
constexpr std::array<NativeFunction, k_MaxHooks> MakeEntries(std::index_sequence<Indices...> /*indices*/)
{
	return {&HookEntry<Indices>...};
}

constexpr std::array<NativeFunction, k_MaxHooks> k_Entries = MakeEntries(std::make_index_sequence<k_MaxHooks>());
} // namespace

HookManager::HookManager(Options options)
    : _options(std::move(options))
    , _mainThreadId(_options.mainThreadId != 0 ? _options.mainThreadId : GetCurrentThreadId())
{
	if (_options.filter == nullptr)
	{
		_options.filter = &CatchCallbackFault;
	}
}

HookManager::~HookManager()
{
	const std::scoped_lock lock(_mutex);
	// Each call of RemoveLocked removes the hook from the map, also if the jump stays in the code.
	while (!_hooks.empty())
	{
		(void)RemoveLocked(_hooks.begin()->first);
	}
}

ts4::Result<std::unique_ptr<HookManager>> HookManager::Make(Options options)
{
	if (const auto status = StartMinHook(); status != MH_OK)
	{
		return ts4::MakeError("MinHook did not start: {}", MH_StatusToString(status));
	}
	return std::unique_ptr<HookManager>(new HookManager(std::move(options)));
}

ts4::Result<void> HookManager::CheckCaller(std::string_view what) const
{
	if (GetCurrentThreadId() != _mainThreadId)
	{
		return ts4::MakeError("{} must run on the main thread", what);
	}
	if (t_callbackDepth > 0)
	{
		return ts4::MakeError("{} cannot run in a callback", what);
	}
	return {};
}

ts4::Result<void> HookManager::AddCallback(const void* target, std::string_view functionName, HookCallback callback)
{
	if (auto caller = CheckCaller("a new callback"); !caller)
	{
		return caller.GetError();
	}
	if (target == nullptr)
	{
		return ts4::MakeError("the function {} has no address", functionName);
	}
	if (callback.name.empty() || !callback.function)
	{
		return ts4::MakeError("a callback on {} has no name or no function", functionName);
	}
	if (callback.parameterCount > TS4_CALL_MAX_ARGUMENTS)
	{
		return ts4::MakeError("the callback {} on {} reads {} parameters, more than {}", callback.name, functionName,
		                      callback.parameterCount, TS4_CALL_MAX_ARGUMENTS);
	}
	const std::scoped_lock lock(_mutex);
	if (std::ranges::find(_failedOwners, callback.owner) != _failedOwners.end())
	{
		return ts4::MakeError("the owner of the callback {} stopped after an error", callback.name);
	}
	if (const auto found = _hooks.find(target); found != _hooks.end())
	{
		auto& hook = *found->second;
		const auto same = std::ranges::any_of(hook.callbacks, [&](const auto& entry) {
			return entry->callback.owner == callback.owner && entry->callback.name == callback.name;
		});
		if (same)
		{
			return ts4::MakeError("the callback {} on {} exists", callback.name, hook.function);
		}
		hook.callbacks.push_back(std::make_shared<CallbackEntry>(std::move(callback)));
		return {};
	}

	std::size_t index = 0;
	for (; index < s_slots.size(); ++index)
	{
		bool expected = false;
		if (s_slots[index].inUse.compare_exchange_strong(expected, true))
		{
			break;
		}
	}
	if (index == s_slots.size())
	{
		return ts4::MakeError("no hook on {}: all {} hook places are in use", functionName, k_MaxHooks);
	}
	auto& slot = s_slots[index];
	auto* code = const_cast<void*>(target);
	void* original = nullptr;
	if (const auto status = MH_CreateHook(code, reinterpret_cast<void*>(k_Entries[index]), &original); status != MH_OK)
	{
		slot.inUse = false;
		if (status == MH_ERROR_ALREADY_CREATED)
		{
			return ts4::MakeError("no hook on {}: another hook manager has a hook on it", functionName);
		}
		return ts4::MakeError("no hook on {}: MinHook cannot make the hook: {}", functionName, MH_StatusToString(status));
	}
	auto hook = std::unique_ptr<Hook>(new Hook {
	    .manager = *this,
	    .target = target,
	    .function = std::string(functionName),
	    .slot = index,
	});
	hook->callbacks.push_back(std::make_shared<CallbackEntry>(std::move(callback)));
	slot.original.store(reinterpret_cast<NativeFunction>(original), std::memory_order_release);
	slot.mainThreadId.store(_mainThreadId, std::memory_order_relaxed);
	slot.directCalls = 0;
	slot.inCallbacks = false;
	slot.hook.store(hook.get(), std::memory_order_release);
	if (const auto status = MH_EnableHook(code); status != MH_OK)
	{
		slot.hook = nullptr;
		(void)MH_RemoveHook(code);
		slot.original = nullptr;
		slot.inUse = false;
		return ts4::MakeError("no hook on {}: MinHook cannot put the jump into the code: {}", functionName,
		                      MH_StatusToString(status));
	}
	spdlog::info("hook: installed on {}", functionName);
	_hooks.emplace(target, std::move(hook));
	return {};
}

bool HookManager::RemoveLocked(const void* target)
{
	const auto found = _hooks.find(target);
	if (found == _hooks.end())
	{
		return false;
	}
	auto& hook = *found->second;
	auto& slot = s_slots[hook.slot];
	auto* code = const_cast<void*>(target);
	slot.hook.store(nullptr, std::memory_order_release);
	if (const auto status = MH_DisableHook(code); status != MH_OK)
	{
		// The jump stays in the code, so the place and its trampoline must stay. The calls then run only the original
		// function.
		spdlog::error("hook: the jump on {} stays in the code: {}", hook.function, MH_StatusToString(status));
		_hooks.erase(found);
		return false;
	}
	if (slot.active.load(std::memory_order_acquire) == 0)
	{
		(void)MH_RemoveHook(code);
		slot.original = nullptr;
		slot.inUse = false;
		spdlog::info("hook: removed from {}", hook.function);
	}
	else
	{
		spdlog::warn("hook: removed from {} during a call; its hook place stays in use", hook.function);
	}
	_hooks.erase(found);
	return true;
}

void HookManager::RemoveOwner(HookOwner owner)
{
	if (auto caller = CheckCaller("the removal of an owner"); !caller)
	{
		spdlog::error("hook: {}", caller.GetError().message);
		return;
	}
	const std::scoped_lock lock(_mutex);
	std::vector<const void*> empty;
	for (auto& [target, hook] : _hooks)
	{
		std::erase_if(hook->callbacks, [owner](const auto& entry) { return entry->callback.owner == owner; });
		if (hook->callbacks.empty())
		{
			empty.push_back(target);
		}
	}
	for (const auto* target : empty)
	{
		(void)RemoveLocked(target);
	}
}

bool HookManager::Remove(const void* target)
{
	if (auto caller = CheckCaller("the removal of a hook"); !caller)
	{
		spdlog::error("hook: {}", caller.GetError().message);
		return false;
	}
	const std::scoped_lock lock(_mutex);
	return RemoveLocked(target);
}

void HookManager::RemoveAll()
{
	if (auto caller = CheckCaller("the removal of the hooks"); !caller)
	{
		spdlog::error("hook: {}", caller.GetError().message);
		return;
	}
	const std::scoped_lock lock(_mutex);
	std::vector<const void*> targets;
	for (const auto& [target, hook] : _hooks)
	{
		targets.push_back(target);
	}
	for (const auto* target : targets)
	{
		(void)RemoveLocked(target);
	}
}

void HookManager::FailOwner(HookOwner owner, const std::string& reason)
{
	{
		const std::scoped_lock lock(_mutex);
		if (std::ranges::find(_failedOwners, owner) == _failedOwners.end())
		{
			_failedOwners.push_back(owner);
		}
		for (const auto& [target, hook] : _hooks)
		{
			for (const auto& entry : hook->callbacks)
			{
				if (entry->callback.owner != owner || entry->stopped.exchange(true))
				{
					continue;
				}
				entry->error = reason;
			}
		}
	}
	spdlog::error("hook: {}; all callbacks of owner {} stop", reason, owner);
	if (_options.ownerFailed)
	{
		try
		{
			_options.ownerFailed(owner, reason);
		}
		catch (const std::exception& exception)
		{
			spdlog::error("hook: the function for a failed owner failed: {}", exception.what());
		}
	}
}

bool HookManager::OwnerFailed(HookOwner owner) const
{
	const std::scoped_lock lock(_mutex);
	return std::ranges::find(_failedOwners, owner) != _failedOwners.end();
}

bool HookManager::Hooked(const void* target) const
{
	const std::scoped_lock lock(_mutex);
	return _hooks.contains(target);
}

std::vector<HookStatus> HookManager::Statuses() const
{
	const std::scoped_lock lock(_mutex);
	std::vector<HookStatus> statuses;
	for (const auto& [target, hook] : _hooks)
	{
		HookStatus status {
		    .function = hook->function,
		    .installed = s_slots[hook->slot].hook.load() == hook.get(),
		    .calls = hook->calls.load(),
		    .directCalls = s_slots[hook->slot].directCalls.load(),
		    .callbacks = {},
		};
		for (const auto& entry : hook->callbacks)
		{
			status.callbacks.push_back(HookCallbackStatus {
			    .owner = entry->callback.owner,
			    .name = entry->callback.name,
			    .phase = entry->callback.phase,
			    .stopped = entry->stopped.load(),
			    .calls = entry->calls.load(),
			    .error = entry->error,
			});
		}
		statuses.push_back(std::move(status));
	}
	return statuses;
}

nlohmann::json HookManager::Describe() const
{
	auto hooks = nlohmann::json::array();
	for (const auto& status : Statuses())
	{
		auto callbacks = nlohmann::json::array();
		for (const auto& callback : status.callbacks)
		{
			callbacks.push_back({
			    {"owner", callback.owner},
			    {"name", callback.name},
			    {"phase", PhaseName(callback.phase)},
			    {"stopped", callback.stopped},
			    {"calls", callback.calls},
			    {"error", callback.error},
			});
		}
		hooks.push_back({
		    {"function", status.function},
		    {"installed", status.installed},
		    {"calls", status.calls},
		    {"directCalls", status.directCalls},
		    {"callbacks", std::move(callbacks)},
		});
	}
	return hooks;
}

} // namespace ts4_mod
