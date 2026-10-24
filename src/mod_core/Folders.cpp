/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <cwctype>

#include <algorithm>
#include <format>
#include <string>

#include <mod_core/Folders.h>

namespace ts4_mod
{

namespace
{
[[nodiscard]] wchar_t FoldCase(wchar_t c)
{
	return c == L'\\' ? L'/' : static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
}
} // namespace

bool IsInsideFolder(const std::filesystem::path& path, const std::filesystem::path& folder)
{
	auto normalised = [](const std::filesystem::path& value) {
		auto text = value.lexically_normal().wstring();
		std::ranges::transform(text, text.begin(), FoldCase);
		while (text.size() > 1 && text.back() == L'/')
		{
			text.pop_back();
		}
		return text;
	};
	const auto inner = normalised(path);
	const auto outer = normalised(folder);
	if (outer.empty() || !inner.starts_with(outer))
	{
		return false;
	}
	return inner.size() == outer.size() || inner[outer.size()] == L'/' || outer.back() == L'/';
}

bool IsInsideAnyFolder(const std::filesystem::path& path, std::span<const std::filesystem::path> folders)
{
	return std::ranges::any_of(folders, [&](const auto& folder) { return IsInsideFolder(path, folder); });
}

ts4::Result<ModFolders> MakeModFolders(const ModFolderInputs& inputs, std::span<const std::filesystem::path> gameFolders)
{
	if (inputs.localAppData.empty() || !inputs.localAppData.is_absolute())
	{
		return ts4::MakeError("the local application data folder '{}' is not an absolute path", inputs.localAppData.string());
	}
	ModFolders folders;
	folders.settingsFolder = inputs.localAppData / k_SettingsFolderName;
	folders.settingsFile = inputs.settingsFile.empty() ? folders.settingsFolder / k_SettingsFileName : inputs.settingsFile;
	folders.logFolder = inputs.logFolder.empty() ? folders.settingsFolder / k_LogFolderName : inputs.logFolder;
	folders.startupMarker = folders.settingsFile.parent_path() / k_StartupMarkerName;
	for (const auto* path : {&folders.settingsFolder, &folders.settingsFile, &folders.logFolder, &folders.startupMarker})
	{
		if (!path->is_absolute())
		{
			return ts4::MakeError("the path '{}' is not absolute", path->string());
		}
		if (IsInsideAnyFolder(*path, gameFolders))
		{
			return ts4::MakeError("the path '{}' is in the game folder; the extender never writes there", path->string());
		}
	}
	return folders;
}

} // namespace ts4_mod
