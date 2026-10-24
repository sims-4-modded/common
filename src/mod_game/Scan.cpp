/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "mod_game/Scan.h"

#include <cmath>
#include <cstdint>

#include <array>

namespace ts4_mod
{

double ByteEntropy(std::span<const std::byte> bytes)
{
	if (bytes.empty())
	{
		return 0.0;
	}
	std::array<std::uint64_t, 256> counts {};
	for (const auto byte : bytes)
	{
		++counts[static_cast<std::uint8_t>(byte)];
	}
	const auto total = static_cast<double>(bytes.size());
	double entropy = 0.0;
	for (const auto count : counts)
	{
		if (count != 0)
		{
			const auto part = static_cast<double>(count) / total;
			entropy -= part * std::log2(part);
		}
	}
	return entropy;
}

ts4::Result<void> CheckDecrypted(const ModuleImage& module, std::string_view section, double limit)
{
	const auto* found = module.FindSection(section);
	if (found == nullptr)
	{
		return ts4::MakeError("the module {} has no section {}", module.Name(), section);
	}
	if (!found->Executable() || !IsReadable(found->bytes))
	{
		return ts4::MakeError("the section {} of {} is not readable code", section, module.Name());
	}
	if (const auto entropy = ByteEntropy(found->bytes); entropy > limit)
	{
		return ts4::MakeError("the section {} of {} is encrypted: its entropy is {:.2f} bits for each byte, more than {:.2f}",
		                      section, module.Name(), entropy, limit);
	}
	return {};
}

ts4::Result<const std::byte*> FindUniqueSignature(const ModuleImage& module, std::span<const SignatureByte> signature,
                                                  std::string_view name)
{
	if (signature.empty())
	{
		return ts4::MakeError("the signature {} has no bytes", name);
	}
	std::size_t count = 0;
	const std::byte* match = nullptr;
	std::size_t scanned = 0;
	for (const auto& section : module.Sections())
	{
		if (!section.Executable())
		{
			continue;
		}
		if (!IsReadable(section.bytes))
		{
			return ts4::MakeError("the signature {}: the process cannot read the section {} of {}", name, section.name,
			                      module.Name());
		}
		++scanned;
		const auto matches = FindSignature(section.bytes, signature);
		count += matches.offsets.size();
		if (!matches.offsets.empty())
		{
			match = section.bytes.data() + matches.offsets.front();
		}
	}
	if (scanned == 0)
	{
		return ts4::MakeError("the signature {}: the module {} has no executable section", name, module.Name());
	}
	if (count != 1)
	{
		return ts4::MakeError("the signature {} matches {} places in {}, not one", name, count, module.Name());
	}
	return match;
}

ts4::Result<const std::byte*> FindUniqueSignature(const ModuleImage& module, std::string_view signatureText,
                                                  std::string_view name)
{
	auto signature = Signature::Parse(signatureText);
	if (!signature)
	{
		return ts4::MakeError("the signature {} is not correct: {}", name, signature.GetError().message);
	}
	return FindUniqueSignature(module, signature->Bytes(), name);
}

} // namespace ts4_mod
