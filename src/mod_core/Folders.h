/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <filesystem>
#include <span>
#include <string_view>

#include <common/Result.h>

namespace ts4_mod
{

/// The name of the settings folder of the mods in the local application data of the user (`%LOCALAPPDATA%`).
inline constexpr std::string_view k_SettingsFolderName = "Sims4Mods";
/// The name of the settings file of the mods in the settings folder.
inline constexpr std::string_view k_SettingsFileName = "mods.json";
/// The name of the folder of the log files in the settings folder.
inline constexpr std::string_view k_LogFolderName = "logs";
/// The name of the marker file of the start in the settings folder.
inline constexpr std::string_view k_StartupMarkerName = "startup-marker.json";

/// True if `path` is `folder` or a folder or file in it. The test ignores the case of letters and the form of the
/// separators, as Windows does.
[[nodiscard]] bool IsInsideFolder(const std::filesystem::path& path, const std::filesystem::path& folder);

/// True if `path` is in one of `folders`.
[[nodiscard]] bool IsInsideAnyFolder(const std::filesystem::path& path, std::span<const std::filesystem::path> folders);

/// The folders and the files that the extender and the mods write. None of them is in a game folder.
struct ModFolders
{
	std::filesystem::path settingsFolder; ///< `%LOCALAPPDATA%\Sims4Mods`.
	std::filesystem::path settingsFile;   ///< `mods.json` in the settings folder, or the path that the user gives.
	std::filesystem::path logFolder;      ///< `logs` in the settings folder, or the path that the user gives.
	std::filesystem::path startupMarker;  ///< The marker file of the start, next to `mods.json`.
};

/// The values that select the folders of the mods. The extender reads them from the environment.
struct ModFolderInputs
{
	/// The value of `%LOCALAPPDATA%`.
	std::filesystem::path localAppData;
	/// The path of the settings file that the user gives (`TS4_MODS_SETTINGS`), or empty for the default path.
	std::filesystem::path settingsFile;
	/// The folder of the log files that the user gives (`TS4_LOADER_LOG_FOLDER`), or empty for the default folder.
	std::filesystem::path logFolder;
};

/// Select the folders of the mods. Return an error if `localAppData` is empty or not absolute, or if a folder or a
/// file is in one of `gameFolders`. The extender never writes into the game folder.
[[nodiscard]] ts4::Result<ModFolders> MakeModFolders(const ModFolderInputs& inputs,
                                                     std::span<const std::filesystem::path> gameFolders);

} // namespace ts4_mod
