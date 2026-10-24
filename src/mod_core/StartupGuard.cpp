/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <algorithm>
#include <exception>

#include <mod_core/StartupGuard.h>
#include <nlohmann/json.hpp>

namespace ts4_mod
{

std::string FormatStartupMarker(const StartupMarker& marker)
{
	const nlohmann::json json {
	    {"processId", marker.processId}, {"version", marker.version}, {"gameVersion", marker.gameVersion},
	    {"startedAt", marker.startedAt}, {"stage", marker.stage},     {"features", marker.features},
	};
	return json.dump(2) + "\n";
}

ts4::Result<StartupMarker> ParseStartupMarker(std::string_view text)
{
	const auto json = nlohmann::json::parse(text, nullptr, false);
	if (json.is_discarded() || !json.is_object())
	{
		return ts4::MakeError("the marker is not a JSON object");
	}
	try
	{
		StartupMarker marker;
		marker.processId = json.value("processId", uint32_t {0});
		marker.version = json.value("version", std::string());
		marker.gameVersion = json.value("gameVersion", std::string());
		marker.startedAt = json.value("startedAt", std::string());
		marker.stage = json.value("stage", std::string());
		marker.features = json.value("features", std::vector<std::string>());
		return marker;
	}
	catch (const std::exception& error)
	{
		return ts4::MakeError("the marker has a field of a wrong type: {}", error.what());
	}
}

StartupDecision DecideStartup(const std::optional<std::string>& markerText, std::span<const std::string> requested)
{
	StartupDecision decision;
	if (!markerText)
	{
		decision.featuresOn.assign(requested.begin(), requested.end());
		return decision;
	}
	decision.lastStartFailed = true;
	auto marker = ParseStartupMarker(*markerText);
	if (marker)
	{
		decision.lastStart = std::move(*marker);
	}
	else
	{
		decision.markerError = marker.GetError().message;
	}
	for (const auto& feature : requested)
	{
		// A marker that the extender cannot read does not tell which features ran, so each feature stays off.
		const bool used = !decision.lastStart ||
		                  std::ranges::find(decision.lastStart->features, feature) != decision.lastStart->features.end();
		(used ? decision.featuresOff : decision.featuresOn).push_back(feature);
	}
	return decision;
}

StartupStep StartupSequence::OnFrame()
{
	++_frames;
	if (!_lateFrame)
	{
		if (_frames >= std::max<uint64_t>(_plan.lateStartFrame, 1))
		{
			_lateFrame = _frames;
			return StartupStep::LateStart;
		}
		return StartupStep::None;
	}
	if (!_stable && _frames >= *_lateFrame + std::max<uint64_t>(_plan.stableFrames, 1))
	{
		_stable = true;
		return StartupStep::Stable;
	}
	return StartupStep::None;
}

} // namespace ts4_mod
