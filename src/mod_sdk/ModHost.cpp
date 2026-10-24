/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "ModHost.h"

namespace ts4_mod::sdk
{

ts4::Result<std::unique_ptr<ModHost>> ModHost::Create(const Ts4HostApi* api)
{
	if (api == nullptr)
	{
		return ts4::MakeError("the extender gave no table");
	}
	if (api->apiVersion != TS4_MOD_API_VERSION)
	{
		return ts4::MakeError("the extender has the interface version {}, the mod has {}", api->apiVersion,
		                      TS4_MOD_API_VERSION);
	}
	if (api->size < TS4_HOST_API_FIRST_SIZE)
	{
		return ts4::MakeError("the table of the extender has {} bytes, less than {}", api->size, TS4_HOST_API_FIRST_SIZE);
	}
	if (api->log == nullptr || api->postTask == nullptr || api->gameVersion == nullptr || api->gameFolder == nullptr ||
	    api->userFolder == nullptr || api->settingsFolder == nullptr || api->logFolder == nullptr)
	{
		return ts4::MakeError("the table of the extender has a missing function or folder");
	}
	auto version = ParseGameVersion(api->gameVersion);
	if (!version)
	{
		return version.GetError();
	}

	std::unique_ptr<ModHost> host(new ModHost());
	host->_api = api;
	host->_version = *version;
	host->_gameFolder = api->gameFolder;
	host->_userFolder = api->userFolder;
	host->_settingsFolder = api->settingsFolder;
	host->_logFolder = api->logFolder;
	return host;
}

void ModHost::Log(Ts4LogLevel level, std::string_view text) const
{
	const std::string line(text);
	_api->log(_api->context, level, line.c_str());
}

} // namespace ts4_mod::sdk
