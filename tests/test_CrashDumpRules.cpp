/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <gtest/gtest.h>
#include <mod_core/CrashDumpRules.h>

using namespace ts4_mod;

namespace
{
constexpr AddressRange k_Stack {.low = 0x10000, .high = 0x110000};
} // namespace

TEST(CrashDumpRules, AStackOverflowGetsADump)
{
	EXPECT_EQ(DecideCrashDump(k_ExceptionStackOverflow, 0, k_Stack), CrashDumpReason::StackOverflow);
}

TEST(CrashDumpRules, AnAccessViolationOnTheStackGetsADump)
{
	// The stack probe touches the page below the last guard page.
	EXPECT_EQ(DecideCrashDump(k_ExceptionAccessViolation, 0x10ff8, k_Stack), CrashDumpReason::StackExhausted);
	EXPECT_EQ(DecideCrashDump(k_ExceptionAccessViolation, k_Stack.low, k_Stack), CrashDumpReason::StackExhausted);
}

TEST(CrashDumpRules, OtherAccessViolationsGetNoDump)
{
	// The game and its libraries handle some access violations themselves.
	EXPECT_EQ(DecideCrashDump(k_ExceptionAccessViolation, 0, k_Stack), CrashDumpReason::None);
	EXPECT_EQ(DecideCrashDump(k_ExceptionAccessViolation, k_Stack.high, k_Stack), CrashDumpReason::None);
	EXPECT_EQ(DecideCrashDump(k_ExceptionAccessViolation, 0x7fff0000, k_Stack), CrashDumpReason::None);
}

TEST(CrashDumpRules, OtherExceptionsGetNoDump)
{
	constexpr uint32_t k_Breakpoint = 0x80000003;
	constexpr uint32_t k_CppException = 0xE06D7363;
	EXPECT_EQ(DecideCrashDump(k_Breakpoint, 0x10ff8, k_Stack), CrashDumpReason::None);
	EXPECT_EQ(DecideCrashDump(k_CppException, 0x10ff8, k_Stack), CrashDumpReason::None);
}

TEST(CrashDumpRules, AnEmptyStackRangeGetsNoDump)
{
	EXPECT_EQ(DecideCrashDump(k_ExceptionAccessViolation, 0, AddressRange {}), CrashDumpReason::None);
}
