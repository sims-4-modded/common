/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <cstdint>

#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <mod_game/Hooks.h>

using namespace ts4_mod;

namespace
{

using TestFunction = std::uintptr_t (*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);

/// The functions that the tests hook. Each one has a different body, so the linker cannot fold them into one.
__declspec(noinline) std::uintptr_t AddDigits(std::uintptr_t a, std::uintptr_t b, std::uintptr_t c, std::uintptr_t d)
{
	return a * 1000 + b * 100 + c * 10 + d;
}

__declspec(noinline) std::uintptr_t MultiplyPairs(std::uintptr_t a, std::uintptr_t b, std::uintptr_t c, std::uintptr_t d)
{
	return a * b + c * d;
}

// The tests call the functions through these pointers. The compiler cannot see the function behind a volatile
// pointer, so each call goes to the first byte of the function, where the hook is.
TestFunction volatile s_addDigits = &AddDigits;
TestFunction volatile s_multiplyPairs = &MultiplyPairs;

[[nodiscard]] const void* Target(TestFunction volatile& function)
{
	return reinterpret_cast<const void*>(function);
}

[[nodiscard]] std::unique_ptr<HookManager> MakeManager(HookManager::Options options = {})
{
	auto manager = HookManager::Make(std::move(options));
	EXPECT_TRUE(manager) << manager.GetError().message;
	return std::move(manager).Value();
}

[[nodiscard]] HookCallback Callback(HookOwner owner, std::string name, HookPhase phase, std::uint32_t parameterCount,
                                    std::function<void(const Ts4Call&)> function)
{
	return HookCallback {
	    .owner = owner,
	    .name = std::move(name),
	    .phase = phase,
	    .parameterCount = parameterCount,
	    .function = std::move(function),
	};
}

/// Write to an address that does not exist. The guard of the hook catches the access violation.
void Fault()
{
	int* volatile address = nullptr;
	*address = 1;
}

} // namespace

TEST(Hooks, RunsTheCallbacksInOrderWithTheArgumentsAndTheResult)
{
	auto manager = MakeManager();
	std::vector<std::string> order;
	Ts4Call first {};
	Ts4Call second {};
	Ts4Call after {};
	ASSERT_TRUE(manager->AddCallback(Target(s_addDigits), "AddDigits",
	                                 Callback(1, "first", HookPhase::Before, 4, [&](const Ts4Call& call) {
		                                 order.emplace_back("first");
		                                 first = call;
	                                 })));
	ASSERT_TRUE(manager->AddCallback(Target(s_addDigits), "AddDigits",
	                                 Callback(1, "after", HookPhase::After, 1, [&](const Ts4Call& call) {
		                                 order.emplace_back("after");
		                                 after = call;
	                                 })));
	ASSERT_TRUE(manager->AddCallback(Target(s_addDigits), "AddDigits",
	                                 Callback(2, "second", HookPhase::Before, 2, [&](const Ts4Call& call) {
		                                 order.emplace_back("second");
		                                 second = call;
	                                 })));
	EXPECT_TRUE(manager->Hooked(Target(s_addDigits)));

	EXPECT_EQ(s_addDigits(1, 2, 3, 4), 1234u);
	EXPECT_EQ(order, (std::vector<std::string> {"first", "second", "after"}));
	EXPECT_EQ(first.arguments[0], 1u);
	EXPECT_EQ(first.arguments[1], 2u);
	EXPECT_EQ(first.arguments[2], 3u);
	EXPECT_EQ(first.arguments[3], 4u);
	EXPECT_EQ(first.result, 0u);
	// Each callback gets only the parameters that it reads.
	EXPECT_EQ(second.arguments[0], 1u);
	EXPECT_EQ(second.arguments[1], 2u);
	EXPECT_EQ(second.arguments[2], 0u);
	EXPECT_EQ(second.arguments[3], 0u);
	EXPECT_EQ(after.arguments[0], 1u);
	EXPECT_EQ(after.arguments[1], 0u);
	EXPECT_EQ(after.result, 1234u);

	const auto statuses = manager->Statuses();
	ASSERT_EQ(statuses.size(), 1u);
	EXPECT_EQ(statuses[0].function, "AddDigits");
	EXPECT_TRUE(statuses[0].installed);
	EXPECT_EQ(statuses[0].calls, 1u);
	ASSERT_EQ(statuses[0].callbacks.size(), 3u);
	EXPECT_EQ(statuses[0].callbacks[0].calls, 1u);
	EXPECT_EQ(manager->Describe()[0]["function"], "AddDigits");
}

TEST(Hooks, TheRemovalPutsTheOriginalFunctionBack)
{
	auto manager = MakeManager();
	int calls = 0;
	ASSERT_TRUE(manager->AddCallback(Target(s_addDigits), "AddDigits",
	                                 Callback(1, "count", HookPhase::Before, 0, [&](const Ts4Call&) { ++calls; })));
	EXPECT_EQ(s_addDigits(4, 3, 2, 1), 4321u);
	EXPECT_EQ(calls, 1);
	EXPECT_TRUE(manager->Remove(Target(s_addDigits)));
	EXPECT_FALSE(manager->Hooked(Target(s_addDigits)));
	EXPECT_FALSE(manager->Remove(Target(s_addDigits)));
	EXPECT_EQ(s_addDigits(4, 3, 2, 1), 4321u);
	EXPECT_EQ(calls, 1);

	// The function can get a hook again.
	ASSERT_TRUE(manager->AddCallback(Target(s_addDigits), "AddDigits",
	                                 Callback(1, "count", HookPhase::After, 0, [&](const Ts4Call&) { ++calls; })));
	EXPECT_EQ(s_addDigits(1, 1, 1, 1), 1111u);
	EXPECT_EQ(calls, 2);
	manager->RemoveAll();
	EXPECT_EQ(s_addDigits(1, 1, 1, 1), 1111u);
	EXPECT_EQ(calls, 2);
	EXPECT_TRUE(manager->Statuses().empty());
}

TEST(Hooks, TheManagerRemovesItsHooksWhenItGoesOutOfScope)
{
	int calls = 0;
	{
		auto manager = MakeManager();
		ASSERT_TRUE(manager->AddCallback(Target(s_addDigits), "AddDigits",
		                                 Callback(1, "count", HookPhase::Before, 0, [&](const Ts4Call&) { ++calls; })));
		ASSERT_TRUE(manager->AddCallback(Target(s_multiplyPairs), "MultiplyPairs",
		                                 Callback(1, "count", HookPhase::Before, 0, [&](const Ts4Call&) { ++calls; })));
		EXPECT_EQ(s_addDigits(0, 0, 0, 7), 7u);
		EXPECT_EQ(s_multiplyPairs(2, 3, 4, 5), 26u);
		EXPECT_EQ(calls, 2);
	}
	EXPECT_EQ(s_addDigits(0, 0, 0, 7), 7u);
	EXPECT_EQ(s_multiplyPairs(2, 3, 4, 5), 26u);
	EXPECT_EQ(calls, 2);
}

TEST(Hooks, ACallbackThatThrowsStopsOnlyItsOwner)
{
	std::vector<std::pair<HookOwner, std::string>> failures;
	auto manager = MakeManager(HookManager::Options {
	    .ownerFailed = [&](HookOwner owner, const std::string& reason) { failures.emplace_back(owner, reason); },
	});
	int badCalls = 0;
	int badOtherCalls = 0;
	int goodCalls = 0;
	ASSERT_TRUE(
	    manager->AddCallback(Target(s_addDigits), "AddDigits", Callback(1, "bad", HookPhase::Before, 0, [&](const Ts4Call&) {
		                         ++badCalls;
		                         throw std::runtime_error("the mod has a bug");
	                         })));
	ASSERT_TRUE(manager->AddCallback(Target(s_multiplyPairs), "MultiplyPairs",
	                                 Callback(1, "other", HookPhase::After, 0, [&](const Ts4Call&) { ++badOtherCalls; })));
	ASSERT_TRUE(manager->AddCallback(Target(s_addDigits), "AddDigits",
	                                 Callback(2, "good", HookPhase::Before, 0, [&](const Ts4Call&) { ++goodCalls; })));

	EXPECT_EQ(s_addDigits(1, 2, 3, 4), 1234u);
	EXPECT_EQ(s_addDigits(1, 2, 3, 4), 1234u);
	EXPECT_EQ(s_multiplyPairs(1, 2, 3, 4), 14u);
	EXPECT_EQ(badCalls, 1);
	EXPECT_EQ(badOtherCalls, 0);
	EXPECT_EQ(goodCalls, 2);
	EXPECT_TRUE(manager->OwnerFailed(1));
	EXPECT_FALSE(manager->OwnerFailed(2));
	ASSERT_EQ(failures.size(), 1u);
	EXPECT_EQ(failures[0].first, 1u);
	EXPECT_NE(failures[0].second.find("the mod has a bug"), std::string::npos) << failures[0].second;
	EXPECT_NE(failures[0].second.find("bad before AddDigits"), std::string::npos) << failures[0].second;

	// A stopped owner cannot add a callback.
	EXPECT_FALSE(manager->AddCallback(Target(s_addDigits), "AddDigits",
	                                  Callback(1, "again", HookPhase::Before, 0, [](const Ts4Call&) {})));
	const auto statuses = manager->Statuses();
	ASSERT_EQ(statuses.size(), 2u);
	for (const auto& status : statuses)
	{
		for (const auto& callback : status.callbacks)
		{
			EXPECT_EQ(callback.stopped, callback.owner == 1) << callback.name;
		}
	}
}

TEST(Hooks, ACallbackThatFaultsStopsOnlyItsOwner)
{
	std::string reason;
	auto manager = MakeManager(HookManager::Options {
	    .ownerFailed = [&](HookOwner, const std::string& text) { reason = text; },
	});
	int goodCalls = 0;
	ASSERT_TRUE(manager->AddCallback(Target(s_addDigits), "AddDigits",
	                                 Callback(3, "fault", HookPhase::After, 0, [](const Ts4Call&) { Fault(); })));
	ASSERT_TRUE(manager->AddCallback(Target(s_addDigits), "AddDigits",
	                                 Callback(4, "good", HookPhase::After, 0, [&](const Ts4Call&) { ++goodCalls; })));
	EXPECT_EQ(s_addDigits(9, 8, 7, 6), 9876u);
	EXPECT_EQ(s_addDigits(9, 8, 7, 6), 9876u);
	EXPECT_EQ(goodCalls, 2);
	EXPECT_TRUE(manager->OwnerFailed(3));
	EXPECT_FALSE(manager->OwnerFailed(4));
	EXPECT_NE(reason.find("an access violation"), std::string::npos) << reason;
}

TEST(Hooks, ACallOnAnotherThreadRunsOnlyTheOriginalFunction)
{
	auto manager = MakeManager();
	int calls = 0;
	ASSERT_TRUE(manager->AddCallback(Target(s_multiplyPairs), "MultiplyPairs",
	                                 Callback(1, "count", HookPhase::Before, 0, [&](const Ts4Call&) { ++calls; })));
	std::uintptr_t result = 0;
	std::thread([&] { result = s_multiplyPairs(3, 3, 4, 4); }).join();
	EXPECT_EQ(result, 25u);
	EXPECT_EQ(calls, 0);
	const auto statuses = manager->Statuses();
	ASSERT_EQ(statuses.size(), 1u);
	EXPECT_EQ(statuses[0].calls, 0u);
	EXPECT_EQ(statuses[0].directCalls, 1u);
}

TEST(Hooks, ACallFromACallbackRunsOnlyTheOriginalFunction)
{
	auto manager = MakeManager();
	int calls = 0;
	std::uintptr_t inner = 0;
	ASSERT_TRUE(
	    manager->AddCallback(Target(s_addDigits), "AddDigits", Callback(1, "again", HookPhase::Before, 0, [&](const Ts4Call&) {
		                         ++calls;
		                         inner = s_addDigits(5, 5, 5, 5);
	                         })));
	EXPECT_EQ(s_addDigits(1, 0, 0, 0), 1000u);
	EXPECT_EQ(calls, 1);
	EXPECT_EQ(inner, 5555u);
	EXPECT_EQ(manager->Statuses()[0].directCalls, 1u);
}

TEST(Hooks, RefusesBadCallbacksAndBadCallers)
{
	auto manager = MakeManager();
	const auto noop = [](const Ts4Call&) {};
	EXPECT_FALSE(manager->AddCallback(nullptr, "Nothing", Callback(1, "a", HookPhase::Before, 0, noop)));
	EXPECT_FALSE(manager->AddCallback(Target(s_addDigits), "AddDigits", Callback(1, "", HookPhase::Before, 0, noop)));
	EXPECT_FALSE(manager->AddCallback(Target(s_addDigits), "AddDigits", Callback(1, "a", HookPhase::Before, 0, nullptr)));
	const auto tooMany = manager->AddCallback(Target(s_addDigits), "AddDigits", Callback(1, "a", HookPhase::Before, 5, noop));
	ASSERT_FALSE(tooMany);
	EXPECT_NE(tooMany.GetError().message.find("5 parameters"), std::string::npos);
	EXPECT_FALSE(manager->Hooked(Target(s_addDigits)));

	ASSERT_TRUE(manager->AddCallback(Target(s_addDigits), "AddDigits", Callback(1, "a", HookPhase::Before, 0, noop)));
	EXPECT_FALSE(manager->AddCallback(Target(s_addDigits), "AddDigits", Callback(1, "a", HookPhase::After, 0, noop)));
	EXPECT_TRUE(manager->AddCallback(Target(s_addDigits), "AddDigits", Callback(2, "a", HookPhase::After, 0, noop)));

	bool fromOtherThread = true;
	std::thread([&] {
		fromOtherThread = static_cast<bool>(
		    manager->AddCallback(Target(s_addDigits), "AddDigits", Callback(3, "a", HookPhase::Before, 0, noop)));
	}).join();
	EXPECT_FALSE(fromOtherThread);

	std::string inCallback;
	ASSERT_TRUE(
	    manager->AddCallback(Target(s_addDigits), "AddDigits", Callback(4, "add", HookPhase::Before, 0, [&](const Ts4Call&) {
		                         auto added = manager->AddCallback(Target(s_multiplyPairs), "MultiplyPairs",
		                                                           Callback(4, "b", HookPhase::Before, 0, noop));
		                         inCallback = added ? std::string("added") : added.GetError().message;
		                         EXPECT_FALSE(manager->Remove(Target(s_addDigits)));
	                         })));
	EXPECT_EQ(s_addDigits(0, 0, 0, 1), 1u);
	EXPECT_NE(inCallback.find("cannot run in a callback"), std::string::npos) << inCallback;
	EXPECT_TRUE(manager->Hooked(Target(s_addDigits)));
	EXPECT_FALSE(manager->Hooked(Target(s_multiplyPairs)));
}

TEST(Hooks, EachFunctionHasOneHook)
{
	auto manager = MakeManager();
	auto other = MakeManager();
	const auto noop = [](const Ts4Call&) {};
	ASSERT_TRUE(manager->AddCallback(Target(s_addDigits), "AddDigits", Callback(1, "a", HookPhase::Before, 0, noop)));
	ASSERT_TRUE(manager->AddCallback(Target(s_addDigits), "AddDigits", Callback(2, "b", HookPhase::Before, 0, noop)));
	EXPECT_EQ(manager->Statuses().size(), 1u);
	const auto second = other->AddCallback(Target(s_addDigits), "AddDigits", Callback(3, "c", HookPhase::Before, 0, noop));
	ASSERT_FALSE(second);
	EXPECT_NE(second.GetError().message.find("another hook manager"), std::string::npos) << second.GetError().message;
	EXPECT_EQ(s_addDigits(2, 2, 2, 2), 2222u);
}

TEST(Hooks, TheRemovalOfAnOwnerRemovesAHookWithNoCallbacks)
{
	auto manager = MakeManager();
	int calls = 0;
	ASSERT_TRUE(manager->AddCallback(Target(s_addDigits), "AddDigits",
	                                 Callback(1, "a", HookPhase::Before, 0, [&](const Ts4Call&) { ++calls; })));
	ASSERT_TRUE(manager->AddCallback(Target(s_multiplyPairs), "MultiplyPairs",
	                                 Callback(1, "b", HookPhase::Before, 0, [&](const Ts4Call&) { ++calls; })));
	ASSERT_TRUE(manager->AddCallback(Target(s_multiplyPairs), "MultiplyPairs",
	                                 Callback(2, "c", HookPhase::Before, 0, [&](const Ts4Call&) { calls += 10; })));
	manager->RemoveOwner(1);
	EXPECT_FALSE(manager->Hooked(Target(s_addDigits)));
	EXPECT_TRUE(manager->Hooked(Target(s_multiplyPairs)));
	EXPECT_EQ(s_addDigits(0, 0, 1, 0), 10u);
	EXPECT_EQ(s_multiplyPairs(1, 1, 1, 1), 2u);
	EXPECT_EQ(calls, 10);
}
