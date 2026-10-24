/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>

#include <span>
#include <string>
#include <string_view>

#include <common/Result.h>

namespace ts4_mod
{

/// The version of the game, for example 1.128.90.1030. The signatures and the hooks of a mod are true only for the
/// versions that the mod names.
struct GameVersion
{
	std::uint32_t major = 0;
	std::uint32_t minor = 0;
	std::uint32_t patch = 0;
	std::uint32_t build = 0;

	[[nodiscard]] auto operator<=>(const GameVersion&) const = default;
};

/// Read a version from text with four numbers and three dots, for example "1.128.90.1030".
[[nodiscard]] ts4::Result<GameVersion> ParseGameVersion(std::string_view text);

/// Read the version from the bytes of `GameVersion.txt` in the user folder. The file has the length of the text in
/// four bytes (little endian), then the text.
[[nodiscard]] ts4::Result<GameVersion> ReadGameVersionFile(std::span<const std::byte> bytes);

/// The text of a version, for example "1.128.90.1030".
[[nodiscard]] std::string ToString(const GameVersion& version);

} // namespace ts4_mod
