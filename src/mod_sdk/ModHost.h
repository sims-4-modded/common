/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <common/Result.h>
#include <mod_api/Ts4ModApi.h>
#include <mod_core/GameVersion.h>

namespace ts4_mod::sdk
{

/// The C++ side of the table of the extender in a DLL mod. It checks the table one time and copies the texts.
class ModHost
{
public:
	/// Check the table of the extender. The result is an error if the version or the size is wrong, or a text is
	/// missing.
	[[nodiscard]] static ts4::Result<std::unique_ptr<ModHost>> Create(const Ts4HostApi* api);

	/// Write a line to the log of the extender.
	void Log(Ts4LogLevel level, std::string_view text) const;

	template <typename... Args>
	void Info(std::format_string<Args...> format, Args&&... args) const
	{
		Log(TS4_LOG_INFO, std::format(format, std::forward<Args>(args)...));
	}

	template <typename... Args>
	void Error(std::format_string<Args...> format, Args&&... args) const
	{
		Log(TS4_LOG_ERROR, std::format(format, std::forward<Args>(args)...));
	}

	[[nodiscard]] const GameVersion& Version() const { return _version; }
	[[nodiscard]] const std::string& GameFolder() const { return _gameFolder; }
	[[nodiscard]] const std::string& UserFolder() const { return _userFolder; }
	[[nodiscard]] const std::string& SettingsFolder() const { return _settingsFolder; }
	[[nodiscard]] const std::string& LogFolder() const { return _logFolder; }

private:
	ModHost() = default;

	const Ts4HostApi* _api = nullptr;
	GameVersion _version;
	std::string _gameFolder;
	std::string _userFolder;
	std::string _settingsFolder;
	std::string _logFolder;
};

} // namespace ts4_mod::sdk
