/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <optional>
#include <string>
#include <string_view>

#include <common/Result.h>

namespace ts4_mod
{

/// A key and the modifier keys that the user must hold with it. A hotkey with no key is off.
struct Hotkey
{
	bool control = false;
	bool alt = false;
	bool shift = false;
	/// The name of the key, for example "F5", "A" or "Alpha1". `ParseHotkey` gives the names.
	std::string key;

	[[nodiscard]] bool Enabled() const { return !key.empty(); }
	friend bool operator==(const Hotkey&, const Hotkey&) = default;
};

/// The modifier keys that the user holds in a frame. The left and the right key of each pair are the same.
struct ModifierKeys
{
	bool control = false;
	bool alt = false;
	bool shift = false;
};

/// Read a hotkey such as "Ctrl+Shift+F5". The modifiers are "Ctrl", "Alt" and "Shift", in any order, with any case.
/// The last part is the key: a letter, a digit, "F1" to "F15", "Keypad0" to "Keypad9", or a name such as "Home" or
/// "PageUp". An empty text or "none" gives a hotkey that is off.
[[nodiscard]] ts4::Result<Hotkey> ParseHotkey(std::string_view text);

/// Write a hotkey in the form that `ParseHotkey` reads, for example "Ctrl+Shift+F5", or "none".
[[nodiscard]] std::string FormatHotkey(const Hotkey& hotkey);

/// Return true if the user pressed the key of the hotkey in this frame and holds exactly its modifier keys.
[[nodiscard]] bool HotkeyFires(const Hotkey& hotkey, ModifierKeys held, bool keyDown);

/// The Windows virtual-key code of a key name that `ParseHotkey` gives, for example 0x74 for "F5". Return nothing for
/// a name that `ParseHotkey` does not give. "KeypadEnter" has the code of "Return": the extended-key flag of the
/// keyboard message makes them different.
[[nodiscard]] std::optional<std::uint16_t> VirtualKeyCode(std::string_view key);

} // namespace ts4_mod
