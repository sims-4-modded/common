/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <mod_core/StartupGuard.h>

using namespace ts4_mod;

namespace
{

const std::vector<std::string> k_Overlay {std::string(k_FeatureOverlay)};

StartupMarker MakeMarker(std::vector<std::string> features)
{
	return StartupMarker {.processId = 66300,
	                      .version = "0.2.0",
	                      .gameVersion = "1.128.90.1030",
	                      .startedAt = "2026-10-25 19:34:00",
	                      .stage = "menu bar",
	                      .features = std::move(features)};
}

} // namespace

TEST(StartupMarker, TheTextGivesTheSameMarker)
{
	const auto marker = MakeMarker(k_Overlay);
	const auto parsed = ParseStartupMarker(FormatStartupMarker(marker));
	ASSERT_TRUE(parsed) << parsed.GetError().message;
	EXPECT_EQ(*parsed, marker);
}

TEST(StartupMarker, ABadTextGivesAnError)
{
	EXPECT_FALSE(ParseStartupMarker(""));
	EXPECT_FALSE(ParseStartupMarker("{\"processId\": "));
	EXPECT_FALSE(ParseStartupMarker("[1, 2]"));
	EXPECT_FALSE(ParseStartupMarker("{\"features\": \"overlay\"}"));
}

TEST(StartupMarker, MissingFieldsAreEmpty)
{
	const auto parsed = ParseStartupMarker("{}");
	ASSERT_TRUE(parsed);
	EXPECT_EQ(*parsed, StartupMarker {});
}

TEST(StartupDecision, NoMarkerUsesEachRequestedFeature)
{
	const auto decision = DecideStartup(std::nullopt, k_Overlay);
	EXPECT_FALSE(decision.lastStartFailed);
	EXPECT_EQ(decision.featuresOn, k_Overlay);
	EXPECT_TRUE(decision.featuresOff.empty());
}

TEST(StartupDecision, AFailedStartWithTheMenuBarKeepsTheMenuBarOff)
{
	const auto decision = DecideStartup(FormatStartupMarker(MakeMarker(k_Overlay)), k_Overlay);
	EXPECT_TRUE(decision.lastStartFailed);
	ASSERT_TRUE(decision.lastStart);
	EXPECT_EQ(decision.lastStart.value_or(StartupMarker {}).stage, "menu bar");
	EXPECT_EQ(decision.featuresOff, k_Overlay);
	EXPECT_TRUE(decision.featuresOn.empty());
}

TEST(StartupDecision, AFailedStartWithoutTheMenuBarDoesNotBlameIt)
{
	const auto decision = DecideStartup(FormatStartupMarker(MakeMarker({})), k_Overlay);
	EXPECT_TRUE(decision.lastStartFailed);
	EXPECT_EQ(decision.featuresOn, k_Overlay);
	EXPECT_TRUE(decision.featuresOff.empty());
}

TEST(StartupDecision, AMarkerThatIsNotCorrectKeepsEachFeatureOff)
{
	const auto decision = DecideStartup(std::string("not json"), k_Overlay);
	EXPECT_TRUE(decision.lastStartFailed);
	EXPECT_FALSE(decision.lastStart);
	EXPECT_FALSE(decision.markerError.empty());
	EXPECT_EQ(decision.featuresOff, k_Overlay);
}

TEST(StartupDecision, AnEmptyMarkerFileIsAFailedStart)
{
	const auto decision = DecideStartup(std::string(), k_Overlay);
	EXPECT_TRUE(decision.lastStartFailed);
	EXPECT_EQ(decision.featuresOff, k_Overlay);
}

TEST(StartupDecision, NoRequestedFeatureGivesNoFeature)
{
	const auto decision = DecideStartup(FormatStartupMarker(MakeMarker(k_Overlay)), {});
	EXPECT_TRUE(decision.lastStartFailed);
	EXPECT_TRUE(decision.featuresOn.empty());
	EXPECT_TRUE(decision.featuresOff.empty());
}

TEST(StartupSequence, TheStepsOccurOneTimeAtTheirFrames)
{
	StartupSequence sequence(StartupPlan {.lateStartFrame = 3, .stableFrames = 4});
	std::vector<StartupStep> steps;
	steps.reserve(20);
	for (int frame = 0; frame < 20; ++frame)
	{
		steps.push_back(sequence.OnFrame());
	}
	const auto late = std::ranges::find(steps, StartupStep::LateStart);
	const auto stable = std::ranges::find(steps, StartupStep::Stable);
	ASSERT_NE(late, steps.end());
	ASSERT_NE(stable, steps.end());
	EXPECT_EQ(late - steps.begin(), 2);   // The third frame.
	EXPECT_EQ(stable - steps.begin(), 6); // Four frames after the late start.
	EXPECT_EQ(std::ranges::count(steps, StartupStep::LateStart), 1);
	EXPECT_EQ(std::ranges::count(steps, StartupStep::Stable), 1);
	EXPECT_TRUE(sequence.LateStarted());
	EXPECT_TRUE(sequence.Stable());
	EXPECT_EQ(sequence.Frames(), 20u);
}

TEST(StartupSequence, ZeroFramesStillPutTheStepsInDifferentFrames)
{
	StartupSequence sequence(StartupPlan {.lateStartFrame = 0, .stableFrames = 0});
	EXPECT_EQ(sequence.OnFrame(), StartupStep::LateStart);
	EXPECT_FALSE(sequence.Stable());
	EXPECT_EQ(sequence.OnFrame(), StartupStep::Stable);
	EXPECT_EQ(sequence.OnFrame(), StartupStep::None);
}

TEST(StartupSequence, NothingOccursBeforeTheLateStartFrame)
{
	StartupSequence sequence(StartupPlan {.lateStartFrame = 120, .stableFrames = 600});
	for (int frame = 1; frame < 120; ++frame)
	{
		ASSERT_EQ(sequence.OnFrame(), StartupStep::None) << frame;
	}
	EXPECT_FALSE(sequence.LateStarted());
	EXPECT_EQ(sequence.OnFrame(), StartupStep::LateStart);
}
