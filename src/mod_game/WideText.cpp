/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "mod_game/WideText.h"

#include <limits>

#include <windows.h>

namespace ts4_mod
{

std::wstring ToWide(std::string_view text)
{
	if (text.empty() || text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
	{
		return {};
	}
	const auto size = static_cast<int>(text.size());
	const auto count = MultiByteToWideChar(CP_UTF8, 0, text.data(), size, nullptr, 0);
	std::wstring wide(static_cast<std::size_t>(count), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), size, wide.data(), count);
	return wide;
}

std::string ToUtf8(std::wstring_view text)
{
	if (text.empty() || text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
	{
		return {};
	}
	const auto size = static_cast<int>(text.size());
	const auto count = WideCharToMultiByte(CP_UTF8, 0, text.data(), size, nullptr, 0, nullptr, nullptr);
	std::string utf8(static_cast<std::size_t>(count), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.data(), size, utf8.data(), count, nullptr, nullptr);
	return utf8;
}

} // namespace ts4_mod
