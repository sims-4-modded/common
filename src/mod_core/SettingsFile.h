/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <common/Result.h>
#include <nlohmann/json.hpp>

namespace ts4_mod
{

/// The size and the time of the last change of a file. A different stamp tells that the file changed.
struct FileStamp
{
	bool exists = false;
	std::uintmax_t size = 0;
	std::filesystem::file_time_type time {};

	friend bool operator==(const FileStamp&, const FileStamp&) = default;
};

/// Read the stamp of a file. A file that does not exist, or that the process cannot read, gives `exists` false.
[[nodiscard]] FileStamp ReadFileStamp(const std::filesystem::path& path);

/// Read all the bytes of a file. Return nothing if the file does not exist or cannot be read.
[[nodiscard]] std::optional<std::string> ReadTextFile(const std::filesystem::path& path);

/// Apply a JSON merge patch to the text of a settings file, and return the new text. The fields that the patch does
/// not name stay, also the fields that the mod does not know. An empty text is an empty object. Return an error if
/// the text is not a JSON object. The caller checks the new values before it makes the patch.
[[nodiscard]] ts4::Result<std::string> PatchSettingsText(std::string_view original, const nlohmann::json& patch);

/// Write a file so that a reader sees the old text or the new text, never a part: write a temporary file in the same
/// folder, then rename it over the file.
[[nodiscard]] ts4::Result<void> WriteTextFileAtomically(const std::filesystem::path& path, std::string_view text);

/// Find the changes of one file. The extender calls `Changed` approximately one time in each second on its own
/// thread.
class FileChangeMonitor
{
public:
	explicit FileChangeMonitor(std::filesystem::path path)
	    : _path(std::move(path))
	    , _stamp(ReadFileStamp(_path))
	{
	}

	/// Return true if the stamp of the file is different from the stamp at the last call (or at the start).
	[[nodiscard]] bool Changed()
	{
		auto stamp = ReadFileStamp(_path);
		if (stamp == _stamp)
		{
			return false;
		}
		_stamp = stamp;
		return true;
	}

	[[nodiscard]] const std::filesystem::path& Path() const { return _path; }

private:
	std::filesystem::path _path;
	FileStamp _stamp;
};

} // namespace ts4_mod
