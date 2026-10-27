/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "mod_game/CrashDump.h"

#include <atomic>
#include <chrono>
#include <format>
#include <string>
#include <system_error>

#include <mod_core/CrashDumpRules.h>
#include <mod_core/Folders.h>
#include <spdlog/spdlog.h>
#include <windows.h>

// dbghelp.h needs the types of windows.h first.
#include <dbghelp.h>

#include "mod_game/Guard.h"
#include "mod_game/WideText.h"

namespace ts4_mod
{

namespace
{
using WriteDumpFunction = decltype(&MiniDumpWriteDump);

/// The longest time that the thread of the exception waits for the dump.
constexpr DWORD k_DumpTimeoutMs = 60'000;
/// The most dumps that one process writes.
constexpr int k_MaxDumps = 3;

/// The state that the handler and the worker share. The handler cannot allocate, so all of it is static. The process
/// keeps the events and the worker thread until it ends.
struct DumpState
{
	std::atomic<WriteDumpFunction> writeDump = nullptr;
	/// The path of the dumps, with no extension.
	std::filesystem::path path;
	HANDLE request = nullptr;
	HANDLE done = nullptr;
	/// One request at a time: a thread that finds the writer busy writes no dump.
	std::atomic<bool> busy = false;
	std::atomic<int> count = 0;
	DWORD threadId = 0;
	EXCEPTION_POINTERS* pointers = nullptr;
	CrashDumpReason reason = CrashDumpReason::None;
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): the exception handler needs this state.
DumpState s_dump;

[[nodiscard]] const char* ReasonName(CrashDumpReason reason)
{
	switch (reason)
	{
	case CrashDumpReason::StackOverflow:
		return "a stack overflow";
	case CrashDumpReason::StackExhausted:
		return "an access violation on a full stack";
	case CrashDumpReason::CaughtAccessViolation:
		return "a fault that a guard of the extender caught";
	case CrashDumpReason::None:
		break;
	}
	return "no reason";
}

/// The module and the position in the module of an address, for the log.
[[nodiscard]] std::string DescribeAddress(const void* address)
{
	HMODULE module = nullptr;
	constexpr DWORD k_Flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
	if (address == nullptr || GetModuleHandleExW(k_Flags, static_cast<LPCWSTR>(address), &module) == FALSE)
	{
		return std::format("{} (no module)", address);
	}
	std::wstring name(MAX_PATH, L'\0');
	name.resize(GetModuleFileNameW(module, name.data(), static_cast<DWORD>(name.size())));
	const auto position = static_cast<const std::uint8_t*>(address) - reinterpret_cast<const std::uint8_t*>(module);
	return std::format("{}+0x{:X}", ToUtf8(std::filesystem::path(name).filename().wstring()), position);
}

void WriteOneDump(int number)
{
	auto path = s_dump.path;
	path += number == 1 ? L".dmp" : std::format(L"-{}.dmp", number);
	auto* const file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	BOOL written = FALSE;
	if (file != INVALID_HANDLE_VALUE)
	{
		MINIDUMP_EXCEPTION_INFORMATION exception {
		    .ThreadId = s_dump.threadId, .ExceptionPointers = s_dump.pointers, .ClientPointers = FALSE};
		constexpr auto k_Type =
		    static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules |
		                               MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithProcessThreadData);
		written =
		    s_dump.writeDump.load()(GetCurrentProcess(), GetCurrentProcessId(), file, k_Type, &exception, nullptr, nullptr);
		CloseHandle(file);
	}
	const auto error = GetLastError();
	try
	{
		const auto* record = s_dump.pointers->ExceptionRecord;
		const auto* dataAddress =
		    record->NumberParameters >= 2 ? reinterpret_cast<const void*>(record->ExceptionInformation[1]) : nullptr;
		spdlog::critical("crash dump: thread {} had {} (exception 0x{:08X} at {}, data address {}); {}", s_dump.threadId,
		                 ReasonName(s_dump.reason), static_cast<std::uint32_t>(record->ExceptionCode),
		                 DescribeAddress(record->ExceptionAddress), dataAddress,
		                 written != FALSE ? std::format("the dump is in {}", ToUtf8(path.wstring()))
		                                  : std::format("the dump failed with error {}", error));
		spdlog::default_logger()->flush();
	}
	catch (...)
	{
		// The log failed. The dump is there all the same.
	}
}

/// The worker thread. It waits for a request, writes the dump and tells the thread of the exception.
DWORD WINAPI DumpWorker(void* /*parameter*/)
{
	for (int number = 1;; ++number)
	{
		WaitForSingleObject(s_dump.request, INFINITE);
		WriteOneDump(number);
		SetEvent(s_dump.done);
	}
}

/// Ask the worker for a dump, and wait until the worker writes it. The function needs little stack: it does no
/// allocation and no log.
void RequestDump(EXCEPTION_POINTERS* pointers, CrashDumpReason reason)
{
	if (s_dump.writeDump.load() == nullptr || s_dump.count.load() >= k_MaxDumps || s_dump.busy.exchange(true))
	{
		return;
	}
	++s_dump.count;
	s_dump.threadId = GetCurrentThreadId();
	s_dump.pointers = pointers;
	s_dump.reason = reason;
	SetEvent(s_dump.request);
	WaitForSingleObject(s_dump.done, k_DumpTimeoutMs);
	s_dump.busy = false;
}

/// The vectored exception handler. It runs on the thread of the exception, which can have almost no stack.
LONG CALLBACK DumpHandler(EXCEPTION_POINTERS* pointers)
{
	const auto* record = pointers->ExceptionRecord;
	ULONG_PTR low = 0;
	ULONG_PTR high = 0;
	GetCurrentThreadStackLimits(&low, &high);
	const std::uintptr_t dataAddress = record->NumberParameters >= 2 ? record->ExceptionInformation[1] : 0;
	const auto reason = DecideCrashDump(record->ExceptionCode, dataAddress, AddressRange {.low = low, .high = high});
	if (reason != CrashDumpReason::None)
	{
		RequestDump(pointers, reason);
	}
	return EXCEPTION_CONTINUE_SEARCH;
}
} // namespace

int CatchCallbackFaultWithDump(_EXCEPTION_POINTERS* pointers)
{
	if (CatchCallbackFault(pointers) != EXCEPTION_EXECUTE_HANDLER)
	{
		return EXCEPTION_CONTINUE_SEARCH;
	}
	RequestDump(pointers, CrashDumpReason::CaughtAccessViolation);
	return EXCEPTION_EXECUTE_HANDLER;
}

bool CrashDumpWriterInstalled()
{
	return s_dump.writeDump.load() != nullptr;
}

ts4::Result<std::filesystem::path> InstallCrashDumpWriter(const std::filesystem::path& folder,
                                                          std::span<const std::filesystem::path> gameFolders,
                                                          std::string_view prefix)
{
	if (CrashDumpWriterInstalled())
	{
		return ts4::MakeError("the crash dump writer is already installed");
	}
	if (folder.empty() || !folder.is_absolute())
	{
		return ts4::MakeError("the folder of the crash dumps is not an absolute path");
	}
	if (IsInsideAnyFolder(folder, gameFolders))
	{
		return ts4::MakeError("the folder {} of the crash dumps is in the game folder", ToUtf8(folder.wstring()));
	}
	// dbghelp.dll comes from the system folder only. The game folder gets no DLL of the extender.
	auto* const dbghelp = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (dbghelp == nullptr)
	{
		return ts4::MakeError("the system has no dbghelp.dll (error {})", GetLastError());
	}
	const auto writeDump = reinterpret_cast<WriteDumpFunction>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
	if (writeDump == nullptr)
	{
		return ts4::MakeError("dbghelp.dll has no MiniDumpWriteDump");
	}
	std::error_code error;
	std::filesystem::create_directories(folder, error);
	if (error)
	{
		return ts4::MakeError("the folder {} of the crash dumps cannot be made: {}", ToUtf8(folder.wstring()), error.message());
	}
	const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
	s_dump.path = folder / ToWide(std::format("{}-{:%Y%m%d-%H%M%S}-{}", prefix, now, GetCurrentProcessId()));
	s_dump.request = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	s_dump.done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (s_dump.request == nullptr || s_dump.done == nullptr)
	{
		return ts4::MakeError("CreateEventW failed with error {}", GetLastError());
	}
	auto* const worker = CreateThread(nullptr, 0, DumpWorker, nullptr, 0, nullptr);
	if (worker == nullptr)
	{
		return ts4::MakeError("the worker thread of the crash dumps did not start (error {})", GetLastError());
	}
	CloseHandle(worker);
	s_dump.writeDump = writeDump;
	// The handler is the first in the list, so that it sees the exception before a handler of the game stops the
	// process.
	if (AddVectoredExceptionHandler(1, DumpHandler) == nullptr)
	{
		s_dump.writeDump = nullptr;
		return ts4::MakeError("AddVectoredExceptionHandler failed");
	}
	spdlog::info("crash dump: a full stack in a thread of the game writes a dump to {}.dmp", ToUtf8(s_dump.path.wstring()));
	return s_dump.path;
}

} // namespace ts4_mod
