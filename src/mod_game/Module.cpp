/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "mod_game/Module.h"

#include <cstring>

#include <algorithm>
#include <filesystem>
#include <optional>

#include <windows.h>

#include "mod_game/WideText.h"

namespace ts4_mod
{

namespace
{
/// The PE format permits at most this number of sections.
constexpr std::size_t k_MaxSections = 96;
/// The largest part of the first region of a module that the header reader uses.
constexpr std::size_t k_MaxHeaderBytes = 64 * 1024;

/// Copy a structure from `bytes` at `position`. Return nothing if the structure does not fit. The copy has no
/// alignment rule, so a bad header cannot cause an access that is not aligned.
template <typename T>
[[nodiscard]] std::optional<T> ReadAt(std::span<const std::byte> bytes, std::size_t position)
{
	if (position > bytes.size() || bytes.size() - position < sizeof(T))
	{
		return std::nullopt;
	}
	T value {};
	std::memcpy(&value, bytes.data() + position, sizeof(T));
	return value;
}

/// True if `start + size` fits in `limit`, with no overflow.
[[nodiscard]] constexpr bool FitsIn(std::size_t start, std::size_t size, std::size_t limit)
{
	return start <= limit && limit - start >= size;
}

[[nodiscard]] std::string SectionName(const IMAGE_SECTION_HEADER& section)
{
	// A name of eight characters has no terminating zero.
	const auto* name = reinterpret_cast<const char*>(section.Name);
	return {name, strnlen(name, IMAGE_SIZEOF_SHORT_NAME)};
}

[[nodiscard]] std::string ModuleFileName(HMODULE module)
{
	std::wstring path(MAX_PATH, L'\0');
	for (;;)
	{
		const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
		if (length == 0)
		{
			return {};
		}
		if (length < path.size())
		{
			path.resize(length);
			break;
		}
		path.resize(path.size() * 2);
	}
	return ToUtf8(std::filesystem::path(path).filename().wstring());
}

/// Hold a reference to a module. The deleter gives the reference back.
[[nodiscard]] std::shared_ptr<void> HoldModule(HMODULE module)
{
	return {module, [](void* handle) { FreeLibrary(static_cast<HMODULE>(handle)); }};
}
} // namespace

ts4::Result<ImageLayout> ReadImageLayout(std::span<const std::byte> headers)
{
	const auto dos = ReadAt<IMAGE_DOS_HEADER>(headers, 0);
	if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE)
	{
		return ts4::MakeError("the image has no DOS header");
	}
	if (dos->e_lfanew < 0)
	{
		return ts4::MakeError("the position of the PE header is negative");
	}
	const auto ntStart = static_cast<std::size_t>(dos->e_lfanew);
	const auto signature = ReadAt<DWORD>(headers, ntStart);
	if (!signature || *signature != IMAGE_NT_SIGNATURE)
	{
		return ts4::MakeError("the image has no PE signature");
	}
	const auto fileStart = ntStart + sizeof(DWORD);
	const auto file = ReadAt<IMAGE_FILE_HEADER>(headers, fileStart);
	if (!file)
	{
		return ts4::MakeError("the PE file header does not fit in the headers");
	}
	if (file->Machine != IMAGE_FILE_MACHINE_AMD64)
	{
		return ts4::MakeError("the image is not an x64 image (machine 0x{:04X})", file->Machine);
	}
	const auto optionalStart = fileStart + sizeof(IMAGE_FILE_HEADER);
	// The data directories at the end of the optional header can be absent. Only the fixed part must be there.
	constexpr auto k_FixedOptionalSize = offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory);
	if (file->SizeOfOptionalHeader < k_FixedOptionalSize)
	{
		return ts4::MakeError("the optional header is too small ({} bytes)", file->SizeOfOptionalHeader);
	}
	if (!FitsIn(optionalStart, k_FixedOptionalSize, headers.size()))
	{
		return ts4::MakeError("the optional header does not fit in the headers");
	}
	IMAGE_OPTIONAL_HEADER64 fixed {};
	std::memcpy(&fixed, headers.data() + optionalStart, k_FixedOptionalSize);
	if (fixed.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
	{
		return ts4::MakeError("the optional header is not the header of an x64 image");
	}
	if (fixed.SizeOfImage == 0 || fixed.SizeOfHeaders > fixed.SizeOfImage)
	{
		return ts4::MakeError("the image size ({}) or the header size ({}) is bad", fixed.SizeOfImage, fixed.SizeOfHeaders);
	}
	if (file->NumberOfSections == 0 || file->NumberOfSections > k_MaxSections)
	{
		return ts4::MakeError("the image has {} sections", file->NumberOfSections);
	}

	ImageLayout layout {.imageSize = fixed.SizeOfImage, .sections = {}};
	const auto tableStart = optionalStart + file->SizeOfOptionalHeader;
	for (std::size_t index = 0; index < file->NumberOfSections; ++index)
	{
		const auto section = ReadAt<IMAGE_SECTION_HEADER>(headers, tableStart + (index * sizeof(IMAGE_SECTION_HEADER)));
		if (!section)
		{
			return ts4::MakeError("the section table does not fit in the headers");
		}
		// In memory, a section has its virtual size. An old linker can write 0 there and only the size on disk.
		const std::size_t size = section->Misc.VirtualSize != 0 ? section->Misc.VirtualSize : section->SizeOfRawData;
		auto name = SectionName(*section);
		if (!FitsIn(section->VirtualAddress, size, layout.imageSize))
		{
			return ts4::MakeError("the section '{}' does not fit in the image", name);
		}
		layout.sections.push_back(SectionLayout {
		    .name = std::move(name),
		    .start = section->VirtualAddress,
		    .size = size,
		    .characteristics = section->Characteristics,
		});
	}
	return layout;
}

bool ModuleSection::Executable() const
{
	return (characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE)) != 0;
}

bool ModuleSection::Readable() const
{
	return (characteristics & IMAGE_SCN_MEM_READ) != 0;
}

bool IsReadable(std::span<const std::byte> bytes)
{
	const auto* position = bytes.data();
	const auto* end = bytes.data() + bytes.size();
	while (position < end)
	{
		MEMORY_BASIC_INFORMATION information {};
		if (VirtualQuery(position, &information, sizeof(information)) == 0 || information.State != MEM_COMMIT)
		{
			return false;
		}
		constexpr DWORD k_Readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
		                             PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
		if ((information.Protect & k_Readable) == 0 || (information.Protect & PAGE_GUARD) != 0)
		{
			return false;
		}
		const auto* regionEnd = static_cast<const std::byte*>(information.BaseAddress) + information.RegionSize;
		if (regionEnd <= position)
		{
			return false;
		}
		position = regionEnd;
	}
	return true;
}

ts4::Result<ModuleImage> ModuleImage::Find(std::string_view name)
{
	if (name.empty())
	{
		return ts4::MakeError("the module name is empty");
	}
	const auto wide = ToWide(name);
	HMODULE module = nullptr;
	if (GetModuleHandleExW(0, wide.c_str(), &module) == FALSE || module == nullptr)
	{
		return ts4::MakeError("the module {} is not loaded", name);
	}
	return FromHandle(HoldModule(module));
}

ts4::Result<ModuleImage> ModuleImage::Containing(const void* address)
{
	HMODULE module = nullptr;
	if (address == nullptr ||
	    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, static_cast<LPCWSTR>(address), &module) == FALSE ||
	    module == nullptr)
	{
		return ts4::MakeError("no loaded module holds the address");
	}
	return FromHandle(HoldModule(module));
}

ts4::Result<ModuleImage> ModuleImage::FromHandle(std::shared_ptr<void> module)
{
	const auto* base = static_cast<const std::byte*>(module.get());
	auto name = ModuleFileName(static_cast<HMODULE>(module.get()));
	MEMORY_BASIC_INFORMATION information {};
	if (VirtualQuery(base, &information, sizeof(information)) == 0 || information.BaseAddress != base)
	{
		return ts4::MakeError("the headers of the module {} are not in memory", name);
	}
	const auto readable = std::min<std::size_t>(information.RegionSize, k_MaxHeaderBytes);
	const std::span<const std::byte> headers(base, readable);
	if (!IsReadable(headers))
	{
		return ts4::MakeError("the process cannot read the headers of the module {}", name);
	}
	auto layout = ReadImageLayout(headers);
	if (!layout)
	{
		return ts4::MakeError("the module {} has bad PE headers: {}", name, layout.GetError().message);
	}

	ModuleImage image;
	image._image = std::span<const std::byte>(base, layout->imageSize);
	for (auto& section : layout->sections)
	{
		image._sections.push_back(ModuleSection {
		    .name = std::move(section.name),
		    .bytes = image._image.subspan(section.start, section.size),
		    .characteristics = section.characteristics,
		});
	}
	image._name = std::move(name);
	image._module = std::move(module);
	return image;
}

const ModuleSection* ModuleImage::FindSection(std::string_view name) const
{
	const auto found = std::ranges::find(_sections, name, &ModuleSection::name);
	return found != _sections.end() ? &*found : nullptr;
}

bool ModuleImage::Contains(const void* address) const
{
	const auto value = reinterpret_cast<std::uintptr_t>(address);
	return value >= Base() && value - Base() < _image.size();
}

} // namespace ts4_mod
