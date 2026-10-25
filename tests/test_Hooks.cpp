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
#include <windows.h>

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

// The address of a function of any type, for the functions that the new tests hook.
template <class Function>
[[nodiscard]] const void* TargetOf(Function volatile& function)
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

// A function with eight integer parameters. The x64 call puts the first four in registers and the other four on the
// stack. The thunk must keep all eight for the original function.
using Function8 = std::uintptr_t (*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t,
                                     std::uintptr_t, std::uintptr_t, std::uintptr_t);
__declspec(noinline) std::uintptr_t EightArguments(std::uintptr_t a, std::uintptr_t b, std::uintptr_t c, std::uintptr_t d,
                                                   std::uintptr_t e, std::uintptr_t f, std::uintptr_t g, std::uintptr_t h)
{
	return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8;
}
Function8 volatile s_eight = &EightArguments;

// A function with a mix of integer and floating-point parameters and a double result. The x64 call puts the integers
// in integer registers and the stack, and the doubles in vector registers and the stack. The thunk must keep both.
using FunctionMixed = double (*)(int, double, int, double, double, int);
__declspec(noinline) double MixedArguments(int a, double x, int b, double y, double z, int c)
{
	return static_cast<double>(a) + x * 10.0 + static_cast<double>(b) * 100.0 + y * 1000.0 + z * 10000.0 +
	       static_cast<double>(c) * 100000.0;
}
FunctionMixed volatile s_mixed = &MixedArguments;

// A function with six double parameters and a double result. Two parameters go on the stack.
using FunctionDoubles = double (*)(double, double, double, double, double, double);
__declspec(noinline) double SumDoubles(double a, double b, double c, double d, double e, double f)
{
	return a + b * 2.0 + c * 4.0 + d * 8.0 + e * 16.0 + f * 32.0;
}
FunctionDoubles volatile s_doubles = &SumDoubles;

// A function that gets a struct by a pointer and reads its fields.
struct Thing
{
	std::uintptr_t first;
	std::uintptr_t second;
	double value;
};
using FunctionStruct = std::uintptr_t (*)(const Thing*);
__declspec(noinline) std::uintptr_t ReadThing(const Thing* thing)
{
	return thing->first * 10 + thing->second;
}
FunctionStruct volatile s_struct = &ReadThing;

// A function that calls itself through the hooked pointer, for a deep recursion through a hook.
using FunctionRecursion = std::uintptr_t (*)(std::uintptr_t);
std::uintptr_t CountDown(std::uintptr_t n);
FunctionRecursion volatile s_countDown = &CountDown;
__declspec(noinline) std::uintptr_t CountDown(std::uintptr_t n)
{
	if (n == 0)
	{
		return 0;
	}
	return n + s_countDown(n - 1);
}

// A function that throws a C++ exception. The exception must go through the thunk to the caller.
using FunctionThrow = std::uintptr_t (*)(std::uintptr_t);
__declspec(noinline) std::uintptr_t ThrowIf42(std::uintptr_t x)
{
	if (x == 42)
	{
		throw std::runtime_error("the original function throws");
	}
	return x * 3;
}
FunctionThrow volatile s_throw = &ThrowIf42;

// A function that walks its own stack. The hook must not break the walk.
std::uint32_t volatile s_walkFrames = 0;
__declspec(noinline) std::uintptr_t WalkStack(std::uintptr_t x)
{
	void* frames[32] = {};
	s_walkFrames = RtlCaptureStackBackTrace(0, 32, frames, nullptr);
	return x + 1;
}
using FunctionWalk = std::uintptr_t (*)(std::uintptr_t);
FunctionWalk volatile s_walk = &WalkStack;

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

TEST(Hooks, KeepsEightIntegerArgumentsWithTheStackArguments)
{
	const auto base = s_eight(1, 2, 3, 4, 5, 6, 7, 8);
	auto manager = MakeManager();
	Ts4Call before {};
	Ts4Call after {};
	ASSERT_TRUE(manager->AddCallback(TargetOf(s_eight), "EightArguments",
	                                 Callback(1, "before", HookPhase::Before, 4, [&](const Ts4Call& call) { before = call; })));
	ASSERT_TRUE(manager->AddCallback(TargetOf(s_eight), "EightArguments",
	                                 Callback(1, "after", HookPhase::After, 4, [&](const Ts4Call& call) { after = call; })));
	const auto hooked = s_eight(1, 2, 3, 4, 5, 6, 7, 8);
	// The result is the same, so the thunk kept the four stack arguments.
	EXPECT_EQ(hooked, base);
	EXPECT_EQ(hooked, 1u + 4u + 9u + 16u + 25u + 36u + 49u + 64u);
	EXPECT_EQ(before.arguments[0], 1u);
	EXPECT_EQ(before.arguments[1], 2u);
	EXPECT_EQ(before.arguments[2], 3u);
	EXPECT_EQ(before.arguments[3], 4u);
	EXPECT_EQ(after.result, base);
}

TEST(Hooks, KeepsTheFloatArgumentsAndTheDoubleResult)
{
	const auto base = s_mixed(1, 2.0, 3, 4.0, 5.0, 6);
	auto manager = MakeManager();
	Ts4Call after {};
	ASSERT_TRUE(manager->AddCallback(TargetOf(s_mixed), "MixedArguments",
	                                 Callback(1, "before", HookPhase::Before, 1, [&](const Ts4Call&) {})));
	ASSERT_TRUE(manager->AddCallback(TargetOf(s_mixed), "MixedArguments",
	                                 Callback(1, "after", HookPhase::After, 1, [&](const Ts4Call& call) { after = call; })));
	const auto hooked = s_mixed(1, 2.0, 3, 4.0, 5.0, 6);
	EXPECT_DOUBLE_EQ(hooked, base);
	EXPECT_DOUBLE_EQ(hooked, 1.0 + 20.0 + 300.0 + 4000.0 + 50000.0 + 600000.0);
	// The first integer argument reaches the callback.
	EXPECT_EQ(after.arguments[0], 1u);
}

TEST(Hooks, KeepsSixDoubleArgumentsAndTheDoubleResult)
{
	const auto base = s_doubles(1.0, 2.0, 3.0, 4.0, 5.0, 6.0);
	auto manager = MakeManager();
	int calls = 0;
	ASSERT_TRUE(manager->AddCallback(TargetOf(s_doubles), "SumDoubles",
	                                 Callback(1, "after", HookPhase::After, 0, [&](const Ts4Call&) { ++calls; })));
	const auto hooked = s_doubles(1.0, 2.0, 3.0, 4.0, 5.0, 6.0);
	EXPECT_DOUBLE_EQ(hooked, base);
	EXPECT_DOUBLE_EQ(hooked, 1.0 + 4.0 + 12.0 + 32.0 + 80.0 + 192.0);
	EXPECT_EQ(calls, 1);
}

TEST(Hooks, KeepsAStructPointerArgument)
{
	const Thing thing {.first = 7, .second = 9, .value = 0.0};
	const auto base = s_struct(&thing);
	auto manager = MakeManager();
	Ts4Call before {};
	ASSERT_TRUE(manager->AddCallback(TargetOf(s_struct), "ReadThing",
	                                 Callback(1, "before", HookPhase::Before, 1, [&](const Ts4Call& call) { before = call; })));
	const auto hooked = s_struct(&thing);
	EXPECT_EQ(hooked, base);
	EXPECT_EQ(hooked, 79u);
	EXPECT_EQ(before.arguments[0], reinterpret_cast<std::uintptr_t>(&thing));
}

TEST(Hooks, OnlyABeforeCallbackTakesTheFastPath)
{
	auto manager = MakeManager();
	Ts4Call before {};
	ASSERT_TRUE(manager->AddCallback(Target(s_addDigits), "AddDigits",
	                                 Callback(1, "before", HookPhase::Before, 4, [&](const Ts4Call& call) { before = call; })));
	EXPECT_EQ(s_addDigits(5, 6, 7, 8), 5678u);
	EXPECT_EQ(before.arguments[3], 8u);
	EXPECT_EQ(before.result, 0u);
}

TEST(Hooks, OnlyAnAfterCallbackGetsTheResult)
{
	auto manager = MakeManager();
	Ts4Call after {};
	ASSERT_TRUE(manager->AddCallback(Target(s_addDigits), "AddDigits",
	                                 Callback(1, "after", HookPhase::After, 4, [&](const Ts4Call& call) { after = call; })));
	EXPECT_EQ(s_addDigits(2, 4, 6, 8), 2468u);
	EXPECT_EQ(after.arguments[0], 2u);
	EXPECT_EQ(after.result, 2468u);
}

TEST(Hooks, TwoOwnersGetTheSameCallWithAllArguments)
{
	auto manager = MakeManager();
	Ts4Call firstOwner {};
	Ts4Call secondOwner {};
	ASSERT_TRUE(manager->AddCallback(TargetOf(s_eight), "EightArguments",
	                                 Callback(1, "a", HookPhase::After, 4, [&](const Ts4Call& call) { firstOwner = call; })));
	ASSERT_TRUE(manager->AddCallback(TargetOf(s_eight), "EightArguments",
	                                 Callback(2, "b", HookPhase::After, 4, [&](const Ts4Call& call) { secondOwner = call; })));
	const auto result = s_eight(8, 7, 6, 5, 4, 3, 2, 1);
	EXPECT_EQ(firstOwner.result, result);
	EXPECT_EQ(secondOwner.result, result);
	EXPECT_EQ(firstOwner.arguments[0], 8u);
	EXPECT_EQ(secondOwner.arguments[3], 5u);
}

TEST(Hooks, ReentryFromACallbackKeepsTheArgumentsOfBothCalls)
{
	auto manager = MakeManager();
	std::uintptr_t inner = 0;
	Ts4Call outer {};
	ASSERT_TRUE(manager->AddCallback(TargetOf(s_eight), "EightArguments",
	                                 Callback(1, "before", HookPhase::Before, 4, [&](const Ts4Call& call) {
		                                 outer = call;
		                                 // The reentrant call runs only the original function and keeps its own arguments.
		                                 inner = s_eight(1, 1, 1, 1, 1, 1, 1, 1);
	                                 })));
	const auto result = s_eight(2, 0, 0, 0, 0, 0, 0, 0);
	EXPECT_EQ(result, 2u);
	EXPECT_EQ(outer.arguments[0], 2u);
	EXPECT_EQ(inner, 1u + 2u + 3u + 4u + 5u + 6u + 7u + 8u);
}

TEST(Hooks, ACallbackThatThrowsStillGivesTheCorrectResult)
{
	auto manager = MakeManager();
	int goodCalls = 0;
	ASSERT_TRUE(manager->AddCallback(
	    TargetOf(s_eight), "EightArguments",
	    Callback(1, "bad", HookPhase::After, 0, [](const Ts4Call&) { throw std::runtime_error("the mod has a bug"); })));
	ASSERT_TRUE(manager->AddCallback(TargetOf(s_eight), "EightArguments",
	                                 Callback(2, "good", HookPhase::After, 0, [&](const Ts4Call&) { ++goodCalls; })));
	// The after callback of owner 1 throws, but the call still returns the correct result with all arguments.
	const auto result = s_eight(1, 2, 3, 4, 5, 6, 7, 8);
	EXPECT_EQ(result, 1u + 4u + 9u + 16u + 25u + 36u + 49u + 64u);
	EXPECT_EQ(s_eight(1, 2, 3, 4, 5, 6, 7, 8), 1u + 4u + 9u + 16u + 25u + 36u + 49u + 64u);
	EXPECT_TRUE(manager->OwnerFailed(1));
	EXPECT_FALSE(manager->OwnerFailed(2));
	EXPECT_EQ(goodCalls, 2);
}

TEST(Hooks, ADeepRecursionThroughAHookReturnsTheCorrectResult)
{
	auto manager = MakeManager();
	std::uint64_t beforeCount = 0;
	std::uint64_t afterCount = 0;
	ASSERT_TRUE(manager->AddCallback(TargetOf(s_countDown), "CountDown",
	                                 Callback(1, "before", HookPhase::Before, 1, [&](const Ts4Call&) { ++beforeCount; })));
	ASSERT_TRUE(manager->AddCallback(TargetOf(s_countDown), "CountDown",
	                                 Callback(1, "after", HookPhase::After, 1, [&](const Ts4Call&) { ++afterCount; })));
	// The recursion goes deeper than the shadow stack. A level that is deeper than the bound runs only the original
	// function, so the result stays correct and the shadow stack cannot grow without a bound.
	constexpr std::uintptr_t depth = 1000;
	const auto result = s_countDown(depth);
	EXPECT_EQ(result, depth * (depth + 1) / 2);
	// Each level of the recursion is a main-thread call, so each ran the before callbacks.
	EXPECT_EQ(beforeCount, depth + 1);
	// Only the levels inside the bound of the shadow stack ran the after callbacks.
	EXPECT_GT(afterCount, 0u);
	EXPECT_LE(afterCount, beforeCount);
}

TEST(Hooks, AnExceptionGoesThroughAHookToTheCaller)
{
	auto manager = MakeManager();
	int beforeCalls = 0;
	ASSERT_TRUE(manager->AddCallback(TargetOf(s_throw), "ThrowIf42",
	                                 Callback(1, "before", HookPhase::Before, 1, [&](const Ts4Call&) { ++beforeCalls; })));
	EXPECT_EQ(s_throw(5), 15u);
	bool caught = false;
	try
	{
		(void)s_throw(42);
	}
	catch (const std::runtime_error& error)
	{
		caught = true;
		EXPECT_STREQ(error.what(), "the original function throws");
	}
	EXPECT_TRUE(caught);
	EXPECT_EQ(beforeCalls, 2);
	// The hook is still correct after the exception.
	EXPECT_EQ(s_throw(7), 21u);
	EXPECT_EQ(beforeCalls, 3);
}

TEST(Hooks, AStackWalkGoesThroughAHook)
{
	s_walkFrames = 0;
	const auto base = s_walk(10);
	const auto baseFrames = s_walkFrames;
	auto manager = MakeManager();
	ASSERT_TRUE(manager->AddCallback(TargetOf(s_walk), "WalkStack",
	                                 Callback(1, "before", HookPhase::Before, 1, [&](const Ts4Call&) {})));
	s_walkFrames = 0;
	const auto hooked = s_walk(10);
	const auto hookedFrames = s_walkFrames;
	EXPECT_EQ(hooked, base);
	// The walk from inside the original function still finds its callers through the thunk and the trampoline.
	EXPECT_GT(baseFrames, 2u);
	EXPECT_GT(hookedFrames, 2u);
}
