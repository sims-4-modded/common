/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <array>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <mod_game/Module.h>
#include <windows.h>

using namespace ts4_mod;

namespace
{

/// Make the headers of a small x64 image with one section. A test changes a field to make a bad header.
struct FakeImage
{
	/// The place of the PE header. A test can write a bad position into the DOS header, but the PE header stays here.
	static constexpr LONG k_NtPosition = 128;

	std::vector<std::byte> bytes = std::vector<std::byte>(1024);
	IMAGE_DOS_HEADER dos {};
	IMAGE_NT_HEADERS64 nt {};
	IMAGE_SECTION_HEADER section {};

	FakeImage()
	{
		dos.e_magic = IMAGE_DOS_SIGNATURE;
		dos.e_lfanew = k_NtPosition;
		nt.Signature = IMAGE_NT_SIGNATURE;
		nt.FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
		nt.FileHeader.NumberOfSections = 1;
		nt.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
		nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
		nt.OptionalHeader.SizeOfImage = 0x3000;
		nt.OptionalHeader.SizeOfHeaders = 0x400;
		std::memcpy(section.Name, ".text", 5);
		section.VirtualAddress = 0x1000;
		section.Misc.VirtualSize = 0x1800;
		section.Characteristics = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
	}

	[[nodiscard]] std::vector<std::byte> Build() const
	{
		auto image = bytes;
		std::memcpy(image.data(), &dos, sizeof(dos));
		std::memcpy(image.data() + k_NtPosition, &nt, sizeof(nt));
		std::memcpy(image.data() + k_NtPosition + sizeof(nt), &section, sizeof(section));
		return image;
	}
};

[[nodiscard]] std::string LayoutError(const FakeImage& image)
{
	const auto bytes = image.Build();
	const auto layout = ReadImageLayout(bytes);
	return layout ? std::string() : layout.GetError().message;
}

/// A function of the test exe, for the tests that look for the module of an address.
__declspec(noinline) int ModuleTestFunction()
{
	return 7;
}

} // namespace

TEST(ImageLayout, ReadsTheSizeAndTheSections)
{
	const FakeImage image;
	const auto bytes = image.Build();
	const auto layout = ReadImageLayout(bytes);
	ASSERT_TRUE(layout) << layout.GetError().message;
	EXPECT_EQ(layout->imageSize, 0x3000u);
	ASSERT_EQ(layout->sections.size(), 1u);
	EXPECT_EQ(layout->sections[0].name, ".text");
	EXPECT_EQ(layout->sections[0].start, 0x1000u);
	EXPECT_EQ(layout->sections[0].size, 0x1800u);
}

TEST(ImageLayout, UsesTheSizeOnDiskIfTheVirtualSizeIsZero)
{
	FakeImage image;
	image.section.Misc.VirtualSize = 0;
	image.section.SizeOfRawData = 0x200;
	const auto bytes = image.Build();
	const auto layout = ReadImageLayout(bytes);
	ASSERT_TRUE(layout) << layout.GetError().message;
	EXPECT_EQ(layout->sections[0].size, 0x200u);
}

TEST(ImageLayout, KeepsANameOfEightCharacters)
{
	FakeImage image;
	std::memcpy(image.section.Name, "ABCDEFGH", 8);
	const auto bytes = image.Build();
	const auto layout = ReadImageLayout(bytes);
	ASSERT_TRUE(layout) << layout.GetError().message;
	EXPECT_EQ(layout->sections[0].name, "ABCDEFGH");
}

TEST(ImageLayout, RefusesEachBadHeader)
{
	{
		FakeImage image;
		image.dos.e_magic = 0;
		EXPECT_NE(LayoutError(image).find("DOS header"), std::string::npos);
	}
	{
		FakeImage image;
		image.dos.e_lfanew = -4;
		EXPECT_NE(LayoutError(image).find("negative"), std::string::npos);
	}
	{
		FakeImage image;
		image.nt.Signature = 0;
		EXPECT_NE(LayoutError(image).find("PE signature"), std::string::npos);
	}
	{
		FakeImage image;
		image.nt.FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
		EXPECT_NE(LayoutError(image).find("not an x64 image"), std::string::npos);
	}
	{
		FakeImage image;
		image.nt.FileHeader.SizeOfOptionalHeader = 16;
		EXPECT_NE(LayoutError(image).find("too small"), std::string::npos);
	}
	{
		FakeImage image;
		image.nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
		EXPECT_NE(LayoutError(image).find("not the header of an x64 image"), std::string::npos);
	}
	{
		FakeImage image;
		image.nt.OptionalHeader.SizeOfImage = 0;
		EXPECT_NE(LayoutError(image).find("image size"), std::string::npos);
	}
	{
		FakeImage image;
		image.nt.FileHeader.NumberOfSections = 0;
		EXPECT_NE(LayoutError(image).find("0 sections"), std::string::npos);
	}
	{
		FakeImage image;
		image.nt.FileHeader.NumberOfSections = 40;
		EXPECT_NE(LayoutError(image).find("section table does not fit"), std::string::npos);
	}
	{
		FakeImage image;
		image.section.Misc.VirtualSize = 0x2001;
		EXPECT_NE(LayoutError(image).find("does not fit in the image"), std::string::npos);
	}
	{
		FakeImage image;
		image.section.VirtualAddress = 0xFFFFFFF0;
		EXPECT_NE(LayoutError(image).find("does not fit in the image"), std::string::npos);
	}
}

TEST(ImageLayout, RefusesHeadersThatAreCut)
{
	const FakeImage image;
	const auto bytes = image.Build();
	for (const std::size_t size : {std::size_t {0}, std::size_t {16}, std::size_t {130}, std::size_t {200}, std::size_t {400}})
	{
		const auto layout = ReadImageLayout(std::span(bytes).first(size));
		EXPECT_FALSE(layout) << "size " << size;
	}
}

TEST(ModuleImage, FindsTheTestExe)
{
	const auto containing = ModuleImage::Containing(reinterpret_cast<const void*>(&ModuleTestFunction));
	ASSERT_TRUE(containing) << containing.GetError().message;
	EXPECT_EQ(containing->Base(), reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)));
	EXPECT_TRUE(containing->Contains(reinterpret_cast<const void*>(&ModuleTestFunction)));
	EXPECT_FALSE(containing->Contains(nullptr));

	const auto found = ModuleImage::Find(containing->Name());
	ASSERT_TRUE(found) << found.GetError().message;
	EXPECT_EQ(found->Base(), containing->Base());
	EXPECT_EQ(found->Image().size(), containing->Image().size());

	const auto* text = found->FindSection(".text");
	ASSERT_NE(text, nullptr);
	EXPECT_TRUE(text->Executable());
	EXPECT_TRUE(text->Readable());
	EXPECT_TRUE(IsReadable(text->bytes));
	EXPECT_TRUE(found->Contains(text->bytes.data()));
	const auto* data = found->FindSection(".rdata");
	ASSERT_NE(data, nullptr);
	EXPECT_FALSE(data->Executable());
	EXPECT_EQ(found->FindSection(".nothing"), nullptr);
}

TEST(ModuleImage, FindsASystemModuleWithAnyCase)
{
	const auto found = ModuleImage::Find("KERNEL32.dll");
	ASSERT_TRUE(found) << found.GetError().message;
	EXPECT_EQ(found->Base(), reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"kernel32.dll")));
	EXPECT_FALSE(found->Sections().empty());
}

TEST(ModuleImage, RefusesAModuleThatIsNotLoaded)
{
	const auto found = ModuleImage::Find("Simulation_x64.dll");
	ASSERT_FALSE(found);
	EXPECT_NE(found.GetError().message.find("Simulation_x64.dll is not loaded"), std::string::npos);
	EXPECT_FALSE(ModuleImage::Find(""));
	EXPECT_FALSE(ModuleImage::Containing(nullptr));
}

TEST(ModuleImage, IsReadableRefusesMemoryThatIsNotThere)
{
	auto* reserved = VirtualAlloc(nullptr, 4096, MEM_RESERVE, PAGE_NOACCESS);
	ASSERT_NE(reserved, nullptr);
	EXPECT_FALSE(IsReadable(std::span(static_cast<const std::byte*>(reserved), 4096)));
	VirtualFree(reserved, 0, MEM_RELEASE);
	const std::array<std::byte, 8> local {};
	EXPECT_TRUE(IsReadable(local));
}
