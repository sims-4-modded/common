/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <common/Result.h>

namespace ts4_mod
{

/// One byte of a signature: a fixed value, or any byte.
struct SignatureByte
{
	std::uint8_t value = 0;
	bool any = false;

	friend constexpr bool operator==(const SignatureByte&, const SignatureByte&) = default;
};

/// The result of a read of a signature text: the number of bytes, or the reason that the text is not correct.
struct SignatureRead
{
	std::size_t count = 0;
	/// Empty if the text is correct.
	std::string_view error {};
	/// The position in the text where the error starts.
	std::size_t position = 0;

	[[nodiscard]] constexpr bool Correct() const { return error.empty(); }
};

namespace detail
{
[[nodiscard]] constexpr int HexDigit(char c)
{
	if (c >= '0' && c <= '9')
	{
		return c - '0';
	}
	if (c >= 'a' && c <= 'f')
	{
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F')
	{
		return c - 'A' + 10;
	}
	return -1;
}

[[nodiscard]] constexpr bool IsSpace(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
} // namespace detail

/// Read a signature text. The text has one token for each byte, with spaces between the tokens. A token is two hex
/// digits (a fixed byte, in either case), or "??" or "?" (any byte). `out` gets the bytes while it has space; the
/// result has the count of all the bytes. A text with no bytes, or with no fixed byte, is not correct, because it
/// matches at each position. The function can run at compile time.
[[nodiscard]] constexpr SignatureRead ReadSignature(std::string_view text, std::span<SignatureByte> out)
{
	SignatureRead read;
	bool fixed = false;
	std::size_t index = 0;
	while (index < text.size())
	{
		if (detail::IsSpace(text[index]))
		{
			++index;
			continue;
		}
		const auto start = index;
		while (index < text.size() && !detail::IsSpace(text[index]))
		{
			++index;
		}
		const auto token = text.substr(start, index - start);
		SignatureByte byte;
		if (token == "?" || token == "??")
		{
			byte.any = true;
		}
		else if (token.size() == 2 && detail::HexDigit(token[0]) >= 0 && detail::HexDigit(token[1]) >= 0)
		{
			byte.value = static_cast<std::uint8_t>((detail::HexDigit(token[0]) * 16) + detail::HexDigit(token[1]));
			fixed = true;
		}
		else
		{
			return SignatureRead {.count = read.count, .error = "a token is not two hex digits or ??", .position = start};
		}
		if (read.count < out.size())
		{
			out[read.count] = byte;
		}
		++read.count;
	}
	if (read.count == 0)
	{
		return SignatureRead {.error = "the signature has no bytes"};
	}
	if (!fixed)
	{
		return SignatureRead {.count = read.count, .error = "the signature has no fixed byte"};
	}
	return read;
}

/// A signature that the compiler reads from a literal text (see `MakeSignature`). It also keeps the text, so that a
/// mod can give the same text to the extender.
template <std::size_t Capacity>
struct FixedSignature
{
	std::array<SignatureByte, Capacity> bytes {};
	std::size_t size = 0;
	/// The literal text, with its terminating zero after the view.
	std::string_view text {};

	[[nodiscard]] constexpr std::span<const SignatureByte> Bytes() const { return {bytes.data(), size}; }
};

/// Read a literal signature text at compile time. A text that is not correct stops the compile.
template <std::size_t N>
// NOLINTNEXTLINE(modernize-avoid-c-arrays): the parameter takes a literal text, with its length.
[[nodiscard]] consteval auto MakeSignature(const char (&text)[N])
{
	static_assert(N > 1, "the signature text is empty");
	// Each token has one character or more, and a space after it, so the text has at most N / 2 bytes.
	FixedSignature<(N / 2) + 1> signature;
	signature.text = std::string_view(text, N - 1);
	const auto read = ReadSignature(signature.text, signature.bytes);
	if (!read.Correct())
	{
		// A throw in a constant evaluation stops the compile.
		throw "the signature text is not correct";
	}
	signature.size = read.count;
	return signature;
}

/// A signature that the extender reads at run time, for example from a DLL mod.
class Signature
{
public:
	/// Read a signature text (see `ReadSignature`). Return an error that tells the position of a bad token.
	[[nodiscard]] static ts4::Result<Signature> Parse(std::string_view text);

	[[nodiscard]] std::span<const SignatureByte> Bytes() const { return _bytes; }
	[[nodiscard]] std::size_t Size() const { return _bytes.size(); }

	/// The text in the normal form: two capital hex digits or "??" for each byte, with one space between two bytes.
	[[nodiscard]] std::string ToString() const;

private:
	std::vector<SignatureByte> _bytes;
};

/// True if the signature matches the memory at `offset`. The function can run at compile time.
[[nodiscard]] constexpr bool MatchesAt(std::span<const std::byte> memory, std::size_t offset,
                                       std::span<const SignatureByte> signature)
{
	if (signature.empty() || offset > memory.size() || memory.size() - offset < signature.size())
	{
		return false;
	}
	for (std::size_t i = 0; i < signature.size(); ++i)
	{
		if (!signature[i].any && memory[offset + i] != static_cast<std::byte>(signature[i].value))
		{
			return false;
		}
	}
	return true;
}

/// How many places a signature matches.
enum class MatchCount
{
	None,
	One,
	Many,
};

/// The places where a signature matches.
struct SignatureMatches
{
	/// The position of each match from the start of the memory, in increasing order. Two matches can overlap.
	std::vector<std::size_t> offsets;

	[[nodiscard]] MatchCount Count() const
	{
		return offsets.empty() ? MatchCount::None : offsets.size() == 1 ? MatchCount::One : MatchCount::Many;
	}

	/// The position of the match if there is exactly one match, else nothing.
	[[nodiscard]] std::optional<std::size_t> Unique() const
	{
		return offsets.size() == 1 ? std::optional<std::size_t>(offsets.front()) : std::nullopt;
	}
};

/// Find each place in `memory` where the signature matches.
[[nodiscard]] SignatureMatches FindSignature(std::span<const std::byte> memory, std::span<const SignatureByte> signature);

/// Find the one place where the signature matches. The extender refuses a signature that matches no place or more
/// than one place: the result is then an error that names the signature (`name`) and tells the count.
[[nodiscard]] ts4::Result<std::size_t> FindUniqueSignature(std::span<const std::byte> memory,
                                                           std::span<const SignatureByte> signature, std::string_view name);

} // namespace ts4_mod
