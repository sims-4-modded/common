/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <cstddef>
#include <cstdint>

#include <array>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <mod_game/Scan.h>

using namespace ts4_mod;

// A small function with known bytes, in its own section of code in the test exe: it puts a constant into the result
// register and returns. The bytes after the return are a marker, so that the signature matches only here.
#pragma section(".ts4scan", read, execute)
__declspec(allocate(".ts4scan")) extern const unsigned char k_KnownFunction[] = {
    0xB8, 0xDE, 0xC0, 0x57, 0x7E,                   // The function puts the constant into the result register.
    0xC3,                                           // The function returns.
    0xCC, 0x54, 0x53, 0x34, 0x4D, 0x4F, 0x44, 0xCC, // The marker.
};

namespace
{

constexpr std::uint32_t k_KnownResult = 0x7E57C0DE;
constexpr auto k_KnownSignature = MakeSignature("B8 DE C0 57 7E C3 CC 54 53 34 4D 4F 44");
constexpr auto k_KnownSignatureWithAnyBytes = MakeSignature("B8 ?? ?? ?? ?? C3 CC 54 53 34 4D 4F 44");
constexpr auto k_MissingSignature = MakeSignature("CC 44 4F 4D 34 53 54 CC C3 7E 57 C0 DE B8");

[[nodiscard]] ModuleImage TestExe()
{
	auto module = ModuleImage::Containing(k_KnownFunction);
	EXPECT_TRUE(module) << module.GetError().message;
	return std::move(module).Value();
}

} // namespace

TEST(Scan, TheKnownFunctionRuns)
{
	using Function = std::uint32_t (*)();
	const auto function = reinterpret_cast<Function>(reinterpret_cast<std::uintptr_t>(k_KnownFunction));
	EXPECT_EQ(function(), k_KnownResult);
}

TEST(Scan, FindsTheKnownFunction)
{
	const auto module = TestExe();
	const auto* section = module.FindSection(".ts4scan");
	ASSERT_NE(section, nullptr);
	EXPECT_TRUE(section->Executable());

	const auto found = FindUniqueSignature(module, k_KnownSignature.Bytes(), "KnownFunction");
	ASSERT_TRUE(found) << found.GetError().message;
	EXPECT_EQ(*found, reinterpret_cast<const std::byte*>(k_KnownFunction));

	const auto withAnyBytes = FindUniqueSignature(module, k_KnownSignatureWithAnyBytes.Bytes(), "KnownFunction");
	ASSERT_TRUE(withAnyBytes) << withAnyBytes.GetError().message;
	EXPECT_EQ(*withAnyBytes, reinterpret_cast<const std::byte*>(k_KnownFunction));

	const auto fromText = FindUniqueSignature(module, k_KnownSignature.text, "KnownFunction");
	ASSERT_TRUE(fromText) << fromText.GetError().message;
	EXPECT_EQ(*fromText, reinterpret_cast<const std::byte*>(k_KnownFunction));
}

TEST(Scan, NamesTheSignatureAndTheCount)
{
	const auto module = TestExe();
	const auto missing = FindUniqueSignature(module, k_MissingSignature.Bytes(), "MissingFunction");
	ASSERT_FALSE(missing);
	EXPECT_NE(missing.GetError().message.find("MissingFunction matches 0 places in " + module.Name()), std::string::npos)
	    << missing.GetError().message;

	// Each function of the exe starts with a common byte, so this signature matches many places.
	const auto many = FindUniqueSignature(module, "CC", "CommonByte");
	ASSERT_FALSE(many);
	EXPECT_NE(many.GetError().message.find("CommonByte matches"), std::string::npos) << many.GetError().message;
	EXPECT_EQ(many.GetError().message.find("matches 1 places"), std::string::npos);

	const auto bad = FindUniqueSignature(module, "B8 XY", "BadText");
	ASSERT_FALSE(bad);
	EXPECT_NE(bad.GetError().message.find("BadText is not correct"), std::string::npos) << bad.GetError().message;
}

TEST(Scan, DoesNotScanSectionsOfData)
{
	const auto module = TestExe();
	// The bytes of this array are in a section of data only. The scan does not find them.
	static const std::array<unsigned char, 12> k_DataOnly {0xCC, 0x5A, 0xA5, 0x13, 0x37, 0x99,
	                                                       0x42, 0x24, 0x81, 0x18, 0x7F, 0xF7};
	const auto signature = MakeSignature("CC 5A A5 13 37 99 42 24 81 18 7F F7");
	const auto found = FindUniqueSignature(module, signature.Bytes(), "DataOnly");
	EXPECT_FALSE(found);
	EXPECT_EQ(k_DataOnly[0], 0xCC);
}

TEST(Scan, EntropyOfKnownBytes)
{
	EXPECT_DOUBLE_EQ(ByteEntropy({}), 0.0);
	const std::vector<std::byte> same(100, std::byte {7});
	EXPECT_DOUBLE_EQ(ByteEntropy(same), 0.0);
	std::vector<std::byte> all;
	for (int value = 0; value < 256; ++value)
	{
		all.push_back(static_cast<std::byte>(value));
		all.push_back(static_cast<std::byte>(value));
	}
	EXPECT_DOUBLE_EQ(ByteEntropy(all), 8.0);
	const std::array<std::byte, 2> two {std::byte {0}, std::byte {1}};
	EXPECT_DOUBLE_EQ(ByteEntropy(two), 1.0);
}

TEST(Scan, TheCodeOfTheTestExeIsDecrypted)
{
	const auto module = TestExe();
	const auto checked = CheckDecrypted(module);
	EXPECT_TRUE(checked) << checked.GetError().message;

	// With a very low limit, normal code looks encrypted.
	const auto strict = CheckDecrypted(module, ".text", 1.0);
	ASSERT_FALSE(strict);
	EXPECT_NE(strict.GetError().message.find("is encrypted"), std::string::npos) << strict.GetError().message;

	const auto missing = CheckDecrypted(module, ".nothing");
	ASSERT_FALSE(missing);
	EXPECT_NE(missing.GetError().message.find("has no section .nothing"), std::string::npos);

	const auto data = CheckDecrypted(module, ".rdata");
	ASSERT_FALSE(data);
	EXPECT_NE(data.GetError().message.find("not readable code"), std::string::npos);
}
