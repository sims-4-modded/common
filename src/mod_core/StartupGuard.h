/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <common/Result.h>

namespace ts4_mod
{

/// The name of the menu bar in the list of the features that can stop the game at the start.
inline constexpr std::string_view k_FeatureOverlay = "overlay";

/// The text of the marker file. The extender writes the file when the game starts, and removes it when the game runs
/// stable. A file that is there at the next start tells that the last start did not complete.
struct StartupMarker
{
	uint32_t processId = 0;
	/// The version of the extender.
	std::string version;
	/// The version of the game, for example "1.128.90.1030".
	std::string gameVersion;
	std::string startedAt;
	/// The last step that the start began. The log of the next start tells it.
	std::string stage;
	/// The features that the start used.
	std::vector<std::string> features;

	friend bool operator==(const StartupMarker&, const StartupMarker&) = default;
};

[[nodiscard]] std::string FormatStartupMarker(const StartupMarker& marker);
/// Read the text of a marker file. Return an error if the text is not a correct marker.
[[nodiscard]] ts4::Result<StartupMarker> ParseStartupMarker(std::string_view text);

/// The features that one start of the game uses.
struct StartupDecision
{
	/// The marker file was there: the last start did not become stable. The game stopped (a crash), or the user
	/// stopped it before it became stable.
	bool lastStartFailed = false;
	/// The marker of the last start, if the text was correct.
	std::optional<StartupMarker> lastStart;
	/// The error of the marker text, if the text was not correct.
	std::string markerError;
	/// The features that the settings turn on and that this start can use.
	std::vector<std::string> featuresOn;
	/// The features that the settings turn on and that this start keeps off.
	std::vector<std::string> featuresOff;
};

/// Decide which of the `requested` features this start uses. `markerText` is the text of the marker file, or
/// nothing if there is no file. After a start that did not complete, each feature that the last start used stays off.
/// If the marker text is not correct, each requested feature stays off.
[[nodiscard]] StartupDecision DecideStartup(const std::optional<std::string>& markerText,
                                            std::span<const std::string> requested);

/// The frames of the main thread at which the late steps of the start occur.
struct StartupPlan
{
	/// The frame of the late start. The menu bar and the monitor of the settings file start in this frame.
	uint64_t lateStartFrame = 120;
	/// The frames after the late start after which the start is stable. The marker file then goes.
	uint64_t stableFrames = 600;
};

/// The step that the main thread does in a frame.
enum class StartupStep
{
	None,
	LateStart,
	Stable,
};

/// Counts the frames of the main thread and tells when each late step of the start occurs. Each step occurs one
/// time, and `Stable` always occurs in a frame after `LateStart`.
class StartupSequence
{
public:
	explicit StartupSequence(StartupPlan plan = {})
	    : _plan(plan)
	{
	}

	/// Count one frame. Return the step to do in this frame.
	[[nodiscard]] StartupStep OnFrame();

	[[nodiscard]] uint64_t Frames() const { return _frames; }
	[[nodiscard]] bool LateStarted() const { return _lateFrame.has_value(); }
	[[nodiscard]] bool Stable() const { return _stable; }

private:
	StartupPlan _plan;
	uint64_t _frames = 0;
	std::optional<uint64_t> _lateFrame;
	bool _stable = false;
};

} // namespace ts4_mod
