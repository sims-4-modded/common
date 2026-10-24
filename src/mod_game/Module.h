/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <common/Result.h>

namespace ts4_mod
{

/// One section of a module image, as the PE headers give it. `start` and `size` are a part of the image in memory,
/// from the base of the module.
struct SectionLayout
{
	std::string name;
	std::size_t start = 0;
	std::size_t size = 0;
	/// The IMAGE_SCN_* flags of the section.
	std::uint32_t characteristics = 0;
};

/// The parts of a module image that the PE headers give.
struct ImageLayout
{
	/// The size of the full image in memory.
	std::size_t imageSize = 0;
	std::vector<SectionLayout> sections;
};

/// Read the PE headers of an x64 module image in memory. `headers` holds the first bytes of the image: the headers
/// and possibly more. The function reads only bytes in `headers`. It returns an error for a bad header, a header that
/// does not fit in `headers`, or a section that does not fit in the image.
[[nodiscard]] ts4::Result<ImageLayout> ReadImageLayout(std::span<const std::byte> headers);

/// One section of a loaded module.
struct ModuleSection
{
	std::string name;
	std::span<const std::byte> bytes;
	std::uint32_t characteristics = 0;

	/// True if the section holds code.
	[[nodiscard]] bool Executable() const;
	/// True if the PE headers let the process read the section.
	[[nodiscard]] bool Readable() const;
};

/// True if the process can read each byte of `bytes` now: each page is committed, and no page is a guard page or has
/// no access.
[[nodiscard]] bool IsReadable(std::span<const std::byte> bytes);

/// A view of a module that the process loaded, for example `TS4_x64.exe` or `Simulation_x64.dll`. The view reads the
/// PE headers in memory one time. It never writes to the module.
///
/// The object holds a reference to the module, so the module stays loaded while a copy of the object exists. The
/// spans stay valid while the object exists. A part of the image can have pages that the process cannot read. Call
/// `IsReadable` before you read a part that is not in a readable section.
class ModuleImage
{
public:
	/// Find a loaded module by its file name, for example "Simulation_x64.dll". The case of the letters is not
	/// important. Return an error if no module has that name or if its headers are bad.
	[[nodiscard]] static ts4::Result<ModuleImage> Find(std::string_view name);

	/// Find the loaded module that holds `address`. Return an error if no module holds it.
	[[nodiscard]] static ts4::Result<ModuleImage> Containing(const void* address);

	/// The file name of the module, for example "TS4_x64.exe".
	[[nodiscard]] const std::string& Name() const { return _name; }
	/// The address of the first byte of the image.
	[[nodiscard]] std::uintptr_t Base() const { return reinterpret_cast<std::uintptr_t>(_image.data()); }
	/// The full image in memory.
	[[nodiscard]] std::span<const std::byte> Image() const { return _image; }
	[[nodiscard]] std::span<const ModuleSection> Sections() const { return _sections; }

	/// The first section with the name `name` (for example ".text"), or null.
	[[nodiscard]] const ModuleSection* FindSection(std::string_view name) const;

	/// True if `address` is in the image.
	[[nodiscard]] bool Contains(const void* address) const;

private:
	ModuleImage() = default;

	[[nodiscard]] static ts4::Result<ModuleImage> FromHandle(std::shared_ptr<void> module);

	/// The reference to the module. The deleter calls FreeLibrary.
	std::shared_ptr<void> _module;
	std::string _name;
	std::span<const std::byte> _image;
	std::vector<ModuleSection> _sections;
};

} // namespace ts4_mod
