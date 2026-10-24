/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <mutex>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace ts4_mod
{

/// What one mod does now, for the query `mods.status` and the log. The main thread writes it and the inspector
/// threads read it.
class ModStatus
{
public:
	explicit ModStatus(std::string name)
	    : _name(std::move(name))
	{
	}

	/// The settings turned the mod on.
	void SetEnabled(bool enabled)
	{
		const std::scoped_lock lock(_mutex);
		_enabled = enabled;
	}

	/// The mod found all the signatures that it needs, and the extender installed its callbacks.
	void SetReady(bool ready)
	{
		const std::scoped_lock lock(_mutex);
		_ready = ready;
	}

	/// The mod changed the game in the last frame, or the reason that it did nothing.
	void SetActive(bool active, std::string reason = {})
	{
		const std::scoped_lock lock(_mutex);
		_active = active;
		_activeFrames += active ? 1 : 0;
		_reason = std::move(reason);
	}

	/// Keep an error. The log gets each new error text, up to a limit, so that an error in each frame does not fill
	/// the log.
	void SetError(std::string error)
	{
		const std::scoped_lock lock(_mutex);
		_errors += 1;
		if (error != _lastError && _loggedErrors < k_MaxLoggedErrors)
		{
			_loggedErrors += 1;
			spdlog::warn("{} mod: {}", _name, error);
		}
		_lastError = std::move(error);
	}

	[[nodiscard]] nlohmann::json Describe() const
	{
		const std::scoped_lock lock(_mutex);
		return {
		    {"name", _name},     {"enabled", _enabled},           {"ready", _ready},
		    {"active", _active}, {"activeFrames", _activeFrames}, {"reason", _reason},
		    {"errors", _errors}, {"lastError", _lastError},
		};
	}

private:
	static constexpr int k_MaxLoggedErrors = 20;

	mutable std::mutex _mutex;
	std::string _name;
	bool _enabled = false;
	bool _ready = false;
	bool _active = false;
	uint64_t _activeFrames = 0;
	std::string _reason;
	uint64_t _errors = 0;
	std::string _lastError;
	int _loggedErrors = 0;
};

} // namespace ts4_mod
