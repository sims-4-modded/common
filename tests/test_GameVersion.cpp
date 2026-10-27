/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <cstddef>

#include <string_view>
#include <vector>

#include <gtest/gtest.h>
#include <mod_core/GameVersion.h>

using ts4_mod::GameVersion;

namespace
{

/// Make the bytes of a version file: the length in four bytes (little endian), then the text.
std::vector<std::byte> VersionFile(std::string_view text, std::size_t length)
{
	std::vector<std::byte> bytes;
	bytes.reserve(4 + text.size());
	for (std::size_t index = 0; index < 4; ++index)
	{
		bytes.push_back(static_cast<std::byte>((length >> (8 * index)) & 0xFF));
	}
	for (const char letter : text)
	{
		bytes.push_back(static_cast<std::byte>(letter));
	}
	return bytes;
}

} // namespace

TEST(GameVersion, ParsesFourNumbers)
{
	const auto version = ts4_mod::ParseGameVersion("1.128.90.1030");
	ASSERT_TRUE(version);
	EXPECT_EQ(*version, (GameVersion {.major = 1, .minor = 128, .patch = 90, .build = 1030}));
	EXPECT_EQ(ts4_mod::ToString(*version), "1.128.90.1030");
}

TEST(GameVersion, RefusesBadText)
{
	EXPECT_FALSE(ts4_mod::ParseGameVersion(""));
	EXPECT_FALSE(ts4_mod::ParseGameVersion("1.128.90"));
	EXPECT_FALSE(ts4_mod::ParseGameVersion("1.128.90.1030.5"));
	EXPECT_FALSE(ts4_mod::ParseGameVersion("1.128.x.1030"));
	EXPECT_FALSE(ts4_mod::ParseGameVersion("1..90.1030"));
	EXPECT_FALSE(ts4_mod::ParseGameVersion("1.128.90.99999999999"));
}

TEST(GameVersion, ComparesInOrder)
{
	const GameVersion older {.major = 1, .minor = 128, .patch = 90, .build = 1030};
	const GameVersion newer {.major = 1, .minor = 129, .patch = 10, .build = 1020};
	EXPECT_LT(older, newer);
	EXPECT_GT(newer, older);
}

TEST(GameVersion, ReadsTheVersionFile)
{
	const auto bytes = VersionFile("1.128.90.1030", 13);
	const auto version = ts4_mod::ReadGameVersionFile(bytes);
	ASSERT_TRUE(version);
	EXPECT_EQ(version->build, 1030u);
}

TEST(GameVersion, ReadsOnlyTheLengthOfTheText)
{
	const auto bytes = VersionFile("1.128.90.1030trailing", 13);
	EXPECT_TRUE(ts4_mod::ReadGameVersionFile(bytes));
}

TEST(GameVersion, RefusesAShortFile)
{
	EXPECT_FALSE(ts4_mod::ReadGameVersionFile(std::vector<std::byte>(2)));
	EXPECT_FALSE(ts4_mod::ReadGameVersionFile(VersionFile("1.128", 13)));
}
