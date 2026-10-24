/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <string>

struct _EXCEPTION_POINTERS;

namespace ts4_mod
{

/// The filter of a guard around a call into a mod: return EXCEPTION_EXECUTE_HANDLER (1) to catch the exception, or
/// EXCEPTION_CONTINUE_SEARCH (0) to give it to the next handler.
using ExceptionFilter = int (*)(_EXCEPTION_POINTERS* pointers);

/// The result of a guarded call.
struct GuardResult
{
	/// True if the function returned with no exception.
	bool completed = true;
	/// The code of the Windows exception that the filter caught, or 0.
	std::uint32_t exceptionCode = 0;
	/// The text of a C++ exception that the guard caught, or empty.
	std::string error;

	/// A text for the log, for example "an access violation (exception 0xC0000005)".
	[[nodiscard]] std::string Describe() const;
};

/// Call `function(context)`. Catch each C++ exception, and each Windows exception that `filter` catches (for example an
/// access violation in a mod). After a Windows exception, the stack unwinds with no destructors. Thus the caller must
/// stop all calls to the code that failed, because its state can be bad.
[[nodiscard]] GuardResult GuardedCall(void (*function)(void*), void* context, ExceptionFilter filter);

/// The default filter of `GuardedCall`. It catches the exceptions that bad code in a callback causes: an access
/// violation, a bad instruction, a division by zero and a misaligned access. It does not catch a stack overflow,
/// because the thread then has no guard page. It also does not catch a breakpoint, so that a debugger can stop there.
[[nodiscard]] int CatchCallbackFault(_EXCEPTION_POINTERS* pointers);

} // namespace ts4_mod
