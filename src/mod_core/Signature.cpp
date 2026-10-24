/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <algorithm>
#include <format>
#include <functional>
#include <ranges>

#include <mod_core/Signature.h>

namespace ts4_mod
{

namespace
{
/// The longest part of a signature with only fixed bytes: the search looks for it first.
struct FixedRun
{
	std::size_t start = 0;
	std::size_t size = 0;
};

[[nodiscard]] FixedRun LongestFixedRun(std::span<const SignatureByte> signature)
{
	FixedRun best;
	FixedRun current;
	for (std::size_t i = 0; i < signature.size(); ++i)
	{
		if (signature[i].any)
		{
			current = FixedRun {.start = i + 1, .size = 0};
			continue;
		}
		++current.size;
		if (current.size > best.size)
		{
			best = current;
		}
	}
	return best;
}
} // namespace

ts4::Result<Signature> Signature::Parse(std::string_view text)
{
	const auto read = ReadSignature(text, {});
	if (!read.Correct())
	{
		return ts4::MakeError("the signature '{}' is not correct at position {}: {}", text, read.position, read.error);
	}
	Signature signature;
	signature._bytes.resize(read.count);
	(void)ReadSignature(text, signature._bytes);
	return signature;
}

std::string Signature::ToString() const
{
	std::string text;
	for (const auto& byte : _bytes)
	{
		if (!text.empty())
		{
			text += ' ';
		}
		text += byte.any ? std::string("??") : std::format("{:02X}", byte.value);
	}
	return text;
}

SignatureMatches FindSignature(std::span<const std::byte> memory, std::span<const SignatureByte> signature)
{
	SignatureMatches matches;
	const auto run = LongestFixedRun(signature);
	if (run.size == 0 || memory.size() < signature.size())
	{
		return matches;
	}
	// The search uses unsigned char, because the searcher has a fast table for one-byte types.
	std::vector<unsigned char> needle(run.size);
	std::ranges::transform(signature.subspan(run.start, run.size), needle.begin(),
	                       [](const SignatureByte& byte) { return byte.value; });
	const auto* const begin = reinterpret_cast<const unsigned char*>(memory.data());
	const auto* const end = begin + memory.size();
	const std::boyer_moore_horspool_searcher searcher(needle.begin(), needle.end());
	for (auto cursor = begin; cursor < end;)
	{
		const auto found = searcher(cursor, end).first;
		if (found == end)
		{
			break;
		}
		const auto position = static_cast<std::size_t>(found - begin);
		if (position >= run.start && MatchesAt(memory, position - run.start, signature))
		{
			matches.offsets.push_back(position - run.start);
		}
		cursor = found + 1;
	}
	return matches;
}

ts4::Result<std::size_t> FindUniqueSignature(std::span<const std::byte> memory, std::span<const SignatureByte> signature,
                                             std::string_view name)
{
	const auto matches = FindSignature(memory, signature);
	switch (matches.Count())
	{
	case MatchCount::None:
		return ts4::MakeError("the signature '{}' matches no place", name);
	case MatchCount::One:
		return matches.offsets.front();
	case MatchCount::Many:
		break;
	}
	return ts4::MakeError("the signature '{}' matches {} places; it must match exactly one", name, matches.offsets.size());
}

} // namespace ts4_mod
