/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "mod_game/Guard.h"

#include <exception>
#include <format>
#include <utility>

#include <windows.h>

namespace ts4_mod
{

namespace
{
/// The values that the C++ part of a guarded call gives to the caller.
struct GuardContext
{
	void (*function)(void*) = nullptr;
	void* context = nullptr;
	std::string error;
	bool threw = false;
};

/// Call the function and catch its C++ exceptions. A function with `__try` cannot have objects with destructors, so
/// this part is a different function.
void CallAndCatch(GuardContext& guard)
{
	try
	{
		guard.function(guard.context);
	}
	catch (const std::exception& exception)
	{
		guard.threw = true;
		guard.error = exception.what();
	}
	catch (...)
	{
		guard.threw = true;
		guard.error = "an exception that is not a std::exception";
	}
}

/// Keep the exception code, then ask the filter of the caller.
int FilterAndKeepCode(EXCEPTION_POINTERS* pointers, ExceptionFilter filter, DWORD& code)
{
	const auto decision = filter != nullptr ? filter(pointers) : EXCEPTION_CONTINUE_SEARCH;
	if (decision == EXCEPTION_EXECUTE_HANDLER && pointers != nullptr && pointers->ExceptionRecord != nullptr)
	{
		code = pointers->ExceptionRecord->ExceptionCode;
	}
	return decision;
}

/// Run the call in a structured exception handler. Return the code of the exception that the filter caught, or 0.
DWORD CallWithHandler(GuardContext& guard, ExceptionFilter filter)
{
	DWORD code = 0;
	__try
	{
		CallAndCatch(guard);
	}
	__except (FilterAndKeepCode(GetExceptionInformation(), filter, code))
	{
		if (code == 0)
		{
			code = EXCEPTION_NONCONTINUABLE_EXCEPTION;
		}
	}
	return code;
}

[[nodiscard]] const char* ExceptionName(std::uint32_t code)
{
	switch (code)
	{
	case EXCEPTION_ACCESS_VIOLATION:
		return "an access violation";
	case EXCEPTION_IN_PAGE_ERROR:
		return "a page that the system cannot read";
	case EXCEPTION_ILLEGAL_INSTRUCTION:
	case EXCEPTION_PRIV_INSTRUCTION:
		return "a bad instruction";
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
	case EXCEPTION_INT_OVERFLOW:
		return "a bad integer division";
	case EXCEPTION_DATATYPE_MISALIGNMENT:
		return "a misaligned access";
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
		return "an index out of the bounds";
	default:
		break;
	}
	return "a Windows exception";
}
} // namespace

std::string GuardResult::Describe() const
{
	if (exceptionCode != 0)
	{
		return std::format("{} (exception 0x{:08X})", ExceptionName(exceptionCode), exceptionCode);
	}
	if (!error.empty())
	{
		return std::format("a C++ exception: {}", error);
	}
	return "no error";
}

GuardResult GuardedCall(void (*function)(void*), void* context, ExceptionFilter filter)
{
	GuardContext guard {.function = function, .context = context, .error = {}, .threw = false};
	if (function == nullptr)
	{
		return GuardResult {.completed = false, .exceptionCode = 0, .error = "no function"};
	}
	const auto code = CallWithHandler(guard, filter);
	return GuardResult {
	    .completed = code == 0 && !guard.threw,
	    .exceptionCode = static_cast<std::uint32_t>(code),
	    .error = std::move(guard.error),
	};
}

int CatchCallbackFault(_EXCEPTION_POINTERS* pointers)
{
	if (pointers == nullptr || pointers->ExceptionRecord == nullptr)
	{
		return EXCEPTION_CONTINUE_SEARCH;
	}
	switch (pointers->ExceptionRecord->ExceptionCode)
	{
	case EXCEPTION_ACCESS_VIOLATION:
	case EXCEPTION_IN_PAGE_ERROR:
	case EXCEPTION_ILLEGAL_INSTRUCTION:
	case EXCEPTION_PRIV_INSTRUCTION:
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
	case EXCEPTION_INT_OVERFLOW:
	case EXCEPTION_DATATYPE_MISALIGNMENT:
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
		return EXCEPTION_EXECUTE_HANDLER;
	default:
		return EXCEPTION_CONTINUE_SEARCH;
	}
}

} // namespace ts4_mod
