/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>

#include <gtest/gtest.h>
#include <mod_core/FrameCallbacks.h>
#include <mod_core/MainThreadTasks.h>
#include <mod_core/SlotSwap.h>
#include <nlohmann/json.hpp>

using namespace ts4_mod;

namespace
{

/// A stand-in for one entry of a method table of a native module: the code pointer is the first field.
struct FakeMethod
{
	void* code;
	const char* name;
};

int s_calls = 0;

void OriginalCode()
{
	s_calls += 1;
}

void ReplacementCode()
{
	s_calls += 100;
}

void* Address(void (*function)())
{
	return reinterpret_cast<void*>(function);
}

void CallThrough(const FakeMethod& method)
{
	reinterpret_cast<void (*)()>(method.code)();
}

} // namespace

TEST(SlotSwap, PutsTheReplacementInAndTheOriginalBack)
{
	FakeMethod method {.code = Address(&OriginalCode), .name = "tick"};
	s_calls = 0;
	{
		auto swap = SlotSwap::Install(&method.code, Address(&OriginalCode), Address(&ReplacementCode));
		ASSERT_TRUE(swap.Installed());
		EXPECT_EQ(swap.Original(), Address(&OriginalCode));
		CallThrough(method);
		EXPECT_EQ(s_calls, 100);
	}
	EXPECT_EQ(method.code, Address(&OriginalCode));
	CallThrough(method);
	EXPECT_EQ(s_calls, 101);
}

TEST(SlotSwap, DoesNothingIfTheSlotHoldsADifferentPointer)
{
	FakeMethod method {.code = Address(&ReplacementCode), .name = "tick"};
	const auto swap = SlotSwap::Install(&method.code, Address(&OriginalCode), Address(&ReplacementCode));
	EXPECT_FALSE(swap.Installed());
	EXPECT_EQ(method.code, Address(&ReplacementCode));
	EXPECT_FALSE(SlotSwap::Install(nullptr, Address(&OriginalCode), Address(&ReplacementCode)).Installed());
}

TEST(SlotSwap, KeepsAPointerThatAnotherProgramPutInAfterTheInstall)
{
	FakeMethod method {.code = Address(&OriginalCode), .name = "tick"};
	auto swap = SlotSwap::Install(&method.code, Address(&OriginalCode), Address(&ReplacementCode));
	ASSERT_TRUE(swap.Installed());
	int other = 0;
	method.code = &other; // Another mod changed the slot.
	EXPECT_FALSE(swap.Restore());
	EXPECT_EQ(method.code, &other);
	EXPECT_FALSE(swap.Installed());
	EXPECT_TRUE(swap.Restore()); // A second restore does nothing.
}

TEST(SlotSwap, MovesTheOwnership)
{
	FakeMethod method {.code = Address(&OriginalCode), .name = "tick"};
	SlotSwap kept;
	{
		auto swap = SlotSwap::Install(&method.code, Address(&OriginalCode), Address(&ReplacementCode));
		kept = std::move(swap);
	}
	EXPECT_EQ(method.code, Address(&ReplacementCode));
	kept.Restore();
	EXPECT_EQ(method.code, Address(&OriginalCode));
}

TEST(FrameCallbacks, CallsEachFunctionOneTimeInEachFrame)
{
	FrameCallbacks callbacks;
	int first = 0;
	int second = 0;
	EXPECT_TRUE(callbacks.Add("first", [&] { ++first; }));
	EXPECT_TRUE(callbacks.Add("second", [&] { ++second; }));
	EXPECT_FALSE(callbacks.Add("first", [] {}));
	EXPECT_FALSE(callbacks.Add("empty", nullptr));
	EXPECT_TRUE(callbacks.Run());
	EXPECT_TRUE(callbacks.Run());
	EXPECT_EQ(first, 2);
	EXPECT_EQ(second, 2);
	EXPECT_EQ(callbacks.Frames(), 2u);

	EXPECT_TRUE(callbacks.SetEnabled("second", false));
	EXPECT_FALSE(callbacks.SetEnabled("missing", false));
	callbacks.Run();
	EXPECT_EQ(first, 3);
	EXPECT_EQ(second, 2);

	callbacks.Clear();
	callbacks.Run();
	EXPECT_EQ(first, 3);
	EXPECT_TRUE(callbacks.Statuses().empty());
}

TEST(FrameCallbacks, StopsAFunctionThatThrows)
{
	FrameCallbacks callbacks;
	int good = 0;
	int bad = 0;
	callbacks.Add("bad", [&] {
		++bad;
		throw std::runtime_error("the zone is missing");
	});
	callbacks.Add("good", [&] { ++good; });
	callbacks.Run();
	callbacks.Run();
	EXPECT_EQ(bad, 1);
	EXPECT_EQ(good, 2);
	const auto statuses = callbacks.Statuses();
	ASSERT_EQ(statuses.size(), 2u);
	EXPECT_FALSE(statuses[0].enabled);
	EXPECT_EQ(statuses[0].error, "the zone is missing");
	EXPECT_EQ(statuses[0].calls, 1u);

	callbacks.SetEnabled("bad", true);
	EXPECT_TRUE(callbacks.Statuses()[0].error.empty());
}

TEST(FrameCallbacks, RunsOnlyOnTheFirstThread)
{
	FrameCallbacks callbacks;
	int calls = 0;
	callbacks.Add("count", [&] { ++calls; });
	EXPECT_TRUE(callbacks.Run());
	bool otherResult = true;
	std::thread([&] { otherResult = callbacks.Run(); }).join();
	EXPECT_FALSE(otherResult);
	EXPECT_EQ(calls, 1);
}

// The menu bar publishes the state of the mods, which includes the statuses of the hooks, from a function of the
// list and from a task that the list runs. That must not lock the list a second time on the same thread.
TEST(FrameCallbacks, AFunctionCanReadTheStatusesOfItsOwnList)
{
	FrameCallbacks callbacks;
	MainThreadTasks tasks;
	size_t seen = 0;
	callbacks.Add("overlay", [&] { seen = callbacks.Statuses().size() + callbacks.Frames(); });
	callbacks.Add("tasks", [&] { tasks.RunPending(); });
	auto command = tasks.Post(MainThreadTasks::Task([&] { return nlohmann::json(callbacks.Statuses().size()); }));
	EXPECT_TRUE(callbacks.Run());
	ASSERT_EQ(command.wait_for(std::chrono::seconds(0)), std::future_status::ready);
	EXPECT_EQ(command.get(), 2);
	EXPECT_EQ(seen, 3u);
	EXPECT_TRUE(callbacks.Run());
	for (const auto& status : callbacks.Statuses())
	{
		EXPECT_TRUE(status.enabled) << status.name;
		EXPECT_TRUE(status.error.empty()) << status.name << ": " << status.error;
		EXPECT_EQ(status.calls, 2u) << status.name;
	}
}

TEST(FrameCallbacks, AFunctionCanChangeTheListWhileItRuns)
{
	FrameCallbacks callbacks;
	int late = 0;
	int stopped = 0;
	bool nested = true;
	callbacks.Add("first", [&] {
		nested = callbacks.Run();
		callbacks.SetEnabled("stopped", false);
		callbacks.Add("late", [&] { ++late; });
	});
	callbacks.Add("stopped", [&] { ++stopped; });
	EXPECT_TRUE(callbacks.Run());
	EXPECT_FALSE(nested);
	EXPECT_EQ(stopped, 0);
	// A function that a function adds runs from the next frame.
	EXPECT_EQ(late, 0);
	callbacks.Run();
	EXPECT_EQ(late, 1);
}

TEST(FrameCallbacks, ClearDuringRunStopsTheRestOfTheFrame)
{
	FrameCallbacks callbacks;
	int after = 0;
	callbacks.Add("clear", [&] { callbacks.Clear(); });
	callbacks.Add("after", [&] { ++after; });
	EXPECT_TRUE(callbacks.Run());
	EXPECT_EQ(after, 0);
	EXPECT_TRUE(callbacks.Statuses().empty());
}

TEST(MainThreadTasks, RunsATaskFromAnotherThreadAndGivesTheResult)
{
	MainThreadTasks tasks;
	std::future<nlohmann::json> result;
	std::thread([&] { result = tasks.Post(MainThreadTasks::Task([] { return nlohmann::json(42); })); }).join();
	EXPECT_EQ(tasks.Pending(), 1u);
	tasks.RunPending();
	EXPECT_EQ(tasks.Pending(), 0u);
	EXPECT_EQ(result.get(), 42);

	auto failed = tasks.Post(MainThreadTasks::Task([]() -> nlohmann::json { throw std::runtime_error("no zone"); }));
	tasks.RunPending();
	EXPECT_THROW(failed.get(), std::runtime_error);
}
