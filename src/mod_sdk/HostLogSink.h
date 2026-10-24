/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <memory>
#include <mutex>
#include <string>

#include <mod_api/Ts4ModApi.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

namespace ts4_mod::sdk
{

/// A spdlog sink that gives each line to the log of the extender. A DLL mod has its own copy of spdlog, so the mod
/// makes this sink its default logger in `Ts4ModInit`. Then the code of the mod writes with `spdlog::info` and the
/// lines go into the one log of the extender.
class HostLogSink final: public spdlog::sinks::base_sink<std::mutex>
{
public:
	HostLogSink(const Ts4HostApi& api, std::string prefix)
	    : _log(api.log)
	    , _context(api.context)
	    , _prefix(std::move(prefix))
	{
	}

	/// Stop the sink. The mod calls it in `Ts4ModShutdown`, because the table of the extender is not valid after that.
	void Detach()
	{
		const std::scoped_lock lock(mutex_);
		_log = nullptr;
	}

protected:
	void sink_it_(const spdlog::details::log_msg& message) override
	{
		if (_log == nullptr)
		{
			return;
		}
		const auto line = _prefix + ": " + std::string(message.payload.data(), message.payload.size());
		_log(_context, Level(message.level), line.c_str());
	}

	void flush_() override {}

private:
	[[nodiscard]] static Ts4LogLevel Level(spdlog::level::level_enum level)
	{
		switch (level)
		{
		case spdlog::level::trace:
		case spdlog::level::debug:
			return TS4_LOG_DEBUG;
		case spdlog::level::info:
			return TS4_LOG_INFO;
		case spdlog::level::warn:
			return TS4_LOG_WARN;
		default:
			return TS4_LOG_ERROR;
		}
	}

	void (*_log)(void*, Ts4LogLevel, const char*);
	void* _context;
	std::string _prefix;
};

/// Make the default logger of this module write into the log of the extender. Return the sink, so that the mod can
/// stop it at shutdown.
inline std::shared_ptr<HostLogSink> UseHostLog(const Ts4HostApi& api, const std::string& prefix)
{
	auto sink = std::make_shared<HostLogSink>(api, prefix);
	auto logger = std::make_shared<spdlog::logger>(prefix, sink);
	logger->set_level(spdlog::level::debug);
	spdlog::set_default_logger(std::move(logger));
	return sink;
}

} // namespace ts4_mod::sdk
