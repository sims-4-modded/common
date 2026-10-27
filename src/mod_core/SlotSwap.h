/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <atomic>
#include <utility>

namespace ts4_mod
{

/// Put a replacement pointer into a slot that another program reads, and put the original pointer back when the
/// object goes out of scope. The extender uses it for a pointer in a table of functions, for example a method table of
/// a native module. Each change is one atomic compare and exchange, so a thread that reads the slot gets the old or
/// the new pointer, never a part of one.
class SlotSwap
{
public:
	SlotSwap() = default;

	/// Put `replacement` into `slot` if the slot holds `expected`. Return an object that does nothing if the slot
	/// held a different pointer.
	[[nodiscard]] static SlotSwap Install(void** slot, void* expected, void* replacement)
	{
		SlotSwap swap;
		if (slot == nullptr || expected == nullptr || replacement == nullptr)
		{
			return swap;
		}
		auto* value = expected;
		if (std::atomic_ref<void*>(*slot).compare_exchange_strong(value, replacement))
		{
			swap._slot = slot;
			swap._original = expected;
			swap._replacement = replacement;
		}
		return swap;
	}

	~SlotSwap() { Restore(); }

	SlotSwap(const SlotSwap&) = delete;
	SlotSwap& operator=(const SlotSwap&) = delete;
	SlotSwap(SlotSwap&& other) noexcept { *this = std::move(other); }
	SlotSwap& operator=(SlotSwap&& other) noexcept
	{
		if (this != &other)
		{
			Restore();
			_slot = std::exchange(other._slot, nullptr);
			_original = std::exchange(other._original, nullptr);
			_replacement = std::exchange(other._replacement, nullptr);
		}
		return *this;
	}

	/// Put the original pointer back. If another program changed the slot after the install, keep its pointer and
	/// return false.
	bool Restore()
	{
		if (_slot == nullptr)
		{
			return true;
		}
		auto* value = _replacement;
		const auto restored = std::atomic_ref<void*>(*_slot).compare_exchange_strong(value, _original);
		_slot = nullptr;
		return restored;
	}

	[[nodiscard]] bool Installed() const { return _slot != nullptr; }

	/// The pointer that the slot held before the install. The replacement calls it to run the original function.
	[[nodiscard]] void* Original() const { return _original; }

private:
	void** _slot = nullptr;
	void* _original = nullptr;
	void* _replacement = nullptr;
};

} // namespace ts4_mod
