/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <cstddef>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <mod_core/Signature.h>

using namespace ts4_mod;

namespace
{

[[nodiscard]] std::vector<std::byte> Bytes(std::initializer_list<int> values)
{
	std::vector<std::byte> bytes;
	for (const auto value : values)
	{
		bytes.push_back(static_cast<std::byte>(value));
	}
	return bytes;
}

// The compiler reads these signatures. A text that is not correct stops the compile.
constexpr auto k_TestSignature = MakeSignature("48 8b ?? 05");
static_assert(k_TestSignature.size == 4);
static_assert(k_TestSignature.Bytes()[0] == SignatureByte {.value = 0x48});
static_assert(k_TestSignature.Bytes()[1] == SignatureByte {.value = 0x8B});
static_assert(k_TestSignature.Bytes()[2].any);
static_assert(k_TestSignature.text == "48 8b ?? 05");

constexpr std::array<std::byte, 5> k_Memory {std::byte {0x90}, std::byte {0x48}, std::byte {0x8B}, std::byte {0x11},
                                             std::byte {0x05}};
static_assert(MatchesAt(k_Memory, 1, k_TestSignature.Bytes()));
static_assert(!MatchesAt(k_Memory, 0, k_TestSignature.Bytes()));
static_assert(!MatchesAt(k_Memory, 2, k_TestSignature.Bytes()));

} // namespace

TEST(Signature, ReadsFixedBytesAndAnyBytes)
{
	const auto signature = Signature::Parse("  48 8B ?? 05\t? ff  ");
	ASSERT_TRUE(signature) << signature.GetError().message;
	ASSERT_EQ(signature->Size(), 6u);
	EXPECT_EQ(signature->Bytes()[0], (SignatureByte {.value = 0x48}));
	EXPECT_EQ(signature->Bytes()[2], (SignatureByte {.any = true}));
	EXPECT_EQ(signature->Bytes()[4], (SignatureByte {.any = true}));
	EXPECT_EQ(signature->Bytes()[5], (SignatureByte {.value = 0xFF}));
	EXPECT_EQ(signature->ToString(), "48 8B ?? 05 ?? FF");
}

TEST(Signature, TheCompilerAndTheRunTimeReadTheSameBytes)
{
	const auto signature = Signature::Parse(k_TestSignature.text);
	ASSERT_TRUE(signature);
	EXPECT_TRUE(std::ranges::equal(signature->Bytes(), k_TestSignature.Bytes()));
}

TEST(Signature, RefusesATextThatIsNotCorrect)
{
	EXPECT_FALSE(Signature::Parse(""));
	EXPECT_FALSE(Signature::Parse("   "));
	EXPECT_FALSE(Signature::Parse("?? ??"));
	EXPECT_FALSE(Signature::Parse("488B"));
	EXPECT_FALSE(Signature::Parse("48 8"));
	EXPECT_FALSE(Signature::Parse("48 8G"));
	EXPECT_FALSE(Signature::Parse("48 4?"));
	EXPECT_FALSE(Signature::Parse("48 ???"));
	EXPECT_FALSE(Signature::Parse("48,8B"));
	EXPECT_FALSE(Signature::Parse("0x48"));
}

TEST(Signature, TheErrorTellsThePositionOfTheBadToken)
{
	const auto read = ReadSignature("48 8B zz 05", {});
	EXPECT_FALSE(read.Correct());
	EXPECT_EQ(read.position, 6u);
	const auto parsed = Signature::Parse("48 8B zz 05");
	ASSERT_FALSE(parsed);
	EXPECT_NE(parsed.GetError().message.find("position 6"), std::string::npos);
}

TEST(Signature, FindsTheOnePlaceThatMatches)
{
	const auto memory = Bytes({0x00, 0x48, 0x8B, 0x05, 0x10, 0x48, 0x8B, 0x0D, 0x05, 0xCC});
	const auto signature = *Signature::Parse("48 8B ?? 05");
	const auto matches = FindSignature(memory, signature.Bytes());
	EXPECT_EQ(matches.Count(), MatchCount::One);
	EXPECT_EQ(matches.Unique(), 5u);
	const auto unique = FindUniqueSignature(memory, signature.Bytes(), "test");
	ASSERT_TRUE(unique) << unique.GetError().message;
	EXPECT_EQ(*unique, 5u);
}

TEST(Signature, TellsWhenNoPlaceMatches)
{
	const auto memory = Bytes({0x48, 0x8B, 0x00, 0x06, 0x48});
	const auto signature = *Signature::Parse("48 8B ?? 05");
	const auto matches = FindSignature(memory, signature.Bytes());
	EXPECT_EQ(matches.Count(), MatchCount::None);
	EXPECT_FALSE(matches.Unique());
	const auto unique = FindUniqueSignature(memory, signature.Bytes(), "ZoneLoad");
	ASSERT_FALSE(unique);
	EXPECT_NE(unique.GetError().message.find("ZoneLoad"), std::string::npos);
	EXPECT_NE(unique.GetError().message.find("no place"), std::string::npos);
}

TEST(Signature, TellsWhenManyPlacesMatch)
{
	const auto memory = Bytes({0xE8, 0x01, 0xC3, 0xE8, 0x02, 0xC3, 0xE8, 0x03, 0xC3});
	const auto signature = *Signature::Parse("E8 ?? C3");
	const auto matches = FindSignature(memory, signature.Bytes());
	EXPECT_EQ(matches.Count(), MatchCount::Many);
	EXPECT_EQ(matches.offsets, (std::vector<std::size_t> {0, 3, 6}));
	EXPECT_FALSE(matches.Unique());
	const auto unique = FindUniqueSignature(memory, signature.Bytes(), "test");
	ASSERT_FALSE(unique);
	EXPECT_NE(unique.GetError().message.find("3 places"), std::string::npos);
}

TEST(Signature, FindsMatchesThatOverlap)
{
	const auto memory = Bytes({0xAA, 0xAA, 0xAA, 0xAA});
	const auto matches = FindSignature(memory, Signature::Parse("AA AA")->Bytes());
	EXPECT_EQ(matches.offsets, (std::vector<std::size_t> {0, 1, 2}));
}

TEST(Signature, AnyBytesAtTheStartAndTheEndStayInTheMemory)
{
	// The fixed byte is near the start and the end of the memory, but the full signature does not fit there.
	const auto memory = Bytes({0x05, 0x01, 0x02, 0x05});
	EXPECT_EQ(FindSignature(memory, Signature::Parse("?? 05 ??")->Bytes()).Count(), MatchCount::None);
	EXPECT_EQ(FindSignature(memory, Signature::Parse("?? ?? 02 05")->Bytes()).offsets, (std::vector<std::size_t> {0}));
	EXPECT_EQ(FindSignature(memory, Signature::Parse("05 ?? ?? 05")->Bytes()).offsets, (std::vector<std::size_t> {0}));
}

TEST(Signature, MemoryShorterThanTheSignatureHasNoMatch)
{
	const auto memory = Bytes({0x48, 0x8B});
	EXPECT_EQ(FindSignature(memory, Signature::Parse("48 8B 05")->Bytes()).Count(), MatchCount::None);
	EXPECT_EQ(FindSignature({}, Signature::Parse("48")->Bytes()).Count(), MatchCount::None);
	EXPECT_EQ(FindSignature(memory, {}).Count(), MatchCount::None);
	EXPECT_FALSE(MatchesAt(memory, 3, Signature::Parse("48")->Bytes()));
}

TEST(Signature, FindsAMatchInALargeMemory)
{
	std::vector<std::byte> memory(1 << 20, std::byte {0xCC});
	const auto signature = *Signature::Parse("40 53 48 83 EC ?? 48 8B D9");
	const auto expected = memory.size() - 1000;
	const auto pattern = Bytes({0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9});
	std::ranges::copy(pattern, memory.begin() + static_cast<std::ptrdiff_t>(expected));
	const auto unique = FindUniqueSignature(memory, signature.Bytes(), "test");
	ASSERT_TRUE(unique) << unique.GetError().message;
	EXPECT_EQ(*unique, expected);
}
