/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "mod_game/HotkeyInput.h"

#include <utility>

#include <spdlog/spdlog.h>
#include <windows.h>

namespace ts4_mod
{

namespace
{
[[nodiscard]] bool AsyncKeyDown(std::uint16_t virtualKey)
{
	// The high bit of the result tells that the key is down now.
	return (static_cast<std::uint16_t>(GetAsyncKeyState(virtualKey)) & 0x8000U) != 0;
}

[[nodiscard]] bool ProcessHasForeground()
{
	const auto window = GetForegroundWindow();
	if (window == nullptr)
	{
		return false;
	}
	DWORD process = 0;
	GetWindowThreadProcessId(window, &process);
	return process == GetCurrentProcessId();
}
} // namespace

HotkeyInput::HotkeyInput()
    : HotkeyInput(&AsyncKeyDown, &ProcessHasForeground)
{
}

HotkeyInput::HotkeyInput(KeyReader keys, FocusReader focus)
    : _keys(std::move(keys))
    , _focus(std::move(focus))
{
}

void HotkeyInput::SetHotkeys(bool enabled, std::span<const NamedHotkey> hotkeys)
{
	_enabled = enabled;
	_bindings.assign(hotkeys.size(), std::nullopt);
	for (std::size_t index = 0; index < hotkeys.size(); ++index)
	{
		const auto& [name, hotkey] = hotkeys[index];
		if (!hotkey.Enabled())
		{
			continue;
		}
		if (const auto key = VirtualKeyCode(hotkey.key); key)
		{
			// A key that is down now must go up before the hotkey fires.
			_bindings[index] = Binding {.hotkey = hotkey, .key = *key, .wasDown = _keys && _keys(*key)};
		}
		else
		{
			spdlog::error("hotkeys: the key {} has no virtual-key code; the hotkey {} is off", hotkey.key, name);
		}
	}
}

std::vector<std::size_t> HotkeyInput::Poll()
{
	std::vector<std::size_t> actions;
	if (!_keys)
	{
		return actions;
	}
	const bool active = _enabled && (!_focus || _focus());
	std::optional<ModifierKeys> held;
	for (std::size_t index = 0; index < _bindings.size(); ++index)
	{
		auto& binding = _bindings[index];
		if (!binding)
		{
			continue;
		}
		const auto down = _keys(binding->key);
		const auto pressed = down && !binding->wasDown;
		// The state of each key changes also while the hotkeys are off, so that a key held during that time does not
		// fire later.
		binding->wasDown = down;
		if (!pressed || !active)
		{
			continue;
		}
		if (!held)
		{
			held = ModifierKeys {.control = _keys(VK_CONTROL), .alt = _keys(VK_MENU), .shift = _keys(VK_SHIFT)};
		}
		if (HotkeyFires(binding->hotkey, *held, true))
		{
			actions.push_back(index);
		}
	}
	return actions;
}

} // namespace ts4_mod
