/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <filesystem>
#include <span>
#include <string_view>

#include <common/Result.h>

struct _EXCEPTION_POINTERS;

namespace ts4_mod
{

/// Write a minidump when a thread of the game fills its stack (see `DecideCrashDump`), or when a guard of the
/// extender catches a fault (see `CatchCallbackFaultWithDump`). A vectored exception handler sees each exception
/// first. The thread of the exception can have almost no stack, so a worker thread that waits from the start writes
/// the dump. One process writes a maximum of three dumps. Then the exception continues to the next handler (the
/// crash handler of the game).
///
/// `folder` is the log folder of the extender. The function refuses a folder in one of `gameFolders`, because the
/// extender never writes into the game folder. The name of each dump starts with `prefix`, the date and the process.
///
/// Install the writer one time in the game process. It stays until the process ends: the exception handler needs
/// state outside an object, so this state is static. Return the path of the first dump, with no extension.
[[nodiscard]] ts4::Result<std::filesystem::path> InstallCrashDumpWriter(const std::filesystem::path& folder,
                                                                        std::span<const std::filesystem::path> gameFolders,
                                                                        std::string_view prefix = "ts4-extender");

/// True after a correct `InstallCrashDumpWriter`.
[[nodiscard]] bool CrashDumpWriterInstalled();

/// The filter of a guard of the extender (see `GuardedCall`). For each fault that `CatchCallbackFault` catches, write
/// a dump (if the extender installed the writer) and return `EXCEPTION_EXECUTE_HANDLER`. For any other exception,
/// return `EXCEPTION_CONTINUE_SEARCH`.
[[nodiscard]] int CatchCallbackFaultWithDump(_EXCEPTION_POINTERS* pointers);

} // namespace ts4_mod
