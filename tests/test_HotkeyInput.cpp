/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <cstddef>
#include <cstdint>

#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <mod_game/HotkeyInput.h>
#include <mod_game/ModStatus.h>
#include <windows.h>

using namespace ts4_mod;

namespace
{

/// The keys that the test holds down, and the focus of the game.
struct FakeKeyboard
{
	std::set<std::uint16_t> down;
	bool focus = true;

	[[nodiscard]] HotkeyInput MakeInput()
	{
		return HotkeyInput([this](std::uint16_t key) { return down.contains(key); }, [this] { return focus; });
	}
};

[[nodiscard]] std::vector<HotkeyInput::NamedHotkey> Hotkeys(const std::vector<std::string>& texts)
{
	std::vector<HotkeyInput::NamedHotkey> hotkeys;
	for (const auto& text : texts)
	{
		auto hotkey = ParseHotkey(text);
		EXPECT_TRUE(hotkey) << text;
		hotkeys.push_back({.name = text, .hotkey = hotkey ? *hotkey : Hotkey {}});
	}
	return hotkeys;
}

} // namespace

TEST(HotkeyInput, AHotkeyFiresOneTimeForEachPress)
{
	FakeKeyboard keyboard;
	auto input = keyboard.MakeInput();
	const auto hotkeys = Hotkeys({"Ctrl+F5", "G"});
	input.SetHotkeys(true, hotkeys);
	EXPECT_TRUE(input.Poll().empty());

	keyboard.down = {VK_CONTROL, VK_F5};
	EXPECT_EQ(input.Poll(), (std::vector<std::size_t> {0}));
	// The user holds the keys: the hotkey does not fire again.
	EXPECT_TRUE(input.Poll().empty());
	keyboard.down = {VK_CONTROL};
	EXPECT_TRUE(input.Poll().empty());
	keyboard.down = {VK_CONTROL, VK_F5};
	EXPECT_EQ(input.Poll(), (std::vector<std::size_t> {0}));

	keyboard.down = {'G'};
	EXPECT_EQ(input.Poll(), (std::vector<std::size_t> {1}));
}

TEST(HotkeyInput, TheModifierKeysMustBeExact)
{
	FakeKeyboard keyboard;
	auto input = keyboard.MakeInput();
	const auto hotkeys = Hotkeys({"Ctrl+F5"});
	input.SetHotkeys(true, hotkeys);
	keyboard.down = {VK_F5};
	EXPECT_TRUE(input.Poll().empty());
	keyboard.down = {};
	(void)input.Poll();
	keyboard.down = {VK_CONTROL, VK_SHIFT, VK_F5};
	EXPECT_TRUE(input.Poll().empty());
}

TEST(HotkeyInput, NothingFiresWithoutTheFocus)
{
	FakeKeyboard keyboard;
	auto input = keyboard.MakeInput();
	const auto hotkeys = Hotkeys({"Home"});
	input.SetHotkeys(true, hotkeys);
	keyboard.focus = false;
	keyboard.down = {VK_HOME};
	EXPECT_TRUE(input.Poll().empty());
	// The key went down while another window had the keyboard, so it does not fire when the game gets it back.
	keyboard.focus = true;
	EXPECT_TRUE(input.Poll().empty());
	keyboard.down = {};
	(void)input.Poll();
	keyboard.down = {VK_HOME};
	EXPECT_EQ(input.Poll(), (std::vector<std::size_t> {0}));
}

TEST(HotkeyInput, AKeyThatIsDownAtTheStartDoesNotFire)
{
	FakeKeyboard keyboard;
	keyboard.down = {VK_F9};
	auto input = keyboard.MakeInput();
	const auto hotkeys = Hotkeys({"F9"});
	input.SetHotkeys(true, hotkeys);
	EXPECT_TRUE(input.Poll().empty());
}

TEST(HotkeyInput, OffHotkeysAndUnknownKeysDoNothing)
{
	FakeKeyboard keyboard;
	auto input = keyboard.MakeInput();
	std::vector<HotkeyInput::NamedHotkey> hotkeys {
	    {.name = "none", .hotkey = Hotkey {}},
	    {.name = "unknown", .hotkey = Hotkey {.key = "NoSuchKey"}},
	    {.name = "valid", .hotkey = Hotkey {.key = "PageUp"}},
	};
	input.SetHotkeys(true, hotkeys);
	keyboard.down = {VK_PRIOR};
	EXPECT_EQ(input.Poll(), (std::vector<std::size_t> {2}));

	input.SetHotkeys(false, hotkeys);
	keyboard.down = {};
	(void)input.Poll();
	keyboard.down = {VK_PRIOR};
	EXPECT_TRUE(input.Poll().empty());
}

TEST(ModStatus, DescribesTheStateOfAMod)
{
	ModStatus status("camera");
	status.SetEnabled(true);
	status.SetReady(true);
	status.SetActive(false, "no zone");
	status.SetError("the signature is missing");
	status.SetError("the signature is missing");
	const auto described = status.Describe();
	EXPECT_EQ(described["name"], "camera");
	EXPECT_EQ(described["enabled"], true);
	EXPECT_EQ(described["ready"], true);
	EXPECT_EQ(described["active"], false);
	EXPECT_EQ(described["reason"], "no zone");
	EXPECT_EQ(described["errors"], 2);
	EXPECT_EQ(described["lastError"], "the signature is missing");
	status.SetActive(true);
	EXPECT_EQ(status.Describe()["activeFrames"], 1);
}
