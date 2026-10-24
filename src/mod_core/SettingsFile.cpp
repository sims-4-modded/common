/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <fstream>
#include <iterator>
#include <system_error>

#include <mod_core/SettingsFile.h>

namespace ts4_mod
{

FileStamp ReadFileStamp(const std::filesystem::path& path)
{
	std::error_code error;
	FileStamp stamp;
	stamp.size = std::filesystem::file_size(path, error);
	if (error)
	{
		return {};
	}
	stamp.time = std::filesystem::last_write_time(path, error);
	if (error)
	{
		return {};
	}
	stamp.exists = true;
	return stamp;
}

std::optional<std::string> ReadTextFile(const std::filesystem::path& path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
	{
		return std::nullopt;
	}
	std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	if (file.bad())
	{
		return std::nullopt;
	}
	return text;
}

ts4::Result<std::string> PatchSettingsText(std::string_view original, const nlohmann::json& patch)
{
	auto root = original.find_first_not_of(" \t\r\n") == std::string_view::npos
	                ? nlohmann::json::object()
	                : nlohmann::json::parse(original, nullptr, false);
	if (root.is_discarded() || !root.is_object())
	{
		return ts4::MakeError("the settings file is not a JSON object; the mod does not change it");
	}
	root.merge_patch(patch);
	return root.dump(2) + "\n";
}

ts4::Result<void> WriteTextFileAtomically(const std::filesystem::path& path, std::string_view text)
{
	auto temporary = path;
	temporary += ".tmp";
	{
		std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
		if (!file)
		{
			return ts4::MakeError("the mod cannot write {}", temporary.string());
		}
		file.write(text.data(), static_cast<std::streamsize>(text.size()));
		file.flush();
		if (!file)
		{
			std::error_code ignored;
			std::filesystem::remove(temporary, ignored);
			return ts4::MakeError("the mod cannot write all of {}", temporary.string());
		}
	}
	std::error_code error;
	std::filesystem::rename(temporary, path, error);
	if (error)
	{
		std::error_code ignored;
		std::filesystem::remove(temporary, ignored);
		return ts4::MakeError("the mod cannot replace {}: {}", path.string(), error.message());
	}
	return {};
}

} // namespace ts4_mod
