/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <cctype>
#include <cstdint>

#include <algorithm>
#include <array>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <mod_core/Hotkey.h>

namespace ts4_mod
{

namespace
{
/// A key name that a hotkey can use, other than the letters, the digits, the F keys and the keypad digits, and its
/// Windows virtual-key code.
struct NamedKey
{
	std::string_view name;
	std::uint16_t code = 0;
};

constexpr std::array<NamedKey, 33> k_NamedKeys {{
    {"Home", 0x24},       {"End", 0x23},          {"Insert", 0x2D},       {"Delete", 0x2E},         {"PageUp", 0x21},
    {"PageDown", 0x22},   {"Pause", 0x13},        {"ScrollLock", 0x91},   {"Backquote", 0xC0},      {"Minus", 0xBD},
    {"Equals", 0xBB},     {"LeftBracket", 0xDB},  {"RightBracket", 0xDD}, {"Semicolon", 0xBA},      {"Quote", 0xDE},
    {"Comma", 0xBC},      {"Period", 0xBE},       {"Slash", 0xBF},        {"Backslash", 0xDC},      {"Space", 0x20},
    {"Tab", 0x09},        {"Return", 0x0D},       {"Backspace", 0x08},    {"UpArrow", 0x26},        {"DownArrow", 0x28},
    {"LeftArrow", 0x25},  {"RightArrow", 0x27},   {"KeypadDivide", 0x6F}, {"KeypadMultiply", 0x6A}, {"KeypadMinus", 0x6D},
    {"KeypadPlus", 0x6B}, {"KeypadPeriod", 0x6E}, {"KeypadEnter", 0x0D},
}};

/// The virtual-key codes of the first key of each range of keys.
constexpr std::uint16_t k_FirstLetterCode = 0x41;
constexpr std::uint16_t k_FirstDigitCode = 0x30;
constexpr std::uint16_t k_FirstFunctionKeyCode = 0x70;
constexpr std::uint16_t k_FirstKeypadDigitCode = 0x60;

[[nodiscard]] std::string Lower(std::string_view text)
{
	std::string result(text);
	std::ranges::transform(result, result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return result;
}

[[nodiscard]] std::string_view Trim(std::string_view text)
{
	const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
	while (!text.empty() && isSpace(static_cast<unsigned char>(text.front())))
	{
		text.remove_prefix(1);
	}
	while (!text.empty() && isSpace(static_cast<unsigned char>(text.back())))
	{
		text.remove_suffix(1);
	}
	return text;
}

/// Read a number of one or two digits. Return -1 if the text is not that.
[[nodiscard]] int SmallNumber(std::string_view text)
{
	if (text.empty() || text.size() > 2 || !std::ranges::all_of(text, [](unsigned char c) { return std::isdigit(c) != 0; }))
	{
		return -1;
	}
	int value = 0;
	for (const char c : text)
	{
		value = value * 10 + (c - '0');
	}
	return value;
}

/// Find the name of a key, with the case that `FormatHotkey` writes. Return an empty text if the name is not
/// permitted.
[[nodiscard]] std::string KeyName(std::string_view text)
{
	const auto lower = Lower(text);
	if (lower.size() == 1 && std::isalpha(static_cast<unsigned char>(lower[0])) != 0)
	{
		return std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(lower[0]))));
	}
	if (lower.size() == 1 && std::isdigit(static_cast<unsigned char>(lower[0])) != 0)
	{
		return "Alpha" + lower;
	}
	if (lower.starts_with("alpha"))
	{
		const auto digit = SmallNumber(std::string_view(lower).substr(5));
		return digit >= 0 && digit <= 9 ? "Alpha" + std::to_string(digit) : std::string();
	}
	if (lower.starts_with('f'))
	{
		const auto number = SmallNumber(std::string_view(lower).substr(1));
		if (number >= 1 && number <= 15)
		{
			return "F" + std::to_string(number);
		}
	}
	if (lower.starts_with("keypad"))
	{
		const auto digit = SmallNumber(std::string_view(lower).substr(6));
		if (digit >= 0 && digit <= 9)
		{
			return "Keypad" + std::to_string(digit);
		}
	}
	const auto found = std::ranges::find_if(k_NamedKeys, [&](const NamedKey& named) { return Lower(named.name) == lower; });
	return found != k_NamedKeys.end() ? std::string(found->name) : std::string();
}
} // namespace

ts4::Result<Hotkey> ParseHotkey(std::string_view text)
{
	text = Trim(text);
	if (text.empty() || Lower(text) == "none")
	{
		return Hotkey {};
	}
	std::vector<std::string_view> parts;
	for (const auto part : std::views::split(text, '+'))
	{
		parts.emplace_back(Trim(std::string_view(part.begin(), part.end())));
	}
	Hotkey hotkey;
	for (size_t i = 0; i + 1 < parts.size(); ++i)
	{
		const auto modifier = Lower(parts[i]);
		bool* flag = modifier == "ctrl" || modifier == "control" ? &hotkey.control
		             : modifier == "alt"                         ? &hotkey.alt
		             : modifier == "shift"                       ? &hotkey.shift
		                                                         : nullptr;
		if (flag == nullptr)
		{
			return ts4::MakeError("the hotkey '{}' has the modifier '{}'; use Ctrl, Alt or Shift", text, parts[i]);
		}
		if (*flag)
		{
			return ts4::MakeError("the hotkey '{}' has the modifier '{}' two times", text, parts[i]);
		}
		*flag = true;
	}
	hotkey.key = KeyName(parts.back());
	if (hotkey.key.empty())
	{
		return ts4::MakeError("the hotkey '{}' has the key '{}', which is not permitted; use a letter, a digit, F1 to F15 or "
		                      "a name such as Home or PageUp",
		                      text, parts.back());
	}
	return hotkey;
}

std::string FormatHotkey(const Hotkey& hotkey)
{
	if (!hotkey.Enabled())
	{
		return "none";
	}
	std::string text;
	text += hotkey.control ? "Ctrl+" : "";
	text += hotkey.alt ? "Alt+" : "";
	text += hotkey.shift ? "Shift+" : "";
	return text + hotkey.key;
}

bool HotkeyFires(const Hotkey& hotkey, ModifierKeys held, bool keyDown)
{
	return hotkey.Enabled() && keyDown && held.control == hotkey.control && held.alt == hotkey.alt &&
	       held.shift == hotkey.shift;
}

std::optional<std::uint16_t> VirtualKeyCode(std::string_view key)
{
	// Only the exact names that `KeyName` gives are correct here.
	if (KeyName(key) != key)
	{
		return std::nullopt;
	}
	if (key.size() == 1)
	{
		return static_cast<std::uint16_t>(k_FirstLetterCode + (key[0] - 'A'));
	}
	const auto numberAfter = [&](std::string_view prefix) {
		return key.starts_with(prefix) ? SmallNumber(key.substr(prefix.size())) : -1;
	};
	if (const auto digit = numberAfter("Alpha"); digit >= 0)
	{
		return static_cast<std::uint16_t>(k_FirstDigitCode + digit);
	}
	if (const auto digit = numberAfter("Keypad"); digit >= 0)
	{
		return static_cast<std::uint16_t>(k_FirstKeypadDigitCode + digit);
	}
	if (const auto number = numberAfter("F"); number >= 1)
	{
		return static_cast<std::uint16_t>(k_FirstFunctionKeyCode + number - 1);
	}
	const auto found = std::ranges::find(k_NamedKeys, key, &NamedKey::name);
	return found != k_NamedKeys.end() ? std::optional<std::uint16_t>(found->code) : std::nullopt;
}

} // namespace ts4_mod
