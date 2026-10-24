/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <mod_core/Hotkey.h>

namespace ts4_mod
{

/// Reads the hotkeys of the extender and the DLL mods from the key state of Windows, on the main thread. A hotkey
/// fires in the poll after its key goes down, if the user holds exactly its modifier keys. A key that the user holds
/// does not fire again. The hotkeys do nothing while a window of another process has the keyboard.
///
/// The key state of Windows does not tell the Enter key of the keypad from the main Enter key, so "KeypadEnter" and
/// "Return" are the same hotkey here.
class HotkeyInput
{
public:
	/// Return true while the key with this Windows virtual-key code is down.
	using KeyReader = std::function<bool(std::uint16_t virtualKey)>;
	/// Return true if the game has the keyboard.
	using FocusReader = std::function<bool()>;

	/// Read the keys with GetAsyncKeyState, and the keyboard focus from the foreground window.
	HotkeyInput();
	/// Read the keys and the focus with other functions, for example in a test.
	HotkeyInput(KeyReader keys, FocusReader focus);

	/// A hotkey and the name that the log uses for it.
	struct NamedHotkey
	{
		std::string name;
		Hotkey hotkey;
	};

	/// Use these hotkeys. `enabled` false turns all of them off. A key name that has no virtual-key code stops only
	/// that hotkey, and the log tells which one.
	void SetHotkeys(bool enabled, std::span<const NamedHotkey> hotkeys);

	/// Return the indexes (in the list of `SetHotkeys`) of the hotkeys that the user pressed since the last poll. Call
	/// it on the main thread, one time in each frame.
	[[nodiscard]] std::vector<std::size_t> Poll();

private:
	/// A hotkey with the code of its key and the state of the key in the last poll.
	struct Binding
	{
		Hotkey hotkey;
		std::uint16_t key = 0;
		bool wasDown = false;
	};

	KeyReader _keys;
	FocusReader _focus;
	bool _enabled = false;
	std::vector<std::optional<Binding>> _bindings;
};

} // namespace ts4_mod
