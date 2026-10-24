/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "GameVersion.h"

#include <charconv>

#include <array>
#include <format>
#include <system_error>

namespace ts4_mod
{

ts4::Result<GameVersion> ParseGameVersion(std::string_view text)
{
	std::array<std::uint32_t, 4> parts {};
	const char* cursor = text.data();
	const char* const end = text.data() + text.size();
	for (std::size_t index = 0; index < parts.size(); ++index)
	{
		if (index > 0)
		{
			if (cursor == end || *cursor != '.')
			{
				return ts4::MakeError("the version \"{}\" does not have four numbers", text);
			}
			++cursor;
		}
		const auto [next, error] = std::from_chars(cursor, end, parts.at(index));
		if (error != std::errc {} || next == cursor)
		{
			return ts4::MakeError("the version \"{}\" has a part that is not a number", text);
		}
		cursor = next;
	}
	if (cursor != end)
	{
		return ts4::MakeError("the version \"{}\" has more text after the fourth number", text);
	}
	return GameVersion {.major = parts[0], .minor = parts[1], .patch = parts[2], .build = parts[3]};
}

ts4::Result<GameVersion> ReadGameVersionFile(std::span<const std::byte> bytes)
{
	constexpr std::size_t k_LengthSize = 4;
	if (bytes.size() < k_LengthSize)
	{
		return ts4::MakeError("the version file has {} bytes, less than the length", bytes.size());
	}
	std::uint32_t length = 0;
	for (std::size_t index = 0; index < k_LengthSize; ++index)
	{
		length |= static_cast<std::uint32_t>(bytes[index]) << (8 * index);
	}
	const auto text = bytes.subspan(k_LengthSize);
	if (length > text.size())
	{
		return ts4::MakeError("the version file gives a length of {}, but has only {} bytes of text", length, text.size());
	}
	return ParseGameVersion(std::string_view(reinterpret_cast<const char*>(text.data()), length));
}

std::string ToString(const GameVersion& version)
{
	return std::format("{}.{}.{}.{}", version.major, version.minor, version.patch, version.build);
}

} // namespace ts4_mod
