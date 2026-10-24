/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstdint>

namespace ts4_mod
{

/// The exception codes of Windows that the crash dump writer looks at.
inline constexpr uint32_t k_ExceptionAccessViolation = 0xC0000005;
inline constexpr uint32_t k_ExceptionStackOverflow = 0xC00000FD;

/// A range of addresses, from `low` (included) to `high` (not included).
struct AddressRange
{
	uintptr_t low = 0;
	uintptr_t high = 0;

	[[nodiscard]] constexpr bool Contains(uintptr_t address) const { return address >= low && address < high; }
};

/// Why an exception gets a crash dump.
enum class CrashDumpReason
{
	None,                  ///< No dump: the game or a library can handle the exception.
	StackOverflow,         ///< The thread used its last guard page of the stack.
	StackExhausted,        ///< An access violation on the stack of the thread: the stack has no guard page left.
	CaughtAccessViolation, ///< A guard of the mod caught an access violation. The guard asks for the dump itself.
};

/// Decide if an exception gets a crash dump. The game and its libraries handle some access violations themselves, so
/// only an exception that tells that the stack of the thread is full gets a dump. `dataAddress` is the address that
/// an access violation tried to read or write; `stack` is the full stack range of the thread.
[[nodiscard]] constexpr CrashDumpReason DecideCrashDump(uint32_t code, uintptr_t dataAddress, AddressRange stack)
{
	if (code == k_ExceptionStackOverflow)
	{
		return CrashDumpReason::StackOverflow;
	}
	if (code == k_ExceptionAccessViolation && stack.low < stack.high && stack.Contains(dataAddress))
	{
		return CrashDumpReason::StackExhausted;
	}
	return CrashDumpReason::None;
}

} // namespace ts4_mod
